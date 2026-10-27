/* gldepth [split [clearcolor [orphan [alpha0 [nofinish]]]]]: is depth kept across batches? 160x64 FBO, RGBA texture + DEPTH_COMPONENT16
 * renderbuffer. Clear depth 1.0, draw x < 0 (left half) at depth 0.25, then (with "split")
 * end the batch by binding FBO 0 and rebinding, then draw everywhere at depth 0.5 with LESS.
 * Expected: left half green, right half blue. Prints a row as a string of g/b/? per 4 px. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#ifndef W
#define W 160
#endif
#ifndef H
#define H 64
#endif
static void quad(float x0, float x1, float z)
{
	float v[] = { x0, -1, z, x1, -1, z, x0, 1, z, x1, 1, z };
	glVertexAttribPointer(0, 3, GL_FLOAT, 0, 0, v);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
int main(int argc, char **argv)
{
	int split = argc > 1;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) return 1;
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint tex, fbo, rb;
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, W, H);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { puts("incomplete"); return 1; }
	glViewport(0, 0, W, H);
	GLuint p = glCreateProgram();
	GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
	const char *vsrc = "attribute vec3 pos; void main(){ gl_Position = vec4(pos, 1.0); }";
	const char *fsrc = "precision mediump float; uniform vec4 c; void main(){ gl_FragColor = c; }";
	glShaderSource(vs, 1, &vsrc, NULL); glCompileShader(vs); glShaderSource(fs, 1, &fsrc, NULL); glCompileShader(fs);
	glAttachShader(p, vs); glAttachShader(p, fs); glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	GLint c = glGetUniformLocation(p, "c");
	glEnableVertexAttribArray(0);
	glClearColor(1, 0, 0, 1); glClearDepthf(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
	glUniform4f(c, 0, 1, 0, 1); quad(-1, 0, -0.5f);          /* depth 0.25 */
	if (split) { glBindFramebuffer(GL_FRAMEBUFFER, 0); if (argc < 6) glFinish();
		if (argc > 3) {   /* dEQP recreate_colorbuffer.no_rebind: orphan the attached texture */
			glDeleteTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		}
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		if (argc > 2) { glClearColor(1, 0, 0, argc > 4 ? 0 : 1); glClear(GL_COLOR_BUFFER_BIT); } }
	glUniform4f(c, 0, 0, 1, 1); quad(-1, 1, 0.0f);           /* depth 0.5 */
	static unsigned char px[W * H * 4];
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
	for (int y = 0; y < H; y += 16) {
		char row[W / 4 + 1];
		for (int x = 0; x < W; x += 4) {
			unsigned char *q = px + (y * W + x) * 4;
			row[x / 4] = q[1] > 200 && q[2] < 50 ? 'g' : q[2] > 200 && q[1] < 50 ? 'b' : q[0] > 200 ? 'r' : '?';
		}
		row[W / 4] = 0;
		printf("y%2d %s\n", y, row);
	}
	return 0;
}
