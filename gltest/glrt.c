/* glrt <w> <n>: per-batch round trip. n times: scissored 8x8 clear (or a 3-vertex draw with
 * arg 3 = "draw") of a w x w FBO, then glFinish. Prints microseconds per iteration. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
int main(int argc, char **argv)
{
	int w = argc > 1 ? atoi(argv[1]) : 64, n = argc > 2 ? atoi(argv[2]) : 500;
	int draw = argc > 3 && !strcmp(argv[3], "draw"), nofinish = argc > 4;
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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glViewport(0, 0, w, w);
	GLuint p = glCreateProgram();
	GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
	const char *vsrc = "attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }";
	const char *fsrc = "precision mediump float; void main(){ gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0); }";
	glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs); glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
	glAttachShader(p, vs); glAttachShader(p, fs); glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	static const float t[] = { -1, -1, -0.9f, -1, -1, -0.9f };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof t, t, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	glClear(GL_COLOR_BUFFER_BIT); glFinish();
	glEnable(GL_SCISSOR_TEST); glScissor(0, 0, 8, 8);
	double t0 = now();
	for (int i = 0; i < n; i++) {
		if (draw) glDrawArrays(GL_TRIANGLES, 0, 3);
		else { glClearColor(i & 1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); }
		if (nofinish) glFlush(); else glFinish();
	}
	glFinish();
	double dt = now() - t0;
	printf("%dx%d %s %s: %.1f us per batch\n", w, w, draw ? "draw" : "scissored clear", nofinish ? "glFlush" : "glFinish", dt / n * 1e6);
	return 0;
}
