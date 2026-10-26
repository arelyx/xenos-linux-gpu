/* read-only: open /dev/xegpu, dump the registers libxenon looks at during init */
#include <stdio.h>
#include "../libxe/xe_linux.h"

static const struct { unsigned off; const char *name; } r[] = {
	{0x0e6c, "?? (init expects 0xF00 bits)"},
	{0x00e0, "RBBM_? (0x10 after reset)"},
	{0x1740, "RBBM_STATUS"},
	{0x07d8, "CP_ME_CNTL"},
	{0x0700, "CP_RB_BASE"},
	{0x0704, "CP_RB_CNTL"},
	{0x070c, "CP_RB_RPTR_ADDR"},
	{0x0710, "CP_RB_RPTR"},
	{0x0714, "CP_RB_WPTR"},
	{0x0770, "SCRATCH_UMSK"},
	{0x0774, "SCRATCH_ADDR"},
	{0x6110, "D1GRPH_PRIMARY_SURFACE_ADDRESS"},
	{0x6120, "D1GRPH_PITCH"},
	{0x6134, "D1GRPH_X_END"},
	{0x6138, "D1GRPH_Y_END"},
	{0x6100, "D1GRPH_ENABLE"},
};

int main(void)
{
	if (xe_linux_open()) return 1;
	const struct xegpu_info *i = xe_linux_info();
	printf("regs @ %llx size %x, scanout %08x %ux%u pitch %u\n",
	       (unsigned long long)i->reg_phys, i->reg_size, i->scanout_addr,
	       i->scanout_width, i->scanout_height, i->scanout_pitch);
	volatile unsigned *R = xe_linux_regs();
	for (unsigned k = 0; k < sizeof(r) / sizeof(r[0]); k++)
		printf("  %04x %-34s %08x\n", r[k].off, r[k].name, R[r[k].off / 4]);
	uint32_t phys;
	void *p = xe_linux_alloc(&phys, 4096, 4096);
	printf("test alloc: virt %p gpu %08x\n", p, phys);
	xe_linux_close();
	return 0;
}
