/* glbigtex: dEQP texture.size 2048x2048 rgb888 then rgb888 mipmapped in one
 * context; prints the GPU pool's free KB after each step to find what keeps
 * the first texture's memory. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
static void pool(const char *what)
{
	char b[64] = "";
	FILE *f = fopen("/sys/module/xegpu/parameters/pool_free_kb", "r");
	if (f) { fgets(b, sizeof(b), f); fclose(f); }
	printf("%-28s pool free %s", what, b);
}
static GLuint tex(int mip, int draw)
{
	static unsigned char img[2048 * 2048 * 3];
	GLuint t;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	for (int l = 0, s = 2048; s >= 1 && (l == 0 || mip); l++, s /= 2)
		{ glTexImage2D(GL_TEXTURE_2D, l, GL_RGB, s, s, 0, GL_RGB, GL_UNSIGNED_BYTE, img); GLenum e = glGetError(); if (e) { printf("level %d error 0x%x, ", l, e); pool("after"); } }
	printf("teximage error 0x%x\n", glGetError());
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
	if (draw) {
		glEnable(GL_TEXTURE_2D);
		glBegin(GL_QUADS); glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1);
		glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1); glEnd();
		unsigned char px[4]; glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	}
	return t;
}
int main(int argc, char **argv)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, c;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 256, 256);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glViewport(0, 0, 256, 256);
	pool("start");
	GLuint t = tex(0, argc < 2 || argc > 2);
	pool("16 MB texture drawn");
	glDeleteTextures(1, &t);
	pool("deleted");
	if (argc > 2) {	/* glbigtex draw unbind: draw once without a texture */
		glDisable(GL_TEXTURE_2D);
		glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); glEnd();
		glFinish();
		pool("drew untextured");
	}
	glFinish();
	pool("finished");
	t = tex(1, 1);
	pool("21 MB mipmapped texture");
	return 0;
}
