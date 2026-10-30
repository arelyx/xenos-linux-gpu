# Handoff (2026-10-30)

Where the Xbox 360 Linux / Xenos GPU project stands when this session closed, and how to pick it up. The detail is in `PROGRESS.md` (this folder, chronological) and the public repo `~/peer_pressure/xenos-linux-gpu` (github.com/arelyx/xenos-linux-gpu); start with its `README.md`, `docs/apps.md` and `docs/setup.md`.

## State

- **Default kernel:** 64 KB pages, `/srv/tftp/vmlinux` = `vmlinux-64k-nopstore`. Stable.
- **Boot args:** `kboot.conf` (TFTP and USB stick, kept in sync) with `coherent_pool=6M`.
- **Mesa:** patches through 0086 in the repo, installed in `/srv/nfs/xenon/opt/mesa-xenos`.
- **Conformance:** dEQP-GLES2 17,062 of 17,075; piglit GL 2.0 list 1,065 pass. The user called conformance done (2026-10-29); the focus moved to real applications.
- **Desktop:** IceWM on glamor. Mesa 0085 fixed corruption (black boxes, trails) from border emulation. `xwd` screenshots don't show what reaches the TV; check the screen.
- **Memory:** about 200 MB available with the desktop (GPU pool 48 MB, `slab-trim` service on).

## Applications (`docs/apps.md`)

- **Browser:** surf (WebKitGTK 4.1) with WebKit on software GL: `NO_XENOS_MESA=1 /root/run-browser.sh surf URL`. Light pages work; heavy ones thrash.
  - **GPU-path crash:** on our Mesa, WebKit's web process aborts on any page with a text field. The cause is unknown, and ppc64 has no debug symbols. Reproducer: `/opt/piglit-runs/g1.html`.
  - **Not viable:** Epiphany is too big (GTK 4), and Firefox can't be installed (`libvpx` missing in ppc64 ports).
- **Video:** mpv on GLES 2.0 (`/root/.config/mpv/mpv.conf`); YouTube via `yt-play URL` (H.264, at most 720p and 30 fps). 480p plays without drops.
- **Audio:** `snd-xenon` (kernel patch 0003) ported, with HDMI audio enabled; DMA runs at 48 kHz. **Not yet heard on the TV.** Test with `mpv /opt/piglit-runs/media/av-720p.mp4` (440 Hz tone).
- **Games:** OpenArena timedemo at 32 fps, CPU bound. Profile with `tools/pcsample.c` (the kernel has no perf events).

## Open items

1. **4 KB-page kernel** (`/srv/tftp/vmlinux-4k-elf32`). It gives about 294 MB available, but it freezes:
   - silently under network/NFS-heavy load (dEQP, browser);
   - sometimes during boot, so it never comes up.
   It needs someone watching the TV during a freeze, to tell a dead network from a dead console. Build notes are in `docs/setup.md`: package it with `objcopy -O elf32-powerpc`, and apply patch 0002 (AltiVec not advertised).
2. **Audio by ear** (above).
3. **WebKit GPU-path abort:** needs a debug WebKit build.
4. **IR power-on:** lands in Aurora often. If `xbox-cycle` keeps missing, a manual "power off, wait until off, +25 s, Media Center press" usually works.
5. **pstore:** a 64 KB kernel with pstore/ramoops hung at boot (`vmlinux-pstore-hang`). Cause unknown.
6. **Optional:** skip program re-upload on texture-only changes (at most ~3.5% CPU in OpenArena); verify on screen.

## Rules that still apply

- Never write NAND or `/dev/mtd*`.
- Don't write to the console's USB stick (it holds `kboot.conf` and the softmod payload) except `kboot.conf` via Aurora FTP.
- Install packages from the host in a qemu chroot of `/srv/nfs/xenon`, not on the console: two console freezes came from dpkg on NFS.
- Don't count on swap.
- Mesa commits carry `Generated-by: LLM`.
- Push only to the user's repo.
