/* glborder: a red 4x4 texture, GL_CLAMP_TO_BORDER, sampled outside its edge
 * (texcoord -1) and inside with each border color; NEAREST and LINEAR. */
#include <stdio.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
int main(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint rt, fbo;
	glGenTextures(1, &rt); glBindTexture(GL_TEXTURE_2D, rt);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
	glViewport(0, 0, 64, 64);
	unsigned char red[4 * 4 * 4];
	for (int i = 0; i < 16; i++) { red[i*4] = 255; red[i*4+1] = 0; red[i*4+2] = 0; red[i*4+3] = 255; }
	GLuint t;
	glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
	glEnable(GL_TEXTURE_2D);
	glDisable(GL_BLEND);
	const float colors[][4] = {{0, 0, 0, 0}, {0, 0, 0, 1}, {1, 1, 1, 1}, {0, 1, 0, 1}, {0.25, 0.5, 0.75, 0.5}};
	/* f: 0 nearest, 1 linear, 2 min nearest mag linear, 3 min linear mag nearest,
	 * 4 nearest with GL_CLAMP, 5 nearest with mirror-clamp-to-border */
	for (int f = 0; f < 6; f++) {
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (f == 1 || f == 3) ? GL_LINEAR : GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (f == 1 || f == 2) ? GL_LINEAR : GL_NEAREST);
		GLenum wrap = f == 4 ? GL_CLAMP : f == 5 ? GL_MIRROR_CLAMP_TO_BORDER_EXT : GL_CLAMP_TO_BORDER;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
		for (int c = 2; c < 4; c++) {
			glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, colors[c]);
			glBegin(GL_QUADS);   /* texcoords -1..2: the middle third is the texture */
			glTexCoord2f(-1, -1); glVertex2f(-1, -1); glTexCoord2f(2, -1); glVertex2f(1, -1);
			glTexCoord2f(2, 2); glVertex2f(1, 1); glTexCoord2f(-1, 2); glVertex2f(-1, 1);
			glEnd();
			unsigned char o[4], in[4];
			glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, o);
			glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, in);
			printf("%s border (%.2f,%.2f,%.2f,%.2f): outside %d,%d,%d,%d inside %d,%d,%d,%d\n",
			       (const char *[]){"nearest", "linear", "min N mag L", "min L mag N", "N GL_CLAMP", "N mirror-border"}[f], colors[c][0], colors[c][1], colors[c][2], colors[c][3],
			       o[0], o[1], o[2], o[3], in[0], in[1], in[2], in[3]);
		}
	}
	return 0;
}
