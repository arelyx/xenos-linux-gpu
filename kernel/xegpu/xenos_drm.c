// SPDX-License-Identifier: GPL-2.0
/*
 * xegpu DRM display: the xenos tiny DRM driver, with framebuffers in contiguous
 * DMA memory and the linear -> tiled scanout conversion done by the GPU.
 *
 * Included from xegpu.c (shares the command processor, ring and fences).
 * Bind it in place of the built-in driver at runtime:
 *   echo 0000:01:0f.0 > /sys/bus/pci/drivers/xenos/unbind
 *   echo 0000:01:0f.0 > /sys/bus/pci/drivers/xegpu_drm/bind
 */
#include <linux/aperture.h>
#include <linux/iosys-map.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_atomic_state_helper.h>
#include <drm/drm_connector.h>
#include <drm/drm_damage_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fbdev_dma.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/clients/drm_client_setup.h>

#include "xenos_regs.h"
#include "xegpu_blit_rec.h"
static bool msm_cached;
module_param(msm_cached, bool, 0644);
static bool xe_creating_msm_bo;
static bool msm_pool = true;
static bool msm_debug;
module_param(msm_debug, bool, 0644);
module_param(msm_pool, bool, 0644);
/* GEM object plus a flag: buffers made through the msm interface hold GL render
 * targets, which Mesa keeps in the Xenos tiled layout the display reads natively */
struct xe_gem_obj {
	struct drm_gem_dma_object dma;
	bool gl_tiled;
	size_t pool_size;   /* nonzero: memory comes from the xegpu boot-time pool */
};

#include "msm_compat.c"

static bool direct_scanout = true;
module_param(direct_scanout, bool, 0644);
MODULE_PARM_DESC(direct_scanout, "Scan out GL (msm) framebuffers in place: they are already tiled");
static unsigned long blit_direct_count;
module_param(blit_direct_count, ulong, 0644);
static int gpu_blit;          /* set to 1 once EDRAM is trained (xegpu.service does it) */
module_param(gpu_blit, int, 0644);
MODULE_PARM_DESC(gpu_blit, "Use the GPU for the scanout conversion (needs trained EDRAM)");
static int blit_gpu_count, blit_cpu_count, blit_fail_count;
module_param(blit_gpu_count, int, 0444);
module_param(blit_cpu_count, int, 0444);
module_param(blit_fail_count, int, 0444);
/* time spent converting damage to scanout, microseconds, per path (write 0 to reset) */
static unsigned long blit_gpu_us, blit_cpu_us;
module_param(blit_gpu_us, ulong, 0644);
module_param(blit_cpu_us, ulong, 0644);

struct xenos_fb {
	dma_addr_t dma_addr;
	void *vaddr;
	size_t size;
};

struct xenos_device {
	struct drm_device dev;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
	struct drm_display_mode fixed_mode;
	struct xenos_fb real_framebuffer;
	u32 scanout_now;   /* what D1GRPH_PRIMARY_SURFACE_ADDRESS points at */
	void __iomem *regs;
};

/* replay buffers for the recorded blit streams */
static void *blit_arena_v;
static dma_addr_t blit_arena_d;
static u32 *blit_ib_v;
static dma_addr_t blit_ib_d;
#define BLIT_IB_BYTES 0x10000


static struct xenos_device *xenos_of_pipe(struct drm_simple_display_pipe *pipe)
{
	return container_of(pipe, struct xenos_device, pipe);
}

static struct xenos_device *xenos_of_dev(struct drm_device *dev)
{
	return container_of(dev, struct xenos_device, dev);
}

static int blit_alloc(struct device *dev)
{
	if (blit_arena_v)
		return 0;
	blit_arena_v = dma_alloc_coherent(dev, PAGE_ALIGN(XEGPU_BLIT_ARENA_SIZE), &blit_arena_d, GFP_KERNEL);
	blit_ib_v = dma_alloc_coherent(dev, BLIT_IB_BYTES, &blit_ib_d, GFP_KERNEL);
	if (!blit_arena_v || !blit_ib_v)
		return -ENOMEM;
	memcpy(blit_arena_v, blit_arena, XEGPU_BLIT_ARENA_SIZE);
	return 0;
}

static void blit_free(struct device *dev)
{
	if (blit_arena_v)
		dma_free_coherent(dev, PAGE_ALIGN(XEGPU_BLIT_ARENA_SIZE), blit_arena_v, blit_arena_d);
	if (blit_ib_v)
		dma_free_coherent(dev, BLIT_IB_BYTES, blit_ib_v, blit_ib_d);
	blit_arena_v = NULL;
	blit_ib_v = NULL;
}

/* copy a recorded stream into the IB buffer, apply relocations, submit, wait */
static int blit_run(const u32 *words, size_t n, const struct xegpu_reloc *rel, size_t nrel,
		    u32 src, u32 dst)
{
	u32 arena_base = lower_32_bits(blit_arena_d) - XEGPU_BLIT_ARENA_SKIP;
	u64 fence;
	size_t i;
	int rc;

	if (n * 4 > BLIT_IB_BYTES)
		return -E2BIG;
	memcpy(blit_ib_v, words, n * 4);
	for (i = 0; i < nrel; i++) {
		u32 v;
		switch (rel[i].kind) {
		case 1: v = arena_base + rel[i].delta; break;
		case 2: v = src | rel[i].delta; break;
		case 3: v = dst | rel[i].delta; break;
		default: return -EINVAL;
		}
		blit_ib_v[rel[i].idx] = v;
	}
	wmb();
	rc = cp_submit(lower_32_bits(blit_ib_d), n, &fence);
	if (rc)
		return rc;
	/* display updates must not be cut short by signals to the committing process */
	return cp_wait_x(fence, 500, false);
}

/* Run libxenon's recorded Xe_Init state stream once after every CP (re)initialisation.
 * The display blit needs it, and so does Mesa: freedreno leaves registers that the Xenos
 * reset values do not cover (found 26 Oct 2026: every GL first submit wedged the GPU unless a
 * display blit had run since the CP was last initialised). */
static unsigned int sq_gpr_pix = 64, sq_gpr_vtx = 64;
module_param(sq_gpr_pix, uint, 0644);
module_param(sq_gpr_vtx, uint, 0644);

static int xenos_gpu_state_init(void)
{
	int rc;

	if (READ_ONCE(cp_ready) && !blit_init_needed_flag)
		return 0;
	rc = blit_alloc(&xe_pdev->dev);
	if (rc)
		return rc;
	mutex_lock(&cp_lock);
	rc = cp_ready ? 0 : cp_init_locked();
	mutex_unlock(&cp_lock);
	if (rc)
		return rc;
	rc = blit_run(blit_init_words, ARRAY_SIZE(blit_init_words),
		      blit_init_relocs, ARRAY_SIZE(blit_init_relocs), 0, 0);
	if (rc)
		return rc;
	/* Xe_Init leaves SQ_GPR_MANAGEMENT at 0x70100: 16 pixel-shader GPRs, 112 vertex.
	 * A pixel shader needing more (glamor's composite shader uses 23) wedges the GPU;
	 * split the 128 registers evenly (REG_SIZE_PIX [10:4], REG_SIZE_VTX [18:12]).
	 * The GPU is idle here (blit_run waited for its fence). */
	{
		/* the way Xe_Init itself changes it: WAIT_UNTIL idle, eight writes with
		 * bit 0 (REG_DYNAMIC) set to one register (PKT0 bit 15), then the final value */
		u32 v = (sq_gpr_vtx << 12) | (sq_gpr_pix << 4);
		u32 gpr[] = { 0x000005c8, 0x00020000, 0x00078d00, v | 1, v | 1, v | 1, v | 1,
			      v | 1, v | 1, v | 1, v | 1, 0x00000d00, v };
		rc = blit_run(gpr, ARRAY_SIZE(gpr), NULL, 0, 0, 0);
		if (rc)
			return rc;
	}
	blit_init_needed_flag = false;
	return 0;
}

static int xenos_gpu_blit(u32 src, u32 dst)
{
	int rc = xenos_gpu_state_init();

	if (rc)
		return rc;
	return blit_run(blit_frame_words, ARRAY_SIZE(blit_frame_words),
			blit_frame_relocs, ARRAY_SIZE(blit_frame_relocs), src, dst);
}

/* --- Main display pipe --- */

static void xenos_enable(struct drm_simple_display_pipe *pipe,
			 struct drm_crtc_state *crtc_state,
			 struct drm_plane_state *plane_state)
{
	struct xenos_device *xenos = xenos_of_pipe(pipe);

	if (crtc_state->planes_changed || IS_ERR_OR_NULL(xenos->real_framebuffer.vaddr)) {
		int width = plane_state->fb->width;
		int height = plane_state->fb->height;
		int bpp = drm_format_info_bpp(plane_state->fb->format, 0);
		int cpp = DIV_ROUND_UP(bpp, 8);
		/* tiled surfaces are padded to 32 lines; the GPU resolve writes whole tiles */
		size_t size = width * ALIGN(height, 32) * cpp;

		/* Keep the scanout buffer across disable/enable (every Xorg start and
		 * stop) and reuse it when it is big enough: after hours of uptime a
		 * fresh 3.6 MB contiguous allocation failed, the display pointed at
		 * address 0, and the allocator's pressure took the network driver
		 * down with it (26 Oct 2026). */
		if (IS_ERR_OR_NULL(xenos->real_framebuffer.vaddr) || xenos->real_framebuffer.size < size) {
			dma_addr_t dma;
			void *v = dma_alloc_coherent(xenos->dev.dev, size, &dma, GFP_KERNEL | __GFP_NOWARN);

			if (!v) {
				drm_err(&xenos->dev, "no memory for a %dx%d scanout buffer\n", width, height);
				if (IS_ERR_OR_NULL(xenos->real_framebuffer.vaddr))
					return;
			} else {
				if (!IS_ERR_OR_NULL(xenos->real_framebuffer.vaddr))
					dma_free_coherent(xenos->dev.dev, xenos->real_framebuffer.size,
							  xenos->real_framebuffer.vaddr,
							  xenos->real_framebuffer.dma_addr);
				xenos->real_framebuffer.vaddr = v;
				xenos->real_framebuffer.dma_addr = dma;
				xenos->real_framebuffer.size = size;
			}
		}

		drm_info(&xenos->dev, "Using %dx%d (%04lx) fb at %pad\n", width, height,
			 xenos->real_framebuffer.size, &xenos->real_framebuffer.dma_addr);
		iowrite32be(xenos->real_framebuffer.dma_addr,
			    xenos->regs + D1GRPH_PRIMARY_SURFACE_ADDRESS);
		xenos->scanout_now = lower_32_bits(xenos->real_framebuffer.dma_addr);
	}

	iowrite32be(1, xenos->regs + D1GRPH_ENABLE);
}

static void xenos_disable(struct drm_simple_display_pipe *pipe)
{
	struct xenos_device *xenos = xenos_of_pipe(pipe);
	iowrite32be(0, xenos->regs + D1GRPH_ENABLE);
	/* the scanout buffer is kept for the next enable (see xenos_enable) */
}

static void xenos_cpu_blit(struct iosys_map dst, const void *src_vaddr,
			   struct drm_rect src_rect, struct drm_framebuffer *fb)
{
	for (int y = src_rect.y1; y < src_rect.y2; y++) {
		for (int x = src_rect.x1 & ~3; x < src_rect.x2; x += 4) {
			int ndx = ((((y & ~31) * fb->width) + (x & ~31) * 32) +
				   (((x & 3) + ((y & 1) << 2) +
				     ((x & 28) << 1) + ((y & 30) << 5)) ^
				    ((y & 8) << 2)));
			u32 *dst32 = dst.vaddr + ndx * 4;
			const u32 *src32 = src_vaddr + x * 4 + y * fb->pitches[0];
			*dst32++ = swab32(*src32++);
			*dst32++ = swab32(*src32++);
			*dst32++ = swab32(*src32++);
			*dst32++ = swab32(*src32++);
		}
	}
}

static void xenos_update(struct drm_simple_display_pipe *pipe,
			 struct drm_plane_state *old_state)
{
	struct xenos_device *xenos = xenos_of_pipe(pipe);
	struct drm_device *dev = &xenos->dev;
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_atomic_helper_damage_iter iter;
	struct drm_rect damage;
	struct iosys_map dst;
	struct drm_gem_dma_object *obj;
	bool any = false;
	int idx;

	ktime_t t0 = ktime_get();

	if (!fb || IS_ERR_OR_NULL(xenos->real_framebuffer.vaddr))
		return;
	if (!drm_dev_enter(dev, &idx))
		return;

	/* a GL (msm) buffer is already tiled: point the display at it, no copy */
	obj = drm_fb_dma_get_gem_obj(fb, 0);
	if (direct_scanout && container_of(obj, struct xe_gem_obj, dma)->gl_tiled &&
	    fb->width == XEGPU_BLIT_W && fb->height == XEGPU_BLIT_H) {
		u32 addr = lower_32_bits(drm_fb_dma_get_gem_addr(fb, state, 0));
		if (addr != xenos->scanout_now) {
			pr_info("xegpu: direct scanout %08x (gem %08llx size %zu, fb %ux%u pitch %u fmt %p4cc)\n",
				addr, (u64)obj->dma_addr, obj->base.size, fb->width, fb->height,
				fb->pitches[0], &fb->format->format);
			iowrite32be(addr, xenos->regs + D1GRPH_PRIMARY_SURFACE_ADDRESS);
			xenos->scanout_now = addr;
		}
		blit_direct_count++;
		drm_dev_exit(idx);
		return;
	}
	if (xenos->scanout_now != lower_32_bits(xenos->real_framebuffer.dma_addr)) {
		iowrite32be(xenos->real_framebuffer.dma_addr, xenos->regs + D1GRPH_PRIMARY_SURFACE_ADDRESS);
		xenos->scanout_now = lower_32_bits(xenos->real_framebuffer.dma_addr);
	}

	/* write back the CPU cache over the damaged lines (framebuffers are cached) */
	drm_fb_dma_sync_non_coherent(dev, old_state, state);

	drm_atomic_helper_damage_iter_init(&iter, old_state, state);
	drm_atomic_for_each_plane_damage(&iter, &damage)
		any = true;

	if (any && gpu_blit && fb->width == XEGPU_BLIT_W && fb->height == XEGPU_BLIT_H &&
	    fb->pitches[0] == XEGPU_BLIT_W * 4 && fb->format->format == DRM_FORMAT_XRGB8888 &&
	    !blit_alloc(dev->dev)) {
		u32 src = lower_32_bits(drm_fb_dma_get_gem_addr(fb, state, 0));
		int rc = xenos_gpu_blit(src, lower_32_bits(xenos->real_framebuffer.dma_addr));
		if (!rc) {
			blit_gpu_count++;
			blit_gpu_us += ktime_us_delta(ktime_get(), t0);
			drm_dev_exit(idx);
			return;
		}
		blit_fail_count++;
		/* a failed blit means the GPU is wedged (no software reset on Xenos):
		 * stop trying, or every screen update waits out the fence timeout */
		gpu_blit = 0;
		pr_warn("xegpu: GPU blit failed (%d); CPU scanout conversion from now on (gpu_blit=0)\n", rc);
	}

	obj = drm_fb_dma_get_gem_obj(fb, 0);
	iosys_map_set_vaddr(&dst, xenos->real_framebuffer.vaddr);
	drm_atomic_helper_damage_iter_init(&iter, old_state, state);
	drm_atomic_for_each_plane_damage(&iter, &damage) {
		struct drm_rect dst_clip = state->dst;

		if (!drm_rect_intersect(&dst_clip, &damage))
			continue;
		xenos_cpu_blit(dst, obj->vaddr, damage, fb);
		blit_cpu_count++;
	}
	blit_cpu_us += ktime_us_delta(ktime_get(), t0);
	drm_dev_exit(idx);
}

static const struct drm_simple_display_pipe_funcs xenos_pipe_funcs = {
	.enable = xenos_enable,
	.disable = xenos_disable,
	.update = xenos_update,
};

/* --- Connector --- */

static int xenos_connector_helper_get_modes(struct drm_connector *connector)
{
	struct xenos_device *xenos = xenos_of_dev(connector->dev);

	return drm_connector_helper_get_modes_fixed(connector, &xenos->fixed_mode);
}

static const struct drm_connector_helper_funcs xenos_connector_helper_funcs = {
	.get_modes = xenos_connector_helper_get_modes,
};

static const struct drm_connector_funcs xenos_connector_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* --- Main driver --- */

static const struct drm_mode_config_funcs xenos_mode_config_funcs = {
	.fb_create = drm_gem_fb_create_with_dirty,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

static int xenos_load(struct xenos_device *xenos)
{
	struct drm_device *dev = &xenos->dev;
	struct drm_connector *connector = &xenos->connector;
	int ret;

	uint32_t fb_width = ioread32be(xenos->regs + D1GRPH_X_END);
	uint32_t fb_height = ioread32be(xenos->regs + D1GRPH_Y_END);
	uint32_t is_progressive = ioread32be(xenos->regs + AVIVO_D1MODE_DATA_FORMAT);

	if (is_progressive)
		fb_height = fb_height / 2;

	struct drm_display_mode mode = { DRM_MODE_INIT(
		60, fb_width, fb_height, DRM_MODE_RES_MM(fb_width, 96ul),
		DRM_MODE_RES_MM(fb_height, 96ul)) };
	static const uint32_t formats[] = { DRM_FORMAT_XRGB8888 };

	ret = drmm_mode_config_init(dev);
	if (ret)
		return ret;

	dev->mode_config.min_width = 32;
	dev->mode_config.max_width = 4096;
	dev->mode_config.min_height = 32;
	dev->mode_config.max_height = 4096;
	dev->mode_config.preferred_depth = 24;
	dev->mode_config.funcs = &xenos_mode_config_funcs;

	xenos->fixed_mode = mode;

	ret = drm_connector_init(dev, connector, &xenos_connector_funcs, DRM_MODE_CONNECTOR_Unknown);
	if (ret)
		return ret;

	drm_connector_helper_add(connector, &xenos_connector_helper_funcs);

	ret = drm_simple_display_pipe_init(dev, &xenos->pipe, &xenos_pipe_funcs,
					   formats, ARRAY_SIZE(formats), NULL, connector);
	if (ret)
		return ret;

	drm_plane_enable_fb_damage_clips(&xenos->pipe.plane);
	drm_mode_config_reset(dev);
	return 0;
}

/* framebuffers: contiguous (the GPU has no MMU) and CPU-cached, synced on update.
 * msm (GL) buffers: uncached by default (msm_cached=0), like Adreno's write-combined
 * BOs; Mesa writes vertex/upload buffers through persistent maps and never asks for
 * a cache flush, so cached GL buffers showed stale data to the GPU. */
static struct drm_gem_object *xenos_gem_create_object(struct drm_device *drm, size_t size)
{
	struct xe_gem_obj *xo = kzalloc(sizeof(*xo), GFP_KERNEL);
	struct drm_gem_dma_object *obj = &xo->dma;

	if (!xo)
		return ERR_PTR(-ENOMEM);
	xo->gl_tiled = xe_creating_msm_bo;
	obj->map_noncoherent = !xe_creating_msm_bo || msm_cached;
	return &obj->base;
}

DEFINE_DRM_GEM_DMA_FOPS(xenos_fops);

static struct drm_driver xenos_driver = {
	.driver_features = DRIVER_MODESET | DRIVER_GEM | DRIVER_ATOMIC | DRIVER_RENDER,
	.name = "xegpu",
	.ioctls = xenos_msm_ioctls,
	.num_ioctls = ARRAY_SIZE(xenos_msm_ioctls),
	.desc = "Xenos DRM with GPU scanout conversion",
	.major = 1,
	.minor = 0,
	.fops = &xenos_fops,
	.gem_create_object = xenos_gem_create_object,
	DRM_GEM_DMA_DRIVER_OPS,
	DRM_FBDEV_DMA_DRIVER_OPS,
};

static int xenos_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct drm_device *dev;
	struct xenos_device *xenos;
	int ret;

	if (msm_compat) {
		/* freedreno keys features off the minor version: 4 = softpin + iova, no fence fds yet */
		xenos_driver.name = "msm";
		xenos_driver.minor = 4;
	}
	ret = aperture_remove_conflicting_pci_devices(pdev, xenos_driver.name);
	if (ret)
		return ret;
	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;
	/* the built-in xenos driver never releases its regions on unbind; share them */
	if (pci_request_regions(pdev, "xegpu_drm"))
		dev_info(&pdev->dev, "BAR already reserved (built-in xenos driver), sharing it\n");

	xenos = devm_drm_dev_alloc(&pdev->dev, &xenos_driver, struct xenos_device, dev);
	if (IS_ERR(xenos))
		return PTR_ERR(xenos);
	dev = &xenos->dev;

	xenos->regs = devm_ioremap(&pdev->dev, pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	if (!xenos->regs)
		return -ENOMEM;

	ret = xenos_load(xenos);
	if (ret)
		return ret;

	pci_set_drvdata(pdev, dev);
	ret = drm_dev_register(dev, 0);
	if (ret)
		return ret;

	drm_client_setup(dev, NULL);
	drm_info(dev, "xegpu DRM bound; GPU scanout conversion %s\n", gpu_blit ? "on" : "off until gpu_blit=1");
	return 0;
}

static void xenos_pci_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);

	drm_dev_unplug(dev);
	drm_atomic_helper_shutdown(dev);
	{
		struct xenos_device *xenos = container_of(dev, struct xenos_device, dev);

		if (!IS_ERR_OR_NULL(xenos->real_framebuffer.vaddr))
			dma_free_coherent(dev->dev, xenos->real_framebuffer.size,
					  xenos->real_framebuffer.vaddr, xenos->real_framebuffer.dma_addr);
		xenos->real_framebuffer.vaddr = NULL;
	}
	xe_fence_retire();
	xe_fence_flush_all();
	blit_free(&pdev->dev);
}

static const struct pci_device_id xenos_drm_pci_tbl[] = {
	{ PCI_VDEVICE(MICROSOFT, 0x5811), 0 },
	{ PCI_VDEVICE(MICROSOFT, 0x5821), 0 },
	{ PCI_VDEVICE(MICROSOFT, 0x5831), 0 },
	{ PCI_VDEVICE(MICROSOFT, 0x5841), 0 },
	{}
};

static struct pci_driver xenos_drm_pci_driver = {
	.name = "xegpu_drm",
	.id_table = xenos_drm_pci_tbl,
	.probe = xenos_pci_probe,
	.remove = xenos_pci_remove,
};
