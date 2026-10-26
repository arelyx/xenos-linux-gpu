/* Offscreen GLES2 throughput on freedreno/Xenos: N frames of clear + T triangles into an
 * S x S FBO, glFinish each frame. Usage: glbench [size] [tris] [seconds] */
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec * 1e-9; }
static GLuint sh(GLenum t, const char *s) { GLuint x = glCreateShader(t); glShaderSource(x, 1, &s, NULL); glCompileShader(x); return x; }
int main(int argc, char **argv)
{
	int S = argc > 1 ? atoi(argv[1]) : 256, T = argc > 2 ? atoi(argv[2]) : 100; double secs = argc > 3 ? atof(argv[3]) : 3;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL); eglInitialize(d, NULL, NULL);
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint ca[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, ca); eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c);
	printf("%s, %dx%d, %d triangles/frame\n", glGetString(GL_RENDERER), S, S, T);
	GLuint tex, fbo; glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, S, S, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0); glViewport(0, 0, S, S);
	GLuint p = glCreateProgram();
	glAttachShader(p, sh(GL_VERTEX_SHADER, "attribute vec2 pos; attribute vec3 col; varying vec3 c; uniform float t; void main(){ c = col; float s = sin(t), k = cos(t); gl_Position = vec4(pos.x*k - pos.y*s, pos.x*s + pos.y*k, 0.0, 1.0); }"));
	glAttachShader(p, sh(GL_FRAGMENT_SHADER, "precision mediump float; varying vec3 c; void main(){ gl_FragColor = vec4(c, 1.0); }"));
	glBindAttribLocation(p, 0, "pos"); glBindAttribLocation(p, 1, "col"); glLinkProgram(p); glUseProgram(p);
	GLint ut = glGetUniformLocation(p, "t");
	float *v = malloc(sizeof(float) * 15 * T);
	for (int i = 0; i < T; i++) {
		float x = (i % 10) / 5.0f - 0.95f, y = (i / 10 % 10) / 5.0f - 0.95f;
		float q[15] = { x, y, 1, 0, 0,  x + 0.15f, y, 0, 1, 0,  x, y + 0.15f, 0, 0, 1 };
		for (int j = 0; j < 15; j++) v[i * 15 + j] = q[j];
	}
	GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 15 * T, v, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, 0); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, (void *)8);
	glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
	int frames = 0; double t0 = now();
	while (now() - t0 < secs) {
		glClearColor(0.1f, 0.1f, 0.1f, 1); glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(ut, frames * 0.01f);
		glDrawArrays(GL_TRIANGLES, 0, 3 * T);
		glFinish(); frames++;
	}
	double dt = now() - t0;
	unsigned char *px = malloc(S * S * 4); glReadPixels(0, 0, S, S, GL_RGBA, GL_UNSIGNED_BYTE, px);
	int bg = 0, colored = 0;
	for (int i = 0; i < S * S; i++) { if (px[i*4] == 26 && px[i*4+1] == 26 && px[i*4+2] == 26) bg++; else colored++; }
	printf("last frame: %d background pixels, %d covered by triangles\n", bg, colored);
	printf("%d frames in %.2f s: %.1f fps, %.0f triangles/s, glGetError %x\n", frames, dt, frames / dt, frames * T / dt, glGetError());
	return 0;
}
