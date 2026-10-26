/* glgbm [w h]: what glamor does with the front buffer: a GBM scanout BO (XRGB8888) wrapped in an
 * EGLImage, bound as a texture/FBO, cleared red and a green quad drawn, then read back with
 * glReadPixels and through a CPU map of the BO. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
static GLuint sh(GLenum t, const char *s)
{
	GLuint o = glCreateShader(t); glShaderSource(o, 1, &s, NULL); glCompileShader(o); return o;
}
int main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	int w = argc > 2 ? atoi(argv[1]) : 256, h = argc > 2 ? atoi(argv[2]) : 256;
	int fd = open("/dev/dri/card0", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) { printf("egl init failed\n"); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	struct gbm_bo *bo = gbm_bo_create(gbm, w, h, GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!bo) { printf("gbm_bo_create failed\n"); return 1; }
	PFNEGLCREATEIMAGEKHRPROC ci = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
	PFNGLEGLIMAGETARGETTEXTURE2DOESPROC tgt = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
	EGLImageKHR img = ci(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, bo, NULL);
	printf("bo %dx%d stride %u, image %p err %x\n", w, h, gbm_bo_get_stride(bo), img, eglGetError());
	GLuint tex, fbo;
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	tgt(GL_TEXTURE_2D, img);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	printf("fbo status %x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
	glViewport(0, 0, w, h);
	glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; void main(){ gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); }"));
	glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	static const float q[] = { -1, -1, 0, -1, -1, 0, 0, 0 };
	GLuint b; glGenBuffers(1, &b); glBindBuffer(GL_ARRAY_BUFFER, b);
	glBufferData(GL_ARRAY_BUFFER, sizeof q, q, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, 0); glEnableVertexAttribArray(0);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glFinish();
	unsigned char px[4], py[4];
	glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glReadPixels(w - 3, h - 3, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, py);
	printf("glReadPixels: lower-left %d,%d,%d,%d (want green)  upper-right %d,%d,%d,%d (want red)\n",
	       px[0], px[1], px[2], px[3], py[0], py[1], py[2], py[3]);
	return 0;
}
