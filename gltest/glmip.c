/* glmip <mode> <level>: 64x64 RGBA texture with a full mip chain.
 * mode 0: FBO on mip <level>, clear it red, read back;  mode 1: glGenerateMipmap;
 * mode 2: FBO on a separate texture of size (64>>level) square, clear, read back. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
int main(int argc, char **argv)
{
	int mode = argc > 1 ? atoi(argv[1]) : 0, lvl = argc > 2 ? atoi(argv[2]) : 0;
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
	static unsigned char grey[64 * 64 * 4];
	for (int i = 0; i < (int)sizeof grey; i++) grey[i] = 0x80;
	if (mode == 2) {
		int s = 64 >> lvl;
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey);
		lvl = 0;
	} else {
		for (int l = 0; l <= 6; l++)
			glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA, 64 >> l, 64 >> l, 0, GL_RGBA, GL_UNSIGNED_BYTE, grey);
	}
	if (mode == 1) {
		glGenerateMipmap(GL_TEXTURE_2D);
		glFinish();
		printf("mode 1 generate: done\n");
		return 0;
	}
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, lvl);
	int s = mode == 2 ? 64 >> atoi(argv[2]) : 64 >> lvl;
	printf("mode %d level %d size %d: status %x", mode, lvl, s, glCheckFramebufferStatus(GL_FRAMEBUFFER));
	glViewport(0, 0, s, s);
	glClearColor(1, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
	unsigned char px[4];
	glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("  pixel %d,%d,%d,%d (want 255,0,0,255)\n", px[0], px[1], px[2], px[3]);
	return 0;
}
