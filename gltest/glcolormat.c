/* glcolormat [light]: piglit push-pop-color-material in small. Three triangles with
 * glColor4ubv red / green / blue between glDrawElements (user indices), optionally under
 * fixed-function lighting with GL_COLOR_MATERIAL; prints the color at each triangle. */
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
	int light = argc > 1 && !strcmp(argv[1], "light");
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
	glClearColor(0.5, 0.5, 0.5, 1); glClear(GL_COLOR_BUFFER_BIT);
	if (light) {
		glEnable(GL_LIGHTING);
		glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
		glEnable(GL_COLOR_MATERIAL);
		glEnable(GL_LIGHT0);
		GLfloat spec[] = {0, 0, 0, 0}, diff[] = {1, 1, 1, 1};
		glLightfv(GL_LIGHT0, GL_SPECULAR, spec);
		glLightfv(GL_LIGHT0, GL_DIFFUSE, diff);
		glNormal3f(0, 0, 1);
	}
	static const GLfloat v[] = {-1, -1, 0, -0.4f, -1, 0, -1, -0.4f, 0,   -0.3f, -1, 0, 0.3f, -1, 0, -0.3f, -0.4f, 0,   0.4f, -1, 0, 1, -1, 0, 0.4f, -0.4f, 0};
	static const GLuint i0[] = {0, 1, 2}, i1[] = {3, 4, 5}, i2[] = {6, 7, 8};
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, v);
	static const GLfloat n[27] = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
	if (getenv("NARRAY")) {   /* normals from an array, as piglit does */
		glNormalPointer(GL_FLOAT, 0, n);
		glEnableClientState(GL_NORMAL_ARRAY);
	}
	GLubyte red[] = {255, 0, 0, 255}, green[] = {0, 255, 0, 255}, blue[] = {0, 0, 255, 255};
	if (getenv("PUSH")) {   /* piglit's order: push red, draw green, pop, push, draw blue, pop, draw red */
		glColor4ubv(red); glPushAttrib(GL_CURRENT_BIT);
		glColor4ubv(green); glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i1);
		glPopAttrib(); glPushAttrib(GL_CURRENT_BIT);
		glColor4ubv(blue); glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i2);
		glPopAttrib();
		glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i0);
	} else {
	glColor4ubv(red);   glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i0);
	glColor4ubv(green); glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i1);
	glColor4ubv(blue);  glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i2);
	}
	unsigned char px[64 * 64 * 4];
	glReadPixels(0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int xs[3] = {6, 26, 46};
	for (int k = 0; k < 3; k++) { unsigned char *q = px + (4 * 64 + xs[k]) * 4; printf("tri %d: %3d %3d %3d\n", k, q[0], q[1], q[2]); }
	printf("want red, green, blue\n");
	return 0;
}
