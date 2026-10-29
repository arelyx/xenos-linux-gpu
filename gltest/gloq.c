/* gloq: piglit occlusion_query_conform's GetObjivAval_multi2. A red occluder
 * at z -10, then three quads behind it at z -15 with color and depth writes
 * off, each in its own GL_SAMPLES_PASSED query: 1.4 wide, 1.06 wide, and 0.8
 * wide (fully hidden). Prints the counts (want p1 > p2 > p3 == 0). */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
static void rect(float x0, float y0, float x1, float y1)
{
	glBegin(GL_QUADS);
	glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
	glEnd();
}
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
	GLuint rt, ds, fbo;
	glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 160, 160, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenRenderbuffers(1, &ds); glBindRenderbuffer(GL_RENDERBUFFER, ds);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, 160, 160);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, ds);
	glViewport(0, 0, 160, 160);
	glClearDepth(1.0); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1, 1, -1, 1, 0, 25);
	glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glTranslatef(0, 0, -10);
	glColor3f(1, 0, 0); rect(-0.5, 0.5, 0.5, -0.5);
	glTranslatef(0, 0, -5);
	if (!getenv("MASKON")) { glColorMask(0, 0, 0, 0); glDepthMask(GL_FALSE); }
	GLuint q[3];
	glGenQueries(3, q);
	const float s[3] = {0.7f, 0.53f, 0.4f};
	for (int i = 0; i < 3; i++) {
		glBeginQuery(GL_SAMPLES_PASSED, q[i]);
		rect(-s[i], s[i], s[i], -s[i]);
		glEndQuery(GL_SAMPLES_PASSED);
	}
	GLuint p[3];
	for (int i = 0; i < 3; i++)
		glGetQueryObjectuiv(q[i], GL_QUERY_RESULT, &p[i]);
	printf("p1 %u p2 %u p3 %u (want p1 > p2 > p3 == 0; areas: %u, %u, 0)\n", p[0], p[1], p[2],
	       112 * 112 - 80 * 80, 85 * 85 - 80 * 80);
	return 0;
}
