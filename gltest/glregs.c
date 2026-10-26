/* glregs <n>: fragment shader that keeps n vec4 temporaries live (forces about n GPRs), 64x64 FBO.
 * Finds the pixel-shader register count at which Xenos wedges. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define W 64
static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
	GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
	if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader: %s\n", l); }
	return o;
}
int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 8;
	static char fs[65536];
	char *p = fs;
	p += sprintf(p, "precision highp float; varying vec2 v; uniform vec4 u[%d]; void main(){\n", n);
	for (int i = 0; i < n; i++) p += sprintf(p, " vec4 t%d = sin(u[%d] * v.x + v.y * %d.0);\n", i, i, i + 1);
	p += sprintf(p, " vec4 s = vec4(0.0);\n");
	for (int i = n - 1; i >= 0; i--) p += sprintf(p, " s = s * 0.5 + t%d;\n", i);
	p += sprintf(p, " gl_FragColor = vec4(fract(s.xyz), 1.0); }\n");
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
	GLuint pr = glCreateProgram();
	glAttachShader(pr, sh(GL_VERTEX_SHADER, "attribute vec2 pos; varying vec2 v; void main(){ v = pos; gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(pr, sh(GL_FRAGMENT_SHADER, fs));
	glBindAttribLocation(pr, 0, "pos"); glLinkProgram(pr); glUseProgram(pr);
	float u[4 * 64];
	for (int i = 0; i < 4 * 64; i++) u[i] = 0.1f * (i % 13);
	glUniform4fv(glGetUniformLocation(pr, "u"), n, u);
	static const float q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[4];
	glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("n %d: pixel %d,%d,%d,%d\n", n, px[0], px[1], px[2], px[3]);
	return 0;
}
