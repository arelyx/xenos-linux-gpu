/* glmipcolor: a 64x64 RGBA texture (red, or the color given as r g b), mipmapped
 * by glGenerateMipmap or GL_GENERATE_MIPMAP, drawn from each level (base
 * level = n); prints the color read back per level. Checks that mipmap
 * generation keeps the channel order on big-endian (games looked blue). */
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
	unsigned char c[4] = {200, 120, 40, 255};
	if (argc > 3) for (int i = 0; i < 3; i++) c[i] = atoi(argv[1 + i]);
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, rb;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 16, 16);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	glViewport(0, 0, 16, 16);
	static unsigned char img[64 * 64 * 4];
	for (int i = 0; i < 64 * 64; i++) for (int k = 0; k < 4; k++) img[i * 4 + k] = c[k];
	for (int mode = 0; mode < 3; mode++) {
		GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
		if (mode == 1) glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
		glTexImage2D(GL_TEXTURE_2D, 0, mode == 2 ? GL_RGB : GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		if (mode != 1) glGenerateMipmap(GL_TEXTURE_2D);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glEnable(GL_TEXTURE_2D);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		printf("%s:", mode == 0 ? "glGenerateMipmap RGBA" : mode == 1 ? "GL_GENERATE_MIPMAP RGBA" : "glGenerateMipmap RGB");
		for (int l = 0; l < 7; l++) {
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, l);
			glClear(GL_COLOR_BUFFER_BIT);
			glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1);
			glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1); glEnd();
			unsigned char px[4]; glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
			printf(" L%d %d,%d,%d", l, px[0], px[1], px[2]);
		}
		printf("   (want %d,%d,%d)\n", c[0], c[1], c[2]);
	}
	return 0;
}
