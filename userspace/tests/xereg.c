/* Read or write one Xenos register with a single aligned 32-bit access:  xereg OFF [VAL]
 * (Python mmap slicing does byte loads, which hang the bus on Xenos MMIO.) */
#include <stdio.h>
#include <stdlib.h>
#include "../libxe/xe_linux.h"
int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: xereg OFF [VAL]\n"); return 1; }
	unsigned off = strtoul(argv[1], 0, 0);
	if (off & 3 || off >= XEGPU_REG_SIZE) { fprintf(stderr, "offset must be 4-aligned and < 0x%x\n", XEGPU_REG_SIZE); return 1; }
	if (xe_linux_open()) return 1;
	volatile unsigned *R = xe_linux_regs();
	if (argc > 2) { R[off / 4] = strtoul(argv[2], 0, 0); __asm__ volatile("eieio" ::: "memory"); }
	printf("%04x = %08x\n", off, R[off / 4]);
	return 0;   /* note: closing /dev/xegpu quiesces the CP and restores the scanout */
}
