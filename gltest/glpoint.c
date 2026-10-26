/* glpoint <mode>: GL_POINTS on a 64x64 FBO.  mode 0: VS writes gl_Position then gl_PointSize = 8;
 * 1: gl_PointSize first; 2: no gl_PointSize write; 3: GL_LINES (control).  Prints the pixel at the
 * point centre and 3 px away (inside an 8 px point, outside a 1 px one). */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define W 64
static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o); return o;
}
int main(int argc, char **argv)
{
	int mode = argc > 1 ? atoi(argv[1]) : 0;
	const char *size = argc > 2 ? argv[2] : "8.0";
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
	char v0[256], v1[256];
	snprintf(v0, sizeof v0, "attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); gl_PointSize = %s; }", size);
	snprintf(v1, sizeof v1, "attribute vec2 pos; void main(){ gl_PointSize = %s; gl_Position = vec4(pos, 0.0, 1.0); }", size);
	const char *vs[] = {
		v0, v1,
		"attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }",
		"attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }",
	};
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, vs[mode & 3]));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; void main(){ gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0); }"));
	glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	/* centre of pixel (32,32) and a horizontal line through it */
	static const float pt[] = { 1.0f / 64, 1.0f / 64, -0.5f, 1.0f / 64, 0.5f, 1.0f / 64 };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof pt, pt, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	if (mode == 3)
		glDrawArrays(GL_LINES, 1, 2);
	else
		glDrawArrays(GL_POINTS, 0, 1);
	unsigned char px[W * W * 4];
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, px);
	unsigned char *c = px + 4 * (32 * W + 32), *o = px + 4 * (32 * W + 35), *u = px + 4 * (35 * W + 32);
	printf("mode %d size %s: centre %d,%d,%d  +3x %d,%d,%d  +3y %d,%d,%d\n", mode, size, c[0], c[1], c[2], o[0], o[1], o[2], u[0], u[1], u[2]);
	return 0;
}
