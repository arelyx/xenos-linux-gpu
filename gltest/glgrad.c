/* glgrad [mode]: one triangle with red/green/blue vertices on a 64x64 FBO, prints the pixels
 * near each corner and at the centroid.  mode 0: separate float VBOs (vec2 pos, vec4 color);
 * 1: interleaved; 2: color as normalized unsigned bytes (stride 4); 3: color from a uniform
 * varying computed from position (tests interpolation without color attributes). */
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
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o);
	GLint ok; glGetShaderiv(o, GL_COMPILE_STATUS, &ok);
	if (!ok) { char l[512]; glGetShaderInfoLog(o, 512, NULL, l); printf("shader: %s\n", l); }
	return o;
}
int main(int argc, char **argv)
{
	int mode = argc > 1 ? atoi(argv[1]) : 0;
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
	GLuint p = glCreateProgram();
	if (mode == 3) {
		glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; varying vec4 c; void main(){ c = vec4(pos * 0.5 + 0.5, 0.0, 1.0); gl_Position = vec4(pos, 0.0, 1.0); }"));
	} else {
		glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; attribute vec4 col; varying vec4 c; void main(){ c = col; gl_Position = vec4(pos, 0.0, 1.0); }"));
	}
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; varying vec4 c; void main(){ gl_FragColor = c; }"));
	glBindAttribLocation(p, 0, "pos"); glBindAttribLocation(p, 1, "col");
	glLinkProgram(p); glUseProgram(p);
	static const float pos[] = { -1, -1, 1, -1, -1, 1 };
	static const float col[] = { 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1 };
	static const float il[] = { -1, -1, 1, 0, 0, 1, 1, -1, 0, 1, 0, 1, -1, 1, 0, 0, 1, 1 };
	static const unsigned char colb[] = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255 };
	GLuint b[2]; glGenBuffers(2, b);
	if (mode == 1) {
		glBindBuffer(GL_ARRAY_BUFFER, b[0]); glBufferData(GL_ARRAY_BUFFER, sizeof il, il, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, 0, 24, (void *)0);
		glVertexAttribPointer(1, 4, GL_FLOAT, 0, 24, (void *)8);
	} else {
		glBindBuffer(GL_ARRAY_BUFFER, b[0]); glBufferData(GL_ARRAY_BUFFER, sizeof pos, pos, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
		glBindBuffer(GL_ARRAY_BUFFER, b[1]);
		if (mode == 2) {
			glBufferData(GL_ARRAY_BUFFER, sizeof colb, colb, GL_STATIC_DRAW);
			glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, 1, 0, 0);
		} else {
			glBufferData(GL_ARRAY_BUFFER, sizeof col, col, GL_STATIC_DRAW);
			glVertexAttribPointer(1, 4, GL_FLOAT, 0, 0, 0);
		}
	}
	glEnableVertexAttribArray(0);
	if (mode != 3) glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	static unsigned char px[W * W * 4];
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int pts[4][2] = { { 2, 2 }, { 58, 2 }, { 2, 58 }, { 21, 21 } };
	const char *nm[4] = { "near v0 (red)", "near v1 (green)", "near v2 (blue)", "centroid" };
	printf("mode %d:", mode);
	for (int i = 0; i < 4; i++) {
		unsigned char *q = px + 4 * (pts[i][1] * W + pts[i][0]);
		printf("  %s %3d %3d %3d %3d;", nm[i], q[0], q[1], q[2], q[3]);
	}
	printf("\n");
	return 0;
}
