# Setup

What we ran, and how to rebuild it. Paths and addresses are from our setup; change them for yours. Scripts read `XBOX_HOST` (Linux on the console, default 10.0.0.206), `XBOX_DASH_HOST` (the dashboard's address, default 10.0.0.205), `XBOX_SSH_OPTS` (for example `-i ~/.ssh/xbox`) and `XBOX_NFS_ROOT` (default `/srv/nfs/xenon`).

## Console

- Xbox 360 Trinity (slim), dashboard 17559.
- Peer Pressure softmod v1.0.477 with Aurora and the bundled "Peer Pressure compatible" Xell (xell-reloaded fork). Don't replace that Xell; it is what reads `kboot.conf`.
- A USB stick with `kboot.conf` at its root. Xell reads the stick first and then fetches the kernel over TFTP.

## Boot chain

```
power on (MCE remote, reason 0x31) -> Peer Pressure -> Xell -> kboot.conf (USB) -> TFTP vmlinux -> NFS root
```

`config/boot/kboot.conf.example` is our `kboot.conf`. Entry `debian` boots Debian unstable ppc64 from NFS:

```
debian="vmlinux root=/dev/nfs nfsroot=<host>:/srv/nfs/xenon,vers=3,tcp,nolock ip=dhcp rw console=tty0 panic=30 maxcpus=6 coherent_pool=16M video=xenosfb"
```

### The one-byte softmod patch (optional, for unattended power cycles)

Peer Pressure boots Xell when `EjectButtonAction=1` and the SMC power-on reason is `0x12` (physical eject). Its `Hdd1:\PeerPressure\System\KernelExpansion.bin` checks the reason with `cmplwi cr6,r11,0x12`. On our copy (md5 `63a1be23ea3192287a92fd3864a02b2b`) that immediate is the byte at offset `0x10f`. We changed it to `0x31`, the reason a Windows Media Center remote's power button produced on this console. Then:

- MCE remote power: Xell, then Linux.
- Physical eject and Xbox remote: the dashboard.

There's no integrity check on that file. Keep a backup. A Peer Pressure update overwrites it. To find your console's reasons, boot Linux and run `softmod/smc.py powerup` after each kind of power-on. `softmod/fatxcfg.py` reads and rewrites FATX files in place from Linux (same clusters, size field kept, backup and read-back verify). It's how we edited and restored `Config.ini` without the dashboard.

## Host services

- TFTP (tftpd-hpa) serving `vmlinux`.
- NFS v3 export of the root filesystem (`/srv/nfs/xenon`, pinned mountd/lockd/statd ports).
- Debian unstable ppc64 from debian-ports via `debootstrap`; the second stage runs under qemu-user.
- netconsole listener on UDP 6666 helped a lot during kernel bring-up.

## Kernel

Linux 6.18.58 with Free60's xenon patch (`linux-kernel-xbox360`), built with the `xenon-` toolchain from Free60's libxenon container. Apply:

- `kernel/patches/0001-sis190-tx-wake-race.patch`: without it the network dies the first time the TX ring fills (NFS root boot).
- `kernel/patches/0002-xenon-altivec.patch`: advertise AltiVec, or every vector instruction gets SIGILL.

Config notes: `CONFIG_XENOS_RB=m` (built in, it claims the GPU before the DRM driver). `coherent_pool=16M` on the command line.

### xegpu module

```
make -C <kernel tree> ARCH=powerpc CROSS_COMPILE=xenon- M=$PWD/kernel/xegpu modules
```

`kernel/xegpu/Makefile` builds one object; `xegpu.c` includes `xenos_drm.c`, which includes `msm_compat.c`. Parameters worth knowing:

| parameter | default | |
|---|---|---|
| `msm_compat` | 0 | expose the msm DRM interface Mesa needs |
| `gpu_blit` | 1 after bring-up | GPU linear-to-tiled blit for CPU framebuffers |
| `direct_scanout` | 1 | scan out GL (tiled) framebuffers in place |
| `msm_pool` | 1 | GL buffers from the boot-time contiguous pool (48 MB) |
| `msm_min_free_mb` | 48 | refuse GL buffers from outside the pool when less system RAM than this is available. A GL client that allocated command buffers without end (a driver bug) used to take all RAM and freeze the console in the OOM killer's thrashing; now its allocation fails and only the client dies |
| `pool_free_kb` | read-only | free and total pool memory, `free of total` in KB (idle IceWM desktop: 37568 of 49152; no leak after a 2,145-case dEQP sample and a piglit run) |
| `msm_zero_bo` | 0 | clear pool buffers when they are allocated. The pool is recycled between processes, so without it one process can read what another left behind (piglit `security/initialized-*` warn). Costs about 12% in x11perf `-putimage500`. |
| `msm_state_init` | 1 | run libxenon's `Xe_Init` stream before the first GL submit after a CP init |
| `sq_gpr_pix`, `sq_gpr_vtx` | 64, 64 | shader GPR split written after the state init |
| `fence_timeouts`, `fence_stalls`, `gpu_hung` | read-only | test harnesses check these |

`scripts/xegpu-up.sh` (run by `config/systemd/xegpu.service`) loads the module, trains EDRAM with `userspace/tests/xeinit`, swaps the display from the built-in xenos DRM driver to `xegpu_drm`, and turns on the GPU scanout blit. For GL: `XEGPU_OPTS=msm_compat=1 xegpu-up.sh`.

`kernel/xenon_idle` is a separate module: a low-priority idle loop so an idle SMT thread stops stealing its sibling's cycles.

### User space tools

```
cd userspace && make     # on the console, or cross with CC=powerpc64-linux-gnu-gcc and -mcpu=970 -mno-altivec
```

`xeinit` (EDRAM training, once per boot), `xecube` (libxenon's spinning cube through the kernel), `xereplay` (replay `.xrp` blobs), `msmtest`.

## Mesa

Base: Mesa main at `ab1502f` (October 2026). Apply `mesa/patches/*.patch` with `git am`. Cross-build:

```
meson setup build-mesa src/mesa --cross-file mesa/mesa-ppc64-cross.ini \
  -Dbuildtype=release -Db_ndebug=true -Dprefix=/opt/mesa-xenos -Dgallium-drivers=freedreno \
  -Dfreedreno-kmds=msm -Dvulkan-drivers= -Dplatforms=x11 -Dglx=dri -Degl=enabled \
  -Dgbm=enabled -Dgles1=disabled -Dgles2=enabled -Dopengl=true -Dllvm=disabled \
  -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=enabled -Dtools=drm-shim -Dglvnd=disabled
ninja -C build-mesa && DESTDIR=/srv/nfs/xenon ninja -C build-mesa install
```

`-Dplatforms=x11` gives EGL clients under X (DRI3); it needs the X and xcb development packages in the sysroot (`libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxcb-dri2-0-dev libxcb-dri3-dev libxcb-present-dev libxcb-shm0-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-randr0-dev libxcb-glx0-dev libxshmfence-dev libxxf86vm-dev libxrandr-dev`). The cross file uses `-mcpu=970 -mtune=cell -mno-altivec`. Don't use `-mcpu=cell` (see footguns). On the console: `LD_LIBRARY_PATH=/opt/mesa-xenos/lib`. Useful environment: `XENOS_DEBUG=1` (layouts and maps), `XENOS_COHER=<hex>`, `XENOS_RESOLVE_<FORMAT>=<endian><swap>`, `XENOS_INTERP=<hex>` (experiment hooks), `XENOS_FLUSH_BARRIER=1` (texture barriers flush the batch, as upstream freedreno does), `XENOS_BLIT_MIN_PIXELS=<n>` (texture transfers smaller than n pixels use the CPU path), `XENOS_READ_BLIT_MIN_PIXELS=<n>` (the same for glReadPixels), `XENOS_FULL_INVAL=1` (invalidate all of DRAM before every draw, as before exact ranges), `FD_MESA_DEBUG=disasm`, `FD_RD_DUMP=enable,full FD_RD_DUMP_PATH=...`.

## Xorg + glamor

Debian's xorg-server 21.1.25 with `xorg/glamor-be-gles.patch`. We only rebuild `libglamoregl.so` (`xorg/build-glamor.sh`, run in a chroot of the NFS root on the host via qemu-user; building on the console runs it out of memory) and load it ahead of the system modules:

```
Xorg :0 -configdir /etc/X11/glamor-test -modulepath /opt/xegpu-xorg/modules,/usr/lib/xorg/modules
```

`config/xorg/10-xenos.conf` selects modesetting with `AccelMethod glamor`. Environment: `LD_LIBRARY_PATH=/opt/mesa-xenos/lib`, `PIXMAN_DISABLE=vmx`. Load xegpu with `msm_compat=1` first.

## dEQP

VK-GL-CTS from Khronos, `external/fetch_sources.py`, then apply `conformance/vk-gl-cts-big-endian.patch` and configure with the toolchain file:

```
cmake <cts> -G Ninja -DCMAKE_TOOLCHAIN_FILE=conformance/ppc64-toolchain.cmake \
  -DDEQP_TARGET=surfaceless -DDE_CPU=DE_CPU_VANILLA \
  -DSELECTED_BUILD_TARGETS="deqp-gles2" \
  -DCMAKE_C_FLAGS="-mcpu=970 -mtune=cell -mno-altivec" -DCMAKE_CXX_FLAGS="-mcpu=970 -mtune=cell -mno-altivec"
ninja deqp-gles2
```

`-DDE_CPU=DE_CPU_VANILLA` matters: CMake's default (x86_64) makes dEQP assume little-endian. Install `deqp-gles2`, its `gles2/` data directory and the must-pass list (`external/openglcts/data/gl_cts/data/mustpass/gles/aosp_mustpass/main/gles2-main.txt`) under `/opt/deqp` on the console, with `conformance/run-watched.sh` and `gl-ready.sh`. Then see [conformance.md](conformance.md).

## Keeping RAM free (458 MB)

The default desktop is IceWM (`config/systemd/icewm-glamor.service`). Xfce fits but leaves too little for anything else. Userspace settings that help, all in `config/`:

- `tmpfiles/zswap-off.conf`: swap is already zram (compressed RAM). zswap in front of it compressed the same pages a second time and held a 38 MB pool.
- `systemd/udev-prune-legacy-pty.service`: the kernel creates 512 legacy BSD pty devices. udev keeps a small record file for each in /run, and with 64 KB pages each file costs a whole page: 16 MB of RAM for devices nobody uses.
- `journald/small.conf`: journal in RAM only, capped at 8 MB.
- `icewm/preferences` (`~/.icewm/preferences`): xterm as IceWM's terminal. xfce4-terminal plus the xfconfd it starts take about 45 MB.
- cron and rpcbind disabled (the NFS root is mounted with `nolock`, so it doesn't need rpcbind).

Bigger savings would need kernel changes: 4 KB pages instead of 64 KB (every small file, page-table page and partly used allocation costs 64 KB now), `pty.legacy_count=0` or `CONFIG_LEGACY_PTYS=n`, a smaller `coherent_pool` than 16M, and a smaller GPU pool than 48 MB.
