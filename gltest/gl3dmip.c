/* gl3dmip [size]: a size^3 RGBA 3D texture whose mip level n is filled with
 * color n (red, green, blue, yellow, cyan, magenta), sampled with mipmapping
 * and BASE_LEVEL = MAX_LEVEL = n; prints the color fetched per level. Checks
 * where the GPU looks for 3D mip levels. */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
static const unsigned char cols[7][4] = {{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255},{0,255,255,255},{255,0,255,255},{255,255,255,255}};
int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 32;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, rb, t;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &rb); glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 8, 8);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	glViewport(0, 0, 8, 8);
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_3D, t);
	int levels = 0;
	for (int s = n, l = 0; s >= 1; s /= 2, l++) {
		unsigned char *img = malloc(s * s * s * 4);
		for (int i = 0; i < s * s * s; i++) for (int k = 0; k < 4; k++) img[i * 4 + k] = cols[l % 7][k];
		glTexImage3D(GL_TEXTURE_3D, l, GL_RGBA, s, s, s, 0, GL_RGBA, GL_UNSIGNED_BYTE, img);
		free(img);
		levels = l + 1;
	}
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glEnable(GL_TEXTURE_3D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	for (int l = 0; l < levels; l++) {
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_BASE_LEVEL, l);
		glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, l);
		glClearColor(0.5, 0.5, 0.5, 1); glClear(GL_COLOR_BUFFER_BIT);
		glBegin(GL_QUADS);
		glTexCoord3f(0, 0, 0.5); glVertex2f(-1, -1); glTexCoord3f(1, 0, 0.5); glVertex2f(1, -1);
		glTexCoord3f(1, 1, 0.5); glVertex2f(1, 1); glTexCoord3f(0, 1, 0.5); glVertex2f(-1, 1);
		glEnd();
		unsigned char px[4]; glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		printf("level %d: %3d,%3d,%3d  want %3d,%3d,%3d\n", l, px[0], px[1], px[2], cols[l % 7][0], cols[l % 7][1], cols[l % 7][2]);
	}
	return 0;
}
