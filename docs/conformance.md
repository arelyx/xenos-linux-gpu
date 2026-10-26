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

After the performance work (`results/full6-report.txt`, 26 October afternoon): 98.3% of executed cases pass, 49 fixed and 5 regressed compared with full3, no hangs. The regressions are four `fbo.render` cases that fail only after other cases have run in the same process (see "What the full dEQP run caught" in [performance.md](performance.md)) and one 512x512 cube texture that ran out of memory. The list below is from `results/full3-report.txt` and still applies:

- Cube maps account for most texture failures: `texture.vertex.cube.*` (45), `texture.mipmap.cube.*` (28), cube cases in `texture.specification` and `texture.filtering`. Probably the cube face layout; cube textures are the one texture kind freedreno doesn't tile.
- `fbo.render` (36): to investigate.
- Crashes (17) are shader-compiler aborts on very large shaders (`shaders.indexing ... const_write_dynamic_loop_read`, ir2 runs out of its register array) and three `fbo.completeness` float formats.
- `state_query.integers.implementation_color_read_*` regressed: we report `GL_UNSIGNED_INT_8_8_8_8_REV` as the read type on big-endian so glamor accelerates depth 24/32, and ES2 doesn't list that type.

- Cube-map filtering at texel boundaries on the first face rendered (`texture.filtering.cube.*`).
- RGBA4444 textures sample like 565.
- `rasterization.limits.points` (largest point size) fails without hanging.
