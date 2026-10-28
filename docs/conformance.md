# Conformance testing

We test against the Khronos dEQP-GLES2 must-pass list (`aosp_mustpass/main/gles2-main.txt`, 17,165 cases), built from VK-GL-CTS for ppc64 (see [setup.md](setup.md)). The harness and its design are in [methodology.md](methodology.md#5-test-with-a-conformance-suite-not-just-hand-written-tests).

## Running

```
cd conformance
./deqp-run.py <caselist> runs/<name> --batch 100 --skip known-hangs.txt
./report.py runs/<name> [runs/<older>]
./stop-run.sh            # stop a run and any power cycle it started
```

Surface: 256x256 pbuffer, config `rgba8888d24s8ms0`, surfaceless EGL. A batch of 100 cases takes 5-20 s when nothing hangs; texture filtering batches take minutes because dEQP's result verification runs on the Xenon CPU.

`known-hangs.txt` holds classes skipped in full runs. Today it has two: ETC1 (dEQP's decoder runs the 458 MB console out of memory) and cube-map mipmap filtering (verification exceeds the batch timeout).

## Results

| run | cases | result |
|---|---|---|
| 1-in-20 sample, little-endian dEQP (invalid) | 859 | 533 pass (62%) |
| 1-in-20 sample, big-endian dEQP | 859 | 829 pass, 15 fail, 5 hang, 4 crash, 6 not supported (96.5%) |
| full run before the small-target, GPR and SIGILL fixes (partial) | 14,928 of 17,165 | 14,436 of 14,642 executed pass (98.6%), 286 skipped |
| full run with all fixes (26 Oct, 10:00) | 17,165 | 16,735 of 17,075 executed pass (98.0%): 16,612 pass, 119 not supported, 4 quality warnings, 323 fail, 17 crash, **0 hangs**, 90 skipped |

Per-group tables are in `results/`. A case counts as passing if dEQP reports Pass, NotSupported, QualityWarning or CompatibilityWarning, as in Khronos submissions.

## Bugs dEQP found that hand-written tests didn't

| class | cause |
|---|---|
| buffer.write.* wedged the GPU | index DMA size counted bytes, Xenos counts 16-bit words |
| draw.random with GL_LINE_LOOP | primitive type 7 is "triangle with W flags" on Xenos |
| every process, first submit | missing `Xe_Init` state after CP init |
| fragment_ops, clipping, vertex_arrays on pbuffers | byte vertex attributes reversed by the buffer-wide endian swap |
| fragment_ops.blend | blend constants are floats on Xenos |
| texture.specification la88 | 8in16 swap applied to an 8-bit array format |
| mipmap generate, copyteximage, stencil-only FBO | render targets under 8x8 wedge the GPU |
| shaders.indexing / struct dynamic, fragment | pixel shaders limited to 16 GPRs |
| shaders.operator mul ivec4 | SIGILL: GCC used AltiVec `vmulouh`, which VMX128 lacks |
| everything, at first | dEQP built little-endian (`DE_CPU` defaulted to x86_64) |

## Known failures

Latest full run: full9 (`results/full9-report.txt`, 26 October evening), **99.7% of executed cases pass** (17,025 of 17,075), 242 fixed against full8, none regressed, no hangs. The jump came from tiling every 2D and cube texture (hardware rule 23): cube maps and non-renderable formats had been linear, and Xenos lays out linear mips differently from freedreno.

History: full3 98.0%. full6 98.3% after the performance work (49 fixed, 5 regressed: four order-dependent `fbo.render` cases and a 512x512 cube out of memory). full7 98.3%: the order-dependent cases were a stale texture cache (hardware rule 14). full8 98.3% after the glmark2 fixes; `random_teximage2d.cube_7` passed by luck in full7.

full10 (`results/full10-report.txt`) fixed the 14 `fbo.render.recreate_colorbuffer` cases: a color-only clear kept the application's depth test (Mesa patch 0036). It also shows 9 `GL_OUT_OF_MEMORY` cases because the Xfce session was still running and holding GPU pool memory; 8 pass on a rerun without it (`gl-ready.sh` now stops the session). Counting those, 17,038 of 17,075 executed cases pass (99.8%).

full12 (`results/full12-report.txt`, 27 October, with Mesa built for desktop GL and GLX as well): 99.8% (17,039 of 17,075), 9 fixed against full10 (the out-of-memory cases), none regressed, no hangs.

What still failed in full9 (50 cases; the first row is fixed in full10):

| cases | what | notes |
|---:|---|---|
| 14 | `fbo.render.recreate_colorbuffer.*` | fixed in full10: color-only clears kept the depth test |
| 8 | `shaders.indexing.tmp_array.*` (Crash) | ir2 can't compile dynamically indexed temporary arrays; it aborts instead of hanging the GPU |
| 6 | `texture.mipmap.2d.projected.*` | projected mipmap level selection |
| 6 | `texture.size` 2048x2048 mipmapped, 512 cube (ResourceError) | out of GPU pool memory (48 MB) |
| 5 | `clipping` wide points and lines | |
| 3 | `uniform_api.random.*` | |
| 3 | `state_query` read type, max vertex uniform vectors (getfloat) | we report `GL_UNSIGNED_INT_8_8_8_8_REV` as the read type on big-endian so glamor accelerates depth 24/32; ES2 doesn't list it |
| 2 | `shaders.return.return_in_dynamic_loop_dynamic_*` | |
| 1 each | `rasterization.limits.points`, `implementation_limits.max_vertex_uniform_vectors`, `clipping.polygon_edge.poly_fan` | |

## Desktop OpenGL: piglit

Since 27 October Mesa is built with desktop GL and GLX; the driver reports OpenGL 2.0 / GLSL 1.20. piglit (Debian's 2026-07 snapshot, installed in the NFS root) is run with `conformance/piglit/piglit-run.py`, which works like `deqp-run.py`: batches over SSH, a GPU check after every test, power cycle on a hang. The list `conformance/piglit/gl20.txt` is the 2,610 tests of piglit's `quick_gl` profile that apply to GL 2.0, GLSL 1.10/1.20 and the extensions the driver advertises (GLX tests excluded; they need an X server).

First pass (`results/piglit-gl20-1-summary.txt`): 763 pass, 307 fail, 63 crash, 2 hang, 1,471 skip; 67.0% of executed tests pass. During the pass four GPU hang classes turned up and were fixed (Mesa 0041, 0042): a pixel shader that exports nothing (depth-only framebuffers), sampling `k_32_32_32_FLOAT`, 1D fetch constants, and the earlier ones that came from those two. Open:

- Depth and stencil read back by the CPU or blitted (most of `ext_packed_depth_stencil`, `arb_depth_texture`, `ext_framebuffer_object`): depth goes to memory as raw EDRAM color data, which round-trips across batches but isn't the depth layout the CPU expects.
- `varying-packing` and `glsl-max-varyings`: every varying the driver advertises, packed.
- `tex-miplevel-selection`: all variants.
- Crashes: mostly ir2 aborting on texture ops it doesn't implement (`txf`).
- Hangs: `arb_point_sprite-checkerboard` and `-mipmap`.

Third pass (gl20-3, after Mesa 0043-0045: varying count, point sprite exports, derivatives, texelFetch): 802 pass, 324 fail, 9 crash, 0 hangs, 1,471 skip: **70.4%** of executed tests. dEQP full15 after the same changes: 99.8% (two `texture.size` cases out of GPU pool memory this time, which come and go between runs).

Fourth pass (gl20-4, with Mesa 0046's first version, which also converted CPU writes of depth): 808 pass, 70.9%, 9 fixed against gl20-3, 3 regressed and 6 new crashes. The final 0046 converts reads only and skips swapped halves that fall past the row: `depth-tex-modes`, `depth-tex-modes-glsl` pass again and the crashes are gone. `glsl-fs-fragcoord-zw-ortho` still fails (it probes depth at x = 255 of a 256-pixel window, inside a partial EDRAM tile).

Fifth pass (gl20-5, after Mesa 0047 LOD clamps/base level): 824 pass, **72.3%** of executed, no hangs. Mesa 0048 then fixes flat shading and the base vertex (seven more tests pass alone); gl20-6 measures it.

Sixth pass (gl20-6, with the fixed 0048): 836 pass, **73.4%** of executed, no hangs; 13 fixed, one regressed (`arb_draw_elements_base_vertex-bounds`: the index bounds must include the base vertex, Mesa 0049, passes now). Texture wrap with linear filtering is off by a few values near texel edges (`texwrap`); not understood.

Seventh pass (gl20-7, after Mesa 0050, depth resolved through the depth path): 905 pass, **79.5%** of executed, no hangs; 72 fixed (CPU depth/stencil readback, `depthstencil-render-miplevels`, depth texture reads), 2 regressed (`tex-miplevel-selection 2drectshadow`: rectangle textures were linear and skipped the CPU depth conversion; they had passed by accident). The two came back with the next Mesa change, which also adds depth export from the pixel shader (export 61, `PS_EXPORT_MODE` bit 0), compare functions for shadow samplers, and tiled uploads for unsynchronized maps. Rerunning the 234 gl20-7 non-passes with it: 50 more pass (depth `drawpixels`/`copypixels`/`blit`, `glsl-1.10-fragdepth`, shadow `tex-miplevel-selection`, `pbo-drawpixels`, bitmaps and raster color).

Eighth pass (gl20-8, Mesa 0051): 955 pass, **83.8%** of executed, no hangs, 50 fixed, 0 regressed. dEQP full19 with it: 99.8%, no regressions (one `texture.size.cube` case out of GPU memory, as before). Mesa 0052 then adds user clip planes and 3D textures: 8 of gl20-8's 184 non-passes pass with it (`user-clip`, `gl-1.0-user-clip-all-planes`, `clip-plane-transformation` fixed/arb, `tex3d`, `tex3d-npot`, `tex3d-maxsize`, `getteximage-targets 3d`); a dEQP sample of 2,145 cases matched full19 exactly. GLSL `gl_ClipVertex` and rendering into 3D textures are still missing.

Ninth pass (gl20-9, Mesa 0052 and 0053): 969 pass, **85.1%** of executed, no hangs, 15 fixed. The one non-pass that had passed, `tex-miplevel-selection 1dshadow`, was killed by the OOM killer during the run and passes alone. Mesa 0053 rounds vertex positions to the 1/16 grid instead of truncating them (`PA_SU_VTX_CNTL.ROUND_MODE`): truncation shifted texture coordinates so that linear filtering at texel centers took a few percent from the neighbor, which was the `texwrap` failure. dEQP full20: 99.8%, one case moved from Pass to QualityWarning (`rasterization.interpolation.basic.lines`). Mesa 0054 samples packed depth/stencil as depth (on big endian the first channel of `Z24_UNORM_S8_UINT` is the stencil, which made the fetch constant integer and depth read 1.0) and copies stencil in blits on the CPU: 31 of gl20-9's 170 non-passes pass with it (depth/stencil blits and copies, `copyteximage`, `texdepth`, `fbo-depthtex`); dEQP sample unchanged. Mesa 0055 makes `gl_PointCoord` work (the vertex shader has to export the point size for Xenos to generate sprite coordinates; the generated value needs abs() and a y flip): `glsl-fs-pointcoord` and `fbo-gl_pointcoord` pass. Fixed-function point sprite texture coordinate replacement (`arb_point_sprite-*`) is still missing.

Tenth pass (gl20-10, Mesa 0054-0056): 1,004 pass, **88.1%** of executed, no hangs, 35 fixed, 0 regressed. Mesa 0056 makes `gl_FragCoord.z` linear in screen space (it was interpolated perspective-correctly) and splits the 32 fetch constants: 0-11 fragment textures, 12-19 and 26-31 vertex textures, 20-25 vertex buffers. Advertising 16 + 16 had let vertex textures overwrite the vertex buffers (`max-samplers`); dEQP full21 was run with an interim 16 + 4 split, which turned 47 `uniform_api` cases NotSupported; with 12 + 14 the 1,285 sampler and uniform cases match full20 except `uniform_api.random.60`, which needs 15 fragment units.

Eleventh pass (gl20-11, Mesa 0057: polygon offset slope in 1/16-pixel units, triangle fans over 32,766 vertices drawn as indexed lists): 1,007 pass, **88.4%** of executed, no regressions. dEQP full22: 99.8% (17,044 of 17,075), 49 cases back from NotSupported against full21, `uniform_api.random.60` NotSupported (15 fragment units).

Twelfth pass (gl20-12, Mesa 0058: 128 vertex / 112 fragment uniform vectors, the gl_FragCoord.z viewport constants moved from C65/C66 to C254/C255, two ir2 stack overflows on big shaders fixed): 1,009 pass, **88.6%** of executed, no regressions. dEQP full23: 99.8%; two large `texture.size` cases out of GPU memory this run, and `uniform_api.random.3` now links and aborts in register allocation instead of failing to compile.

Thirteenth pass (gl20-13, Mesa 0059 and 0060: destination alpha is 1 in color buffers without alpha; 16-bit packed textures had red and blue swapped, and the 1-5-5-5 formats with alpha at bit 0 don't match `k_1_5_5_5`): 1,016 pass, **89.2%** of executed, 7 fixed, no regressions. dEQP full24: 99.8%, no regressions. Mesa 0061 does blits into and out of 3D textures on the CPU (st blits into them for format conversion and readback, and rendering into a linear 3D texture writes the tiled layout): `texsubimage`, `texsubimage pbo`, `arb_texture_float-get-tex3d` pass; no regressions among 222 rerun tests; a half sample of dEQP texture and fbo cases matches full24. (`fbo.render.resize.rbo_rgba4_depth_component16` fails when it runs right after `rbo_rgb5_a1_stencil_index8` in one process, with 0060 too; alone it passes.)

Fourteenth pass (gl20-14, Mesa 0061 and the first version of 0062): 1,018 pass, **89.4%** of executed; `gl-1.4-polygon-offset` regressed because the depth export pass then covered the whole framebuffer and its float depth can come back one step off. The final 0062 runs the pass only over the columns the strips can't reach; with it the 608 depth/stencil/offset piglit tests match gl20-13 and a half sample of dEQP depth/stencil/fbo cases matches full24 (apart from the order-dependent `rbo_rgba4_depth_component16`). dEQP full25: 99.8%.

Fifteenth pass (gl20-15, Mesa 0062 final, 0063 and 0064: native quads, quad strips and polygons, so line polygon mode draws only their outer edges): 1,020 pass, **89.6%** of executed. The one non-pass that had passed, `tex-miplevel-selection 1dshadow`, was killed by the OOM killer again: a tiled 1D texture takes 32 rows. Mesa 0065 keeps tiling for 1D depth textures only; the 48 1D/miplevel tests match gl20-14. dEQP full26: 99.8%, no regressions apart from a `texture.size.cube` case out of GPU memory.

Sixteenth pass (gl20-16, Mesa 0066: 132 vertex uniform vectors, plus the xegpu `msm_min_free_mb` guard): 1,017 pass. Four `tex-miplevel-selection` bias shadow cases went from pass to segfault. They draw for a long time without a flush; the draw ring grew until 38 MB of RAM was free, the new kernel guard refused the next ring buffer, and Mesa mapped a NULL buffer. dEQP full27: 99.8%, fixed the 2 `max_vertex_uniform_vectors` cases, no regressions.

Seventeenth pass (gl20-17, Mesa 0067: flush a batch once its draw ring holds more than 4 MB): 1,021 pass, **89.6%** of executed. The four bias shadow cases pass again with about 170 MB of RAM free throughout. dEQP full28: identical to full27.


Harness fix after gl20-17: `piglit-run.py` passed `-fbo` to every test, but upstream piglit adds it only to tests marked `run_concurrent`. 185 tests in the GL 2.0 list are not; some of them check their result by reading framebuffer 0, which under `-fbo` is the GBM window rather than the framebuffer they drew to. Rerun in window mode as upstream does, three more pass (`fcc-front-buffer-distraction`, `get-attachment-parameter-default-framebuffer`, `fbo-scissor-blit window`): **1,024 pass, 89.9%** of executed. `fbo-blit-stretch` then fails only its 15 stretched `GL_LINEAR` cases, each by about 4/255: the bilinear weights look 6-bit and truncated (1/8 comes out as 7/64). `tex-upside-down-miptree` fails only at base level 6, where Mesa sizes level 0 at 4096x4096 RGBA, more than 64 MB, which does not fit the 48 MB contiguous pool.

Eighteenth pass (gl20-18, Mesa 0068: bilinear weight rounding, see hardware rule 34; harness runs `gl_builtin` tests with their data path under `tests/`): 1,027 pass. Newly passing: `1-1-linear-texture`, `fbo-blit-stretch`, `built-in constants` (GLSL 1.10), plus the three from the window-mode harness fix. `fp-indirections2` then went from fail to abort: with the rounding its first cases passed and it went on to a 64-fetch chain, past the 64 fetches ir2 records per shader, because the driver reported 16384 texture indirections. Mesa 0069 reports 32 on Xenos; `fp-indirections` and `fp-indirections2` both pass: **1,029 pass, 90.3%** of 1,140 executed. dEQP full29: 99.8%; the one change each way is the `texture.size.cube.512x512` cases that run out of GPU memory depending on what ran before.

Nineteenth pass (gl20-19, Mesa 0069 and 0070: depth clears through an exported depth, hardware rule 35): **1,030 pass, 90.4%** of 1,140 executed, no regressions. Newly passing: `depth-clear-precision-check`, `fp-indirections`, `fp-indirections2`. dEQP full30: 99.8%, only the out-of-memory `texture.size.cube.512x512` flip.

Twentieth pass (xegpu merges physically adjacent pool blocks into one range, hardware rule 36; Mesa 0071 purges the BO cache and retries at the exact size when the kernel refuses a buffer): dEQP full31 **17,048 of 17,075, 99.8%**, four more passes than full30 (the 2048x2048 textures), no regressions; ResourceError went from 6 to 2. The two left, `texture.size.2d.2048x2048_rgb888_mipmap` and `_rgba8888_mipmap` (21 MB each), pass alone but not right after the 16 MB case before them: 18 MB of the pool is still held and the free 30 MB is split. piglit gl20-21: 1,030 pass, identical to gl20-19 (`tex3d-maxsize` needed 0071: it allocates, deletes and reallocates its largest texture).

Mesa 0072 (performance: skip unchanged filter rounding constants within a batch): dEQP full32 identical to full31; piglit gl20-22 1,029, the one difference `tex3d-maxsize`, which fails right after a full run and passes 4 of 4 on its own. Its largest textures exceed what is free in the pool, and the system-RAM fallback for them needs free 16 MB blocks, which a long run fragments.
