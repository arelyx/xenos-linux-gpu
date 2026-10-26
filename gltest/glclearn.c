/* glclearn N [flushes]: N clears of a 64x64 FBO in one batch (no flush between them), then glFinish
 * and read back.  dEQP's first submit (3 clears in one batch) wedges the GPU; glsmoke's (1 clear) does not. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define W 64
int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 2, fl = argc > 2 ? atoi(argv[2]) : 0;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) { printf("eglInitialize failed\n"); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint tex, fbo;
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, W, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glViewport(0, 0, W, W);
	for (int i = 0; i < n; i++) {
		glClearColor(i & 1, (i >> 1) & 1, 0.5f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
		if (fl) glFlush();
	}
	glFinish();
	unsigned char px[4];
	glReadPixels(W / 2, W / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int i = n - 1;
	printf("clears=%d flush=%d center %d %d %d %d (want %d %d 128 255)\n", n, fl, px[0], px[1], px[2], px[3], (i & 1) * 255, ((i >> 1) & 1) * 255);
	return 0;
}
