/* gltexsub <fmt>: 64x64 texture, full glTexImage2D of grey, glTexSubImage2D of a white 32x32 block at
 * (16,16), drawn 1:1 to a 64x64 FBO.  fmt: rgba | la | l | a | rgb565 | rgba4444 */
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
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o); return o;
}
int main(int argc, char **argv)
{
	const char *f = argc > 1 ? argv[1] : "rgba";
	int tw = argc > 3 ? atoi(argv[2]) : W, th = argc > 3 ? atoi(argv[3]) : W;
	GLenum fmt = GL_RGBA, type = GL_UNSIGNED_BYTE; int bpp = 4;
	if (!strcmp(f, "la")) { fmt = GL_LUMINANCE_ALPHA; bpp = 2; }
	else if (!strcmp(f, "l")) { fmt = GL_LUMINANCE; bpp = 1; }
	else if (!strcmp(f, "a")) { fmt = GL_ALPHA; bpp = 1; }
	else if (!strcmp(f, "rgb565")) { fmt = GL_RGB; type = GL_UNSIGNED_SHORT_5_6_5; bpp = 2; }
	else if (!strcmp(f, "rgba4444")) { fmt = GL_RGBA; type = GL_UNSIGNED_SHORT_4_4_4_4; bpp = 2; }
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) return 1;
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint rt, fbo, tex;
	glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, W, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
	glViewport(0, 0, W, W);
	static unsigned char grey[W * W * 4], white[32 * 32 * 4];
	memset(grey, 0x80, sizeof grey); memset(white, 0xff, sizeof white);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, fmt, tw, th, 0, fmt, type, grey);
	if (tw >= 48 && th >= 48) glTexSubImage2D(GL_TEXTURE_2D, 0, 16, 16, 32, 32, fmt, type, white);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	(void)bpp;
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; varying vec2 t; void main(){ t = pos * 0.5 + 0.5; gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; varying vec2 t; uniform sampler2D s; void main(){ gl_FragColor = texture2D(s, t); }"));
	glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	static const float q[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	unsigned char px[W * W * 4];
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, px);
#define P(x, y) px[4 * ((y) * W + (x))], px[4 * ((y) * W + (x)) + 1], px[4 * ((y) * W + (x)) + 2], px[4 * ((y) * W + (x)) + 3]
	printf("%s %dx%d: grey(4,4)=%d,%d,%d,%d  white(20,20)=%d,%d,%d,%d  white(40,40)=%d,%d,%d,%d  grey(60,60)=%d,%d,%d,%d\n",
	       f, tw, th, P(4, 4), P(20, 20), P(40, 40), P(60, 60));
	return 0;
}
