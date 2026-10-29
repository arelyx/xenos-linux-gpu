/* glbaselvl: piglit tex-upside-down-miptree's inverted miptree (level 6 is
 * 64x64, level 0 is 1x1), sampled with GL_LINEAR and base levels 1-6. Prints
 * the pixel per base level and per texture size (want 255,128,64,32). */
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
	GLuint rt, fbo;
	glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
	glViewport(0, 0, 64, 64);
	static unsigned char img[64 * 64 * 4];
	for (int i = 0; i < 64 * 64; i++) { img[i*4] = 255; img[i*4+1] = 128; img[i*4+2] = 64; img[i*4+3] = 32; }
	GLuint t;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	for (int i = 0, w = 64; w > 0; i++, w /= 2)
		glTexImage2D(GL_TEXTURE_2D, 6 - i, GL_RGBA, w, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glEnable(GL_TEXTURE_2D);
	for (int level = 1; level < 7; level++) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, level);
		glClearColor(0.5, 0.5, 0.5, 0.5); glClear(GL_COLOR_BUFFER_BIT);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1);
		glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1);
		glEnd();
		unsigned char px[4];
		glReadPixels(5, 5, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf("base %d (%dx%d): %d,%d,%d,%d err 0x%x\n", level, 1 << level, 1 << level, px[0], px[1], px[2], px[3], glGetError());
	}
	return 0;
}
