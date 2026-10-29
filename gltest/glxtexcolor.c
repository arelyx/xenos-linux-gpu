/* glxtexcolor: an X window with four textured quads, red, green, blue and
 * yellow, uploaded as GL_RGBA/GL_UNSIGNED_BYTE (top row) and GL_RGB (bottom
 * row). Capture it with xwd to check the channel order of texture uploads in
 * GLX windows on big-endian (neverball's coins came out cyan). */
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>
static const unsigned char cols[4][4] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {255, 255, 0, 255}};
static void quad(GLuint t, float x, float y)
{
	glBindTexture(GL_TEXTURE_2D, t);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(x, y); glTexCoord2f(1, 0); glVertex2f(x + 0.45f, y);
	glTexCoord2f(1, 1); glVertex2f(x + 0.45f, y + 0.9f); glTexCoord2f(0, 1); glVertex2f(x, y + 0.9f);
	glEnd();
}
int main(int argc, char **argv)
{
	int colormode = argc > 1;	/* glxtexcolor color: vertex colors instead of textures */
	Display *d = XOpenDisplay(NULL);
	int attr[] = {GLX_RGBA, GLX_DOUBLEBUFFER, GLX_RED_SIZE, 8, getenv("DEPTH") ? GLX_DEPTH_SIZE : None, 24, None};
	XVisualInfo *vi = glXChooseVisual(d, DefaultScreen(d), attr);
	printf("visual 0x%lx depth %d\n", vi->visualid, vi->depth);
	XSetWindowAttributes swa = {.colormap = XCreateColormap(d, RootWindow(d, vi->screen), vi->visual, AllocNone)};
	Window w = XCreateWindow(d, RootWindow(d, vi->screen), 0, 0, 400, 200, 0, vi->depth, InputOutput, vi->visual, CWColormap, &swa);
	XStoreName(d, w, "glxtexcolor");
	XMapWindow(d, w);
	GLXContext c = glXCreateContext(d, vi, NULL, True);
	glXMakeCurrent(d, w, c);
	GLuint t[8];
	glGenTextures(8, t);
	for (int i = 0; i < 4; i++) {
		unsigned char rgba[4 * 4], rgb[3 * 4];
		for (int p = 0; p < 4; p++) {
			for (int k = 0; k < 4; k++) rgba[p * 4 + k] = cols[i][k];
			for (int k = 0; k < 3; k++) rgb[p * 3 + k] = cols[i][k];
		}
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glBindTexture(GL_TEXTURE_2D, t[i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glBindTexture(GL_TEXTURE_2D, t[4 + i]);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	for (int f = 0; f < 200; f++) {
		if (getenv("CLEAR")) { glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glXSwapBuffers(d, w); usleep(50000); continue; }
		glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
		if (colormode) glDisable(GL_TEXTURE_2D);
		for (int i = 0; i < 4; i++) {
			if (getenv("FLOATCOL")) glColor3f(cols[i][0] / 255.0f, cols[i][1] / 255.0f, cols[i][2] / 255.0f); else glColor4ubv(cols[i]);
			quad(t[i], -1 + i * 0.5f, 0.05f); quad(t[4 + i], -1 + i * 0.5f, -0.95f);
		}
		glXSwapBuffers(d, w);
		usleep(50000);
	}
	return 0;
}
