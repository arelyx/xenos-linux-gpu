# Path C, revised: run Mesa's freedreno a2xx on Xenos

## Evidence (25 Oct 2026)
- Shader ISA: Adreno 200 (a2xx) and Xenos encodings are bit-identical for control flow and ALU,
  same layout for fetch with a few extra Xenos fields. See isa-a2xx-vs-xenos.md. ir2 reuse ~90%;
  the assembler needs explicit shift/mask packing because Mesa's bitfield structs assume a
  little-endian host.
- Render model: freedreno a2xx already renders into on-chip GMEM tiles and copies them out with
  RB_COPY_* (fd2_gmem.c emit_gmem2mem_surf). That is the Xenos EDRAM + resolve model.
  Xenos: GMEM = 10MB EDRAM.
- Command stream: freedreno emits PM4 type0/type3 packets as native u32. On the big-endian
  Xenon, the CP is already set up to read native words (xegpu rings and indirect buffers work
  that way), so packet emission needs no change.
- Kernel side today: xegpu gives contiguous buffers, indirect-buffer submit, fences with
  interrupts, and owns the display (xegpu_drm).
- User space can now use the vector unit (kernel patch: Xenon CPU table lacked AltiVec), which
  also unblocks Debian's llvmpipe as a software GL reference.

## Plan
1. Kernel: expose the msm DRM UAPI subset freedreno's msm backend uses, on xegpu_drm's render node:
   GET_PARAM (GPU_ID, CHIP_ID, GMEM_SIZE=10MB, GMEM_BASE, MAX_FREQ, TIMESTAMP, VA_START/SIZE,
   PRIORITIES=1), GEM_NEW, GEM_INFO (IOVA = physical address, MMAP offset), GEM_CPU_PREP/FINI,
   GEM_SUBMIT (iova mode: cmds reference buffers by iova; kernel validates every address lies
   inside a buffer in the submit's BO list), WAIT_FENCE, SUBMITQUEUE_NEW/CLOSE/QUERY, GEM_MADVISE.
   No MMU, so "iova" is physical; contiguous BOs from the pool.
2. Mesa: add a Xenos variant to freedreno a2xx (new gpu_id, e.g. 360):
   - loader: map kernel driver "xegpu" to freedreno.
   - ir2_assemble.c: shift/mask packers; write the extra Xenos fields (texture dimension,
     single-bit use_reg_lod, maxa for mova).
   - endian: set 8in32 swap in vertex/texture fetch constants and resolve dest for BE host data.
   - GMEM: EDRAM size 10MB, Xenos tile granularity (80x16 samples), resolve formats.
   - display: buffers shared with xegpu_drm via dma-buf/prime (same device, so plain GEM handles).
3. Build Mesa for ppc64 BE (cross, sysroot = /srv/nfs/xenon), run piglit/deqp-gles2 subsets, then
   Xorg glamor.

## Order of work
kernel UAPI (testable with a tiny libdrm client) -> Mesa assembler fix + a "clear + resolve"
smoke test (no shaders) -> textured triangle (kmscube) -> glamor.

## Status 25 Oct 2026, evening (box down after the kexec test)
Done, offline:
- Kernel: msm UAPI subset in xegpu (msm_compat=1). Built, not yet run.
- Mesa branch `xenos` in src/mesa (patches in out/mesa-xenos/):
  1. instr-a2xx.h: little-endian storage order on BE hosts. Verified: ALU, CF and fetch
     encodings byte-identical to an x86 build (ssotest under qemu-ppc64).
  2. Shader words byte-swapped into the command stream; hw binning disabled on BE.
  3. Byte-swap modes: index 8in16/8in32, vertex 8in32, textures by component size.
- Mesa cross-builds in ~30 s (xegpu/mesa-ppc64-cross.ini), installed at /opt/mesa-xenos.
- Tests ready: user/tests/msmtest (UAPI: GPU MEM_WRITE into a BO), gltest/glsmoke (headless
  GLES2: clear, constant triangle, per-vertex color). Runner: xegpu/test-gl.sh.

## The remaining Mesa delta: render-target registers differ (fd2_gmem.c)
Same mechanism (render into on-chip memory, draw a rect in copy mode to resolve), different
register layouts. From Xenia registers.h vs Mesa a2xx.xml:
| register | a2xx | Xenos |
| RB_SURFACE_INFO | pitch | pitch:14, msaa_samples:2 @16, hiz_pitch:14 @18 |
| RB_COLOR_INFO | format @0 (COLORX_*), swap, BASE = 4K-aligned byte address @12 | color_base:12 in EDRAM *tiles* @0, color_format:4 @16 (k_8_8_8_8 = 0 ...), exp_bias:6 @20 |
| RB_DEPTH_INFO | DEPTH_BASE byte address @12 | depth_base:12 tiles @0, depth_format:1 @16 |
| RB_COPY_DEST_INFO | endian 0-2, linear @3, format 4-9, swap 10-11, write masks | endian 0-2, array @3, slice 4-6, format 7-12, number 13-15, exp_bias 16-21, swap @24 |
| RB_COPY_CONTROL | ... | src_select 0-2 (depth = 4), sample_select 4-6, color_clear @8, depth_clear @9, copy_command 20-21 |
EDRAM tile = 80x16 samples x 32 bpp = 5120 bytes; 2048 tiles = 10 MB. Bins should be multiples
of 80x16, and bases are computed in tiles, not bytes. The color format enum must be remapped
(a2xx COLORX_8_8_8_8 = 5, Xenos k_8_8_8_8 = 0). Next: an `is_xenos()` screen flag (new chip id in
freedreno_devices.py, gmem 10 MB, tile align 80x16) and Xenos paths in fd2_gmem.c.

## Verified offline (drm-shim + qemu-ppc64), 25 Oct evening
freedreno's noop drm-shim lets the full GL -> NIR -> ir2 -> command stream path run without
hardware. The same GLES2 smoke test was run on x86 natively and on the ppc64 build under qemu:
all 14 emitted shader binaries are byte-identical (gpu_id 200 and 220). A bug found this way:
util/macros.h redefined PACKED after instr-a2xx.h set it, silently dropping the LE storage
order; fixed with a private A2XX_PACKED macro. Reproduce:
  FD2_DUMP_SHADERS=/tmp/sh.txt FD_GPU_ID=220 LD_PRELOAD=.../libfreedreno_noop_drm_shim.so glsmoke
Full command-stream check (FD_RD_DUMP=enable,full, gpu_id 220): all 13 dump sections compared;
every command/data buffer is identical to x86 after a 32-bit word swap, except (a) the
intentional vertex-fetch 8in32 swap bits and (b) debug marker packets (CP scratch 6/7) that only
the x86 *debug* build emits. So on Xenos the remaining work is the hardware deltas (render-target
registers, EDRAM tiles, resolve formats), not host byte order.

## Tiling (to settle on hardware first)
freedreno a2xx already tiles every render target (fd2_tile_mode) and maps tiled resources
through staging copies. Two hardware questions decide the Xenos path:
1. Can a Xenos resolve write a *linear* destination? libxenon only ever resolves to tiled
   surfaces and RB_COPY_DEST_INFO (Xenia layout) has no linear bit. If not, staging readback must
   detile on the CPU (the 32x32 Xenos formula is in xegpu_drm / xe_dump.h), like fd6_tiled_memcpy.
2. Is a2xx's tiled texture layout the same as Xenos's? Test: render with freedreno into a tiled
   target, resolve, read the raw bytes, compare against the libxenon formula.
Both are one-hour experiments once the console is back (xecube-style test + /dev/xegpu).

## First hardware run of freedreno (25 Oct, 14:24) — hung the GPU
Kernel msm UAPI works on hardware (params, BOs, submit, fences). freedreno's first draw hung the
CP; a wedged Xenos cannot be soft-reset (libxenon's RBBM soft reset hung the whole console), so
every GPU hang costs a power-cycle. Command streams from the hardware run: out/rd-hw-hang/
(decoded with xegpu/rddecode.py). Things freedreno emits that libxenon's known-good streams never
do, i.e. the suspects:
- MMIO writes to a2xx-only blocks: 0x039c/0x039d (RBBM_PM_OVERRIDE1/2 on a2xx: clock/power
  overrides), 0x0444 (CP_PERFMON_CNTL), 0x0e1e (TP0_CHICKEN), 0x0e00, RB_BC_CONTROL (0x0f01).
- Binning machinery even with nobin: DRAW_INDX_BIN, SET_BIN_BASE_OFFSET, PA_SC_VIZ_QUERY,
  VGT_CURRENT_BIN_ID_*, MEM_WRITE of bin data.
- a2xx render-target encodings: RB_COLOR_INFO=0x5 (a2xx COLORX_8_8_8_8; on Xenos = EDRAM tile 5,
  k_8_8_8_8), RB_DEPTH_INFO=0x4001.
Next: an is_xenos() variant that emits none of the above, then compare its init state against
libxenon's Xe_pGInit before any hardware run. Also: the kernel submit path should parse IBs and
refuse MMIO writes outside a whitelist, so a bad stream fails the ioctl instead of hanging.

## Command-stream checker (25 Oct, 14:45) — verified on hardware
xegpu's msm submit now parses every command buffer before it reaches the ring (msm_validate=1):
MMIO writes only to an allow list (scratch, WAIT_UNTIL, COHER_*, SQ setup, context 0x2000-0x3fff,
constants 0x4000-0x49ff, 0x5000-0x5002), opcodes from an allow list (Xenia values), and
IB / MEM_WRITE / event / IM_LOAD addresses must lie inside the submit's buffers; WAIT_REG_MEM only
in libxenon's COHER_STATUS form. libxenon's recorded blit passes; freedreno's a2xx streams are
refused (first hit RB_BC_CONTROL, byte 0x3c04: libxenon writes 0x200e there, freedreno 0x1c004046).
On hardware: freedreno's submit fails with EPERM, no hang, desktop unaffected.
Open: fetch constants (texture/vertex base addresses) are not yet checked.
Hardware fact: PM4 MEM_WRITE (0x3d) writes only the first data dword on Xenos, little-endian.
