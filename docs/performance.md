# Performance

First pass, 26 October 2026. Everything here was measured on the console with x11perf under Xorg + glamor (`conformance/xperf.sh glamor`), the same tests without acceleration (`xperf.sh noaccel`: modesetting with `AccelMethod none`, CPU rendering, GPU scanout blit), and `gltest/glrt`, which times a loop of tiny batches.

## Results

x11perf operations per second, 1280x720, depth 24. "Morning" is after rounds 1 and 2 below, "now" after rounds 3 and 4:

| test | glamor before | glamor morning | glamor now | no acceleration |
|---|---:|---:|---:|---:|
| `-rect10` | 319,000 | 342,000 | 2,880,000 | 1,670,000 |
| `-rect500` | 9,550 | 10,100 | 10,200 | 3,090 |
| `-copywinwin10` | 1,570 | 6,420 | 21,800 | 136,000 |
| `-copywinwin500` | 808 | 2,130 | 5,480 | 487 |
| `-putimage10` | 5,580 | 5,710 | 17,800 | 154,000 |
| `-putimage500` | 29.9 | 167 | 167 | 154 |
| `-shmput500` | 32.3 | 353 | 395 | 321 |
| `-getimage10` | | 3,230 | 3,790 | 9,980 |
| `-getimage500` | | 14.0 | 61.6 | 169 |
| `-aa10text` | 166,000 | 187,000 | 839,000 | 290,000 |
| `-aa24text` | 125,000 | 148,000 | 522,000 | 89,100 |
| `-comppixwin500` | 9,790 | 10,000 | 10,000 | 339 |
| `-oddtilerect100` | 103,000 | 119,000 | 248,000 | 14,100 |
| `-scroll500` | 808 | 2,130 | 5,480 | 904 |

Raw output is in `conformance/results/perf/`. `conformance/perfcheck.sh` reruns these plus `glrt` and `xorg/xcopytest.c` and compares against `results/perf/baseline.txt`, flagging anything more than 7% slower (about 3.5 minutes; `putimage500` varies by about 8% between runs). The GPU now beats CPU rendering on fills (small and large), text, composites, tiled fills, large copies, scrolls and uploads. It still loses on tiny copies and uploads (each costs a GL batch) and on reading pixels back.

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

Things that didn't matter: the full-memory cache invalidates before draws (`XENOS_SKIP_FULL_INVAL` changed copywinwin10 by 5%; they turned out not to work reliably either, see below), write-combined mapping of GL buffers (+6% on putimage500).

## Round 3: fewer batches

Rounds 1 and 2 made each batch cheaper. Round 3 avoids batches.

1. **Texture barriers inside the batch.** glamor calls `glTextureBarrierNV` before every copy within one pixmap (window to window on the same screen, for instance), and freedreno implements the barrier as a full flush: restore, draw, resolve, submit, per copy. A Xenos batch renders as a single bin, so its draw stream runs exactly once and in order. The barrier now emits a resolve of the area drawn since the last barrier into the middle of the draw stream and keeps the batch open. Draws already invalidate the texture cache, so later draws sample what earlier ones wrote. copywinwin10 7,550 to 15,200. `XENOS_FLUSH_BARRIER=1` brings back the flush for comparison.
2. **Blits scissored to their destination.** Texture uploads by blit took 0.6 ms each regardless of size. The blit ran unscissored, so its batch restored and resolved the whole 1280x720 front buffer (3.6 MB each way) for a 10x10 upload, and freedreno flushed the batch after every blit (a workaround for a GLES 3.1 stencil texturing test). Xenos blits now get their destination box as the scissor and skip that flush. With that, a blit beats the CPU path even for 10x10, so every 32-bit upload goes by blit: putimage10 5,710 to 15,800, putimage100 1,070 to 4,870. Readback (`glReadPixels`, GetImage) goes by blit too, with a caveat described under "What the full dEQP run caught".
3. **Cached readback.** Reading the staging buffer through the uncached mapping ran at about 17 MB/s. Readback staging buffers of 64 KB or more are now mapped cached. xegpu cleans every cached buffer the checker didn't already walk before a submit, and Mesa runs `dcbf` over the mapped level before the CPU reads it (the lines are clean, so nothing gets written back over the GPU's data). getimage500 14 to 63.
4. **Checker.** It cleans (`dcbst`) cached command buffers instead of flushing them (`dcbf` threw the lines away just before reading them), and range-checks runs of register writes once instead of per register. 17.5 to 10.7 µs per small submit.

`xorg/xcopytest.c` checks the first two against a CPU model: random fills, overlapping and non-overlapping copies and PutImages, in a window and in a pixmap, read back with `XGetImage`. With the barrier turned into a no-op it reports thousands of wrong pixels; with the in-batch barrier it reports none. The dEQP sample showed no regressions after a fix: blitting 16-bit client data through a staging texture broke rgba4444 and rgb565 cases (the 16-bit formats sample wrongly on Xenos), so blits now also require 4-byte client pixels.

## Round 4: what the profiles showed next

1. **16-bit vertex formats.** glamor sends positions as `GL_SHORT`. Xenos advertised only 32-bit vertex formats, because a single 8in32 endian swap was thought to apply to a whole vertex buffer, so `u_vbuf` translated every draw's vertices to float, reading glamor's VBO back through the write-combined mapping. On a2xx each vertex attribute has its own fetch constant, so each gets its own swap: 8in16 for 16-bit components, none for bytes. Xenos now fetches `k_16_16`, `k_16_16_16_16`, `k_8_8_8_8` and the 16-bit float formats directly. rect10 334,000 to 2,850,000 (now faster than the CPU), aa10text 185,000 to 811,000. All 769 dEQP vertex_arrays, draw and attribute cases pass.
2. **Overlapping copies without a temporary.** glamor copies overlapping areas (scrolling) through a temporary pixmap unless the driver offers `GL_MESA_tile_raster_order`. On Xenos the raster order doesn't matter: a draw reads textures from memory and writes EDRAM, and memory changes only at resolves between draws, so a copy within one texture always reads the old contents. Mesa now advertises the extension on Xenos and ignores the order. scroll500 and copywinwin500 2,160 to 5,520.
3. **Smaller things the stack samples pointed at.** freedreno built the a20x binning ring for every draw although Xenos never runs it; `getenv` ran per draw and per batch for experiment hooks, and once per resolve built a variable name with `snprintf`; an in-batch resolve marked all state dirty where the set a clear dirties is enough. With the first item, copywinwin10 went from 15,000 to 22,600.

## What the full dEQP run caught

The 1-in-20 sample passed after rounds 3 and 4; the full run (17,165 cases, now about 30 minutes) found five regressions and one hang. After the fixes below, the last full run (full7) passes 98.3% of executed cases with no hangs:

- **A hang from the release build.** ir2, the a2xx shader compiler, reports shaders it can't handle with `assert`. Round 2 switched to `-Db_ndebug=true`, so a `tmp_array` shader that used to abort the process compiled to garbage and wedged the GPU. ir2's sources now keep their asserts.
- **Four scissor cases: a freedreno bug.** freedreno stores scissors with inclusive max bounds and handed that struct to the blitter to save. The blitter restores it through `set_scissor_states()`, which expects exclusive bounds, so every scissored blit shrank the application's scissor by one pixel. Upstream only scissors blits when asked to; scissoring every Xenos blit (round 3) exposed it. Fixed by saving exclusive bounds.
- **Order-dependent failures: stale texture cache.** Some cases passed alone and failed after others in the same process: four `fbo.render` cases, and about 10 texture and `uniform_api` cases when readback went through the CPU. Waiting for the whole GPU before every CPU map hid most of them. We first suspected Mesa's per-buffer fence tracking. An 8-second reproducer (261 `uniform_api` render cases, 5 failing) ruled that out: sleeping 2 ms before the map didn't help, while flushing pending work, with or without waiting, did. Every failing case samples a cube map, a linear texture the CPU writes into a buffer recycled from an earlier case. The cause was the cache invalidate before each draw: the all-of-DRAM form doesn't reliably reach the texture cache. Draws now invalidate exact ranges, as libxenon does: vertex and index buffers every draw, a texture or restored render target only after the CPU wrote it or it got a new buffer (resolves already invalidate their destination). The whole-GPU wait is gone. full7: 98.3%, the four `fbo.render` cases fixed, no hangs. The first version invalidated the whole framebuffer at every restore and made `comppixwin500` 30 times slower; `conformance/perfcheck.sh` caught it.
- **One flaky case.** `fbo.render.resize.tex2d_bgra` failed in two group runs and passes alone and when its 100-case batch is replayed. Open.

## Where the time goes now

Measured after the texture cache fix (26 October evening).

- **A tiny batch (`glrt`) costs about 60 µs of user-space CPU** even without waiting for the GPU (`glrt 64 20000 draw nofinish`: 1.2 s user for 20,000 batches). Stack samples: the kernel submit ioctl 27%, creating and destroying the batch and its submit (ring buffers from the BO cache, a slab pool and hash tables per submit) about 30%, building the restore, draw and resolve the rest. The submit is about 790 command dwords; the checker spends about 14 µs on them, but turning it off entirely only saved 0-10 µs of `glrt`'s 86-98 µs (run-to-run noise is about 6 µs).
- **A 10x10 copy (`copywinwin10`, about 52 µs)** has no single hot spot. Xorg waited for the client in 18% of samples. Of the rest, glamor's copy is about half: the GL draw path, `glamor_use_program`, uniforms, binding textures, mapping the VBO, the in-batch barrier, each a few percent. Re-emitting state after an in-batch resolve (`fd2_emit_state`) is about 6%, so skipping unchanged state would save a few microseconds, not halve the cost.
- **A 10x10 GetImage (about 260 µs)**: Xorg waits for the client in 45% of samples. The GPU round trip for the readback blit is about 44 µs. Reading back through the CPU instead (`XENOS_READ_BLIT_MIN_PIXELS`) is slower now: 3,320/s against 3,840/s by blit at 10x10, and 4 times slower at 100x100.
- **Real terminal workloads are bound by the client, not X.** `conformance/appbench.sh` scrolls 100,000 lines through a 160x50 terminal: xterm with core fonts 3.45 s on glamor and 3.28 s without acceleration, xterm with Xft 3.94 s and 3.86 s, xfce4-terminal 16.3 s and 14.9 s (`results/perf/appbench-2026-10-26.txt`). During the xfce4-terminal run Xorg was idle in 148 of 150 stack samples; VTE and cairo render on the CPU in the client.
- Large uploads are near the CPU's limit already: `gltest/bomem` measures about 470 MB/s copying 2000-byte rows into a write-combined buffer (38 MB/s reading one back), so a 500x500 ShmPutImage spends about 2.2 of its 2.5 ms in that copy.

Next:

- Real GL applications as benchmarks (glmark2-es2, es2gears), which is where the GPU matters more than X's small operations.
- Cheaper batches in Mesa: keep the per-submit slab pool, hash tables and ring buffers instead of rebuilding them each batch.
- Skip the restore when a draw covers its whole scissor with an opaque copy (the scroll case).
