/* glupside: piglit tex-upside-down-miptree per level. Levels 6..0 hold
 * 64x64..1x1 (sizes reversed), min filter LINEAR, base level 1..6;
 * print the color drawn for each base level. Want 255,128,64,32. */
#include <stdio.h>
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
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	static unsigned char img[64][64][4];
	for (int i = 0; i < 64 * 64; i++) memcpy(img[0][i], (unsigned char[]){255, 128, 64, 32}, 4);
	GLuint t, fbo, c;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	for (int i = 0, w = 64; w > 0; i++, w /= 2)
		glTexImage2D(GL_TEXTURE_2D, 6 - i, GL_RGBA, w, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 32, 32);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glViewport(0, 0, 32, 32);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	for (int l = 1; l < 7; l++) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, l);
		glClearColor(0.5, 0.5, 0.5, 0.5); glClear(GL_COLOR_BUFFER_BIT);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1);
		glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1);
		glEnd();
		unsigned char px[32 * 32 * 4];
		glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, px);
		int bad = 0;
		for (int i = 0; i < 32 * 32; i++) bad += px[i * 4] != 255 || px[i * 4 + 3] != 32;
		printf("base %d: %3d,%3d,%3d,%3d  bad %d/1024\n", l, px[0], px[1], px[2], px[3], bad);
	}
	return 0;
}
