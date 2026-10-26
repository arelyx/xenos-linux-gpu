/* SPDX-License-Identifier: GPL-2.0 */
/*
 * xegpu: minimal user-space access to the Xenos GPU, for development.
 * Coexists with the xenos DRM driver: DRM keeps the display, xegpu hands
 * user space the GPU registers and physically contiguous GPU memory.
 */
#ifndef XEGPU_UAPI_H
#define XEGPU_UAPI_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint32_t __u32;
typedef uint64_t __u64;
#endif

#define XEGPU_REG_SIZE      0x20000          /* BAR0, mmap at offset 0 */
#define XEGPU_BO_MMAP_SHIFT 28               /* bo N mmaps at N << 28 */

struct xegpu_info {
	__u64 reg_phys;
	__u32 reg_size;
	__u32 scanout_addr;   /* D1GRPH_PRIMARY_SURFACE_ADDRESS (0x6110) at open */
	__u32 scanout_pitch;  /* 0x6120, in pixels */
	__u32 scanout_width;  /* 0x6134 */
	__u32 scanout_height; /* 0x6138 */
	__u32 pad;
};

struct xegpu_alloc {
	__u64 size;           /* in: bytes, rounded up to page size */
	__u32 handle;         /* out */
	__u32 gpu_addr;       /* out: physical address as seen by the GPU */
	__u64 mmap_offset;    /* out: pass to mmap() on the same fd */
};

#define XEGPU_IOC_INFO     _IOR('X', 1, struct xegpu_info)
#define XEGPU_IOC_ALLOC    _IOWR('X', 2, struct xegpu_alloc)
#define XEGPU_IOC_FREE     _IOW('X', 3, __u32)
#define XEGPU_IOC_QUIESCE  _IO('X', 4)   /* halt CP, stop writeback, restore scanout */

/* Kernel-owned command processor (v2). The kernel loads microcode, owns the
 * primary ring and the fence writeback. User space builds command buffers
 * in its own buffers and submits them as indirect buffers. */
struct xegpu_submit {
	__u32 gpu_addr;       /* in: start of the command buffer (dword aligned) */
	__u32 ndw;            /* in: length in dwords */
	__u64 fence;          /* out: fence value, signalled when the CP has finished */
};
struct xegpu_wait {
	__u64 fence;          /* in */
	__u32 timeout_ms;     /* in */
	__u32 pad;
};
#define XEGPU_IOC_SUBMIT   _IOWR('X', 5, struct xegpu_submit)
#define XEGPU_IOC_WAIT     _IOW('X', 6, struct xegpu_wait)   /* -ETIME on timeout */
#define XEGPU_IOC_CP_INIT  _IO('X', 7)    /* (re)initialise the CP; implied by first SUBMIT */

#endif
