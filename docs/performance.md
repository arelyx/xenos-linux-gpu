# Performance

First pass, 26 October 2026. Everything here was measured on the console with x11perf under Xorg + glamor (`conformance/xperf.sh glamor`), the same tests without acceleration (`xperf.sh noaccel`: modesetting with `AccelMethod none`, CPU rendering, GPU scanout blit), and `gltest/glrt`, which times a loop of tiny batches.

## Results

x11perf operations per second, 1280x720, depth 24. "Morning" is after the first two rounds below, "now" after the third:

| test | glamor before | glamor morning | glamor now | no acceleration |
|---|---:|---:|---:|---:|
| `-rect10` | 319,000 | 342,000 | 334,000 | 1,670,000 |
| `-rect500` | 9,550 | 10,100 | 10,100 | 3,090 |
| `-copywinwin10` | 1,570 | 6,420 | 14,900 | 136,000 |
| `-copywinwin500` | 808 | 2,130 | 2,170 | 487 |
| `-putimage10` | 5,580 | 5,710 | 15,800 | 154,000 |
| `-putimage500` | 29.9 | 167 | 168 | 154 |
| `-shmput500` | 32.3 | 353 | 391 | 321 |
| `-getimage10` | | 3,230 | 4,000 | 9,980 |
| `-getimage500` | | 14.0 | 62.9 | 169 |
| `-aa10text` | 166,000 | 187,000 | 185,000 | 290,000 |
| `-aa24text` | 125,000 | 148,000 | 150,000 | 89,100 |
| `-comppixwin500` | 9,790 | 10,000 | 10,000 | 339 |
| `-oddtilerect100` | 103,000 | 119,000 | 117,000 | 14,100 |
| `-scroll500` | 808 | 2,130 | 2,160 | 904 |

Raw output is in `conformance/results/perf/`. The GPU wins big on large fills, composites and tiled fills (30x on `comppixwin500`, 8x on `oddtilerect100`) and beats the CPU on large copies, scrolls and uploads. It still loses on tiny operations and on reading pixels back.

`glrt` (64x64 FBO, one 3-vertex draw, `glFinish`, repeated): 172 µs per batch before, 102 µs after round 2.

A side effect of round 3: the dEQP 1-in-20 sample (859 cases) now runs in about 70 seconds instead of 10 minutes, because every case reads its frame back with `glReadPixels`.

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

## Round 3: fewer batches

Rounds 1 and 2 made each batch cheaper. Round 3 avoids batches.

1. **Texture barriers inside the batch.** glamor calls `glTextureBarrierNV` before every copy within one pixmap (window to window on the same screen, for instance), and freedreno implements the barrier as a full flush: restore, draw, resolve, submit, per copy. A Xenos batch renders as a single bin, so its draw stream runs exactly once and in order. The barrier now emits a resolve of the area drawn since the last barrier into the middle of the draw stream and keeps the batch open. Draws already invalidate the texture cache, so later draws sample what earlier ones wrote. copywinwin10 7,550 to 15,200. `XENOS_FLUSH_BARRIER=1` brings back the flush for comparison.
2. **Blits scissored to their destination.** Texture uploads by blit took 0.6 ms each regardless of size. The blit ran unscissored, so its batch restored and resolved the whole 1280x720 front buffer (3.6 MB each way) for a 10x10 upload, and freedreno flushed the batch after every blit (a workaround for a GLES 3.1 stencil texturing test). Xenos blits now get their destination box as the scissor and skip that flush. With that, a blit beats the CPU path even for 10x10, so every 32-bit transfer goes by blit: putimage10 5,710 to 15,800, putimage100 1,070 to 4,870.
3. **Cached readback.** Reading the staging buffer through the uncached mapping ran at about 17 MB/s. Readback staging buffers of 64 KB or more are now mapped cached. xegpu cleans every cached buffer the checker didn't already walk before a submit, and Mesa runs `dcbf` over the mapped level before the CPU reads it (the lines are clean, so nothing gets written back over the GPU's data). getimage500 14 to 63.
4. **Checker.** It cleans (`dcbst`) cached command buffers instead of flushing them (`dcbf` threw the lines away just before reading them), and range-checks runs of register writes once instead of per register. 17.5 to 10.7 µs per small submit.

`xorg/xcopytest.c` checks the first two against a CPU model: random fills, overlapping and non-overlapping copies and PutImages, in a window and in a pixmap, read back with `XGetImage`. With the barrier turned into a no-op it reports thousands of wrong pixels; with the in-batch barrier it reports none. The dEQP sample showed no regressions after a fix: blitting 16-bit client data through a staging texture broke rgba4444 and rgb565 cases (the 16-bit formats sample wrongly on Xenos), so blits now also require 4-byte client pixels.

## Where the time goes now

For `glrt`'s small batch: about 34 µs in the kernel submit (11 of them the checker), the rest in Mesa building the batch and in the GPU round trip. A copy inside a batch costs about 65 µs; that hasn't been profiled in detail yet, but every in-batch resolve forces Mesa to re-emit all state for the next draw. Large copies and scrolls are bound by GPU memory traffic: a 500x500 scroll goes through a temporary pixmap, restoring and resolving about 4 MB.

Next:

- Don't re-emit unchanged state; Xenos uploads whole 16-constant blocks and reloads shaders after every resolve.
- Skip the restore when a draw covers its whole scissor with an opaque copy (the scroll case).
- Small readbacks: `getimage10` still loses to the CPU (4,000 vs 9,980).
- Merge glamor's small operations into fewer batches.
