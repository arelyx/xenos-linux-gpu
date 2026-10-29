/* glfsidx: a fragment shader reads u[i] for a uniform index i over eight
 * distinct vec4 uniforms (red = i/8). Shows which constant an indexed
 * pixel-shader read returns (a0-relative addressing on Xenos). */
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
	/* argv[1] "v": the index arrives as a varying (MOVA reads a register) */
	int vary = argc > 1 && argv[1][0] == 'v';
	const char *vs = vary ? "attribute vec4 p; uniform float fi; varying float f;"
	                        " void main() { gl_Position = p; f = fi + 0.25; }"
	                      : "attribute vec4 p; void main() { gl_Position = p; }";
	const char *fs = vary ? "precision highp float; uniform vec4 u[8]; varying float f;"
	                        " void main() { gl_FragColor = u[int(f)] + vec4(0.25); }"
	                      : "precision highp float; uniform vec4 u[8]; uniform int i;"
	                        " void main() { gl_FragColor = u[i]; }";
	GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
	GLuint f = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
	GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f);
	glBindAttribLocation(p, 0, "p"); glLinkProgram(p); glUseProgram(p);
	float u[32];
	for (int k = 0; k < 8; k++) {
		u[k * 4] = k / 8.0f; u[k * 4 + 1] = 0.5f; u[k * 4 + 2] = 1.0f; u[k * 4 + 3] = 1.0f;
	}
	glUniform4fv(glGetUniformLocation(p, "u"), 8, u);
	const float q[] = {-1, -1, 1, -1, -1, 1, 1, 1};
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, q); glEnableVertexAttribArray(0);
	for (int k = 0; k < 8; k++) {
		if (vary)
			glUniform1f(glGetUniformLocation(p, "fi"), k);
		else
			glUniform1i(glGetUniformLocation(p, "i"), k);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		unsigned char px[4];
		glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf("i %d: pixel %d,%d,%d,%d (want %d,128,255,255)\n", k, px[0], px[1], px[2], px[3], k * 32);
	}
	return 0;
}
