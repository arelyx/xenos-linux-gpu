/* glzeq [bits]: clear a 16- or 24-bit depth renderbuffer to d, draw with
 * gl_FragDepth = d and report which of LESS/EQUAL/GREATER passes, for the
 * piglit depth-clear-precision-check values (k * 2^(bits-8) / max, as half
 * floats). Prints the first values where EQUAL fails. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <fcntl.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
static float half_round(float f)	/* round to the nearest half float, positive normal values */
{
	if (f == 0) return 0;
	int e; frexpf(f, &e);
	float q = ldexpf(1.0f, e - 11);
	return nearbyintf(f / q) * q;
}
int main(int argc, char **argv)
{
	int bits = argc > 1 ? atoi(argv[1]) : 16;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLuint fbo, cb, db;
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &cb); glBindRenderbuffer(GL_RENDERBUFFER, cb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 20, 20);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, cb);
	glGenRenderbuffers(1, &db); glBindRenderbuffer(GL_RENDERBUFFER, db);
	glRenderbufferStorage(GL_RENDERBUFFER, bits == 16 ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT24, 20, 20);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, db);
	glViewport(0, 0, 20, 20);
	const char *fs = "uniform float depth; void main() { gl_FragColor = vec4(0, 1, 0, 1); gl_FragDepth = depth; }";
	GLuint s = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(s, 1, &fs, NULL); glCompileShader(s);
	GLuint p = glCreateProgram(); glAttachShader(p, s); glLinkProgram(p); glUseProgram(p);
	GLint loc = glGetUniformLocation(p, "depth");
	glEnable(GL_DEPTH_TEST);
	unsigned max = (1u << bits) - 1, step = 1u << (bits - 8);
	int shown = 0;
	if (argc > 2) {	/* glzeq bits export: what an exported depth stores, and a rasterized one */
		const char *vs = "uniform float depth; void main() { gl_Position = vec4(gl_Vertex.xy, depth * 2.0 - 1.0, 1.0); }";
		const char *fs2 = "void main() { gl_FragColor = vec4(0, 1, 0, 1); }";
		GLuint v = glCreateShader(GL_VERTEX_SHADER); glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
		GLuint s2 = glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(s2, 1, &fs2, NULL); glCompileShader(s2);
		GLuint p2 = glCreateProgram(); glAttachShader(p2, v); glAttachShader(p2, s2); glLinkProgram(p2);
		GLint loc2 = glGetUniformLocation(p2, "depth");
		glDepthFunc(GL_ALWAYS);
		for (unsigned k = argc > 3 ? atoi(argv[3]) * step : 0; k < max; k += step * (argc > 4 ? atoi(argv[4]) : 8)) {
			float d = half_round(k / (float)max);
			unsigned got[3];
			glClearDepth(d); glClear(GL_DEPTH_BUFFER_BIT);
			{ GLuint zr; glReadPixels(5, 5, 1, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &zr); got[2] = zr >> (32 - bits); }
			for (int m = 0; m < 2; m++) {
				glUseProgram(m ? p2 : p);
				glUniform1f(m ? loc2 : loc, d);
				glClearDepth(0); glClear(GL_DEPTH_BUFFER_BIT);
				glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); glEnd();
				GLuint zr; glReadPixels(5, 5, 1, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &zr);
				got[m] = zr >> (32 - bits);
			}
			printf("d %.8f  d*(2^n-1) %.3f  d*2^n %.3f  export %u  raster %u  clear %u\n", d, d * max, d * (max + 1.0), got[0], got[1], got[2]);
		}
		return 0;
	}
	for (unsigned k = 0; k < max && shown < 6; k += step) {
		float d = half_round(k / (float)max);
		const GLenum funcs[3] = {GL_LESS, GL_EQUAL, GL_GREATER};
		int ok[3];
		for (int f = 0; f < 3; f++) {
			glClearDepth(d); glClearColor(1, 0, 0, 1);
			glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
			glDepthFunc(funcs[f]); glUniform1f(loc, d);
			glBegin(GL_QUADS); glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1); glEnd();
			unsigned char px[4]; glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
			ok[f] = px[1] == 255;
		}
		if (!ok[1]) {
			GLuint zr; glReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, &zr);
			printf("d %.8f (x max = %.3f): less %d equal %d greater %d, read back %u (>> %d = %u)\n",
			       d, d * max, ok[0], ok[1], ok[2], zr, 32 - bits, zr >> (32 - bits));
			shown++;
		}
	}
	if (!shown) printf("all equal\n");
	return 0;
}
