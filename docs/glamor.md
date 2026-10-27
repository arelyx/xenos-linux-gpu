# Getting X accelerated with glamor

Xorg's modesetting driver uses glamor for acceleration: every X drawing operation becomes GLES draws into textures. On Xenos that meant six separate problems, found one after another. They're listed in the order they showed up, because each one hid the next.

1. **glamor saw GL "0.0".** Our Mesa had no desktop GL library, so epoxy resolved desktop-GL entry points through the system's glvnd and cached NULL pointers. Fix: build Mesa GLES-only (`-Dopengl=false`). glamor then picks its GLES 2.0 path.

2. **No room for the front buffer.** Xorg's 1280x720 front buffer (4 MB) failed to allocate from the fragmented contiguous heap and Mesa crashed on a NULL BO. Fix: GL buffers come from xegpu's boot-time pool.

3. **glamor's GLES path is little-endian only** (`assert(X_BYTE_ORDER == X_LITTLE_ENDIAN)` in `glamor_setup_formats`). X's ARGB32 words are, in GLES terms, `GL_BGRA` + `GL_UNSIGNED_INT_8_8_8_8_REV` on a big-endian CPU. `xorg/glamor-be-gles.patch` uses that, and two Mesa patches make the pair legal in GLES on big-endian hosts (the ES-specific check and the generic type check) and return it as `GL_IMPLEMENTATION_COLOR_READ_TYPE`. Without the read-type patch glamor logs "Falling back to software" for depth 24/32 and quietly does everything on the CPU.

4. **The first submit wedged the GPU.** Not a glamor bug: every GL client wedged unless libxenon's `Xe_Init` state stream had run since the last CP initialisation. The kernel now runs it before the first GL submit. (dEQP showed the same hang, which is how we found it.)

5. **Partial updates landed in the corner.** freedreno bins only the scissor-bounded area of a batch. Xenos has no copy-destination offset, so a bin at (x, y) resolved to (0, 0). Xenos now always renders whole-framebuffer bins. This costs performance and is the first thing to optimise.

6. **The first anti-aliased draw wedged the GPU.** Bisecting clients (`conformance/xg-one.sh`) showed xclock with Render hangs and `xclock -norender` doesn't. `FD_MESA_DEBUG=disasm` gave the composite shader: straight-line code, 23 GPRs. `gltest/glregs` found the limit at 16 pixel-shader registers, which is what libxenon's `Xe_Init` leaves in `SQ_GPR_MANAGEMENT`. The kernel now splits the pool 64/64.

Result: xclock, xeyes (Shape), xlogo and xterm (Xft text) render correctly, with depth 24/32 accelerated, no fence timeouts or stalls, and the front buffer scanned out in place (`direct_scanout`: the GPU's tiled layout is what the display reads, so no copy). `docs/images/glamor-scanout-2026-10-26.png` is the scanout buffer read back from memory; `glamor-xwd-2026-10-26.png` is X's own view.

## GL applications under X

EGL/GLES2 clients render on the GPU in X windows through DRI3: the client allocates its back buffer, exports it as a dma-buf, and glamor imports it. Two things were missing. Mesa had been built without the X11 platform (`-Dplatforms=x11` now). And freedreno couldn't import its own a2xx buffers: the import path assumed a pitch alignment of `gmem_align_w` (128 pixels) where a2xx uses 32, so every `xcb_dri3_pixmap_from_buffer` failed with BadAlloc and the client crashed without a back buffer. On Xenos an imported buffer is also tiled, since that is the only layout it renders. `gltest/glprime.c` exercises the export/import on its own. glmark2-es2 in an 800x600 window: build 1020 FPS, texture 1016, jellyfish 680 (`docs/images/glmark2-x11-jellyfish-2026-10-26.png`). Clients need `LD_LIBRARY_PATH=/opt/mesa-xenos/lib`; the xfce-glamor unit sets it for the session. There is no GLX: Mesa is GLES-only, so desktop-GL programs still get the system's software renderer.

## Running it

```
# on the console
XEGPU_OPTS=msm_compat=1 /opt/xegpu/xegpu-up.sh
echo 0 > /sys/module/xegpu/parameters/gpu_blit
LD_LIBRARY_PATH=/opt/mesa-xenos/lib PIXMAN_DISABLE=vmx \
  Xorg :0 vt1 -configdir /etc/X11/glamor-test -modulepath /opt/xegpu-xorg/modules,/usr/lib/xorg/modules
```

`conformance/xg-dump.sh` and `xg-one.sh` wrap this for testing.

## Open

- Performance: see [performance.md](performance.md). Restores and resolves now cover only each batch's scissor bounds.
- A full Xfce session runs on glamor (`config/systemd/xfce-glamor.service`; `docs/images/xfce-glamor-scanout-2026-10-26.png` is its scanout), but hasn't been used for long sessions yet.
- glamor still falls back to software for depth 8, 15 and 16.
