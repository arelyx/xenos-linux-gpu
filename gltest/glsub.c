/* glsub: viewport/scissor/readback offsets on a 256x256 FBO.
 * Clears black, scissor-clears red at (40,72) 32x16, draws a green quad through a viewport at
 * (128,128) 64x64, then reads pixels with full-surface and sub-rectangle glReadPixels. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define W 256
static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o); return o;
}
static unsigned char full[W * W * 4], sub[64 * 64 * 4];
static void pf(const char *what, unsigned char *b, int stride, int x, int y)
{
	unsigned char *q = b + 4 * (y * stride + x);
	printf("  %-34s (%3d,%3d) = %3d %3d %3d %3d\n", what, x, y, q[0], q[1], q[2], q[3]);
}
int main(int argc, char **argv)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) return 1;
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint tex, fbo;
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, W, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glViewport(0, 0, W, W);
	glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
	glEnable(GL_SCISSOR_TEST); glScissor(40, 72, 32, 16);
	glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
	glDisable(GL_SCISSOR_TEST);
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; void main(){ gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }"));
	glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	static const float q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	glViewport(128, 128, 64, 64);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, full);
	printf("full readback:\n");
	pf("black (outside)", full, W, 10, 10);
	pf("red scissor clear", full, W, 50, 80);
	pf("black right of red", full, W, 80, 80);
	pf("green viewport quad", full, W, 150, 150);
	pf("black left of quad", full, W, 120, 150);
	pf("black above quad", full, W, 150, 200);
	glReadPixels(128, 128, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, sub);
	printf("sub readback at (128,128) 64x64:\n");
	pf("green (0,0)", sub, 64, 0, 0);
	pf("green (63,63)", sub, 64, 63, 63);
	glReadPixels(40, 72, 32, 16, GL_RGBA, GL_UNSIGNED_BYTE, sub);
	printf("sub readback at (40,72) 32x16:\n");
	pf("red (0,0)", sub, 32, 0, 0);
	pf("red (31,15)", sub, 32, 31, 15);
	return 0;
}
