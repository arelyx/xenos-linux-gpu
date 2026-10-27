/* gldepthonly <tex|rb> <none|color> [nodraw]: depth FBO variants (piglit fbo-depth readpixels hung
 * the GPU with a depth-only FBO). Clears depth, draws a rect with depth ALWAYS, glFinish.
 * Prints ok if it returns. Desktop GL through GBM/EGL. */
#include <stdio.h>
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
	puts("ok");
	return 0;
}
