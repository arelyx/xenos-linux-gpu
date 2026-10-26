# Licenses

This repository mixes code under the licenses of the projects it extends.

| path | license | notes |
|---|---|---|
| `kernel/` | GPL-2.0-only | Linux kernel module and patches. `xegpu_ucode.h` (Xenos CP microcode) and `xegpu_blit_rec.h` (a recorded `Xe_Init` command stream) come from Free60's libxenon. |
| `userspace/` | GPL-2.0-only | port of libxenon's `xe.c`/`edram.c` and tools built on it |
| `mesa/patches/` | MIT | patches against Mesa |
| `xorg/` | MIT/X11 | patch against xorg-server |
| `conformance/vk-gl-cts-big-endian.patch` | Apache-2.0 | patch against Khronos VK-GL-CTS |
| everything else (tools, tests, harness, scripts, irblaster, softmod helpers) | GPL-2.0-only | |
| `docs/` | CC BY 4.0 | |

No Microsoft code, keys, NAND images or softmod binaries are included. The softmod change is described, not distributed.
