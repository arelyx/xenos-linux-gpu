// SPDX-License-Identifier: GPL-2.0
/*
 * xegpu - development access to the Xbox 360 Xenos GPU from user space.
 *
 * Does not bind the PCI device (the xenos DRM driver owns it and keeps the
 * display running). Provides:
 *   - mmap of the GPU register BAR (uncached)
 *   - physically contiguous, uncached buffers the GPU can address
 *   - a safety net on close: halt the command processor, stop CP memory
 *     writeback, restore the scanout registers, then free every buffer.
 *
 * Xenos has no GPU MMU. Anything user space puts in a command stream is a
 * physical address. This module is a development tool for a trusted root
 * user, not a security boundary.
 */
#include <linux/module.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/genalloc.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/uaccess.h>
#include <asm/io.h>

#include "../include/xegpu_uapi.h"
#include "xegpu_ucode.h"

#define PCI_VENDOR_MS 0x1414

struct xegpu_bo {
	u32 handle;
	bool from_pool;
	size_t size;
	void *vaddr;
	dma_addr_t dma;
};

struct xegpu_file {
	struct idr bos;
	struct mutex lock;
	u32 saved_scanout[4];
	bool scanout_saved;
	bool legacy_cp;   /* user space drove the CP registers itself */
};

static struct pci_dev *xe_pdev;
static void __iomem *xe_regs;
static resource_size_t xe_reg_phys;
static DEFINE_MUTEX(xe_open_lock);

/* kernel-owned command processor state */
#define RB_BYTES     0x8000          /* 32KB primary ring, CP_RB_CNTL size field 0xC */
#define RB_DW        (RB_BYTES / 4)
#define WB_BYTES     0x10000
#define WB_RPTR      0x0             /* CP read pointer writeback */
#define WB_SCRATCH   0x100           /* SCRATCH_ADDR: scratch registers are written back here */
#define FENCE_REG    5               /* SCRATCH_REG5 (0x15f4); libxenon itself writes SCRATCH_REG0 */
static DEFINE_MUTEX(cp_lock);
static bool cp_ready;
static u32 *rb_virt, *wb_virt;
static dma_addr_t rb_dma, wb_dma;
static u32 rb_wptr;
static u32 fence_emitted, fence_hi;   /* 32-bit fence in hw, 64-bit to user space */

/* fence interrupts: the CP raises RB_INT from a PM4 INTERRUPT packet after each fence */
#define REG_CP_INT_CNTL      0x07c8
#define REG_CP_INT_STATUS    0x07cc
#define REG_CP_INT_ACK       0x07d0
#define REG_RBBM_INT_STATUS  0x0ed4
#define REG_RBBM_INT_ACK     0x0ed8
#define REG_MASTER_INT       0x0edc
static int intr_opcode = 0x40;       /* a2xx CP_INTERRUPT; tested on hardware. (Xenia lists 0x54, which raises nothing.) */
module_param(intr_opcode, int, 0644);
static int irq_count;
module_param(irq_count, int, 0444);
static int use_irq = 1;
module_param(use_irq, int, 0644);
static DECLARE_WAIT_QUEUE_HEAD(fence_wq);
static bool irq_requested;
static bool gpu_hung;
module_param(gpu_hung, bool, 0444);
/* fences that timed out since load: on Xenos a missed fence has always meant a wedged GPU,
 * so test harnesses treat any nonzero value as "power-cycle before the next test" */
static unsigned int fence_timeouts;
module_param(fence_timeouts, uint, 0444);
/* waits that saw no fence progress for 5 s (Xorg waits without a timeout, so a wedge shows
 * here rather than in fence_timeouts) */
static unsigned int fence_stalls;
module_param(fence_stalls, uint, 0444);
static bool blit_init_needed_flag;
static bool xe_busy;

static const u16 xe_ids[] = { 0x5811, 0x5821, 0x5831, 0x5841 };
static const u32 scanout_regs[4] = { 0x6110, 0x6120, 0x6134, 0x6138 };

static inline u32 rr(u32 off) { return in_be32(xe_regs + off); }
static inline void wr(u32 off, u32 v) { out_be32(xe_regs + off, v); }

static void xegpu_quiesce(struct xegpu_file *xf)
{
	int i, t;

	wr(REG_CP_INT_CNTL, 0);
	/* halt micro engine + prefetch parser */
	wr(0x07d8, 0x1000FFFF);
	udelay(100);
	/* CP_RB_CNTL.RB_NO_UPDATE: stop read pointer writeback to memory */
	wr(0x0704, rr(0x0704) | 0x08000000);
	/* SCRATCH_UMSK = 0: stop scratch register writeback */
	wr(0x0770, 0);
	for (t = 0; t < 1000 && (rr(0x1740) & 0x80000000); t++)
		udelay(100);
	if (rr(0x1740) & 0x80000000)
		pr_warn("xegpu: GPU still busy after halt (RBBM_STATUS %08x)\n", rr(0x1740));

	if (xf && xf->scanout_saved) {
		for (i = 0; i < 4; i++)
			if (rr(scanout_regs[i]) != xf->saved_scanout[i])
				wr(scanout_regs[i], xf->saved_scanout[i]);
	}
}


static int wait_gui_idle(const char *what)
{
	int t;
	for (t = 0; t < 20000; t++) {
		if (!(rr(0x1740) & 0x80000000))
			return 0;
		udelay(50);
	}
	pr_err("xegpu: GPU busy (%s), RBBM_STATUS %08x\n", what, rr(0x1740));
	return -ETIME;
}

/* libxenon Xe_pSetupRingbuffer / Xe_pLoadUcodes / Xe_pResetCP / Xe_pMasterInit */
static void cp_setup_ring(void)
{
	wr(0x0704, rr(0x0704) | 0x80000000);
	wr(0x017c, 0);
	wr(0x0714, 0);
	wr(0x0704, rr(0x0704) & ~0x80000000);
	wr(0x0704, 0xC | 0x8020000);
	wr(0x0700, lower_32_bits(rb_dma));
	wr(0x0718, 0x10);
}

static int cp_init_locked(void)
{
	int i, rc;

	if (!rb_virt) {
		rb_virt = dma_alloc_coherent(&xe_pdev->dev, RB_BYTES, &rb_dma, GFP_KERNEL);
		wb_virt = dma_alloc_coherent(&xe_pdev->dev, WB_BYTES, &wb_dma, GFP_KERNEL);
		if (!rb_virt || !wb_virt)
			return -ENOMEM;
	}
	memset(rb_virt, 0, RB_BYTES);
	memset(wb_virt, 0, WB_BYTES);

	if ((rr(0x0e6c) & 0xF00) != 0xF00)
		pr_warn("xegpu: unexpected 0x0e6c %08x\n", rr(0x0e6c));
	rc = wait_gui_idle("pre-init");
	if (rc) {
		/* A wedged Xenos cannot be recovered from software: libxenon's RBBM soft-reset
		 * sequence hung the whole console when tried (25 Oct 2026). Refuse and say so. */
		pr_err("xegpu: GPU is hung (RBBM_STATUS %08x); power-cycle the console\n", rr(0x1740));
		gpu_hung = true;
		return rc;
	}

	wr(0x07d8, 0x1000FFFF);                 /* halt ME */
	cp_setup_ring();
	/* microcode */
	wr(0x117c, 0); udelay(100);
	for (i = 0; i < ARRAY_SIZE(xegpu_pfp_ucode); i++)
		wr(0x1180, xegpu_pfp_ucode[i]);
	wr(0x07e0, 0);
	for (i = 0; i < ARRAY_SIZE(xegpu_me_ucode); i++)
		wr(0x07e8, xegpu_me_ucode[i]);
	wr(0x07e4, 0);
	for (i = 0; i < ARRAY_SIZE(xegpu_me_ucode); i++)
		if (rr(0x07e8) != xegpu_me_ucode[i]) {
			pr_err("xegpu: ME microcode verify failed at %d\n", i);
			return -EIO;
		}
	wait_gui_idle("ucode");
	wr(0x07d8, 0xFFFF); wr(0x07d0, 0xFFFF); wr(0x07f0, 0);
	wr(0x0774, 0); wr(0x0770, 0);
	wr(0x3214, 7); wr(0x3294, 1); wr(0x3408, 0x800);
	/* reset CP */
	wr(0x07d8, 0x1000FFFF); udelay(2000);
	wr(0x00f0, 1); (void)rr(0x00f0); udelay(1000);
	wr(0x00f0, 0); udelay(1000);
	cp_setup_ring();
	rc = wait_gui_idle("cp reset");
	if (rc)
		return rc;
	wr(0x07d8, 0xFFFF); udelay(1000);
	wr(0x3214, 7); wr(0x3294, 1); wr(0x3408, 0x800);
	wait_gui_idle("cp start");
	if (rr(0x0714) || rr(0x0710))
		pr_warn("xegpu: ring pointers not zero after reset (%x %x)\n", rr(0x0710), rr(0x0714));
	wr(0x07ec, 0x1A);
	/* master init tail */
	wr(0x07d4, 0); wr(0x07d4, 1);
	wr(0x2054, 0x1E); wr(0x2154, 0x1E);
	wr(0x3c10, 0xD);
	wr(0x3c40, 0x17); wr(0x3c48, 0);
	for (i = 0; i < 100000 && (rr(0x3c4c) & 0x80000000); i++) cpu_relax();
	wr(0x3c40, 0x1017); wr(0x3c48, 0);
	for (i = 0; i < 100000 && (rr(0x3c4c) & 0x80000000); i++) cpu_relax();
	wr(0x87e4, 0x17);
	/* read pointer writeback (Xe_pEnableWriteback, block size 6) */
	{
		u32 v = rr(0x0704) & ~0x8003F00;
		wr(0x0704, v);
		wr(0x070c, lower_32_bits(wb_dma + WB_RPTR) | 2);
		wr(0x0704, v | (6 << 8));
	}
	/* scratch register writeback: SCRATCH_ADDR, SCRATCH_UMSK */
	wr(0x0774, lower_32_bits(wb_dma + WB_SCRATCH));
	wr(0x0770, 0x20033);
	if (irq_requested && use_irq) {
		wr(REG_CP_INT_ACK, 0xffffffff);
		wr(REG_CP_INT_CNTL, 0x80000000);   /* RB_INT only */
	}
	rb_wptr = 0;
	fence_emitted = 0;
	fence_hi = 0;
	cp_ready = true;
	blit_init_needed_flag = true;
	pr_info("xegpu: CP up, ring %08x, writeback %08x\n", lower_32_bits(rb_dma), lower_32_bits(wb_dma));
	return 0;
}

static inline void rb_put(u32 v)
{
	rb_virt[rb_wptr++] = v;
	if (rb_wptr == RB_DW)
		rb_wptr = 0;
}

static u32 rb_free(void)
{
	u32 rptr = rr(0x0710);
	return (rptr + RB_DW - rb_wptr - 1) % RB_DW;
}

static u32 fence_done32(void)
{
	return READ_ONCE(wb_virt[WB_SCRATCH / 4 + FENCE_REG]);
}

static u64 fence_to64(u32 f)
{
	/* fences are emitted in order; reconstruct the high word relative to the newest */
	u64 newest = ((u64)fence_hi << 32) | fence_emitted;
	u64 v = (newest & ~0xffffffffull) | f;
	if (v > newest)
		v -= 1ull << 32;
	return v;
}

static int cp_submit(u32 addr, u32 ndw, u64 *fence_out)
{
	int t;

	if (!ndw || ndw > 0xfffff)
		return -EINVAL;
	mutex_lock(&cp_lock);
	if (!cp_ready) {
		int rc = cp_init_locked();
		if (rc) {
			mutex_unlock(&cp_lock);
			return rc;
		}
	}
	if (gpu_hung) {
		mutex_unlock(&cp_lock);
		return -EIO;
	}
	/* Wait for ring space. If the CP stops reading the ring for 3 s the GPU is
	 * hung (no software reset on Xenos): say so, count it as a stall and fail
	 * every later submit with -EIO, instead of user space spinning in this
	 * ioctl with nothing logged (a resize hang did that, 26 Oct 2026). */
	if (rb_free() < 64) {
		u32 last = rr(0x0710);
		unsigned long since = jiffies;

		while (rb_free() < 64) {
			u32 r = rr(0x0710);

			if (r != last) {
				last = r;
				since = jiffies;
			} else if (time_after(jiffies, since + msecs_to_jiffies(3000))) {
				fence_stalls++;
				gpu_hung = true;
				pr_err("xegpu: CP stopped reading the ring (rptr %x wptr %x, RBBM_STATUS %08x, COHER_STATUS_HOST %08x); GPU hung, power-cycle the console\n",
				       r, rb_wptr, rr(0x1740), rr(0x0a31 * 4));
				mutex_unlock(&cp_lock);
				return -EIO;
			}
			usleep_range(50, 200);
		}
	}
	/* invalidate the GPU's cache over the command buffer (Xe_pInvalidateGpuCache_Primary) */
	{
		u32 size = ((ndw * 4 + 0x1000) + 0x1FFF) & ~0x1FFF;
		rb_put(0x00000a31); rb_put(0x01000000);
		rb_put(0x00010a2f); rb_put(size); rb_put(addr);
		rb_put(0xc0043c00); rb_put(0x00000003); rb_put(0x00000a31);
		rb_put(0x00000000); rb_put(0x80000000); rb_put(0x00000008);
	}
	rb_put(0xc0013f00); rb_put(addr); rb_put(ndw);      /* INDIRECT_BUFFER */
	rb_put(0x05c8);    rb_put(0x00020000);              /* WAIT_UNTIL idle (reg 0x1720) */
	fence_emitted++;
	if (!fence_emitted)
		fence_hi++;
	rb_put((0x15e0 + FENCE_REG * 4) >> 2); rb_put(fence_emitted);   /* SCRATCH_REG5 = fence */
	if (irq_requested && use_irq) {
		rb_put(0xC0000000 | ((intr_opcode & 0xff) << 8)); rb_put(0x80000000);   /* INTERRUPT: RB_INT */
	}
	for (t = 0; t < 0x20; t++)
		rb_put(0x80000000);
	wmb();
	wr(0x0714, rb_wptr);
	*fence_out = ((u64)fence_hi << 32) | fence_emitted;
	mutex_unlock(&cp_lock);
	return 0;
}

static bool fence_passed(u64 fence)
{
	return fence_to64(fence_done32()) >= fence;
}

static int cp_wait_x(u64 fence, u32 timeout_ms, bool intr)
{
	unsigned long end = jiffies + msecs_to_jiffies(timeout_ms ? timeout_ms : 1);
	ktime_t spin_until = ktime_add_us(ktime_get(), (irq_requested && use_irq) ? 20 : 2000);

	while (ktime_before(ktime_get(), spin_until)) {
		if (fence_passed(fence))
			return 0;
		udelay(2);
	}
	{
	/* global, so waits that keep restarting (signals: Xorg) still count */
	static u32 stall_seen;
	static unsigned long stall_since;
	static bool stall_counted;
	for (;;) {
		long rc;
		if (fence_passed(fence))
			return 0;
		if (fence_done32() != stall_seen || !stall_since) {
			stall_seen = fence_done32();
			stall_since = jiffies;
			stall_counted = false;
		} else if (!stall_counted && time_after(jiffies, stall_since + msecs_to_jiffies(5000))) {
			stall_counted = true;
			fence_stalls++;
			pr_warn("xegpu: fence %llu stalled 5 s; last completed %u, emitted %u, RBBM_STATUS %08x\n",
				fence, fence_done32(), fence_emitted, rr(0x1740));
		}
		if (time_after(jiffies, end)) {
			pr_warn_ratelimited("xegpu: fence %llu not reached; last completed %u, emitted %u, CP rptr %x wptr %x, RBBM_STATUS %08x\n",
					    fence, fence_done32(), fence_emitted, rr(0x0710), rr(0x0714), rr(0x1740));
			fence_timeouts++;
			return -ETIME;
		}
		if (irq_requested && use_irq) {
			/* 10ms slices so a missed interrupt degrades to polling instead of hanging */
			if (intr) {
				rc = wait_event_interruptible_timeout(fence_wq, fence_passed(fence), msecs_to_jiffies(10));
				if (rc < 0)
					return rc;
			} else {
				wait_event_timeout(fence_wq, fence_passed(fence), msecs_to_jiffies(10));
			}
		} else {
			if (intr && signal_pending(current))
				return -ERESTARTSYS;
			usleep_range(50, 200);
		}
	}
}
}

static int cp_wait(u64 fence, u32 timeout_ms) { return cp_wait_x(fence, timeout_ms, true); }

static void xe_fence_retire(void);

static irqreturn_t xegpu_irq(int irq, void *dev)
{
	u32 master = rr(REG_MASTER_INT), cp = 0, rbbm = 0;

	if (!(master & ((1u << 30) | (1u << 31))))
		return IRQ_NONE;
	if (master & (1u << 30)) {
		cp = rr(REG_CP_INT_STATUS);
		if (cp)
			wr(REG_CP_INT_ACK, cp);
	}
	if (master & (1u << 31)) {
		rbbm = rr(REG_RBBM_INT_STATUS);
		if (rbbm)
			wr(REG_RBBM_INT_ACK, rbbm);
	}
	if (cp & ~0x80000000u)
		pr_warn_ratelimited("xegpu: CP interrupt status %08x\n", cp);
	irq_count++;
	wake_up_all(&fence_wq);
	xe_fence_retire();
	return IRQ_HANDLED;
}

static void cp_shutdown(void)
{
	if (!rb_virt)
		return;
	xegpu_quiesce(NULL);
	cp_ready = false;
	dma_free_coherent(&xe_pdev->dev, RB_BYTES, rb_virt, rb_dma);
	dma_free_coherent(&xe_pdev->dev, WB_BYTES, wb_virt, wb_dma);
	rb_virt = wb_virt = NULL;
}

static int xegpu_open(struct inode *inode, struct file *file)
{
	struct xegpu_file *xf;
	int i;

	mutex_lock(&xe_open_lock);
	if (xe_busy) {
		mutex_unlock(&xe_open_lock);
		return -EBUSY;
	}
	xf = kzalloc(sizeof(*xf), GFP_KERNEL);
	if (!xf) {
		mutex_unlock(&xe_open_lock);
		return -ENOMEM;
	}
	idr_init_base(&xf->bos, 1);
	mutex_init(&xf->lock);
	for (i = 0; i < 4; i++)
		xf->saved_scanout[i] = rr(scanout_regs[i]);
	xf->scanout_saved = true;
	xe_busy = true;
	mutex_unlock(&xe_open_lock);

	file->private_data = xf;
	return 0;
}


/* Contiguous GPU memory reserved at module load, while RAM is unfragmented.
 * Buffers come from here first and fall back to dma_alloc_coherent. */
static int pool_mb = 48;
module_param(pool_mb, int, 0444);
MODULE_PARM_DESC(pool_mb, "MB of contiguous GPU memory to reserve at load (16MB chunks)");
#define POOL_CHUNK (16u << 20)
#define POOL_MAX_CHUNKS 8
static struct gen_pool *xe_pool;
static struct { void *virt; dma_addr_t dma; u32 size; } pool_chunks[POOL_MAX_CHUNKS];
static int pool_nchunks;
static void *pool_vmaps[POOL_MAX_CHUNKS];   /* merged ranges of adjacent chunks */
static int pool_nvmaps;

/* /sys/module/xegpu/parameters/pool_free_kb: what the pool has left (for
 * finding leaks and fragmentation over long uptimes) */
static int pool_free_get(char *buf, const struct kernel_param *kp)
{
	return sysfs_emit(buf, "%zu of %zu\n", xe_pool ? gen_pool_avail(xe_pool) >> 10 : 0,
			  xe_pool ? gen_pool_size(xe_pool) >> 10 : 0);
}
static const struct kernel_param_ops pool_free_ops = { .get = pool_free_get };
module_param_cb(pool_free_kb, &pool_free_ops, NULL, 0444);

static int pool_chunk_cmp(const void *a, const void *b)
{
	const typeof(pool_chunks[0]) *x = a, *y = b;
	return x->dma < y->dma ? -1 : x->dma > y->dma;
}

static void pool_create(void)
{
	int i, want = DIV_ROUND_UP(pool_mb << 20, POOL_CHUNK);

	if (want <= 0)
		return;
	xe_pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!xe_pool)
		return;
	gen_pool_set_algo(xe_pool, gen_pool_first_fit_align, NULL);
	for (i = 0; i < want && i < POOL_MAX_CHUNKS; i++) {
		u32 sz = POOL_CHUNK;
		void *v = NULL;
		dma_addr_t d;
		/* fall back to smaller chunks if 16MB blocks are gone */
		while (sz >= (2u << 20)) {
			v = dma_alloc_coherent(&xe_pdev->dev, sz, &d, GFP_KERNEL | __GFP_NOWARN);
			if (v)
				break;
			sz >>= 1;
		}
		if (!v)
			break;
		if (upper_32_bits(d)) {
			dma_free_coherent(&xe_pdev->dev, sz, v, d);
			break;
		}
		pool_chunks[pool_nchunks].virt = v;
		pool_chunks[pool_nchunks].dma = d;
		pool_chunks[pool_nchunks].size = sz;
		pool_nchunks++;
	}
	/* 16MB (the largest buddy block with 64K pages) is the largest buffer a
	 * single chunk holds: hand physically adjacent chunks to the pool as one
	 * range so that a 2048x2048 RGBA texture with mipmaps (21MB) fits */
	sort(pool_chunks, pool_nchunks, sizeof(pool_chunks[0]), pool_chunk_cmp, NULL);
	for (i = 0; i < pool_nchunks;) {
		int j = i + 1;
		size_t len = pool_chunks[i].size;
		void *v = pool_chunks[i].virt;
		while (j < pool_nchunks && pool_chunks[j].dma == pool_chunks[i].dma + len)
			len += pool_chunks[j++].size;
		/* the coherent allocations are separate kernel mappings: map the
		 * merged range once more, with the same attributes */
		if (j - i > 1) {
			unsigned long k, n = len >> PAGE_SHIFT;
			struct page **pages = kvmalloc_array(n, sizeof(*pages), GFP_KERNEL);
			void *m = NULL;
			if (pages) {
				for (k = 0; k < n; k++)
					pages[k] = pfn_to_page(PHYS_PFN(pool_chunks[i].dma) + k);
				m = vmap(pages, n, VM_MAP, pgprot_dmacoherent(PAGE_KERNEL));
				kvfree(pages);
			}
			if (m && pool_nvmaps < POOL_MAX_CHUNKS) {
				v = m;
				pool_vmaps[pool_nvmaps++] = m;
			} else {
				if (m)
					vunmap(m);
				j = i + 1;
				len = pool_chunks[i].size;
			}
		}
		pr_info("xegpu: pool range %08llx + %zu KB (%d chunks)\n",
			(u64)pool_chunks[i].dma, len >> 10, j - i);
		if (gen_pool_add_virt(xe_pool, (unsigned long)v, pool_chunks[i].dma, len, -1))
			pr_err("xegpu: gen_pool_add_virt failed\n");
		i = j;
	}
	pr_info("xegpu: reserved %zu KB contiguous GPU memory in %d chunks\n",
		gen_pool_size(xe_pool) >> 10, pool_nchunks);
}

static void pool_destroy(void)
{
	int i;
	if (!xe_pool)
		return;
	if (gen_pool_avail(xe_pool) != gen_pool_size(xe_pool)) {
		pr_err("xegpu: pool still in use at unload, leaking it\n");
		return;
	}
	gen_pool_destroy(xe_pool);
	xe_pool = NULL;
	for (i = 0; i < pool_nvmaps; i++)
		vunmap(pool_vmaps[i]);
	pool_nvmaps = 0;
	for (i = 0; i < pool_nchunks; i++)
		dma_free_coherent(&xe_pdev->dev, pool_chunks[i].size, pool_chunks[i].virt, pool_chunks[i].dma);
	pool_nchunks = 0;
}

static bool bo_alloc(struct xegpu_bo *bo)
{
	if (xe_pool) {
		struct genpool_data_align align = { .align = roundup_pow_of_two(bo->size) < (2u << 20) ?
						    roundup_pow_of_two(bo->size) : (2u << 20) };
		unsigned long v = gen_pool_alloc_algo(xe_pool, bo->size, gen_pool_first_fit_align, &align);
		if (v) {
			bo->vaddr = (void *)v;
			bo->dma = gen_pool_virt_to_phys(xe_pool, v);
			bo->from_pool = true;
			return true;
		}
	}
	bo->vaddr = dma_alloc_coherent(&xe_pdev->dev, bo->size, &bo->dma, GFP_KERNEL | __GFP_NOWARN);
	bo->from_pool = false;
	if (bo->vaddr && upper_32_bits(bo->dma)) {
		dma_free_coherent(&xe_pdev->dev, bo->size, bo->vaddr, bo->dma);
		bo->vaddr = NULL;
	}
	return bo->vaddr != NULL;
}

static void bo_release(struct xegpu_bo *bo)
{
	if (bo->from_pool)
		gen_pool_free(xe_pool, (unsigned long)bo->vaddr, bo->size);
	else
		dma_free_coherent(&xe_pdev->dev, bo->size, bo->vaddr, bo->dma);
}

static int free_one(int id, void *p, void *data)
{
	struct xegpu_bo *bo = p;

	bo_release(bo);
	kfree(bo);
	return 0;
}

static int xegpu_release(struct inode *inode, struct file *file)
{
	struct xegpu_file *xf = file->private_data;

	mutex_lock(&cp_lock);
	if (cp_ready && !xf->legacy_cp) {
		/* let in-flight work drain, then restore the display */
		if (cp_wait(((u64)fence_hi << 32) | fence_emitted, 2000))
			pr_warn("xegpu: fence %u not reached on close, halting CP\n", fence_emitted);
		wait_gui_idle("close");
		xegpu_quiesce(xf);       /* halts ME; CP is re-initialised on next use */
		cp_ready = false;
	} else {
		xegpu_quiesce(xf);
		cp_ready = false;
	}
	mutex_unlock(&cp_lock);
	idr_for_each(&xf->bos, free_one, NULL);
	idr_destroy(&xf->bos);
	kfree(xf);

	mutex_lock(&xe_open_lock);
	xe_busy = false;
	mutex_unlock(&xe_open_lock);
	return 0;
}

static long xegpu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct xegpu_file *xf = file->private_data;
	void __user *uarg = (void __user *)arg;

	switch (cmd) {
	case XEGPU_IOC_INFO: {
		struct xegpu_info info = {
			.reg_phys = xe_reg_phys,
			.reg_size = XEGPU_REG_SIZE,
			.scanout_addr = xf->saved_scanout[0],
			.scanout_pitch = xf->saved_scanout[1],
			.scanout_width = xf->saved_scanout[2],
			.scanout_height = xf->saved_scanout[3],
		};
		return copy_to_user(uarg, &info, sizeof(info)) ? -EFAULT : 0;
	}
	case XEGPU_IOC_ALLOC: {
		struct xegpu_alloc a;
		struct xegpu_bo *bo;
		int id;

		if (copy_from_user(&a, uarg, sizeof(a)))
			return -EFAULT;
		if (!a.size || a.size > (16u << 20))
			return -EINVAL;
		bo = kzalloc(sizeof(*bo), GFP_KERNEL);
		if (!bo)
			return -ENOMEM;
		bo->size = PAGE_ALIGN(a.size);
		if (!bo_alloc(bo)) {
			kfree(bo);
			return -ENOMEM;
		}
		memset(bo->vaddr, 0, bo->size);
		mutex_lock(&xf->lock);
		id = idr_alloc(&xf->bos, bo, 1, 1 << 16, GFP_KERNEL);
		mutex_unlock(&xf->lock);
		if (id < 0) {
			bo_release(bo);
			kfree(bo);
			return id;
		}
		bo->handle = id;
		a.handle = id;
		a.gpu_addr = lower_32_bits(bo->dma);
		a.mmap_offset = (u64)id << XEGPU_BO_MMAP_SHIFT;
		a.size = bo->size;
		if (copy_to_user(uarg, &a, sizeof(a)))
			return -EFAULT;
		return 0;
	}
	case XEGPU_IOC_FREE: {
		u32 h;
		struct xegpu_bo *bo;

		if (get_user(h, (u32 __user *)uarg))
			return -EFAULT;
		mutex_lock(&xf->lock);
		bo = idr_remove(&xf->bos, h);
		mutex_unlock(&xf->lock);
		if (!bo)
			return -ENOENT;
		/* caller promises the GPU is done with it */
		free_one(h, bo, NULL);
		return 0;
	}
	case XEGPU_IOC_QUIESCE:
		xegpu_quiesce(xf);
		return 0;
	case XEGPU_IOC_CP_INIT: {
		int rc;
		mutex_lock(&cp_lock);
		rc = cp_init_locked();
		mutex_unlock(&cp_lock);
		return rc;
	}
	case XEGPU_IOC_SUBMIT: {
		struct xegpu_submit sb;
		int rc;
		if (copy_from_user(&sb, uarg, sizeof(sb)))
			return -EFAULT;
		rc = cp_submit(sb.gpu_addr, sb.ndw, &sb.fence);
		if (rc)
			return rc;
		return copy_to_user(uarg, &sb, sizeof(sb)) ? -EFAULT : 0;
	}
	case XEGPU_IOC_WAIT: {
		struct xegpu_wait w;
		if (copy_from_user(&w, uarg, sizeof(w)))
			return -EFAULT;
		return cp_wait(w.fence, w.timeout_ms);
	}
	}
	return -ENOTTY;
}

static int xegpu_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct xegpu_file *xf = file->private_data;
	u64 off = (u64)vma->vm_pgoff << PAGE_SHIFT;
	size_t len = vma->vm_end - vma->vm_start;

	if (off == 0) {
		if (len > PAGE_ALIGN(XEGPU_REG_SIZE))
			return -EINVAL;
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
		vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
		return io_remap_pfn_range(vma, vma->vm_start, xe_reg_phys >> PAGE_SHIFT,
					  len, vma->vm_page_prot);
	} else {
		u32 h = off >> XEGPU_BO_MMAP_SHIFT;
		struct xegpu_bo *bo;

		if (off & ((1ull << XEGPU_BO_MMAP_SHIFT) - 1))
			return -EINVAL;
		mutex_lock(&xf->lock);
		bo = idr_find(&xf->bos, h);
		mutex_unlock(&xf->lock);
		if (!bo || len > bo->size)
			return -EINVAL;
		vma->vm_pgoff = 0;
		if (bo->from_pool) {
			/* pool memory is ordinary RAM handed out uncached, same as dma_mmap_coherent */
			vma->vm_page_prot = pgprot_dmacoherent(vma->vm_page_prot);
			vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
			return remap_pfn_range(vma, vma->vm_start, PHYS_PFN(bo->dma), len, vma->vm_page_prot);
		}
		return dma_mmap_coherent(&xe_pdev->dev, vma, bo->vaddr, bo->dma, len);
	}
}

static const struct file_operations xegpu_fops = {
	.owner = THIS_MODULE,
	.open = xegpu_open,
	.release = xegpu_release,
	.unlocked_ioctl = xegpu_ioctl,
	.mmap = xegpu_mmap,
	.llseek = noop_llseek,
};

static struct miscdevice xegpu_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "xegpu",
	.fops = &xegpu_fops,
	.mode = 0600,
};

#include "xenos_drm.c"

static int __init xegpu_init(void)
{
	int i, rc;

	for (i = 0; i < ARRAY_SIZE(xe_ids) && !xe_pdev; i++)
		xe_pdev = pci_get_device(PCI_VENDOR_MS, xe_ids[i], NULL);
	if (!xe_pdev)
		return -ENODEV;

	xe_reg_phys = pci_resource_start(xe_pdev, 0);
	if (pci_resource_len(xe_pdev, 0) < XEGPU_REG_SIZE) {
		rc = -ENODEV;
		goto err_put;
	}
	xe_regs = ioremap(xe_reg_phys, XEGPU_REG_SIZE);
	if (!xe_regs) {
		rc = -ENOMEM;
		goto err_put;
	}
	rc = dma_set_mask_and_coherent(&xe_pdev->dev, DMA_BIT_MASK(32));
	if (rc)
		pr_warn("xegpu: dma_set_mask failed (%d), continuing\n", rc);

	pool_create();

	if (request_irq(xe_pdev->irq, xegpu_irq, IRQF_SHARED, "xegpu", &xegpu_misc) == 0)
		irq_requested = true;
	else
		pr_warn("xegpu: could not get IRQ %d, fences will be polled\n", xe_pdev->irq);

	rc = misc_register(&xegpu_misc);
	if (rc)
		goto err_unmap;
	if (pci_register_driver(&xenos_drm_pci_driver))
		pr_warn("xegpu: could not register the DRM PCI driver\n");

	pr_info("xegpu: Xenos %04x regs at %llx, RBBM_STATUS %08x, CP_ME_CNTL %08x, scanout %08x\n",
		xe_pdev->device, (unsigned long long)xe_reg_phys, rr(0x1740), rr(0x07d8), rr(0x6110));
	return 0;

err_unmap:
	iounmap(xe_regs);
err_put:
	pci_dev_put(xe_pdev);
	return rc;
}

static void __exit xegpu_exit(void)
{
	pci_unregister_driver(&xenos_drm_pci_driver);
	cp_shutdown();
	if (irq_requested)
		free_irq(xe_pdev->irq, &xegpu_misc);
	pool_destroy();
	misc_deregister(&xegpu_misc);
	iounmap(xe_regs);
	pci_dev_put(xe_pdev);
}

module_init(xegpu_init);
module_exit(xegpu_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Development user-space access to the Xenos GPU");
