/* glcpudepth: a W-wide (default 123) depth texture filled on the CPU with 0.25, attached
 * to an FBO; first fill EDRAM depth with 1.0 in another batch, then draw a green quad at
 * window depth 0.75 with GL_LESS: nothing may turn green. Columns whose depth didn't come
 * back from memory (the restore) show up as G. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
int main(void)
{
	int W = getenv("W") ? atoi(getenv("W")) : 123, H = 16;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	/* another FBO of the same size first, its depth cleared to 1: leaves 1.0 in EDRAM */
	GLuint f0, c0, d0;
	glGenFramebuffers(1, &f0); glBindFramebuffer(GL_FRAMEBUFFER, f0);
	glGenRenderbuffers(1, &c0); glBindRenderbuffer(GL_RENDERBUFFER, c0);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c0);
	glGenRenderbuffers(1, &d0); glBindRenderbuffer(GL_RENDERBUFFER, d0);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, W, H);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, d0);
	glViewport(0, 0, W, H);
	glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	glFinish();
	float *z = malloc(sizeof(float) * W * H);
	for (int i = 0; i < W * H; i++) z[i] = 0.25f;
	GLuint t, f, c;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, W, H, 0, GL_DEPTH_COMPONENT, GL_FLOAT, z);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &f); glBindFramebuffer(GL_FRAMEBUFFER, f);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, H);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t, 0);
	glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
	glFinish();
	glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
	glColor3f(0, 1, 0);
	glBegin(GL_QUADS); glVertex3f(-1, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(1, 1, 0.5f); glVertex3f(-1, 1, 0.5f); glEnd();
	unsigned char *px = malloc(W * H * 4);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int g = 0;
	for (int x = 0; x < W; x++) { int on = px[(8 * W + x) * 4 + 1] > 128; g += on; putchar(on ? 'G' : '.'); }
	printf("\n%d green columns (want 0)\n", g);
	return 0;
}
