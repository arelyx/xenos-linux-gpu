/* Replay a blob made by tools/rdstep.py: one arena, relocated, submitted as an IB.
 * Usage: xereplay blob.xrp [timeout_ms] [out.bin]
 * Logs each stage to /dev/kmsg first, so netconsole on insight keeps the trail if the
 * console dies. Never touches MMIO: success is the kernel fence, nothing else. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "../libxe/xe_linux.h"

static void klog(const char *fmt, const char *a, long b)
{
	char m[256]; int fd = open("/dev/kmsg", O_WRONLY);
	snprintf(m, sizeof m, fmt, a, b);
	if (fd >= 0) { write(fd, m, strlen(m)); close(fd); }
	fputs(m, stdout); fflush(stdout);
	sync();   /* NFS root: get the log out before the GPU runs */
}

static uint32_t be(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int main(int argc, char **argv)
{
	if (argc < 2) { fprintf(stderr, "usage: xereplay blob.xrp [timeout_ms] [out.bin]\n"); return 2; }
	unsigned tmo = argc > 2 ? atoi(argv[2]) : 2000;
	FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
	fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t *b = malloc(len); if (fread(b, 1, len, f) != (size_t)len) return 2; fclose(f);
	if (be(b) != 0x58525031) { fprintf(stderr, "bad magic\n"); return 2; }
	uint32_t nw = be(b + 4), nrel = be(b + 8), entry = be(b + 12), ndw = be(b + 16), rboff = be(b + 20), rblen = be(b + 24);
	if (28 + 4ul * (nw + nrel) != (unsigned long)len || entry + ndw * 4 > nw * 4) { fprintf(stderr, "bad blob\n"); return 2; }

	if (xe_linux_open()) return 1;
	uint32_t phys; uint32_t *arena = xe_linux_alloc(&phys, nw * 4, 0x10000);
	if (!arena) { fprintf(stderr, "alloc failed\n"); return 1; }
	for (uint32_t i = 0; i < nw; i++) arena[i] = be(b + 28 + 4 * i);
	for (uint32_t r = 0; r < nrel; r++) {
		uint32_t i = be(b + 28 + 4 * nw + 4 * r);
		if (i >= nw || arena[i] >= nw * 4) { fprintf(stderr, "bad reloc %u -> %08x\n", i, arena[i]); return 2; }
		arena[i] += phys;
	}
	/* mark the readback area so a resolve that writes nothing shows */
	for (uint32_t i = 0; i < rblen / 4; i++) arena[rboff / 4 + i] = 0xdeadbeef;
	__sync_synchronize();

	/* a small IB of its own: invalidate GPU caches over the arena */
	uint32_t prephys; uint32_t *pre = xe_linux_alloc(&prephys, 64, 64);
	if (!pre) return 1;
	int k = 0;
	pre[k++] = 0x00000a31; pre[k++] = 0x01000000;
	pre[k++] = 0x00010a2f; pre[k++] = (nw * 4 + 0x1fff) & ~0x1fff; pre[k++] = phys;
	pre[k++] = 0xc0043c00; pre[k++] = 3; pre[k++] = 0xa31; pre[k++] = 0; pre[k++] = 0x80000000; pre[k++] = 8;
	__sync_synchronize();
	unsigned long long fp = xe_linux_submit(prephys, k);
	if (xe_linux_wait(fp, 1000)) { klog("xereplay: %s: cache invalidate did not complete, GPU already unwell (%ld)\n", argv[1], 0); return 3; }

	klog("xereplay: %s: submitting %ld dwords\n", argv[1], ndw);
	unsigned long long fence = xe_linux_submit(phys + entry, ndw);
	int rc = xe_linux_wait(fence, tmo);
	if (rc) { klog("xereplay: %s: FENCE TIMEOUT (%ld) - GPU hung\n", argv[1], rc); return 4; }
	klog("xereplay: %s: fence ok (%ld)\n", argv[1], 0);

	uint32_t *rb = arena + rboff / 4; unsigned changed = 0;
	for (uint32_t i = 0; i < rblen / 4; i++) changed += rb[i] != 0xdeadbeef;
	printf("readback: %u of %u words written; first %08x %08x %08x %08x\n", changed, rblen / 4, rb[0], rb[1], rb[2], rb[3]);
	if (argc > 3) { FILE *o = fopen(argv[3], "wb"); if (o) { fwrite(rb, 1, rblen, o); fclose(o); } }
	xe_linux_close();
	return 0;
}
