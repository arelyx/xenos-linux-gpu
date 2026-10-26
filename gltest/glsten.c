/* glsten <w> <h> <step>: FBO with an RGBA texture and a GL_STENCIL_INDEX8 renderbuffer (dEQP
 * fbo.render.stencil.*_stencil_index8 wedges the GPU).  step 0: clear color only; 1: clear
 * color+stencil; 2: + draw a quad writing stencil; 3: + draw a quad tested against stencil. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o); return o;
}
int main(int argc, char **argv)
{
	int w = argc > 1 ? atoi(argv[1]) : 119, h = argc > 2 ? atoi(argv[2]) : 131, step = argc > 3 ? atoi(argv[3]) : 3;
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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_STENCIL_INDEX8, w, h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rb);
	printf("%dx%d step %d: fbo status %x\n", w, h, step, glCheckFramebufferStatus(GL_FRAMEBUFFER));
	glViewport(0, 0, w, h);
	glClearColor(0, 0, 1, 1); glClearStencil(0);
	glClear(GL_COLOR_BUFFER_BIT | (step >= 1 ? GL_STENCIL_BUFFER_BIT : 0));
	if (step >= 2) {
		GLuint p = glCreateProgram();
		glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }"));
		glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; uniform vec4 c; void main(){ gl_FragColor = c; }"));
		glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
		static const float q[] = { -1, -1, 0, -1, -1, 0, 0, 0 };   /* lower-left quarter */
		static const float f[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
		GLuint b[2]; glGenBuffers(2, b);
		glBindBuffer(GL_ARRAY_BUFFER, b[0]); glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
		glEnable(GL_STENCIL_TEST);
		glStencilFunc(GL_ALWAYS, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
		glUniform4f(glGetUniformLocation(p, "c"), 1, 0, 0, 1);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		if (step >= 3) {
			glBindBuffer(GL_ARRAY_BUFFER, b[1]); glBufferData(GL_ARRAY_BUFFER, sizeof f, f, GL_STATIC_DRAW);
			glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0);
			glStencilFunc(GL_EQUAL, 1, 0xff); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
			glUniform4f(glGetUniformLocation(p, "c"), 0, 1, 0, 1);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		}
	}
	unsigned char px[4], py[4];
	glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glReadPixels(w - 3, h - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, py);
	printf("  lower-left %d,%d,%d  upper-right %d,%d,%d  (want %s, blue)\n", px[0], px[1], px[2], py[0], py[1], py[2],
	       step >= 3 ? "green" : step == 2 ? "red" : "blue");
	return 0;
}
