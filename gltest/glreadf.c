/* glreadf [w h]: clear a w x h RGBA8 renderbuffer to 0.5 gray, read it back
 * as GL_RGB/GL_FLOAT and GL_RGBA/GL_UNSIGNED_BYTE, print the first wrong
 * pixel of each (piglit fbo-blit-stretch reads 200x150 as float). */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
int main(int argc, char **argv)
{
	int w = argc > 2 ? atoi(argv[1]) : 200, h = argc > 2 ? atoi(argv[2]) : 150;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, c;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	if (argc > 4) {	/* texture color attachment, like piglit -fbo */
		glGenTextures(1, &c); glBindTexture(GL_TEXTURE_2D, c);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, c, 0);
	} else {
		glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, w, h);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	}
	glViewport(0, 0, w, h);
	GLuint sfbo, sc;
	if (argc > 3) {	/* also blit a 17x11 blue region from a 45x79 fbo, like fbo-blit-stretch */
		glGenFramebuffers(1, &sfbo); glBindFramebuffer(GL_FRAMEBUFFER, sfbo);
		glGenRenderbuffers(1, &sc); glBindRenderbuffer(GL_RENDERBUFFER, sc);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA, 45, 79);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, sc);
		static unsigned char img[79][45][4];
		for (int y = 0; y < 79; y++) for (int x = 0; x < 45; x++) { img[y][x][2] = 255; img[y][x][3] = 255; }
		glWindowPos2i(0, 0);
		glDrawPixels(45, 79, GL_RGBA, GL_UNSIGNED_BYTE, img);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, sfbo);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo);
	}
	glClearColor(0.5, 0.5, 0.5, 0.5); glClear(GL_COLOR_BUFFER_BIT);
	if (argc > 3) {
		glBlitFramebuffer(13, 33, 30, 44, 19, 23, 36, 34, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
	}
	float *f = malloc(w * h * 3 * sizeof(float));
	unsigned char *b = malloc(w * h * 4);
	glReadPixels(0, 0, w, h, GL_RGB, GL_FLOAT, f);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, b);
	int bf = 0, bb = 0;
	for (int i = 0; i < w * h; i++) {
		if (argc > 3 && i % w >= 19 && i % w < 36 && i / w >= 23 && i / w < 34) continue;
		if (f[i * 3] < 0.49 || f[i * 3] > 0.51 || f[i * 3 + 2] < 0.49 || f[i * 3 + 2] > 0.51) {
			if (!bf) printf("float first bad (%d,%d): %.3f %.3f %.3f\n", i % w, i / w, f[i * 3], f[i * 3 + 1], f[i * 3 + 2]);
			bf++;
		}
		if (b[i * 4] < 126 || b[i * 4] > 129 || b[i * 4 + 2] < 126 || b[i * 4 + 2] > 129) {
			if (!bb) printf("ubyte first bad (%d,%d): %d %d %d\n", i % w, i / w, b[i * 4], b[i * 4 + 1], b[i * 4 + 2]);
			bb++;
		}
	}
	if (argc > 3) printf("blit pixel (20,24): %d %d %d\n", b[(24 * w + 20) * 4], b[(24 * w + 20) * 4 + 1], b[(24 * w + 20) * 4 + 2]);
	printf("%dx%d bad: float %d, ubyte %d\n", w, h, bf, bb);
	return 0;
}
