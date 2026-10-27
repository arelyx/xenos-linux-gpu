/* gl3d: upload a W x H x D RGBA8 3D texture whose texel is (x*16, y*16, z*16, 255), sample it
 * with nearest filtering at texel centres, one output row per slice, and print what came back.
 * W, H, D from the environment (default 8x8x4). */
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
int main(void)
{
	int W = getenv("W") ? atoi(getenv("W")) : 8, H = getenv("H") ? atoi(getenv("H")) : 8, D = getenv("D") ? atoi(getenv("D")) : 4;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	unsigned char *t = malloc(W * H * D * 4);
	for (int z = 0; z < D; z++) for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
		unsigned char *p = t + ((z * H + y) * W + x) * 4;
		p[0] = x * 16; p[1] = y * 16; p[2] = z * 16; p[3] = 255;
	}
	GLuint tex, fbo, c;
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_3D, tex);
	glTexImage3D(GL_TEXTURE_3D, 0, getenv("IFMT") ? (int)strtol(getenv("IFMT"), 0, 0) : GL_RGBA8, W, H, D, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);   /* IFMT: internal format */
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, D);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glViewport(0, 0, W, D);
	const char *vs = "void main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}";
	/* SWZ=1: build the coordinate in the shader (exercises ir2's vec3 construction);
	 * default: the texcoord's xyz straight from the vertex */
	const char *fs = getenv("SWZ") ? "uniform sampler3D s;uniform float ty;void main(){gl_FragColor=texture3D(s,vec3(gl_TexCoord[0].x,ty,gl_TexCoord[0].y));}"
	                               : "uniform sampler3D s;void main(){gl_FragColor=texture3D(s,gl_TexCoord[0].xyz);}";
	GLuint p = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
	glShaderSource(f, 1, &fs, NULL); glCompileShader(f);
	glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p); glUseProgram(p);
	int ys[2] = {0, H - 1};
	unsigned char *px = malloc(W * D * 4);
	for (int k = 0; k < 2; k++) {
		glUniform1f(glGetUniformLocation(p, "ty"), (ys[k] + 0.5f) / H);
		glBegin(GL_QUADS);
		float ty = (ys[k] + 0.5f) / H;
		glTexCoord3f(0, getenv("SWZ") ? 0 : ty, 0); glVertex2f(-1, -1); glTexCoord3f(1, getenv("SWZ") ? 0 : ty, 0); glVertex2f(1, -1);
		glTexCoord3f(1, getenv("SWZ") ? 1 : ty, 1); glVertex2f(1, 1); glTexCoord3f(0, getenv("SWZ") ? 1 : ty, 1); glVertex2f(-1, 1);
		glEnd();
		glReadPixels(0, 0, W, D, GL_RGBA, GL_UNSIGNED_BYTE, px);
		for (int z = 0; z < D; z++) {
			printf("y=%d z=%d:", ys[k], z);
			for (int x = 0; x < W && x < 8; x++) { unsigned char *q = px + (z * W + x) * 4; printf(" %d,%d,%d", q[0] / 16, q[1] / 16, q[2] / 16); }
			putchar('\n');
		}
	}
	printf("want x,y,z at each spot; err %x\n", glGetError());
	return 0;
}
