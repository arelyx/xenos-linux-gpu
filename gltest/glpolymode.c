/* glpolymode [line|point|fill]: draw a 48x48 quad with glPolygonMode and count lit pixels
 * (hardware PA_SU_SC_MODE_CNTL.POLYMODE dual mode). */
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
	GLenum mode = argc > 1 && !strcmp(argv[1], "point") ? GL_POINT : argc > 1 && !strcmp(argv[1], "fill") ? GL_FILL : GL_LINE;
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
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glViewport(0, 0, 64, 64);
	glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
	glPolygonMode(getenv("FACE") ? (getenv("FACE")[0] == 'f' ? GL_FRONT : GL_BACK) : GL_FRONT_AND_BACK, mode);   /* FACE=front|back: only that face (the quad is CCW, front) */
	glColor3f(1, 1, 1);
	glBegin(GL_QUADS); glVertex2f(-0.75f, -0.75f); glVertex2f(0.75f, -0.75f); glVertex2f(0.75f, 0.75f); glVertex2f(-0.75f, 0.75f); glEnd();
	static unsigned char px[64 * 64 * 4];
	glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int lit = 0;
	for (int i = 0; i < 64 * 64; i++) lit += px[i * 4] > 128;
	printf("%s: %d lit pixels (fill ~2304, line ~188, point 4)\n", argc > 1 ? argv[1] : "line", lit);
	return 0;
}
