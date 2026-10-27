/* glstencilpix: clear stencil to 0xfe, glDrawPixels(GL_STENCIL_INDEX) left half 0x33 / right
 * half 0x66 (CPU path: Xenos has no stencil export), optionally (INV=1) a full-screen draw that
 * inverts stencil, then read stencil back. Size S (default 123). Tests the CPU stencil write
 * and the next batch's depth/stencil restore. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
int main(void)
{
	int S = getenv("S") ? atoi(getenv("S")) : 123;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, c, d;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, S, S);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glGenRenderbuffers(1, &d); glBindRenderbuffer(GL_RENDERBUFFER, d);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, S, S);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, d);
	glViewport(0, 0, S, S);
	glClearStencil(0xfe); glClear(GL_STENCIL_BUFFER_BIT);
	unsigned char *buf = malloc(S * S);
	for (int i = 0; i < S * S; i++) buf[i] = i % S < S / 2 ? 0x33 : 0x66;
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glWindowPos2i(0, 0);
	glDrawPixels(S, S, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, buf);
	if (getenv("INV")) {
		glEnable(GL_STENCIL_TEST);
		glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
		glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); glEnd();
		glDisable(GL_STENCIL_TEST);
	}
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, S, S, GL_STENCIL_INDEX, GL_UNSIGNED_BYTE, buf);
	for (int y = 0; y < S; y += S / 4) {
		printf("y%3d:", y);
		for (int x = 0; x < S; x += 8) printf(" %02x", buf[y * S + x]);
		putchar('\n');
	}
	printf("want 33 left / 66 right (cc / 99 with INV=1)\n");
	return 0;
}
