// SPDX-License-Identifier: GPL-2.0
/*
 * The subset of the msm DRM UAPI that Mesa's freedreno (a2xx) drm backend uses,
 * implemented on the Xenos command processor. Included from xenos_drm.c.
 *
 * Xenos has no GPU MMU: an "iova" is the buffer's physical address. Buffers are
 * contiguous GEM DMA objects, CPU-cached; they are flushed before every submit
 * that references them and invalidated on CPU_PREP.
 *
 * Not done yet: validation of addresses inside command streams, fence fds,
 * syncobjs, multiple queues with priorities.
 */
#include <uapi/drm/msm_drm.h>
#include <linux/dma-direction.h>
#include <linux/dma-fence.h>
#include <linux/sync_file.h>
#include <linux/file.h>
#include <asm/cacheflush.h>

static int xenos_gpu_state_init(void);
/* submit profiling: totals since load (read with cat; divide by submit_count) */
static unsigned long submit_count, submit_ns, submit_check_ns, submit_fenceout_count;
module_param(submit_count, ulong, 0444);
module_param(submit_ns, ulong, 0444);
module_param(submit_check_ns, ulong, 0444);
static unsigned long submit_check_dw, submit_flush_ns;
module_param(submit_check_dw, ulong, 0444);
module_param(submit_flush_ns, ulong, 0444);
module_param(submit_fenceout_count, ulong, 0444);
static bool msm_map_wc = true;
static bool msm_cached_rings = true;
module_param(msm_cached_rings, bool, 0644);
module_param(msm_map_wc, bool, 0644);
static bool msm_state_init = true;
module_param(msm_state_init, bool, 0644);
MODULE_PARM_DESC(msm_state_init, "run the Xe_Init state stream before msm submits after a CP init (GL needs it)");

/* dma_fence for submits, so user space can get sync_file fds (MSM_SUBMIT_FENCE_FD_OUT)
 * and pass them back in (FENCE_FD_IN). Signalled from the CP interrupt. */
struct xe_fence {
	struct dma_fence base;
	u64 seq;
	struct list_head node;
};
static DEFINE_SPINLOCK(xe_fence_lock);
static LIST_HEAD(xe_fence_pending);
static u64 xe_fence_context;

static const char *xe_fence_driver(struct dma_fence *f) { return "xegpu"; }
static const char *xe_fence_timeline(struct dma_fence *f) { return "xenos-cp"; }
static const struct dma_fence_ops xe_fence_ops = {
	.get_driver_name = xe_fence_driver,
	.get_timeline_name = xe_fence_timeline,
};

/* called from the IRQ handler and after waits: signal everything the CP has finished */
static void xe_fence_retire(void)
{
	struct xe_fence *f, *tmp;
	unsigned long flags;
	u64 done;

	if (!wb_virt)
		return;
	done = fence_to64(fence_done32());
	spin_lock_irqsave(&xe_fence_lock, flags);
	list_for_each_entry_safe(f, tmp, &xe_fence_pending, node) {
		if (f->seq > done)
			break;
		list_del(&f->node);
		dma_fence_signal(&f->base);
		dma_fence_put(&f->base);
	}
	spin_unlock_irqrestore(&xe_fence_lock, flags);
}

/* unload / CP halt: nothing pending will ever complete; signal with an error */
static void xe_fence_flush_all(void)
{
	struct xe_fence *f, *tmp;
	unsigned long flags;

	spin_lock_irqsave(&xe_fence_lock, flags);
	list_for_each_entry_safe(f, tmp, &xe_fence_pending, node) {
		list_del(&f->node);
		dma_fence_set_error(&f->base, -ECANCELED);
		dma_fence_signal(&f->base);
		dma_fence_put(&f->base);
	}
	spin_unlock_irqrestore(&xe_fence_lock, flags);
}

static struct dma_fence *xe_fence_create(u64 seq)
{
	struct xe_fence *f = kzalloc(sizeof(*f), GFP_KERNEL);
	unsigned long flags;

	if (!f)
		return NULL;
	if (!xe_fence_context)
		xe_fence_context = dma_fence_context_alloc(1);
	f->seq = seq;
	dma_fence_init(&f->base, &xe_fence_ops, &xe_fence_lock, xe_fence_context, seq);
	dma_fence_get(&f->base);              /* reference held by the pending list */
	spin_lock_irqsave(&xe_fence_lock, flags);
	list_add_tail(&f->node, &xe_fence_pending);
	spin_unlock_irqrestore(&xe_fence_lock, flags);
	xe_fence_retire();                    /* it may already be done */
	return &f->base;
}

static int msm_compat;            /* report the driver name as "msm" so stock libdrm/Mesa accept it */
module_param(msm_compat, int, 0444);
MODULE_PARM_DESC(msm_compat, "Expose the DRM device as \"msm\" for Mesa freedreno (set at load)");
static int msm_gpu_id = 290;
module_param(msm_gpu_id, int, 0644);
MODULE_PARM_DESC(msm_gpu_id, "GPU id reported to user space (290 = Xenos variant in our Mesa branch; 200 = stock Adreno 200)");

#define XENOS_EDRAM_BYTES (10u << 20)


/*
 * Command stream checker. A hung Xenos cannot be reset (only a power-cycle recovers it),
 * so submits are parsed before they reach the ring:
 *  - type0 (direct MMIO) writes only to registers known to be safe on Xenos;
 *  - type3 opcodes from an allow list; INDIRECT_BUFFER targets and MEM_WRITE destinations
 *    must lie inside buffers listed in the same submit (no MMU: everything is physical).
 * Context registers (SET_CONSTANT type 4, 0x2000+) are allowed; they configure the 3D
 * pipeline and cannot reach memory except through addresses that are also checked.
 */
static int msm_validate = 1;
module_param(msm_validate, int, 0644);
MODULE_PARM_DESC(msm_validate, "Parse submitted command streams and refuse unsafe ones (default 1)");
static int msm_rejects;
module_param(msm_rejects, int, 0444);

struct xe_range { u32 start, end; void *vaddr; bool cached, checked; };

static bool mmio_ok(u32 reg)
{
	/* dword register indices */
	if (reg >= 0x0578 && reg <= 0x057f)	/* SCRATCH_REG0..7 (fence uses 5) */
		return true;
	if (reg >= 0x2000 && reg < 0x4000)	/* 3D context registers (also reachable via SET_CONSTANT) */
		return true;
	/* ALU (0x4000) and fetch (0x4800) constants, as SET_CONSTANT also allows.
	 * TODO: fetch constants carry texture/vertex base addresses that are not yet checked. */
	if (reg >= 0x4000 && reg < 0x4a00)
		return true;
	if (reg >= 0x5000 && reg <= 0x5002)	/* SHADER_CONSTANT_FLUSH_FETCH_* */
		return true;
	switch (reg) {
	case 0x05c8:	/* WAIT_UNTIL */
	case 0x0d02:	/* SQ_INST_STORE_MANAGMENT */
	case 0x0d00: case 0x0d01: case 0x0d04:	/* SQ setup used by libxenon */
	case 0x0a2f: case 0x0a30: case 0x0a31:	/* COHER_SIZE/BASE/STATUS_HOST (cache flush), used by libxenon */
	case 0x0c85: case 0x0e42:	/* libxenon init */
		return true;
	}
	return false;
}

static bool op_ok(u32 op)
{
	/* values from Xenia's xenos.h PM4 list. Deliberately absent: REG_RMW (0x21, arbitrary
	 * register writes), WAIT_REG_EQ/WAIT_REG_MEM (0x52/0x3c, can wait forever), ME_INIT (0x48,
	 * kernel's job), LOAD_CONSTANT_CONTEXT (0x2e, reads unchecked memory), DRAW_INDX_BIN and
	 * SET_BIN_BASE_OFFSET (0x34/0x4b, a2xx binning), INTERRUPT (0x54). */
	switch (op) {
	case 0x10:	/* NOP */
	case 0x22:	/* DRAW_INDX */
	case 0x36:	/* DRAW_INDX_2 */
	case 0x26:	/* WAIT_FOR_IDLE */
	case 0x27:	/* IM_LOAD (address checked) */
	case 0x2b:	/* IM_LOAD_IMMEDIATE */
	case 0x2d:	/* SET_CONSTANT */
	case 0x3b:	/* INVALIDATE_STATE */
	case 0x3d:	/* MEM_WRITE (destination checked) */
	case 0x3f:	/* INDIRECT_BUFFER (target checked) */
	case 0x37:	/* INDIRECT_BUFFER_PFD (target checked) */
	case 0x46:	/* EVENT_WRITE (memory form checked) */
	case 0x56:	/* SET_SHADER_CONSTANTS */
	case 0x4a:	/* SET_SHADER_BASES */
	/* 0x58/0x59 EVENT_WRITE_SHD/_CFL refused: CACHE_FLUSH_TS through it wedged the memory hub (26 Oct) */
		return true;
	}
	return false;
}

/* 3D context registers the checker follows to bound resolve (EDRAM -> memory) writes */
struct xe_cstate {
	u32 modecontrol, copy_base, copy_pitch, copy_info;
};

static void xe_track(struct xe_cstate *st, u32 reg, u32 v)
{
	switch (reg) {
	case 0x2208: st->modecontrol = v; break;	/* RB_MODECONTROL */
	case 0x2319: st->copy_base = v; break;		/* RB_COPY_DEST_BASE */
	case 0x231a: st->copy_pitch = v; break;		/* RB_COPY_DEST_PITCH: pitch | height << 16 */
	case 0x231b: st->copy_info = v; break;		/* RB_COPY_DEST_INFO */
	}
}

static bool in_ranges(const struct xe_range *r, int n, u32 addr, u32 len, void **vaddr);
static const struct xe_range *xe_range_of(const struct xe_range *r, int n, u32 addr, u32 len);

/* The checker reads command words through the kernel's cached linear mapping
 * instead of the uncached one (one bus transaction per load made checking cost
 * more than everything else in a small submit). Drop any stale lines first: the
 * CPU wrote these buffers through uncached/write-combined mappings. */
static bool msm_check_cached = true;
module_param(msm_check_cached, bool, 0644);
static const u32 *xe_check_view(const void *uncached, u32 phys, u32 bytes, bool cached)
{
	void *v;

	if (!msm_check_cached)
		return uncached;
	u64 t = ktime_get_ns();
	v = phys_to_virt(phys);
	/* a buffer user space maps cached only needs its lines written back (and they
	 * stay valid for the reads below); one written through an uncached mapping
	 * may have stale lines here, so drop them */
	if (cached)
		clean_dcache_range((unsigned long)v, (unsigned long)v + bytes);
	else
		flush_dcache_range((unsigned long)v, (unsigned long)v + bytes);
	submit_flush_ns += ktime_get_ns() - t;
	submit_check_dw += bytes / 4;
	return v;
}

/* a draw in EDRAM copy mode writes pitch x height pixels at copy_base */
static bool xe_resolve_ok(const struct xe_range *r, int n, const struct xe_cstate *st)
{
	u32 pitch = st->copy_pitch & 0x3fff, height = (st->copy_pitch >> 16) & 0x3fff;
	u32 fmt = (st->copy_info >> 7) & 0x3f;
	/* k_8_8_8_8, k_24_8 and k_24_8_FLOAT (depth resolves) are 4 bytes; anything
	 * else gets the worst case */
	u32 bpp = (fmt == 6 || fmt == 22 || fmt == 23) ? 4 : 16;
	/* tiled destinations are padded to 32x32 */
	u32 h = (height + 31) & ~31u, p = (pitch + 31) & ~31u;
	return in_ranges(r, n, st->copy_base & ~0xfffu, p * h * bpp, NULL);
}

static const struct xe_range *xe_range_of(const struct xe_range *r, int n, u32 addr, u32 len)
{
	int i;
	for (i = 0; i < n; i++)
		if (addr >= r[i].start && (u64)addr + len <= r[i].end)
			return &r[i];
	return NULL;
}

static bool in_ranges(const struct xe_range *r, int n, u32 addr, u32 len, void **vaddr)
{
	int i;
	for (i = 0; i < n; i++)
		if (addr >= r[i].start && (u64)addr + len <= r[i].end) {
			if (vaddr)
				*vaddr = r[i].vaddr ? (u8 *)r[i].vaddr + (addr - r[i].start) : NULL;
			return true;
		}
	return false;
}

static int xe_check_stream(const struct xe_range *r, int n, const u32 *w, u32 ndw, int depth,
			   struct xe_cstate *st)
{
	u32 k = 0;

	if (depth > 2)
		return -EINVAL;
	while (k < ndw) {
		u32 h = w[k], type = h >> 30;
		if (type == 2) {
			k++;
			continue;
		}
		if (type == 0) {
			u32 cnt = ((h >> 16) & 0x3fff) + 1, reg = h & 0x7fff, j;
			bool one = h & (1 << 15);
			if (k + 1 + cnt > ndw)
				return -EINVAL;
			/* the common case, a run of context registers or constants: one range test */
			if (!one && reg >= 0x2000 && reg + cnt <= 0x4a00) {
				if (reg <= 0x231b && reg + cnt > 0x2208)
					for (j = 0; j < cnt; j++)
						xe_track(st, reg + j, w[k + 1 + j]);
				k += 1 + cnt;
				continue;
			}
			for (j = 0; j < cnt; j++)
				xe_track(st, one ? reg : reg + j, w[k + 1 + j]);
			for (j = 0; j < (one ? 1 : cnt); j++)
				if (!mmio_ok(reg + j)) {
					pr_warn_ratelimited("xegpu: submit rejected: MMIO write to reg 0x%04x (byte 0x%04x)\n",
							    reg + j, (reg + j) * 4);
					return -EPERM;
				}
			k += 1 + cnt;
			continue;
		}
		if (type == 3) {
			u32 cnt = ((h >> 16) & 0x3fff) + 1, op = (h >> 8) & 0x7f;
			if (k + 1 + cnt > ndw)
				return -EINVAL;
			/* WAIT_REG_MEM is allowed only in libxenon's form: wait on COHER_STATUS_HOST (0x0a31),
			 * which always completes once the cache flush it follows is done */
			if (op == 0x3c && !(cnt == 5 && (w[k + 1] & 0x10) == 0 && w[k + 2] == 0x0a31)) {
				pr_warn_ratelimited("xegpu: submit rejected: WAIT_REG_MEM on %08x\n", w[k + 2]);
				return -EPERM;
			}
			if (op != 0x3c && !op_ok(op)) {
				pr_warn_ratelimited("xegpu: submit rejected: PM4 opcode 0x%02x\n", op);
				return -EPERM;
			}
			if (op == 0x3d) {	/* MEM_WRITE: addr, data... */
				if (!in_ranges(r, n, w[k + 1] & ~3u, (cnt - 1) * 4, NULL)) {
					pr_warn_ratelimited("xegpu: submit rejected: MEM_WRITE to %08x\n", w[k + 1]);
					return -EPERM;
				}
			}
			if ((op == 0x46 || op == 0x58 || op == 0x59) && cnt >= 2) {	/* event + address [+ data] */
				if (!in_ranges(r, n, w[k + 2] & ~3u, 4, NULL)) {
					pr_warn_ratelimited("xegpu: submit rejected: event write to %08x\n", w[k + 2]);
					return -EPERM;
				}
			}
			if (op == 0x27) {	/* IM_LOAD: addr | shader type, size in dwords (libxenon Xe_pLoadShader) */
				if (cnt < 2 || !in_ranges(r, n, w[k + 1] & ~3u, w[k + 2] * 4, NULL)) {
					pr_warn_ratelimited("xegpu: submit rejected: IM_LOAD from %08x\n", w[k + 1]);
					return -EPERM;
				}
			}
			if (op == 0x2d && cnt >= 2 && (w[k + 1] >> 16) == 4) {	/* SET_CONSTANT: registers */
				u32 base = 0x2000 + (w[k + 1] & 0x7ff), j;
				if (base <= 0x231b && base + cnt - 1 > 0x2208)
					for (j = 0; j + 1 < cnt; j++)
						xe_track(st, base + j, w[k + 2 + j]);
			}
			if ((op == 0x22 || op == 0x36) && (st->modecontrol & 7) == 6) {	/* draw in copy mode */
				if (!xe_resolve_ok(r, n, st)) {
					pr_warn_ratelimited("xegpu: submit rejected: resolve to %08x (pitch/height %08x) outside the submit\n",
							    st->copy_base, st->copy_pitch);
					return -EPERM;
				}
			}
			if (op == 0x3f || op == 0x37) {	/* IB: addr, size */
				u32 addr = w[k + 1], size = w[k + 2] & 0xfffff;
				const struct xe_range *ib = xe_range_of(r, n, addr, size * 4);
				int rc;
				if (!ib || !ib->vaddr) {
					pr_warn_ratelimited("xegpu: submit rejected: IB at %08x outside the submit\n", addr);
					return -EPERM;
				}
				((struct xe_range *)ib)->checked = true;
				rc = xe_check_stream(r, n, xe_check_view((u8 *)ib->vaddr + (addr - ib->start), addr & ~3u,
									 size * 4, ib->cached),
						     size, depth + 1, st);
				if (rc)
					return rc;
			}
			k += 1 + cnt;
			continue;
		}
		pr_warn_ratelimited("xegpu: submit rejected: type1 packet %08x\n", h);
		return -EPERM;
	}
	return 0;
}

static struct drm_gem_dma_object *msm_lookup(struct drm_file *file, u32 handle)
{
	struct drm_gem_object *obj = drm_gem_object_lookup(file, handle);
	return obj ? to_drm_gem_dma_obj(obj) : NULL;
}

static int msm_ioctl_get_param(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_param *p = data;

	if (p->pipe != MSM_PIPE_3D0 || p->pad)
		return -EINVAL;
	switch (p->param) {
	case MSM_PARAM_GPU_ID:      p->value = msm_gpu_id; return 0;
	case MSM_PARAM_CHIP_ID:     /* core.major.minor.patch, as freedreno decodes it */
		p->value = ((u64)(msm_gpu_id / 100) << 24) | ((msm_gpu_id % 100 / 10) << 16) |
			   ((msm_gpu_id % 10) << 8);
		return 0;
	case MSM_PARAM_GMEM_SIZE:   p->value = XENOS_EDRAM_BYTES; return 0;
	case MSM_PARAM_GMEM_BASE:   p->value = 0; return 0;
	case MSM_PARAM_MAX_FREQ:    p->value = 500000000; return 0;
	case MSM_PARAM_TIMESTAMP:   p->value = ktime_get_raw_ns(); return 0;
	case MSM_PARAM_PRIORITIES:  p->value = 1; return 0;
	case MSM_PARAM_FAULTS:      p->value = 0; return 0;
	case MSM_PARAM_SUSPENDS:    p->value = 0; return 0;
	case MSM_PARAM_VA_START:    p->value = 0; return 0;
	case MSM_PARAM_VA_SIZE:     p->value = 0x20000000; return 0;
	default:                    return -EINVAL;
	}
}

static int msm_ioctl_set_param(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_param *p = data;
	/* COMM/CMDLINE/SYSPROF are hints; accept and ignore */
	if (p->param == MSM_PARAM_COMM || p->param == MSM_PARAM_CMDLINE || p->param == MSM_PARAM_SYSPROF)
		return 0;
	return -EINVAL;
}

/* GL buffers from the boot-time pool: after boot the kernel rarely finds a few
 * contiguous megabytes (Xorg's 1280x720 front buffer failed), the pool always has them */
static void xe_pool_gem_free(struct drm_gem_object *gobj)
{
	struct drm_gem_dma_object *obj = to_drm_gem_dma_obj(gobj);
	struct xe_gem_obj *xo = container_of(obj, struct xe_gem_obj, dma);

	/* A buffer user space mapped cached can leave dirty lines behind. Write
	 * them back and drop them now: evicted later, they would land on whatever
	 * buffer reuses this memory. */
	if (obj->map_noncoherent) {
		unsigned long v = (unsigned long)phys_to_virt(obj->dma_addr);
		flush_dcache_range(v, v + xo->pool_size);
	}
	gen_pool_free(xe_pool, (unsigned long)obj->vaddr, xo->pool_size);
	drm_gem_object_release(gobj);
	kfree(xo);
}

static int xe_pool_gem_mmap(struct drm_gem_object *gobj, struct vm_area_struct *vma)
{
	struct drm_gem_dma_object *obj = to_drm_gem_dma_obj(gobj);

	vma->vm_pgoff -= drm_vma_node_start(&gobj->vma_node);
	vm_flags_set(vma, VM_IO | VM_DONTEXPAND | VM_DONTDUMP);
	/* write-combined (cache-inhibited, unguarded) so the CPU can gather stores
	 * (x11perf putimage500 +6%); the submit ioctl orders them with mb() */
	if (obj->map_noncoherent)
		vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);   /* cached, synced at submit */
	else
		vma->vm_page_prot = msm_map_wc ? pgprot_writecombine(vm_get_page_prot(vma->vm_flags))
					       : pgprot_dmacoherent(vm_get_page_prot(vma->vm_flags));
	return remap_pfn_range(vma, vma->vm_start, (obj->dma_addr >> PAGE_SHIFT) + vma->vm_pgoff,
			       vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static const struct vm_operations_struct xe_pool_vm_ops = {
	.open = drm_gem_vm_open,
	.close = drm_gem_vm_close,
};

static const struct drm_gem_object_funcs xe_pool_gem_funcs = {
	.free = xe_pool_gem_free,
	.mmap = xe_pool_gem_mmap,
	.vm_ops = &xe_pool_vm_ops,
};

static struct drm_gem_dma_object *xe_pool_gem_create(struct drm_device *dev, size_t size)
{
	struct genpool_data_align align;
	struct xe_gem_obj *xo;
	unsigned long v;

	if (!xe_pool)
		return NULL;
	size = PAGE_ALIGN(size);
	align.align = min_t(size_t, roundup_pow_of_two(size), 2u << 20);
	v = gen_pool_alloc_algo(xe_pool, size, gen_pool_first_fit_align, &align);
	if (!v)
		return NULL;
	xo = kzalloc(sizeof(*xo), GFP_KERNEL);
	if (!xo) {
		gen_pool_free(xe_pool, v, size);
		return NULL;
	}
	/* the pool is recycled between processes: clear what the last owner left
	 * (dma_alloc_coherent, the fallback, already returns zeroed memory;
	 * piglit security/initialized-*) */
	if (msm_zero_bo)
		memset((void *)v, 0, size);
	xo->gl_tiled = true;
	xo->pool_size = size;
	xo->dma.vaddr = (void *)v;
	xo->dma.dma_addr = gen_pool_virt_to_phys(xe_pool, v);
	xo->dma.base.funcs = &xe_pool_gem_funcs;
	drm_gem_private_object_init(dev, &xo->dma.base, size);
	return &xo->dma;
}

static int msm_ioctl_gem_new(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_gem_new *a = data;
	struct drm_gem_dma_object *obj;
	int ret;

	if (!a->size || a->size > (64u << 20))
		return -EINVAL;
	obj = msm_pool ? xe_pool_gem_create(dev, a->size) : NULL;
	/* MSM_BO_CACHED_COHERENT (Mesa's command streams): map cached, and the submit
	 * writes the lines back (map_noncoherent). Mesa's per-dword stores to an
	 * uncached ring were a large part of every small batch. */
	if (obj && !IS_ERR(obj) && msm_cached_rings && (a->flags & MSM_BO_CACHED_COHERENT))
		obj->map_noncoherent = true;
	if (!obj) {
		xe_creating_msm_bo = true;   /* serialised by the DRM ioctl path for this device file */
		obj = drm_gem_dma_create(dev, PAGE_ALIGN(a->size));   /* contiguous; caching per msm_cached */
		xe_creating_msm_bo = false;
	}
	if (IS_ERR(obj))
		return PTR_ERR(obj);
	if (upper_32_bits(obj->dma_addr)) {
		drm_gem_object_put(&obj->base);
		return -ENOMEM;
	}
	ret = drm_gem_handle_create(file, &obj->base, &a->handle);
	if (msm_debug)
		pr_info("xegpu: msm bo %u size %zu at %08llx flags %x\n", a->handle, obj->base.size,
			(u64)obj->dma_addr, a->flags);
	drm_gem_object_put(&obj->base);
	return ret;
}

static int msm_ioctl_gem_info(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_gem_info *a = data;
	struct drm_gem_dma_object *obj = msm_lookup(file, a->handle);
	int ret = 0;

	if (!obj)
		return -ENOENT;
	switch (a->info) {
	case MSM_INFO_GET_OFFSET:
		ret = drm_gem_create_mmap_offset(&obj->base);
		if (!ret)
			a->value = drm_vma_node_offset_addr(&obj->base.vma_node);
		break;
	case MSM_INFO_GET_IOVA:
		a->value = obj->dma_addr;
		break;
	case MSM_INFO_SET_NAME:
	case MSM_INFO_SET_METADATA:
		break;
	case MSM_INFO_GET_NAME:
		a->len = 0;
		break;
	case MSM_INFO_GET_FLAGS:
		a->value = MSM_BO_CACHED;
		break;
	case MSM_INFO_SET_IOVA:
		ret = (a->value == obj->dma_addr) ? 0 : -EINVAL;   /* no MMU: iova is fixed */
		break;
	default:
		ret = -EINVAL;
	}
	drm_gem_object_put(&obj->base);
	return ret;
}

/* fences are emitted in order on the single ring; waiting for the newest covers any BO */
static int msm_wait_all(u32 timeout_ms)
{
	u64 newest = ((u64)fence_hi << 32) | fence_emitted;
	return cp_ready ? cp_wait_x(newest, timeout_ms, true) : 0;
}

static int msm_ioctl_gem_cpu_prep(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_gem_cpu_prep *a = data;
	struct drm_gem_dma_object *obj = msm_lookup(file, a->handle);
	int ret = 0;

	if (!obj)
		return -ENOENT;
	if (!(a->op & MSM_PREP_NOSYNC))
		ret = msm_wait_all(2000);
	if (!ret)
		dma_sync_single_for_cpu(dev->dev, obj->dma_addr, obj->base.size, DMA_BIDIRECTIONAL);
	drm_gem_object_put(&obj->base);
	return ret;
}

static int msm_ioctl_gem_cpu_fini(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_gem_cpu_fini *a = data;
	struct drm_gem_dma_object *obj = msm_lookup(file, a->handle);

	if (!obj)
		return -ENOENT;
	dma_sync_single_for_device(dev->dev, obj->dma_addr, obj->base.size, DMA_BIDIRECTIONAL);
	drm_gem_object_put(&obj->base);
	return 0;
}

static int msm_ioctl_gem_submit(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_gem_submit *a = data;
	struct drm_msm_gem_submit_bo *bos = NULL;
	struct drm_msm_gem_submit_cmd *cmds = NULL;
	struct drm_gem_dma_object **objs = NULL;
	u64 fence = 0;
	u32 i;
	int ret = 0;
	u64 t_start = ktime_get_ns(), t_check = 0;

	if (MSM_PIPE_ID(a->flags) != MSM_PIPE_3D0)
		return -EINVAL;
	if (a->flags & (MSM_SUBMIT_SYNCOBJ_IN | MSM_SUBMIT_SYNCOBJ_OUT | MSM_SUBMIT_FENCE_SN_IN))
		return -EOPNOTSUPP;
	if (a->flags & MSM_SUBMIT_FENCE_FD_IN) {
		/* single in-order ring: just wait for the input fence before queueing */
		struct dma_fence *in = sync_file_get_fence(a->fence_fd);
		long r;
		if (!in)
			return -EINVAL;
		r = dma_fence_wait_timeout(in, true, msecs_to_jiffies(10000));
		dma_fence_put(in);
		if (r < 0)
			return r;
		if (r == 0)
			return -ETIMEDOUT;
	}
	if (a->nr_bos > 4096 || !a->nr_cmds || a->nr_cmds > 256)
		return -EINVAL;

	bos = kvmalloc_array(a->nr_bos, sizeof(*bos), GFP_KERNEL);
	cmds = kvmalloc_array(a->nr_cmds, sizeof(*cmds), GFP_KERNEL);
	objs = kvcalloc(a->nr_bos, sizeof(*objs), GFP_KERNEL);
	if (!bos || !cmds || !objs) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(bos, u64_to_user_ptr(a->bos), a->nr_bos * sizeof(*bos)) ||
	    copy_from_user(cmds, u64_to_user_ptr(a->cmds), a->nr_cmds * sizeof(*cmds))) {
		ret = -EFAULT;
		goto out;
	}
	mb();   /* drain gathered stores to write-combined buffers before the GPU reads them */
	for (i = 0; i < a->nr_bos; i++) {
		objs[i] = msm_lookup(file, bos[i].handle);
		if (!objs[i]) {
			ret = -ENOENT;
			goto out;
		}
		bos[i].presumed = objs[i]->dma_addr;
	}
	if (msm_validate) {
		t_check = ktime_get_ns();
	}
	{
		struct xe_range *rg = kvmalloc_array(a->nr_bos, sizeof(*rg), GFP_KERNEL);
		if (!rg) {
			ret = -ENOMEM;
			goto out;
		}
		for (i = 0; i < a->nr_bos; i++) {
			rg[i].start = lower_32_bits(objs[i]->dma_addr);
			rg[i].end = rg[i].start + objs[i]->base.size;
			rg[i].vaddr = objs[i]->vaddr;
			rg[i].cached = objs[i]->map_noncoherent;
			rg[i].checked = false;
		}
		for (i = 0; i < a->nr_cmds && !ret && msm_validate; i++) {
			struct drm_msm_gem_submit_cmd *c = &cmds[i];
			struct drm_gem_dma_object *o;
			if (c->submit_idx >= a->nr_bos || (c->size & 3) ||
			    (u64)c->submit_offset + c->size > objs[c->submit_idx]->base.size) {
				ret = -EINVAL;
				break;
			}
			o = objs[c->submit_idx];
			rg[c->submit_idx].checked = true;
			{
				struct xe_cstate st = { 0 };
				ret = xe_check_stream(rg, a->nr_bos,
						      xe_check_view((u8 *)o->vaddr + c->submit_offset,
								    lower_32_bits(o->dma_addr) + c->submit_offset, c->size,
								    o->map_noncoherent),
						      c->size / 4, 0, &st);
			}
		}
		/* write back CPU caches so the GPU sees what user space wrote; only buffers
		 * mapped cached need it (this flushed the whole 4 MB front buffer on every
		 * submit). The cached checker already wrote back the command words it read;
		 * other cached buffers (Mesa's staging buffers) are cleaned whole. */
		for (i = 0; i < a->nr_bos && !ret; i++)
			if (objs[i]->map_noncoherent && !(rg[i].checked && msm_check_cached))
				dma_sync_single_for_device(dev->dev, objs[i]->dma_addr, objs[i]->base.size,
							   DMA_TO_DEVICE);
		kvfree(rg);
		if (msm_validate)
			submit_check_ns += ktime_get_ns() - t_check;
		if (ret) {
			msm_rejects++;
			goto out;
		}
	}
	for (i = 0; i < a->nr_cmds; i++) {
		struct drm_msm_gem_submit_cmd *c = &cmds[i];
		struct drm_gem_dma_object *o;

		if (c->submit_idx >= a->nr_bos || (c->size & 3) || !c->size) {
			ret = -EINVAL;
			goto out;
		}
		if (c->nr_relocs) {   /* iova mode only: user space writes addresses itself */
			ret = -EOPNOTSUPP;
			goto out;
		}
		o = objs[c->submit_idx];
		if ((u64)c->submit_offset + c->size > o->base.size) {
			ret = -EINVAL;
			goto out;
		}
		if (c->type == MSM_SUBMIT_CMD_CTX_RESTORE_BUF)
			continue;   /* single context: nothing to restore */
		if (msm_state_init) {
			ret = xenos_gpu_state_init();
			if (ret)
				goto out;
		}
		ret = cp_submit(lower_32_bits(o->dma_addr) + c->submit_offset, c->size / 4, &fence);
		if (ret)
			goto out;
	}
	a->fence = lower_32_bits(fence);
	submit_count++;
	if (a->flags & MSM_SUBMIT_FENCE_FD_OUT) {
		struct dma_fence *df = xe_fence_create(fence);
		submit_fenceout_count++;
		struct sync_file *sf;
		int fdn;

		if (!df) {
			ret = -ENOMEM;
			goto out;
		}
		sf = sync_file_create(df);
		dma_fence_put(df);
		fdn = get_unused_fd_flags(O_CLOEXEC);
		if (!sf || fdn < 0) {
			if (sf)
				fput(sf->file);
			if (fdn >= 0)
				put_unused_fd(fdn);
			ret = -ENOMEM;
			goto out;
		}
		fd_install(fdn, sf->file);
		a->fence_fd = fdn;
	}
	if (copy_to_user(u64_to_user_ptr(a->bos), bos, a->nr_bos * sizeof(*bos)))
		ret = -EFAULT;
out:
	if (objs)
		for (i = 0; i < a->nr_bos; i++)
			if (objs[i])
				drm_gem_object_put(&objs[i]->base);
	kvfree(objs);
	kvfree(cmds);
	kvfree(bos);
	submit_ns += ktime_get_ns() - t_start;
	return ret;
}

static int msm_ioctl_wait_fence(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_wait_fence *a = data;
	struct timespec64 ts = { .tv_sec = a->timeout.tv_sec, .tv_nsec = a->timeout.tv_nsec };
	s64 remain_ms = div_s64(timespec64_to_ns(&ts) - ktime_get_ns(), NSEC_PER_MSEC);   /* absolute CLOCK_MONOTONIC */

	if (!cp_ready)
		return 0;
	if (remain_ms < 0)
		remain_ms = 0;
	if (remain_ms > 10000)
		remain_ms = 10000;
	{
		int r = cp_wait_x(fence_to64(a->fence), (u32)remain_ms, true);
		xe_fence_retire();
		return r;
	}
}

static int msm_ioctl_gem_madvise(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_gem_madvise *a = data;
	a->retained = 1;   /* never purged */
	return 0;
}

static int msm_ioctl_submitqueue_new(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_submitqueue *a = data;
	if (a->prio)
		return -EINVAL;
	a->id = 1;
	return 0;
}

static int msm_ioctl_submitqueue_close(struct drm_device *dev, void *data, struct drm_file *file)
{
	return 0;
}

static int msm_ioctl_submitqueue_query(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_msm_submitqueue_query *a = data;
	u32 faults = 0;

	if (a->param != MSM_SUBMITQUEUE_PARAM_FAULTS)
		return -EINVAL;
	if (!a->data) {
		a->len = sizeof(faults);
		return 0;
	}
	if (copy_to_user(u64_to_user_ptr(a->data), &faults, min_t(u32, a->len, sizeof(faults))))
		return -EFAULT;
	return 0;
}

static const struct drm_ioctl_desc xenos_msm_ioctls[] = {
	DRM_IOCTL_DEF_DRV(MSM_GET_PARAM,    msm_ioctl_get_param,    DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SET_PARAM,    msm_ioctl_set_param,    DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_NEW,      msm_ioctl_gem_new,      DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_INFO,     msm_ioctl_gem_info,     DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_PREP, msm_ioctl_gem_cpu_prep, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_CPU_FINI, msm_ioctl_gem_cpu_fini, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_SUBMIT,   msm_ioctl_gem_submit,   DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_WAIT_FENCE,   msm_ioctl_wait_fence,   DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_GEM_MADVISE,  msm_ioctl_gem_madvise,  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SUBMITQUEUE_NEW,   msm_ioctl_submitqueue_new,   DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SUBMITQUEUE_CLOSE, msm_ioctl_submitqueue_close, DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(MSM_SUBMITQUEUE_QUERY, msm_ioctl_submitqueue_query, DRM_RENDER_ALLOW),
};
