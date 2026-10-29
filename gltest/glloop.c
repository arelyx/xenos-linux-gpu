/* glloop <iterations>: one 256x256 quad whose fragment shader loops a uniform
 * number of times. A huge count keeps the GPU busy far past the fence
 * timeout: a runaway-shader wedge for testing GPU reset. Prints the pixel. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 10;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) { printf("eglInitialize failed\n"); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	const EGLint ca[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint tex, fbo;
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glViewport(0, 0, 256, 256);
	const char *vs = "attribute vec4 p; void main() { gl_Position = p; }";
	const char *fs = "precision highp float; uniform int n; void main() { float s = 0.0;"
	                 " for (int i = 0; i < 1000000; i++) { if (i >= n) break; s = fract(s * 1.37 + 0.11); }"
	                 " gl_FragColor = vec4(s, 0.5, 1.0, 1.0); }";
	GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
	GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
	GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
	glBindAttribLocation(p, 0, "p"); glLinkProgram(p); glUseProgram(p);
	glUniform1i(glGetUniformLocation(p, "n"), n);
	const float q[] = {-1, -1, 1, -1, -1, 1, 1, 1};
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[4];
	glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("n %d: pixel %d,%d,%d,%d\n", n, px[0], px[1], px[2], px[3]);
	return 0;
}
