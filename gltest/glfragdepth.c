/* glfragdepth: a fragment shader writing gl_FragDepth = x / W (Xenos oDepth, export 61).
 * Clears depth to 1, draws with depth LESS, reads depth back, then draws a green quad at
 * z = 0.5 (window depth 0.75) with LESS: columns whose written depth is below 0.75 stay red. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
static GLuint prog(const char *vs, const char *fs)
{
	GLuint p = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
	glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
	glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
	return p;
}
int main(int argc, char **argv)
{
	int W = 64;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, c, d;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, W);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glGenRenderbuffers(1, &d); glBindRenderbuffer(GL_RENDERBUFFER, d);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, W, W);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, d);
	glViewport(0, 0, W, W);
	glClearColor(0, 0, 0, 1); glClearDepth(1); glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
	const char *vs = "void main(){gl_Position=gl_Vertex;}";
	GLuint p1 = prog(vs, "void main(){gl_FragColor=vec4(1,0,0,1);gl_FragDepth=gl_FragCoord.x/64.0;}");
	GLuint p2 = prog(vs, "void main(){gl_FragColor=vec4(0,1,0,1);}");
	glUseProgram(p1);
	if (!getenv("NOFIRST")) glBegin(GL_QUADS); else glBegin(GL_POINTS); glVertex3f(-1, -1, 0); glVertex3f(1, -1, 0); glVertex3f(1, 1, 0); glVertex3f(-1, 1, 0); glEnd();
	glFinish();
	printf("drawn, err %x\n", glGetError()); fflush(stdout);
	static float dv[64 * 64];
	glReadPixels(0, 0, W, W, GL_DEPTH_COMPONENT, GL_FLOAT, dv);
	printf("depth x:want/got");
	for (int x = 0; x < W; x += 8) printf(" %d:%.3f/%.3f", x, (x + 0.5f) / W, dv[10 * W + x]);
	putchar('\n');
	if (getenv("DRAWPIX")) {   /* glDrawPixels(depth 0.25) over the left half, depth test ALWAYS */
		static float z[32 * 64];
		int grad = getenv("GRAD") != NULL;   /* depth = x/64 + y/256 instead of 0.25 */
		for (int i = 0; i < 32 * 64; i++) z[i] = grad ? (i % 32) / 64.0f + (i / 32) / 256.0f : 0.25f;
		glUseProgram(0); glDepthFunc(GL_ALWAYS);
		glWindowPos2i(0, 0);
		glDrawPixels(32, getenv("DH") ? atoi(getenv("DH")) : 64, getenv("CDRAW") ? GL_LUMINANCE : GL_DEPTH_COMPONENT, GL_FLOAT, z);
		if (getenv("CDRAW")) {   /* color variant: print red at the same spots */
			static unsigned char c[64 * 64 * 4];
			glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, c);
			for (int y = 0; y < W; y += 12) {
				printf("c y%2d:", y);
				for (int x = 0; x < 40; x += 1) printf(" %d", c[(y * W + x) * 4]);
				putchar('\n');
			}
		}
		glDepthFunc(GL_LESS);
		glReadPixels(0, 0, W, W, GL_DEPTH_COMPONENT, GL_FLOAT, dv);
		printf("after drawpixels:");
		for (int x = 0; x < W; x += 8) printf(" %d:%.3f", x, dv[10 * W + x]);
		if (grad)
			for (int y = 0; y < W; y += 12) {
				printf("\ny%2d:", y);
				for (int x = 0; x < 32; x += 4) printf(" %.3f/%.3f", x / 64.0f + y / 256.0f, dv[y * W + x]);
			}
		printf(" (want 0.25 for x < 32), err %x\n", glGetError());
	}
	glUseProgram(p2);
	glBegin(GL_QUADS); glVertex3f(-1, -1, 0.5f); glVertex3f(1, -1, 0.5f); glVertex3f(1, 1, 0.5f); glVertex3f(-1, 1, 0.5f); glEnd();
	static unsigned char px[64 * 64 * 4];
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("color row (R red, G green, . other): ");
	for (int x = 0; x < W; x++) { unsigned char *q = &px[(10 * W + x) * 4]; putchar(q[0] > 200 && q[1] < 50 ? 'R' : q[1] > 200 && q[0] < 50 ? 'G' : '.'); }
	printf("\nwant R for x < 48, G after\n");
	return 0;
}
