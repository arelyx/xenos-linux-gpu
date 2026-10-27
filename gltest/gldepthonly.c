/* gldepthonly <tex|rb> <none|color> [nodraw]: depth FBO variants (piglit fbo-depth readpixels hung
 * the GPU with a depth-only FBO). Clears depth, draws a rect with depth ALWAYS, glFinish.
 * Prints ok if it returns. Desktop GL through GBM/EGL. */
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
int main(int argc, char **argv)
{
	int tex = !strcmp(argv[1], "tex"), color = !strcmp(argv[2], "color"), draw = argc < 4;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, d, c;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	if (tex) {
		glGenTextures(1, &d); glBindTexture(GL_TEXTURE_2D, d);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 128, 128, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, d, 0);
	} else {
		glGenRenderbuffers(1, &d); glBindRenderbuffer(GL_RENDERBUFFER, d);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 128, 128);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, d);
	}
	if (color) {
		glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 128, 128);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	} else {
		glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
	}
	printf("status %x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER)); fflush(stdout);
	glViewport(0, 0, 128, 128);
	glClearDepth(0); glClear(GL_DEPTH_BUFFER_BIT);
	if (draw) {
		glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS);
		glBegin(GL_QUADS); glVertex3f(-1, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(1, 1, 0.5f); glVertex3f(-1, 1, 0.5f); glEnd();
	}
	glFinish();
	if (getenv("READ")) {   /* depth row/column readback: left half 0.25, right half 0.75 */
		glClearDepth(0.5); glClear(GL_DEPTH_BUFFER_BIT);
		glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS);
		glBegin(GL_QUADS); glVertex3f(-1, -1, -0.5f); glVertex3f(0, -1, -0.5f); glVertex3f(0, 1, -0.5f); glVertex3f(-1, 1, -0.5f);
		glVertex3f(0, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(1, 0, 0.5f); glVertex3f(0, 0, 0.5f); glEnd();
		static float d[128 * 128];
		glReadPixels(0, 0, 128, 128, GL_DEPTH_COMPONENT, GL_FLOAT, d);
		for (int y = 0; y < 128; y += 32) {
			printf("y%3d ", y);
			for (int x = 0; x < 128; x += 2) { float v = d[y * 128 + x]; putchar(v < 0.3f ? 'a' : v < 0.6f ? 'b' : v < 0.8f ? 'c' : '?'); }
			putchar('\n');
		}
		static unsigned u[128 * 128];
		glReadPixels(0, 0, 128, 128, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, u);
		for (int y = 0; y < 128; y += 100) { printf("raw y%d:", y); for (int x = 0; x < 128; x += 20) printf(" %08x", u[y * 128 + x]); putchar('\n'); }
		printf("x=0..127 step 2: a=0.25 b=0.5 c=0.75; d(0,0)=%f d(100,10)=%f d(100,100)=%f\n", d[0], d[10 * 128 + 100], d[100 * 128 + 100]);
	}
	puts("ok");
	return 0;
}
