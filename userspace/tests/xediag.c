/* Diagnose the resolve path: pre-fill target with a pattern, resolve with clear, see what the GPU wrote. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../libxe/xe.h"
#include "../libxe/xe_linux.h"
#include "../libxe/edram.h"

static void census(const char *what, uint32_t *p, int n)
{
	uint32_t vals[8]; int cnt[8]; int nv = 0, other = 0;
	for (int i = 0; i < n; i++) {
		int k;
		for (k = 0; k < nv; k++) if (vals[k] == p[i]) { cnt[k]++; break; }
		if (k == nv) { if (nv < 8) { vals[nv] = p[i]; cnt[nv++] = 1; } else other++; }
	}
	printf("%s:", what);
	for (int k = 0; k < nv; k++) printf(" %08x x%d", vals[k], cnt[k]);
	if (other) printf(" (+%d other)", other);
	printf("\n");
}

int main(int argc, char **argv)
{
	static struct XenosDevice xe_s; struct XenosDevice *xe = &xe_s;
	setvbuf(stdout, NULL, _IONBF, 0);
	if (xe_linux_open()) return 1;
	volatile unsigned *R = xe_linux_regs();
	Xe_Init(xe);
	printf("after init RBBM_STATUS %08x CP_ME_CNTL %08x\n", R[0x1740/4], R[0x07d8/4]);
	if (getenv("XE_EDRAM_INIT")) edram_init(xe);

	struct XenosSurface *s = Xe_CreateTexture(xe, 256, 64, 1, XE_FMT_8888 | (getenv("XE_FMTX") ? (int)strtol(getenv("XE_FMTX"),0,0) : XE_FMT_BGRA), 1);
	uint32_t *px = xe_linux_phys_to_virt(s->ptr);
	int n = s->wpitch / 4 * s->hpitch;
	for (int i = 0; i < n; i++) px[i] = 0x11111111;
	printf("surface %ux%u wpitch %u hpitch %u @ %08x (%d words)\n", s->width, s->height, s->wpitch, s->hpitch, s->ptr, n);

	Xe_SetRenderTarget(xe, s);
	for (int pass = 0; pass < 3; pass++) {
		uint32_t c = 0xff112233;
		Xe_InvalidateState(xe);
		Xe_SetClearColor(xe, c);
		Xe_Resolve(xe);
		printf("pass %d queued, RBBM_STATUS %08x\n", pass, R[0x1740/4]);
		Xe_Sync(xe);
		printf("pass %d synced, RBBM_STATUS %08x rptr %x wptr %x\n", pass, R[0x1740/4], R[0x0710/4], R[0x0714/4]);
		char buf[64]; snprintf(buf, sizeof buf, "  after clear %08x", c);
		census(buf, px, n);
	}
	xe_linux_close();
	return 0;
}
