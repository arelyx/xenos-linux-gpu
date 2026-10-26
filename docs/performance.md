# Performance

First pass, 26 October 2026. Everything here was measured on the console with x11perf under Xorg + glamor (`conformance/xperf.sh glamor`), the same tests without acceleration (`xperf.sh noaccel`: modesetting with `AccelMethod none`, CPU rendering, GPU scanout blit), and `gltest/glrt`, which times a loop of tiny batches.

## Results

x11perf operations per second, 1280x720, depth 24:

| test | glamor before | glamor after | no acceleration |
|---|---:|---:|---:|
| `-rect10` | 319,000 | 342,000 | 1,670,000 |
| `-rect500` | 9,550 | 10,100 | 3,090 |
| `-copywinwin10` | 1,570 | 6,420 | 136,000 |
| `-copywinwin500` | 808 | 2,130 | 487 |
| `-putimage10` | 5,580 | 5,710 | 154,000 |
| `-putimage500` | 29.9 | 167 | 154 |
| `-shmput500` | 32.3 | 353 | 321 |
| `-aa10text` | 166,000 | 187,000 | 290,000 |
| `-aa24text` | 125,000 | 148,000 | 89,100 |
| `-comppixwin500` | 9,790 | 10,000 | 339 |
| `-oddtilerect100` | 103,000 | 119,000 | 14,100 |
| `-scroll500` | 808 | 2,130 | 904 |

Raw output is in `conformance/results/perf/`. The GPU wins big on large fills, composites and tiled fills (30x on `comppixwin500`, 8x on `oddtilerect100`) and now beats the CPU on large copies, scrolls and uploads. It still loses badly on tiny operations, where each one costs a whole GL batch.

`glrt` (64x64 FBO, one 3-vertex draw, `glFinish`, repeated): 172 µs per batch before, 102 µs after.

## How we found the costs

The kernel had no perf events (`CONFIG_PERF_EVENTS` off), so we profiled by sampling stacks: `conformance/pmp.sh <pid> <samples>` runs `eu-stack` (elfutils) in a loop and counts innermost and inclusive frames. With Mesa built `debugoptimized` or `release` (symbols kept), 80-250 samples were enough to see the top costs. The kernel exports submit counters (`submit_count`, `submit_ns`, `submit_check_ns`) for the part `eu-stack` can't see.

Xorg turned out to be CPU-bound in every slow test: its CPU time equalled wall time.

## What changed, in order of payoff

1. **Cache sync per submit.** The msm submit path called `dma_sync_single_for_device` over every buffer in the submit, including the 4 MB front buffer, although GL buffers are mapped uncached. Now only cached buffers are synced. copywinwin and scroll +50%.
2. **Blit-based texture transfers.** glamor's `PutImage` went through Mesa's CPU path: convert ARGB words to the front buffer's B,G,R,A bytes, then detile into uncached memory, per texel. Xenos now advertises `PIPE_TEXTURE_TRANSFER_BLIT`, so the GPU converts and tiles. A blit costs about 0.5 ms of setup, so a new pipe cap (`texture_transfer_blit_min_pixels`) keeps transfers under 128x128 and non-32-bit texels on the CPU path. putimage500 3x, shmput500 6x.
3. **The command checker read uncached memory.** It reads every command word the GPU will execute; through the uncached mapping that's one bus transaction per load. It now flushes those lines from the cache and reads them through the kernel's cached mapping. copywinwin10 +48% (with the checker off entirely it would be another +14%).
4. **Command streams are cached buffers.** Mesa writes command streams one dword at a time, and each store to uncached memory goes to the bus. The kernel now maps `MSM_BO_CACHED_COHERENT` buffers cached, and Mesa uses that for its rings. The checker's flush already writes back exactly the lines the GPU reads, so no whole-buffer sync is needed. -35 µs per batch (137 to 102).
5. **Release build** (`-Dbuildtype=release -Db_ndebug=true`): -25 µs per batch (debug builds name every BO with an ioctl, among other things).
6. **No madvise.** freedreno's BO cache calls `MSM_GEM_MADVISE` on every ring BO it frees and reuses. xegpu never purges, so Mesa skips it on Xenos. -8 µs per batch.
7. **CPU tiling in runs.** The (de)tiling copy moved one texel at a time; texels are contiguous in 16-byte runs (8 for 1-byte texels), so it now copies runs.
8. **Partial restore and resolve.** Xenos resolves keep their rectangle's x, y in the destination, so with one bin at the origin, only the batch's scissor bounds need restoring and resolving. Correct, but no measurable change on these tests.

Things that didn't matter: the full-memory cache invalidates before draws (`XENOS_SKIP_FULL_INVAL` changed copywinwin10 by 5%), write-combined mapping of GL buffers (+6% on putimage500).

## Where the time goes now

Per small batch (~100 µs): about 50 µs in the kernel submit (34 µs of it the command checker) and about 50 µs of Mesa re-emitting state (`fd2_emit_restore`, `fd2_program_emit`, `fd2_emit_state`, tile setup). `putimage10` waits for the previous batch's fence before writing into a busy texture.

Next:

- Don't re-emit unchanged state at every batch; Xenos uploads whole 16-constant blocks and reloads shaders per batch.
- Make the checker cheaper (it walks every constant dword and searches the buffer list for every address), or make it optional for trusted clients.
- Avoid the stall in `putimage10`: write into a fresh buffer when the old one is busy.
- Merge glamor's small operations into fewer batches.
