# Timeline

Times are local (US Pacific), 25 and 26 October 2026.

## Day 1, night: Linux on the console

- 00:55 TFTP on the host, Free60 kernel 6.18.58 built in a minute, busybox initramfs, `kboot.conf` on a USB stick.
- 01:28 First Linux boot (physical eject into Xell, TFTP kernel). SSH works. The built-in xenos ring driver stole the GPU from the DRM driver; made it a module.
- 01:40 Debian unstable ppc64 root over NFS.
- 11:50 Debian boots crashed at ~13 s. netconsole showed a sis190 TX watchdog: a lost-wakeup race in the TX stop/wake logic, only hit when NFS fills the TX ring. Patched. Xfce runs.

## Day 1, afternoon: the GPU from Linux

- 13:00 `xegpu.ko`: register mmap, contiguous buffers, kernel-owned CP (microcode, ring, fences). libxenon ported to user space. libxenon's cube at ~2500 fps.
- 13:30 Boot-time contiguous pool; fence interrupts (opcode 0x40, not Xenia's 0x54); a DRM display driver inside the module that converts CPU framebuffers to the tiled scanout layout on the GPU (1 ms vs 20 ms on the CPU). Found an idle SMT thread stealing 45% of its sibling; `xenon_idle.ko`.
- 14:00 Mesa's LLVM crashed with SIGILL: the kernel didn't advertise AltiVec on Xenon. Patched. Compared the Adreno 200 and Xenos ISAs: bit-identical. Plan: freedreno a2xx on an msm interface subset.
- 14:45 freedreno's first stream wedged the GPU. Software reset hung the console. Added a command checker to the msm submit path.

## Day 1, night: making hangs cheap

- 23:50 `rdstep.py` + `xereplay`: replay single command buffers with edits.
- 00:05 First culprit: `EVENT_WRITE_SHD` fences wedge the memory hub.
- 00:15 Vertex fetch stride/offset are dwords. A220 registers land on Xenos MRT blend controls.
- 00:55 Arduino IR blaster: power off works with a wedged GPU. IR power-on boots the dashboard, not Xell.
- 01:50 Measured SMC power-on reasons and patched one byte of the softmod so the Media Center remote boots Xell. `xbox-cycle`: hands-off power cycles, ~80 s.
- 02:20 Indirect buffers nest two deep at most.
- 03:00 Morphing libxenon's working cube into freedreno's clear, one change per boot. freedreno's VS allocated a parameter it never exported.
- 03:30 ALU constants must be written as whole 16-constant blocks.
- 04:45 Fetch constants as whole 6-dword groups (found with guard areas). **First OpenGL on Xenos**: glsmoke runs every stage without a hang.
- 05:20 Resolves always write the tiled layout; freedreno's a2xx never tiled. CPU (de)tiling. 3/5 checks.
- 06:40 `COHER_STATUS_HOST` bit 25 invalidates the vertex cache. glsmoke 16/17.
- 07:30 Xorg + glamor initialises, then asserts little-endian.

## Day 2: conformance

- 00:30 Built Khronos dEQP-GLES2 for ppc64 and a harness that survives hangs. First result: every process wedged on its first submit.
- 01:30 Root cause: GL needed libxenon's `Xe_Init` state stream; earlier tests only worked because the display blit had run it. Kernel fix. Also fixed the first-clear bug.
- 01:45 Index DMA size is in 16-bit words; no 8-bit indices.
- 02:06 1-in-20 sample: 62% pass. Then found dEQP was built little-endian.
- 02:45 Rebuilt dEQP big-endian; fixed byte vertex attributes, blend constants, line loops. Sample: **829/859 (96.5%)**.
- 03:00-06:00 Full run with a skip list for hang classes (stencil-only FBOs, fragment dynamic indexing, mipmap generation, copy-to-texture, big points, ETC1).
- 06:30 SIGILL class: `ldbrx` (then `vmulouh`, an AltiVec op VMX128 lacks). Build with `-mcpu=970 -mno-altivec`.
- 05:46-06:45 The console "died": it was our own orphaned scripts pressing IR buttons. Killed them; one clean cycle brought it back.
- 07:30 Confirmed each hang class alone on fresh boots.
- 08:30 `glmip`: render targets under 8x8 wedge the GPU. Fixed; mipmap generation, copy-to-texture and stencil-only FBO classes pass.
- 09:00 glamor rendered nothing: partial bins resolved to the corner, and depth 24/32 had silently fallen back to software. Fixed both.
- 09:30 Real acceleration hung on xclock. Client bisection, shader disassembly, `glregs`: pixel shaders were limited to 16 GPRs. Kernel splits the pool 64/64. **glamor fully accelerated, no hangs**; the fragment dynamic-indexing dEQP classes pass too.
