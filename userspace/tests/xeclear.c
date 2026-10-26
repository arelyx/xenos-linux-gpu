/* Xe_Init, then fill our own tiled 32-bit surface with a GPU clear, show it for a few seconds.
 * The kernel module restores the DRM scanout when we exit. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "../libxe/xe.h"
#include "../libxe/xe_linux.h"
#include "../libxe/edram.h"
#include <string.h>

int main(int argc, char **argv)
{
	int secs = argc > 1 ? atoi(argv[1]) : 5;
	static struct XenosDevice xe_s; struct XenosDevice *xe = &xe_s;
	if (xe_linux_open()) return 1;

	printf("Xe_Init...\n"); fflush(stdout);
	Xe_Init(xe);
	printf("Xe_Init done. fb %ux%u @ %08x\n", xe->default_fb.width, xe->default_fb.height, xe->default_fb.ptr); fflush(stdout);

	if (getenv("XE_EDRAM_INIT")) { printf("edram_init...\n"); fflush(stdout); edram_init(xe); printf("edram_init done\n"); fflush(stdout); }
	struct XenosSurface *fb = Xe_GetFramebufferSurface(xe);
	/* first clear: draw nothing, resolve with clear into the current scanout? no: our own surface */
	struct XenosSurface *mine = Xe_CreateTexture(xe, fb->width, fb->height, 1, XE_FMT_8888 | XE_FMT_BGRA, 1);
	printf("own surface %ux%u wpitch %u @ %08x\n", mine->width, mine->height, mine->wpitch, mine->ptr); fflush(stdout);

	Xe_SetRenderTarget(xe, mine);
	unsigned colors[] = { 0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff };
	for (int k = 0; k < 4; k++) {
		Xe_InvalidateState(xe);
		Xe_SetClearColor(xe, colors[k]);
		Xe_Resolve(xe);
		Xe_Sync(xe);
		uint32_t *px = xe_linux_phys_to_virt(mine->ptr);
		printf("pass %d: clear %08x -> first pixels %08x %08x, frameidx %d\n", k, colors[k], px[0], px[1000], xe->frameidx);
		fflush(stdout);
		if (k == 0) {
			Xe_SetFrameBufferSurface(xe, mine);
			printf("scanout switched to our surface\n"); fflush(stdout);
		}
		sleep(secs);
	}
	printf("done, closing (module restores scanout)\n");
	xe_linux_close();
	return 0;
}
