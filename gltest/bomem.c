/* bomem: CPU write/read bandwidth to xegpu GEM buffers mapped write-combined
 * vs cached (MSM_BO_CACHED_COHERENT), plus the cost of cleaning the cached one
 * with dcbst from user space. Needs xegpu with msm_compat=1. */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <xf86drm.h>
#include <drm/msm_drm.h>

#define SZ (1 << 20)
static void copy_aligned_dst(char *d, const char *s, size_t n)
{
	while (n && ((uintptr_t)d & 7)) { *d++ = *s++; n--; }
	uint64_t *d8 = (uint64_t *)d;
	for (; n >= 32; n -= 32, s += 32, d8 += 4) {
		uint64_t a, b, c, e;
		memcpy(&a, s, 8); memcpy(&b, s + 8, 8); memcpy(&c, s + 16, 8); memcpy(&e, s + 24, 8);
		d8[0] = a; d8[1] = b; d8[2] = c; d8[3] = e;
	}
	for (; n >= 8; n -= 8, s += 8, d8++) { uint64_t a; memcpy(&a, s, 8); *d8 = a; }
	d = (char *)d8;
	while (n--) *d++ = *s++;
}
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }

static void *bo(int fd, unsigned flags)
{
	struct drm_msm_gem_new n = { .size = SZ, .flags = flags };
	if (drmIoctl(fd, DRM_IOCTL_MSM_GEM_NEW, &n)) { perror("gem_new"); exit(1); }
	struct drm_msm_gem_info i = { .handle = n.handle, .info = MSM_INFO_GET_OFFSET };
	if (drmIoctl(fd, DRM_IOCTL_MSM_GEM_INFO, &i)) { perror("gem_info"); exit(1); }
	void *p = mmap(NULL, SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, i.value);
	if (p == MAP_FAILED) { perror("mmap"); exit(1); }
	return p;
}

int main(void)
{
	int fd = drmOpenWithType("msm", NULL, DRM_NODE_RENDER);
	if (fd < 0) fd = open("/dev/dri/renderD128", O_RDWR);
	char *src = malloc(SZ + 64);
	memset(src, 0x5a, SZ);
	const char *name[] = { "write-combined", "cached" };
	unsigned flags[] = { MSM_BO_WC, MSM_BO_CACHED_COHERENT };
	for (int k = 0; k < 2; k++) {
		char *p = bo(fd, flags[k]);
		double t = now();
		for (int r = 0; r < 8; r++) memcpy(p, src, SZ);
		double w = (now() - t) / 8;
		t = now();
		for (int r = 0; r < 8; r++)
			for (int row = 0; row < SZ / 2048; row++)
				memcpy(p + row * 2048, src + 4 + row * 2000, 2000);
		double wu = (now() - t) / 8 * SZ / (SZ / 2048 * 2000);
		printf("%-15s rows from a 4-byte-aligned source %7.1f MB/s\n", name[k], 1 / wu);
		t = now();
		for (int r = 0; r < 8; r++)
			for (int row = 0; row < SZ / 2048; row++)
				copy_aligned_dst(p + row * 2048, src + 4 + row * 2000, 2000);
		wu = (now() - t) / 8 * SZ / (SZ / 2048 * 2000);
		printf("%-15s same, 8-byte stores           %7.1f MB/s\n", name[k], 1 / wu);
		t = now();
		volatile uint64_t s = 0;
		for (int r = 0; r < 2; r++) for (size_t i = 0; i < SZ / 8; i++) s += ((uint64_t *)p)[i];
		double rd = (now() - t) / 2;
		printf("%-15s write %7.1f MB/s  read %7.1f MB/s", name[k], 1 / w, 1 / rd);
		if (k == 1) {
			t = now();
			for (uintptr_t a = (uintptr_t)p; a < (uintptr_t)p + SZ; a += 128)
				__asm__ volatile("dcbst 0,%0" : : "r"(a) : "memory");
			__asm__ volatile("sync" : : : "memory");
			printf("  dcbst 1 MB %.0f us", (now() - t) * 1e6);
		}
		printf("\n");
	}
	return 0;
}
