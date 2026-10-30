/* eglfmt: the DMA-BUF formats and modifiers our EGL reports on the GBM and
 * X11 platforms (what WebKit's renderer chooses its buffer format from) */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
static void dump(const char *what, EGLDisplay dpy)
{
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) { printf("%s: eglInitialize failed\n", what); return; }
	PFNEGLQUERYDMABUFFORMATSEXTPROC qf = (void *)eglGetProcAddress("eglQueryDmaBufFormatsEXT");
	PFNEGLQUERYDMABUFMODIFIERSEXTPROC qm = (void *)eglGetProcAddress("eglQueryDmaBufModifiersEXT");
	const char *ext = eglQueryString(dpy, EGL_EXTENSIONS);
	printf("%s: EGL %d.%d, dma_buf_import_modifiers %s\n", what, maj, min,
	       ext && strstr(ext, "EGL_EXT_image_dma_buf_import_modifiers") ? "yes" : "no");
	if (!qf) return;
	EGLint n = 0, fmts[256];
	qf(dpy, 256, fmts, &n);
	printf("  %d formats\n", n);
	for (int i = 0; i < n; i++) {
		EGLuint64KHR mods[64]; EGLBoolean ext_only[64]; EGLint m = 0;
		if (qm) qm(dpy, fmts[i], 64, mods, ext_only, &m);
		printf("  %.4s 0x%08x mods=%d", (char *)&fmts[i], fmts[i], m);
		for (int j = 0; j < m && j < 4; j++) printf(" %llx%s", (unsigned long long)mods[j], ext_only[j] ? "(ext)" : "");
		printf("\n");
	}
}
#include <string.h>
int main(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	dump("gbm", eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL));
	dump("x11", eglGetPlatformDisplay(EGL_PLATFORM_X11_KHR, EGL_DEFAULT_DISPLAY, NULL));
	return 0;
}
