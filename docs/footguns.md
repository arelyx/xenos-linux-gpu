# Footguns

Things that cost us hours. Most are obvious in hindsight.

## Console and hardware

- **Never write `/dev/mtd*`.** The Free60 kernel binds `xenon_nand` to the flash controller. Every mtd device is the console's NAND. A softmodded console has no hardware recovery path. Nothing in this repo touches it.
- **Byte loads from the GPU register BAR freeze the console.** Python `mmap` slicing reads bytes. Use aligned 32-bit accesses only (`memoryview(...).cast("I")`, or `userspace/tests/xereg`).
- **No GPU software reset.** libxenon's RBBM soft-reset hung the console. We removed it. Power cycle instead.
- **kexec hangs** right after the jump on this console. Don't rely on it.
- **E71 if the dashboard path points at `BootXell.xex`.** It isn't a dashboard. We recovered by booting Xell with the physical eject button, then Linux, then restoring `Config.ini` with `softmod/fatxcfg.py`.
- **IR power-on boots the dashboard, not Xell.** Peer Pressure only treats the physical eject (reason `0x12`) as "boot Xell". See the one-byte patch in [setup.md](setup.md).
- **Eject during the post-shutdown cooldown** (light dark, fan still spinning) wakes the console normally instead of booting Xell. Wait until it's fully off.
- **A Peer Pressure update reverts the one-byte patch.**
- **The sis190 Ethernet driver has a TX stop/wake race** that kills the network the first time the 64-entry TX ring fills, which on an NFS root is during boot. Patched (`kernel/patches/0001`).
- **Xell reprograms the EDRAM clock**, so EDRAM must be trained once per boot (`xeinit`, run by `xegpu.service`).
- **Linux gets a different DHCP lease** (10.0.0.206 here) than the dashboard (10.0.0.205), because sis190 hardcodes a MAC.

## Toolchain

- **Ubuntu's ppc64 cross GCC defaults to POWER7.** Instant SIGILL.
- **`-mcpu=cell` emits `ldbrx`/`stdbrx`**, which Xenon lacks. It crashed rarely enough (64-bit byte swaps) to look like a GPU problem.
- **GCC auto-vectorises integer code with AltiVec instructions VMX128 doesn't have** (`vmulouh`). dEQP's `ivec4` multiply tests died with SIGILL. Use `-mno-altivec`. pixman needs `PIXMAN_DISABLE=vmx`.
- **libjpeg-turbo picks its AltiVec code** and dies with SIGILL on an instruction VMX128 lacks (glmark2's terrain scene). `JSIMD_FORCENONE=1` turns its SIMD off.
- **ir2 has fixed-size arrays without bounds checks.** A shader bigger than about 384 instructions overflowed them and hung the GPU. Mesa patch 0034 sizes them for Xenos and checks.
- **The kernel doesn't advertise AltiVec, on purpose**, so libLLVM (pulled in by Mesa's swrast) died at the first vector instruction. Our patch to advertise it was the wrong fix: it is what made pixman and libjpeg-turbo pick vector code that VMX128 can't run (the two items above). Keep AltiVec off and build without libLLVM instead.
- **A release build turns compiler aborts into GPU hangs.** ir2 (the a2xx shader compiler) reports unsupported shaders with `assert`. With `-Db_ndebug=true` the asserts vanish, the half-compiled shader reaches the GPU, and Xenos wedges until a power cycle. In the first full dEQP run after switching to release, a `tmp_array` case that used to crash hung the GPU instead. ir2's sources now keep their asserts.
- **Mesa's meson cross file is read at configure time.** Changing `c_args` needs `meson setup --wipe`, and the wipe re-detects libglvnd in the sysroot: Mesa then builds `libEGL_mesa.so` instead of `libEGL.so`, the old `libEGL.so` stays installed, and the next change to a shared struct crashes every program in `eglInitialize`. Configure with `-Dglvnd=disabled`.
- **dEQP's CMake defaults `DE_CPU` to x86_64 when cross-compiling.** That forces little-endian, so every reference image built with `tcu::Surface::setPixel` was byte-reversed on the Xbox, and dEQP failed correct output. We then "fixed" the resolve byte order to match the broken references and broke real readback. Configure with `-DDE_CPU=DE_CPU_VANILLA` (and apply `conformance/vk-gl-cts-big-endian.patch`).
- **epoxy with a GLES-only Mesa** found desktop-GL entry points through the system's glvnd and cached NULL pointers, so glamor saw GL "0.0". Build Mesa with `-Dopengl=false` and point `LD_LIBRARY_PATH` at it.
- **The vertex fetch endian swap is per attribute, not per buffer.** We believed one 8in32 swap covered a whole vertex buffer and limited Xenos to 32-bit vertex formats. Every draw from glamor (`GL_SHORT` positions) then went through `u_vbuf`, which read the VBO back from write-combined memory and converted it: text and small fills ran 4-8x slower than they had to.
- **glamor copies overlapping areas through a temporary pixmap** unless the driver offers `GL_MESA_tile_raster_order`, and treats `glTextureBarrierNV` as cheap. freedreno's barrier was a full flush. Both cost a batch per scroll step until we changed them.
- **glamor's GLES path asserts little-endian.** See `xorg/glamor-be-gles.patch`, plus two Mesa patches that make `GL_BGRA` + `GL_UNSIGNED_INT_8_8_8_8_REV` legal on big-endian GLES and report it as the implementation read type. Without the second, glamor silently falls back to software for depth 24/32 and still "works".

- **"Invalidate all of DRAM" isn't.** The cache invalidate over all of memory looked like the safe choice and passed every test run on its own. Cases that sampled a CPU-written texture in a recycled buffer then failed only after other cases. Invalidate exact ranges (hardware rule 14).
- **A whole-GPU wait can hide a cache bug.** Waiting for everything before CPU maps fixed most of those failures, and we spent hours suspecting fence tracking. It worked because the flush put the upload and the draw in separate batches. Sleeping instead of flushing told the two apart in one run.

- **"Only render targets need tiling" was wrong on Xenos.** Linear textures follow a different mip layout (hardware rule 23). Tiling everything fixed about 240 dEQP cases. Tiling staging textures as well broke window copies in `xorg/xcopytest.c` (pixmap copies stayed correct); staging stays linear, and we don't yet know why.

- **Benchmarks and conformance didn't resize windows.** The in-batch texture barrier passed x11perf, xcopytest, a 7-minute Xfce soak and the full dEQP run, then hung the GPU the first time someone dragged a window corner. `xorg/xresize.c` is now part of the checks.
- **The kernel's ring-space wait doesn't count as a stall.** When the GPU stops while the ring is full, Xorg spins in the submit ioctl at 100% CPU and `fence_stalls` stays 0. Read `COHER_STATUS_HOST` (0x0a31) and `CP_RB_RPTR` with `xereg` to see where it stopped.

- **A change that passes its piglit tests can still break every dEQP case.** The first flat-shading fix took every `load_input` in a fragment shader as flat, which is true for the GLSL path piglit's flat tests use but not for others: every dEQP case with a varying failed (and ran ten times slower in image comparison). Run a dEQP sample before pushing a shader-state change, not only the tests it targets.

## Process

- **`pkill -f <pattern>` matches the shell that runs it** when the pattern appears in the command line. We killed our own SSH session and our own harness this way. Match exact process names (`pkill -x xbox-cycle`) or check `/proc/<pid>/cmdline`.
- **Orphaned wait loops and power-cycle scripts.** After killing harness runs, leftover loops kept pressing IR buttons and pinging. Two scripts cycling at once undid each other, and the console looked dead for an hour. `xbox-cycle` now takes a lock, `conformance/stop-run.sh` kills the harness and its cycle child, and the rule is one tracked background job.
- **Running a test on a GPU that's already wedged** blames the wrong test. Check `fence_timeouts`/`fence_stalls` before each batch; `gl-ready.sh` does.
- **Hang attribution can be one case late.** A case that submits work without waiting can leave the next one to hit the timeout. Confirm each hang class by running a case alone on a fresh boot.
- **Waits that restart on signals hide stalls.** Xorg's fence waits restart on SIGIO, so a per-call stall timer never fires. The kernel tracks stalls globally.
- **Replay passing doesn't clear a stream.** It clears the stream in replay's state. Twice the real cause was state from earlier work (see methodology).
- **freedreno's rd dump names files by fence number**, and without "full" it omits buffer contents, so replays fail on missing addresses.
- **Apt build-dep on the console runs it out of memory** (458 MB). Build in a chroot of the NFS root on the host with qemu-user binfmt.
- **A desktop session or leftover Xorg runs dEQP out of GPU memory.** GL buffers come from an 80 MB pool (48 MB before 2026-10-28), and an Xorg holds its share. Big texture cases then fail with `GL_OUT_OF_MEMORY`. It bit twice: an Xorg left over from a benchmark, then the Xfce session restarted by `perfcheck.sh`. `xperf.sh` stops its Xorg when it finishes, and `gl-ready.sh` stops the session and any Xorg before GL tests.
- **The Xbox has 458 MB.** dEQP's ETC1 tests run it out of memory and the console stops answering. They're skipped.

## Format descriptions are endian-specific

Mesa's `util_format_description` lists the channels of packed formats in memory order for the host. On big endian the first non-void channel of `Z24_UNORM_S8_UINT` is the 8-bit stencil, not the depth. fd2's `pipe2surface` took the number format from that channel, so packed depth textures were sampled as integers and every depth read 1.0, while `Z24X8_UNORM` (first non-void channel: depth) worked. Look at all channels, or at `util_format_has_depth`, before deciding anything from "the first channel".

## Don't widen the screen scissor past the framebuffer for the depth restore

The depth restore goes through the color path in 40-pixel partner strips. When the framebuffer width isn't a multiple of 80, some visible depth pixels sit in EDRAM columns past the width, so a restore stopping at the width leaves them stale. Restoring whole 80-pixel tiles with `PA_SC_SCREEN_SCISSOR` widened to match fixed a 64-pixel buffer and wedged the GPU on a 123-pixel one (the fence never completed; power cycle). It is harmless for GPU-only rendering, since EDRAM keeps its contents between batches, but data the CPU wrote into a depth/stencil buffer (stencil `glDrawPixels`) is lost in those columns.

Mesa 0062 fixes the depth half of this: after the strips, a pass whose pixel shader writes the sampled depth with `oDepth` covers just the columns the strips can't reach (the depth path places the pixels itself). Stencil in those columns is still lost, since Xenos has no stencil export.

## A console freeze is not always a GPU hang

Three times the console stopped answering ping and needed a power cycle, while `gpu_hung` never fired. The cause was a GL client whose command stream grew without end: each new 64 KB ring buffer came from `dma_alloc_coherent` once the pool was full, the kernel handed out all of RAM, and the box sat in the OOM killer's thrashing (zram swap). `ulimit -v` doesn't help, because buffer memory isn't counted against the process. `xegpu.msm_min_free_mb` (48 by default) now refuses buffers from outside the pool when RAM runs low, so the client fails and the console stays up. With `msm_debug=1` every buffer allocation is logged, which is how the ring growth showed up (1,800 buffers of 64 KB with flags `0x80002`).
