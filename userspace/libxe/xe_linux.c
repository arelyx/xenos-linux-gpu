/* Linux user-space backend for libxenon's xe.c, on top of /dev/xegpu */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "xe_linux.h"

#define CHUNK_SIZE   (2u << 20)   /* sub-allocation chunk */
#define MAX_BOS      256

struct bo {
	uint32_t handle, phys;
	size_t size;
	uint8_t *virt;
	size_t used;          /* bump pointer for chunks */
	int chunk;
};

static int fd = -1;
static volatile unsigned int *regs;
static struct xegpu_info info;
static struct bo bos[MAX_BOS];
static int nbos;

/* record mode: allocations come from one arena, submits are captured instead of sent */
static int rec_on;
static struct bo *rec_arena;
static size_t rec_used;
static uint32_t *rec_buf;
static size_t rec_n, rec_cap;
static unsigned long long rec_fence;

static struct bo *bo_new(size_t size, int chunk)
{
	struct xegpu_alloc a = { .size = size };
	void *v;

	if (nbos == MAX_BOS)
		return NULL;
	if (ioctl(fd, XEGPU_IOC_ALLOC, &a)) {
		fprintf(stderr, "xegpu: alloc %zu failed: %s\n", size, strerror(errno));
		return NULL;
	}
	v = mmap(NULL, a.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, a.mmap_offset);
	if (v == MAP_FAILED) {
		fprintf(stderr, "xegpu: mmap bo failed: %s\n", strerror(errno));
		ioctl(fd, XEGPU_IOC_FREE, &a.handle);
		return NULL;
	}
	struct bo *b = &bos[nbos++];
	b->handle = a.handle; b->phys = a.gpu_addr; b->size = a.size;
	b->virt = v; b->used = 0; b->chunk = chunk;
	return b;
}

int xe_linux_open(void)
{
	fd = open("/dev/xegpu", O_RDWR | O_CLOEXEC);
	if (fd < 0) { perror("open /dev/xegpu"); return -1; }
	if (ioctl(fd, XEGPU_IOC_INFO, &info)) { perror("XEGPU_IOC_INFO"); return -1; }
	{ extern uint32_t xenos_id; FILE *f = fopen("/sys/bus/pci/devices/0000:01:0f.0/device", "r"); unsigned d; if (f && fscanf(f, "%x", &d) == 1) xenos_id = d; if (f) fclose(f); }
	regs = mmap(NULL, XEGPU_REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (regs == MAP_FAILED) { perror("mmap regs"); return -1; }
	return 0;
}

void xe_linux_quiesce(void) { if (fd >= 0) ioctl(fd, XEGPU_IOC_QUIESCE); }

void xe_linux_close(void)
{
	/* the kernel quiesces and frees everything on close */
	if (fd >= 0) close(fd);
	fd = -1;
}

volatile unsigned int *xe_linux_regs(void) { return regs; }
const struct xegpu_info *xe_linux_info(void) { return &info; }

void *xe_linux_alloc(uint32_t *phys, int size, int align)
{
	int i;
	if (!align)
		align = size;
	if (rec_on) {
		unsigned a = 1; while (a < (unsigned)align) a <<= 1;
		uint32_t start = (rec_arena->phys + rec_used + a - 1) & ~(a - 1);
		size_t off = start - rec_arena->phys;
		if (off + size > rec_arena->size) { fprintf(stderr, "record arena full\n"); return NULL; }
		rec_used = off + size;
		memset(rec_arena->virt + off, 0, size);
		if (phys) *phys = start;
		return rec_arena->virt + off;
	}
	/* round alignment to a power of two */
	unsigned a = 1; while (a < (unsigned)align) a <<= 1;

	if ((size_t)size > CHUNK_SIZE / 2 || a > CHUNK_SIZE / 4) {
		/* dedicated buffer: the kernel's buddy allocator aligns to the
		 * allocation order, which covers align <= rounded size */
		size_t sz = size; size_t p2 = 1; while (p2 < sz) p2 <<= 1;
		if (a > p2) p2 = a;
		struct bo *b = bo_new(p2, 0);
		if (!b) return NULL;
		if (b->phys & (a - 1)) { fprintf(stderr, "xegpu: misaligned bo %08x align %x\n", b->phys, a); }
		if (phys) *phys = b->phys;
		return b->virt;
	}
	for (i = 0; i <= nbos; i++) {
		struct bo *b = (i < nbos) ? &bos[i] : NULL;
		if (!b) {
			b = bo_new(CHUNK_SIZE, 1);
			if (!b) return NULL;
		}
		if (!b->chunk) continue;
		uint32_t start = (b->phys + b->used + a - 1) & ~(a - 1);
		size_t off = start - b->phys;
		if (off + size > b->size) continue;
		b->used = off + size;
		if (phys) *phys = start;
		memset(b->virt + off, 0, size);
		return b->virt + off;
	}
	return NULL;
}

void xe_linux_free(void *ptr)
{
	(void)ptr; /* chunks are reclaimed when the device is closed */
}

void *xe_linux_phys_to_virt(uint32_t phys)
{
	for (int i = 0; i < nbos; i++)
		if (phys >= bos[i].phys && phys < bos[i].phys + bos[i].size)
			return bos[i].virt + (phys - bos[i].phys);
	return NULL;
}

uint32_t xe_linux_virt_to_phys(const void *p)
{
	const uint8_t *q = p;
	for (int i = 0; i < nbos; i++)
		if (q >= bos[i].virt && q < bos[i].virt + bos[i].size)
			return bos[i].phys + (q - bos[i].virt);
	return 0;
}

/* register helpers libxenon's edram.c expects (native big-endian MMIO) */
uint32_t xenos_id = 0x5841;
void xenos_write32(int reg, uint32_t val) { regs[reg / 4] = val; __asm__ volatile("eieio" ::: "memory"); }
uint32_t xenos_read32(int reg) { return regs[reg / 4]; }

int xe_linux_cp_init(void)
{
	if (rec_on)
		return 0;
	if (ioctl(fd, XEGPU_IOC_CP_INIT)) { perror("XEGPU_IOC_CP_INIT"); return -1; }
	return 0;
}

unsigned long long xe_linux_submit(uint32_t gpu_addr, uint32_t ndw)
{
	if (rec_on) {
		const uint32_t *src = xe_linux_phys_to_virt(gpu_addr);
		if (rec_n + ndw > rec_cap) { rec_cap = (rec_n + ndw) * 2; rec_buf = realloc(rec_buf, rec_cap * 4); }
		memcpy(rec_buf + rec_n, src, ndw * 4);
		rec_n += ndw;
		return ++rec_fence;
	}
	struct xegpu_submit sb = { .gpu_addr = gpu_addr, .ndw = ndw };
	if (ioctl(fd, XEGPU_IOC_SUBMIT, &sb)) { perror("XEGPU_IOC_SUBMIT"); abort(); }
	return sb.fence;
}

int xe_linux_wait(unsigned long long fence, unsigned timeout_ms)
{
	if (rec_on)
		return 0;
	struct xegpu_wait w = { .fence = fence, .timeout_ms = timeout_ms };
	return ioctl(fd, XEGPU_IOC_WAIT, &w) ? -errno : 0;
}

int xe_linux_record_begin(size_t arena_size)
{
	rec_arena = bo_new(arena_size, 0);
	if (!rec_arena) return -1;
	rec_used = 0; rec_n = 0; rec_on = 1;
	return 0;
}
/* take the commands captured so far; caller frees */
uint32_t *xe_linux_record_take(size_t *n)
{
	uint32_t *r = rec_buf;
	*n = rec_n;
	rec_buf = NULL; rec_n = rec_cap = 0;
	return r;
}
void xe_linux_record_arena(uint8_t **virt, uint32_t *phys, size_t *used)
{
	*virt = rec_arena->virt; *phys = rec_arena->phys; *used = rec_used;
}
void xe_linux_record_end(void) { rec_on = 0; }
