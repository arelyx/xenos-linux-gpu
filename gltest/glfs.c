/* glfs <n>: run fragment shader n of a list of control-flow probes on a 64x64 FBO and print 3
 * pixels.  Probes target the dEQP classes that wedge the GPU (fragment dynamic indexing). */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define W 64
static const char *fs[] = {
	/* 0: control, no flow */
	"precision mediump float; varying vec2 v; void main(){ gl_FragColor = vec4(v, 0.0, 1.0); }",
	/* 1: uniform if */
	"precision mediump float; varying vec2 v; uniform float u; void main(){ vec4 c = vec4(0.0); if (u > 0.5) c = vec4(1.0, 0.0, 0.0, 1.0); else c = vec4(0.0, 1.0, 0.0, 1.0); gl_FragColor = c; }",
	/* 2: divergent if on a varying */
	"precision mediump float; varying vec2 v; void main(){ vec4 c; if (v.x > 0.5) c = vec4(1.0, 0.0, 0.0, 1.0); else c = vec4(0.0, 1.0, 0.0, 1.0); gl_FragColor = c; }",
	/* 3: uniform-count loop */
	"precision mediump float; varying vec2 v; uniform float u; void main(){ float s = 0.0; for (int i = 0; i < 4; i++) s += u * 0.25; gl_FragColor = vec4(s, 0.0, 0.0, 1.0); }",
	/* 4: dynamic loop with uniform bound */
	"precision mediump float; varying vec2 v; uniform int n; void main(){ float s = 0.0; for (int i = 0; i < 8; i++) { if (i >= n) break; s += 0.25; } gl_FragColor = vec4(s, 0.0, 0.0, 1.0); }",
	/* 5: dynamic index of a local array with a uniform index */
	"precision mediump float; varying vec2 v; uniform int n; void main(){ float a[4]; a[0] = 0.1; a[1] = 0.4; a[2] = 0.7; a[3] = 1.0; gl_FragColor = vec4(a[n], 0.0, 0.0, 1.0); }",
	/* 6: dynamic index of a local array with a per-pixel index */
	"precision mediump float; varying vec2 v; void main(){ float a[4]; a[0] = 0.1; a[1] = 0.4; a[2] = 0.7; a[3] = 1.0; int k = int(v.x * 3.99); gl_FragColor = vec4(a[k], 0.0, 0.0, 1.0); }",
	/* 7: dynamic write then read in a uniform loop */
	"precision mediump float; varying vec2 v; uniform int n; void main(){ float a[4]; for (int i = 0; i < 4; i++) a[i] = float(i) * 0.25; float s = 0.0; for (int i = 0; i < 4; i++) { if (i >= n) break; s += a[i]; } gl_FragColor = vec4(s, 0.0, 0.0, 1.0); }",
};
static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
	GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
	if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader: %s\n", l); }
	return o;
}
int main(int argc, char **argv)
{
	int k = argc > 1 ? atoi(argv[1]) : 0;
	if (k < 0 || k >= (int)(sizeof fs / sizeof fs[0])) return 2;
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
	glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; varying vec2 v; void main(){ v = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, fs[k]));
	glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	glUniform1f(glGetUniformLocation(p, "u"), 1.0f);
	glUniform1i(glGetUniformLocation(p, "n"), 2);
	static const float q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[W * W * 4];
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("fs %d: (8,8)=%d,%d,%d (32,32)=%d,%d,%d (56,56)=%d,%d,%d\n", k,
	       px[4 * (8 * W + 8)], px[4 * (8 * W + 8) + 1], px[4 * (8 * W + 8) + 2],
	       px[4 * (32 * W + 32)], px[4 * (32 * W + 32) + 1], px[4 * (32 * W + 32) + 2],
	       px[4 * (56 * W + 56)], px[4 * (56 * W + 56) + 1], px[4 * (56 * W + 56) + 2]);
	return 0;
}
