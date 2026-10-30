# Applications

What runs on the console as of 2026-10-29, how to start it, and what limits it. Measurements are from one Trinity slim at 1280x720 over HDMI, with the IceWM desktop on glamor.

## Memory comes first

Linux sees 458 MB. Before any application starts, the kernel holds a large share of it: the GPU pool (`xegpu pool_mb=48`), the atomic DMA pool (`coherent_pool=6M`, which grows to 16 MB), and about 52 MB of unreclaimable slab after `slab-trim`. With the desktop running, about 200 MB is available. Swap is not counted on: zram exists, but a workload that leans on it thrashes, and NFS swap is an emergency backstop at most.

Run anything large under a memory cap, so that running out kills the application and not the console:

```
systemd-run --scope -p MemoryMax=300M -p MemorySwapMax=0 <command>
```

`/root/run-browser.sh` wraps that for browsers.

The biggest remaining lever is a 4 KB-page kernel (every small allocation, file and mapping currently costs a 64 KB page). One was built but dies early in boot, before anything reaches the network; it needs someone watching the screen to debug.

## Web browsing

| Browser | Result |
|---|---|
| Firefox (ESR 128, 146) | Not installable: the ppc64 ports archive lacks the `libvpx` versions they depend on. |
| Epiphany (GTK 4, WebKitGTK 6.0) | Killed by the OOM killer before loading a page: its UI process alone used 156 MB. |
| surf (GTK 3, WebKitGTK 4.1) | Works with WebKit on software GL. |
| NetSurf, Dillo | Work; no modern JavaScript. |

Start surf with WebKit on software rendering (the desktop stays on the GPU):

```
NO_XENOS_MESA=1 /root/run-browser.sh surf https://example.com
```

On our Mesa, WebKit's web process aborts on any page with an editable text field (most real sites have a search box). It opens our GBM device, walks the buffer formats, and aborts without a message; with Debian's software Mesa it doesn't. Cause unknown: the libraries have no ppc64 debug symbols. Reproducer: `/opt/piglit-runs/g1.html`.

With software WebKit, simple pages load and use about 215 MB across surf's three processes. A heavy page such as a Wikipedia article pushes available memory under 30 MB and the console starts thrashing. One tab of light pages is the realistic target until the 4 KB-page kernel works.

## Video

The Xenos has no video decoder; ffmpeg decodes on the CPU and mpv draws through the GPU (GLES 2.0).

| Clip (20 s, CPU decode only, 6 threads) | Decode speed |
|---|---|
| H.264 480p | 119 fps |
| H.264 720p | 62 fps |
| VP9 480p | 67 fps |

Playback through mpv: H.264 480p drops no frames; 720p drops about 3%. 60 fps sources drop most frames.

`/root/.config/mpv/mpv.conf` sets the GPU output (`--opengl-es=yes`: desktop GL is 2.0, mpv needs 2.1), CPU decoding, ALSA audio and `profile=fast`.

YouTube and other yt-dlp sites:

```
yt-play <url> [mpv options]
```

`yt-play` keeps to H.264 at no more than 720p and 30 fps. mpv's own yt-dlp hook gets empty output on this system, so the script resolves the streams with `yt-dlp -g` and hands them to mpv. Big Buck Bunny came down as 480p30 and played without dropped frames, audio in sync.

Debian's ffmpeg for ppc64 has no AltiVec code, and the Xenon's VMX128 lacks the integer multiply-add instructions ffmpeg's H.264 AltiVec code is built on, so SIMD decoding isn't an option.

## Audio

`snd-xenon` (kernel patch 0003) is Free60's driver ported to 6.18, built as modules and loaded at boot. It plays 48 kHz stereo 16-bit: the DMA position advances at 48 kHz. Whether sound reaches the TV has not been confirmed by ear. Test:

```
mpv /opt/piglit-runs/media/av-720p.mp4
```

## Games

OpenArena (ioquake3, OpenGL 1 renderer) runs. Its timedemo averages 32 fps at both 1280x720 and 640x480, so the GPU isn't the limit. A sampling profile (`tools/pcsample.c`) puts 45% of the main thread in Mesa, spread thin across the GL entry points, the state tracker and this driver, 31% in the game's renderer and about 18% in libc.

```
/usr/games/openarena
```
