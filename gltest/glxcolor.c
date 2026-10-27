/* glxcolor: desktop GL through GLX. Fixed-function draws of known colors, unlit and lit,
 * read back with glReadPixels: prints expected vs got. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
static void px(const char *what, int x, int y, float r, float g, float b)
{
	unsigned char p[4];
	glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
	printf("%-28s want %3d %3d %3d got %3d %3d %3d\n", what, (int)(r * 255), (int)(g * 255), (int)(b * 255), p[0], p[1], p[2]);
}
int main(void)
{
	Display *d = XOpenDisplay(NULL);
	int attr[] = { GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, GLX_DEPTH_SIZE, 16, None };
	XVisualInfo *vi = glXChooseVisual(d, DefaultScreen(d), attr);
	XSetWindowAttributes swa = { .colormap = XCreateColormap(d, RootWindow(d, vi->screen), vi->visual, AllocNone) };
	Window w = XCreateWindow(d, RootWindow(d, vi->screen), 0, 0, 128, 128, 0, vi->depth, InputOutput, vi->visual, CWColormap, &swa);
	XMapWindow(d, w);
	GLXContext c = glXCreateContext(d, vi, NULL, True);
	glXMakeCurrent(d, w, c);
	printf("%s / %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	glViewport(0, 0, 128, 128);
	glClearColor(0.2f, 0.4f, 0.6f, 1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glFinish(); px("clear (0.2,0.4,0.6)", 5, 5, 0.2f, 0.4f, 0.6f);
	glBegin(GL_QUADS); glColor3f(1, 0, 0); glVertex2f(-1, -1); glVertex2f(0, -1); glVertex2f(0, 0); glVertex2f(-1, 0); glEnd();
	glColor3f(0, 1, 0); glRectf(0, -1, 1, 0);
	glFinish(); px("glColor red quad", 32, 32, 1, 0, 0); px("glColor green rect", 96, 32, 0, 1, 0);
	glEnable(GL_LIGHTING); glEnable(GL_LIGHT0);
	GLfloat pos[] = { 0, 0, 1, 0 }, red[] = { 0.8f, 0.1f, 0, 1 };
	glLightfv(GL_LIGHT0, GL_POSITION, pos);
	glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
	glNormal3f(0, 0, 1);
	glRectf(-1, 0, 1, 1);
	glFinish(); px("lit red (0.8,0.1,0) +ambient", 64, 96, 0.8f + 0.2f * 0.8f, 0.1f + 0.02f, 0);
	/* glxgears: material set inside a display list */
	GLfloat green[] = { 0, 0.8f, 0.2f, 1 };
	GLuint l = glGenLists(1);
	glNewList(l, GL_COMPILE);
	glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
	glNormal3f(0, 0, 1);
	glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 0); glVertex2f(-1, 0); glEnd();
	glEndList();
	glCallList(l);
	glFinish(); px("list lit green (0,0.8,0.2)", 64, 32, 0, 0.8f + 0.16f, 0.2f + 0.04f);
	glDisable(GL_LIGHTING);
	GLuint l2 = glGenLists(1);
	glNewList(l2, GL_COMPILE);
	glColor3f(1, 0, 0);
	glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 0); glVertex2f(-1, 0); glEnd();
	glEndList();
	glCallList(l2);
	glFinish(); px("list unlit red", 64, 32, 1, 0, 0);
	/* glxgears' state: light at (5,5,10,0), culling, depth test, smooth/flat */
	glEnable(GL_LIGHTING); glEnable(GL_CULL_FACE); glEnable(GL_DEPTH_TEST);
	GLfloat gpos[] = { 5, 5, 10, 0 }, gred[] = { 0.8f, 0.1f, 0, 1 };
	glLightfv(GL_LIGHT0, GL_POSITION, gpos);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	GLuint l3 = glGenLists(1);
	glNewList(l3, GL_COMPILE);
	glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, gred);
	glShadeModel(GL_FLAT);
	glNormal3f(0, 0, 1);
	glBegin(GL_QUAD_STRIP); glVertex3f(-1, -1, 0); glVertex3f(1, -1, 0); glVertex3f(-1, 1, 0); glVertex3f(1, 1, 0); glEnd();
	glEndList();
	glCallList(l3);
	glFinish();
	{ float k = 10 / 12.247f; px("gears-like red", 64, 64, 0.8f * k + 0.16f, 0.1f * k + 0.02f, 0); }
	glXSwapBuffers(d, w);
	if (getenv("SHOW")) {   /* known colors on screen for a screenshot: clear + four quadrants */
		glDisable(GL_LIGHTING); glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE);
		glClearColor(0.2f, 0.4f, 0.6f, 1); glClear(GL_COLOR_BUFFER_BIT);
		glColor3f(1, 0, 0); glRectf(-1, -1, 0, 0);
		glColor3f(0, 1, 0); glRectf(0, -1, 1, 0);
		glColor3f(0, 0, 1); glRectf(-1, 0, 0, 1);
		glXSwapBuffers(d, w); glFinish();
		sleep(atoi(getenv("SHOW")));
	}
	return 0;
}
