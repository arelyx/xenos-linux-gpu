/* glcolmat: one triangle per case, current color green set as ubyte or
 * float, with and without lighting + GL_COLOR_MATERIAL, and around a
 * glPushAttrib/glPopAttrib(GL_CURRENT_BIT). Prints the pixel (want 0,255,0). */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
static const GLubyte green_ub[4] = {0, 255, 0, 255}, red_ub[4] = {255, 0, 0, 255};
static void tri(void)
{
	static const float v[] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
	static const float n[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
	glVertexPointer(3, GL_FLOAT, 0, v); glNormalPointer(GL_FLOAT, 0, n);
	glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY);
	glDrawArrays(GL_TRIANGLES, 0, 3);
}
static void tri_elements(void)
{
	static const float v[] = {-1, -1, 0, 3, -1, 0, -1, 3, 0};
	static const float n[] = {0, 0, 1, 0, 0, 1, 0, 0, 1};
	static const GLuint idx[] = {0, 1, 2};
	glVertexPointer(3, GL_FLOAT, 0, v); glNormalPointer(GL_FLOAT, 0, n);
	glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, idx);
}
static const float pv[12][3] = {{-0.5,-0.5,0},{-0.5,0.5,0},{0,0,0},{-0.5,0.5,0},{0.5,0.5,0},{0,0,0},
	{0.5,0.5,0},{0.5,-0.5,0},{0.1,-0.1,0},{0.5,-0.5,0},{-0.5,-0.5,0},{0.1,-0.1,0}};
static const float pn[12][3] = {{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1}};
static void show_at(const char *name, int x, int y)
{
	unsigned char px[4];
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%-28s %d,%d,%d\n", name, px[0], px[1], px[2]);
}
static void show(const char *name)
{
	unsigned char px[4];
	glReadPixels(8, 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%-28s %d,%d,%d\n", name, px[0], px[1], px[2]);
	glClear(GL_COLOR_BUFFER_BIT);
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
	GLuint rt, fbo;
	glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt);
	/* RGB=1: an RGB render target, as piglit's RGB visual gets */
	glTexImage2D(GL_TEXTURE_2D, 0, getenv("RGB") ? GL_RGB8 : GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
	glViewport(0, 0, 64, 64);
	glClear(GL_COLOR_BUFFER_BIT);

	if (getenv("PIGLIT")) {
		glViewport(0, 0, 64, 64);
		glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1, 1, -1, 1, -1, 1);
		glEnable(GL_LIGHTING);
		glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
		glEnable(GL_COLOR_MATERIAL); glEnable(GL_LIGHT0);
		glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
		GLfloat sp[] = {0, 0, 0, 0}, df[] = {1, 1, 1, 1};
		glLightfv(GL_LIGHT0, GL_SPECULAR, sp); glLightfv(GL_LIGHT0, GL_DIFFUSE, df);
		glVertexPointer(3, GL_FLOAT, 12, pv); glNormalPointer(GL_FLOAT, 12, pn);
		glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_NORMAL_ARRAY);
		static const GLuint i1[] = {0, 1, 2}, i2[] = {3, 4, 5}, i3[] = {6, 7, 8, 9, 10, 11};
		static const GLubyte b3[4] = {0, 0, 255, 255};
		glColor4ubv(red_ub); glPushAttrib(GL_CURRENT_BIT);
		glColor4ubv(green_ub); glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i1);
		glPopAttrib(); glPushAttrib(GL_CURRENT_BIT);
		glColor4ubv(b3); glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, i3);
		glPopAttrib(); glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, i2);
		show_at("tri1 (green)", 24, 32); show_at("tri3 (blue)", 40, 32); show_at("tri2 (red)", 24, 40);
		return 0;
	}
	if (getenv("FIRST")) {
		glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1, 1, -1, 1, -1, 1);
		glEnable(GL_LIGHTING);
		glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
		glEnable(GL_COLOR_MATERIAL); glEnable(GL_LIGHT0);
		glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
		GLfloat sp[] = {0, 0, 0, 0}, df[] = {1, 1, 1, 1};
		glLightfv(GL_LIGHT0, GL_SPECULAR, sp); glLightfv(GL_LIGHT0, GL_DIFFUSE, df);
		glColor4ubv(red_ub); glPushAttrib(GL_CURRENT_BIT);
		glColor4ubv(green_ub); tri_elements(); show("first: green");
		glPopAttrib(); glPushAttrib(GL_CURRENT_BIT);
		static const GLubyte b3[4] = {0, 0, 255, 255};
		glColor4ubv(b3); tri_elements(); show("first: blue");
		glPopAttrib(); tri_elements(); show("first: red");
		return 0;
	}

	glColor4ubv(green_ub); tri(); show("ubyte, no lighting");
	glColor3f(0, 1, 0); tri(); show("float, no lighting");

	GLfloat white[] = {1, 1, 1, 1}, black[] = {0, 0, 0, 0};
	glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, white); glLightfv(GL_LIGHT0, GL_SPECULAR, black);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
	glEnable(GL_COLOR_MATERIAL);
	glColor4ubv(green_ub); tri(); show("ubyte, color material");
	glColor3f(0, 1, 0); tri(); show("float, color material");

	glColor4ubv(red_ub); glPushAttrib(GL_CURRENT_BIT);
	glColor4ubv(green_ub); tri(); show("ubyte, cm, after push");
	glPopAttrib();
	glColor4ubv(green_ub); tri(); show("ubyte, cm, after pop");
	glColor4ubv(green_ub); tri_elements(); show("ubyte, cm, elements");
	glColor4ubv(red_ub); glPushAttrib(GL_CURRENT_BIT);
	glColor4ubv(green_ub); tri_elements(); show("piglit seq: green");
	glPopAttrib(); glPushAttrib(GL_CURRENT_BIT);
	static const GLubyte blue_ub[4] = {0, 0, 255, 255};
	glColor4ubv(blue_ub); tri_elements(); show("piglit seq: blue");
	glPopAttrib(); tri_elements(); show("piglit seq: red");
	glEnable(GL_LIGHTING);
	glLightf(GL_LIGHT0, GL_CONSTANT_ATTENUATION, 1.0f);
	glColor4ubv(green_ub); tri_elements(); show("+ attenuation call");
	glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(-1, 1, -1, 1, -1, 1);
	glColor4ubv(green_ub); tri_elements(); show("+ ortho projection");
	glColor4ubv(red_ub); glPushAttrib(GL_CURRENT_BIT);
	glColor4ubv(green_ub); tri_elements(); show("+ ortho, push seq green");
	glPopAttrib(); glPushAttrib(GL_CURRENT_BIT);
	static const GLubyte b2[4] = {0, 0, 255, 255};
	glColor4ubv(b2); tri_elements(); show("+ ortho, push seq blue");
	glPopAttrib();
	return 0;
}
