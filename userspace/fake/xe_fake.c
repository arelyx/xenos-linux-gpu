/* Offline libxe backend: no GPU. Allocations come from a heap arena with fake GPU
 * addresses, submits are recorded, and the result is written as a freedreno .rd
 * dump (one buffer = the arena, one cmdstream per submit; gzip it) so rddecode.py and
 * rdstep.py read libxenon's streams the same way as freedreno's.
 * Run under qemu-ppc64. FAKE_RD=out.rd, FAKE_MAX_SUBMITS=n (stop after n). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "../libxe/xe_linux.h"

#define ARENA_PHYS 0x1A000000u   /* distinctive: any stream word in [base, base+used) is an address */
#define ARENA_SIZE (64u << 20)
static uint8_t *arena; static size_t used;
static uint32_t regs[XEGPU_REG_SIZE / 4];
static struct xegpu_info info = { .scanout_width = 1280, .scanout_height = 720, .scanout_pitch = 1280, .scanout_addr = 0x1f000000 };
static struct { uint32_t addr, ndw; } subs[4096]; static int nsubs;
uint32_t xenos_id = 0x5841;

static void be32(FILE *f, uint32_t v) { uint8_t b[4] = { v >> 24, v >> 16, v >> 8, v }; fwrite(b, 1, 4, f); }
static void dump(void)
{
	const char *p = getenv("FAKE_RD") ? getenv("FAKE_RD") : "fake.rd";
	FILE *f = fopen(p, "wb");
	be32(f, 3); be32(f, 12); be32(f, ARENA_PHYS); be32(f, used); be32(f, 0);
	be32(f, 12); be32(f, used); fwrite(arena, 1, used, f);
	for (int i = 0; i < nsubs; i++) { be32(f, 6); be32(f, 12); be32(f, subs[i].addr); be32(f, subs[i].ndw); be32(f, 0); }
	fclose(f);
	fprintf(stderr, "fake: wrote %s, %d submits, arena %zu bytes\n", p, nsubs, used);
}
int xe_linux_open(void)
{
	arena = calloc(1, ARENA_SIZE);
	regs[0x6110 / 4] = info.scanout_addr; regs[0x6120 / 4] = 1280; regs[0x6134 / 4] = 1280; regs[0x6138 / 4] = 720;
	atexit(dump);
	return 0;
}
void xe_linux_close(void) {}
void xe_linux_quiesce(void) {}
volatile unsigned int *xe_linux_regs(void) { return regs; }
const struct xegpu_info *xe_linux_info(void) { return &info; }
void *xe_linux_alloc(uint32_t *phys, int size, int align)
{
	unsigned a = 1; while (a < (unsigned)(align ? align : size)) a <<= 1;
	if (a > 0x100000) a = 0x100000;
	size_t off = (used + a - 1) & ~(size_t)(a - 1);
	if (off + size > ARENA_SIZE) { fprintf(stderr, "fake: arena full\n"); exit(1); }
	used = off + size; if (phys) *phys = ARENA_PHYS + off;
	return arena + off;
}
void xe_linux_free(void *p) { (void)p; }
void *xe_linux_phys_to_virt(uint32_t phys)
{
	if (phys >= ARENA_PHYS && phys < ARENA_PHYS + ARENA_SIZE) return arena + (phys - ARENA_PHYS);
	return NULL;
}
uint32_t xe_linux_virt_to_phys(const void *p) { return ARENA_PHYS + ((const uint8_t *)p - arena); }
void xenos_write32(int reg, uint32_t val) { regs[reg / 4] = val; }
uint32_t xenos_read32(int reg) { return regs[reg / 4]; }
int xe_linux_cp_init(void) { return 0; }
unsigned long long xe_linux_submit(uint32_t gpu_addr, uint32_t ndw)
{
	subs[nsubs].addr = gpu_addr; subs[nsubs].ndw = ndw; nsubs++;
	const char *m = getenv("FAKE_MAX_SUBMITS");
	if (m && nsubs >= atoi(m)) exit(0);
	return nsubs;
}
int xe_linux_wait(unsigned long long fence, unsigned timeout_ms) { return 0; }
int xe_linux_record_begin(size_t n) { return 0; }
uint32_t *xe_linux_record_take(size_t *n) { *n = 0; return NULL; }
void xe_linux_record_arena(uint8_t **v, uint32_t *p, size_t *u) { *v = arena; *p = ARENA_PHYS; *u = used; }
void xe_linux_record_end(void) {}
