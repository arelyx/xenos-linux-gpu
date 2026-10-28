/* gllerp [texw]: measure bilinear weights. A texw x 1 texture alternating
 * black/white, GL_LINEAR, clamp to edge, drawn across 256 pixels; print
 * the drawn value next to the exact one for the first 2 texels' span. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
int main(int argc, char **argv)
{
	int tw = argc > 1 ? atoi(argv[1]) : 2, W = 256;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	unsigned char *img = calloc(tw, 4);
	for (int i = 0; i < tw; i++) img[i * 4] = img[i * 4 + 1] = img[i * 4 + 2] = img[i * 4 + 3] = (i & 1) ? 255 : 0;
	GLuint t, fbo, c;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, 1);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glViewport(0, 0, W, 1);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
	unsigned char px[256 * 4];
	glReadPixels(0, 0, W, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int per = W / tw;	/* pixels per texel */
	for (int i = per / 2; i < per / 2 + 2 * per && i < W; i++) {
		double x = (i + 0.5) / W * tw - 0.5;	/* texel space, centers at integers */
		int k = (int)(x + 1) - 1; double f = x - k;
		double v0 = (k & 1) ? 255 : 0, v1 = ((k + 1) & 1) ? 255 : 0;
		double want = v0 + (v1 - v0) * f;
		printf("px %3d frac %.4f want %6.2f got %3d  (got-want)/255 %+.4f\n", i, f, want, px[i * 4], (px[i * 4] - want) / 255);
	}
	return 0;
}
