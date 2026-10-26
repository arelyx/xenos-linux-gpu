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
- **The kernel didn't advertise AltiVec at all** before our patch, so libLLVM (pulled in by Mesa's swrast) died at the first vector instruction.
- **Mesa's meson cross file is read at configure time.** Changing `c_args` needs `meson setup --wipe`, and the wipe re-detects libglvnd in the sysroot: Mesa then builds `libEGL_mesa.so` instead of `libEGL.so`, the old `libEGL.so` stays installed, and the next change to a shared struct crashes every program in `eglInitialize`. Configure with `-Dglvnd=disabled`.
- **dEQP's CMake defaults `DE_CPU` to x86_64 when cross-compiling.** That forces little-endian, so every reference image built with `tcu::Surface::setPixel` was byte-reversed on the Xbox, and dEQP failed correct output. We then "fixed" the resolve byte order to match the broken references and broke real readback. Configure with `-DDE_CPU=DE_CPU_VANILLA` (and apply `conformance/vk-gl-cts-big-endian.patch`).
- **epoxy with a GLES-only Mesa** found desktop-GL entry points through the system's glvnd and cached NULL pointers, so glamor saw GL "0.0". Build Mesa with `-Dopengl=false` and point `LD_LIBRARY_PATH` at it.
- **glamor's GLES path asserts little-endian.** See `xorg/glamor-be-gles.patch`, plus two Mesa patches that make `GL_BGRA` + `GL_UNSIGNED_INT_8_8_8_8_REV` legal on big-endian GLES and report it as the implementation read type. Without the second, glamor silently falls back to software for depth 24/32 and still "works".

## Process

- **`pkill -f <pattern>` matches the shell that runs it** when the pattern appears in the command line. We killed our own SSH session and our own harness this way. Match exact process names (`pkill -x xbox-cycle`) or check `/proc/<pid>/cmdline`.
- **Orphaned wait loops and power-cycle scripts.** After killing harness runs, leftover loops kept pressing IR buttons and pinging. Two scripts cycling at once undid each other, and the console looked dead for an hour. `xbox-cycle` now takes a lock, `conformance/stop-run.sh` kills the harness and its cycle child, and the rule is one tracked background job.
- **Running a test on a GPU that's already wedged** blames the wrong test. Check `fence_timeouts`/`fence_stalls` before each batch; `gl-ready.sh` does.
- **Hang attribution can be one case late.** A case that submits work without waiting can leave the next one to hit the timeout. Confirm each hang class by running a case alone on a fresh boot.
- **Waits that restart on signals hide stalls.** Xorg's fence waits restart on SIGIO, so a per-call stall timer never fires. The kernel tracks stalls globally.
- **Replay passing doesn't clear a stream.** It clears the stream in replay's state. Twice the real cause was state from earlier work (see methodology).
- **freedreno's rd dump names files by fence number**, and without "full" it omits buffer contents, so replays fail on missing addresses.
- **Apt build-dep on the console runs it out of memory** (458 MB). Build in a chroot of the NFS root on the host with qemu-user binfmt.
- **The Xbox has 458 MB.** dEQP's ETC1 tests run it out of memory and the console stops answering. They're skipped.
