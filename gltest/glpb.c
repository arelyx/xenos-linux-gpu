/* glpb [fbo]: like glsub but on an EGL pbuffer (A8R8G8B8, y-inverted window-system buffer, as dEQP
 * uses) unless "fbo" is given.  Clears black, viewport (0,0) 200x200, draws a small green
 * triangle in the viewport's lower-left quarter, reads back the viewport. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define W 256
static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o); return o;
}
static unsigned char px[200 * 200 * 4];
int main(int argc, char **argv)
{
	int fbo = argc > 1 && !strcmp(argv[1], "fbo");
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLint maj, min, n;
	if (!eglInitialize(dpy, &maj, &min)) { printf("init failed\n"); return 1; }
	static const EGLint ca[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE };
	EGLConfig cfg;
	eglChooseConfig(dpy, ca, &cfg, 1, &n);
	static const EGLint pa[] = { EGL_WIDTH, W, EGL_HEIGHT, W, EGL_NONE };
	EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pa);
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, cattr);
	if (!eglMakeCurrent(dpy, surf, surf, ctx)) { printf("makecurrent failed\n"); return 1; }
	if (fbo) {
		GLuint tex, f;
		glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, W, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glGenFramebuffers(1, &f); glBindFramebuffer(GL_FRAMEBUFFER, f);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	}
	glViewport(0, 0, W, W);
	float cr = 0, cg = 0, cb = 0, cl_a = 1;
	if (getenv("GLPB_CLEAR")) sscanf(getenv("GLPB_CLEAR"), "%f,%f,%f,%f", &cr, &cg, &cb, &cl_a);
	glClearColor(cr, cg, cb, cl_a); glClear(GL_COLOR_BUFFER_BIT);
	if (getenv("GLPB_CLEARONLY")) {
		glReadPixels(0, 0, 200, 200, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf("clear only: (5,5)=%d,%d,%d,%d\n", px[20], px[21], px[22], px[23]);
		return 0;
	}
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; void main(){ gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }"));
	glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	static const float t[] = { -1, -1, 0, -1, -1, 0 };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof t, t, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	glViewport(0, 0, 200, 200);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(0, 0, 200, 200, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int pts[][2] = { { 5, 5 }, { 60, 20 }, { 150, 150 }, { 20, 150 }, { 150, 20 }, { 199, 199 } };
	printf("%s:", fbo ? "fbo" : "pbuffer");
	for (int i = 0; i < 6; i++) {
		unsigned char *q = px + 4 * (pts[i][1] * 200 + pts[i][0]);
		printf(" (%d,%d)=%d,%d,%d,%d", pts[i][0], pts[i][1], q[0], q[1], q[2], q[3]);
	}
	printf("   [want green at (5,5),(60,20); black elsewhere]\n");
	return 0;
}
