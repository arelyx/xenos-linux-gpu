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

