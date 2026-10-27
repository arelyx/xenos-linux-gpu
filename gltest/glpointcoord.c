/* glpointcoord: one 16-pixel point in a 32x32 FBO with gl_FragColor = gl_PointCoord (GLSL,
 * size from glPointSize); prints r,g at the point's four corners and centre. GL wants
 * (0,1) bottom-left and (1,0) top-right with the default upper-left origin. */
#include <stdio.h>
#include <stdlib.h>
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
	GLuint fbo, c;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 32, 32);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glViewport(0, 0, 32, 32);
	glClearColor(0, 0, 1, 1); glClear(GL_COLOR_BUFFER_BIT);
	const char *vs = "void main(){gl_Position=gl_Vertex;}";
	const char *fs = "#version 120\nvoid main(){gl_FragColor=vec4(gl_PointCoord,0.0,1.0);}";
	GLuint p = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
	glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
	glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p); glUseProgram(p);
	glEnable(GL_POINT_SPRITE);
	glPointSize(16);
	glBegin(GL_POINTS); glVertex2f(0, 0); glEnd();
	unsigned char px[32 * 32 * 4];
	glReadPixels(0, 0, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int pts[5][2] = {{8, 8}, {23, 8}, {8, 23}, {23, 23}, {16, 16}};
	const char *names[5] = {"bottom-left", "bottom-right", "top-left", "top-right", "centre"};
	for (int i = 0; i < 5; i++) {
		unsigned char *q = px + (pts[i][1] * 32 + pts[i][0]) * 4;
		printf("%-12s r %3d g %3d b %3d\n", names[i], q[0], q[1], q[2]);
	}
	printf("lit (not blue) pixels: ");
	int n = 0;
	for (int i = 0; i < 32 * 32; i++) n += px[i * 4 + 2] < 128;
	printf("%d (256 for 16x16)\n", n);
	return 0;
}
