/* Smoke test for xegpu's msm-compatible UAPI, without libdrm:
 * params, BO alloc + mmap, a command buffer that MEM_WRITEs a pattern into a second BO,
 * submit, wait fence, CPU_PREP, check the pattern. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <drm/drm.h>
#include <drm/msm_drm.h>

static int fd;
static uint64_t param(uint32_t p)
{
	struct drm_msm_param a = { .pipe = MSM_PIPE_3D0, .param = p };
	if (ioctl(fd, DRM_IOCTL_MSM_GET_PARAM, &a)) return (uint64_t)-1;
	return a.value;
}
static uint32_t bo_new(uint64_t size, void **map, uint64_t *iova)
{
	struct drm_msm_gem_new n = { .size = size, .flags = MSM_BO_WC };
	if (ioctl(fd, DRM_IOCTL_MSM_GEM_NEW, &n)) { perror("GEM_NEW"); return 0; }
	struct drm_msm_gem_info i = { .handle = n.handle, .info = MSM_INFO_GET_IOVA };
	ioctl(fd, DRM_IOCTL_MSM_GEM_INFO, &i); *iova = i.value;
	i.info = MSM_INFO_GET_OFFSET; ioctl(fd, DRM_IOCTL_MSM_GEM_INFO, &i);
	*map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, i.value);
	if (*map == MAP_FAILED) { perror("mmap bo"); return 0; }
	return n.handle;
}

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	fd = open(node, O_RDWR);
	if (fd < 0) { perror(node); return 1; }
	drm_version_t v = {0}; char name[32] = {0}; v.name = name; v.name_len = sizeof(name) - 1;
	ioctl(fd, DRM_IOCTL_VERSION, &v);
	printf("driver %s %d.%d, GPU_ID %llu CHIP_ID %llx GMEM %llu KB\n", name, v.version_major, v.version_minor,
	       (unsigned long long)param(MSM_PARAM_GPU_ID), (unsigned long long)param(MSM_PARAM_CHIP_ID),
	       (unsigned long long)param(MSM_PARAM_GMEM_SIZE) >> 10);

	void *cmap, *dmap; uint64_t civa, diva;
	uint32_t ch = bo_new(65536, &cmap, &civa), dh = bo_new(65536, &dmap, &diva);
	if (!ch || !dh) return 1;
	printf("cmd bo iova %08llx, data bo iova %08llx\n", (unsigned long long)civa, (unsigned long long)diva);
	memset(dmap, 0, 4096);

	/* type3 MEM_WRITE (0x3d): addr, then N dwords */
	uint32_t *c = cmap; int n = 0, words = 16;
	c[n++] = 0xC0000000 | ((words) << 16) | (0x3d << 8);
	c[n++] = (uint32_t)diva;
	for (int k = 0; k < words; k++) c[n++] = 0x5EED0000 + k;
	while (n % 4) c[n++] = 0x80000000;   /* type2 nops */

	struct drm_msm_gem_submit_bo bos[2] = { { .flags = MSM_SUBMIT_BO_READ, .handle = ch },
						{ .flags = MSM_SUBMIT_BO_WRITE, .handle = dh } };
	struct drm_msm_gem_submit_cmd cmd = { .type = MSM_SUBMIT_CMD_BUF, .submit_idx = 0, .submit_offset = 0, .size = n * 4 };
	struct drm_msm_gem_submit s = { .flags = MSM_PIPE_3D0, .nr_bos = 2, .nr_cmds = 1,
					.bos = (uintptr_t)bos, .cmds = (uintptr_t)&cmd, .queueid = 0 };
	if (ioctl(fd, DRM_IOCTL_MSM_GEM_SUBMIT, &s)) { perror("SUBMIT"); return 1; }
	struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
	struct drm_msm_wait_fence w = { .fence = s.fence, .timeout = { now.tv_sec + 2, now.tv_nsec } };
	int rc = ioctl(fd, DRM_IOCTL_MSM_WAIT_FENCE, &w);
	printf("submitted fence %u, wait rc %d (%s)\n", s.fence, rc, rc ? strerror(errno) : "ok");
	struct drm_msm_gem_cpu_prep p = { .handle = dh, .op = MSM_PREP_READ };
	ioctl(fd, DRM_IOCTL_MSM_GEM_CPU_PREP, &p);
	/* the CP writes MEM_WRITE payloads little-endian: compare byte-swapped on this BE host */
	uint32_t *d = dmap; int ok = 0;
	for (int k = 0; k < words; k++) ok += __builtin_bswap32(d[k]) == 0x5EED0000u + k;
	for (int k = 0; k < words; k++) printf("  [%2d] %08x (swapped %08x)\n", k, d[k], __builtin_bswap32(d[k]));
	printf("MEM_WRITE result: %d/%d words correct (first %08x)\n", ok, words, d[0]);
	return ok == words ? 0 : 2;
}
