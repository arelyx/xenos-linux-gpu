/* Headless GLES2 smoke test for freedreno-on-Xenos: EGL on a GBM device, render into an FBO,
 * glReadPixels, check. Stage 1: clear. Stage 2: one triangle with a constant-color shader.
 * Stage 3: per-vertex colors (exercises vertex fetch and interpolation). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#define W 64
#define H 64
static unsigned char px[W * H * 4];

static int stage_no;
static void dump(void)   /* GLSMOKE_DUMP=dir: write the readback of each stage as a PPM */
{
	const char *d = getenv("GLSMOKE_DUMP"); if (!d) return;
	char path[256]; snprintf(path, sizeof path, "%s/stage%d.ppm", d, ++stage_no);
	FILE *f = fopen(path, "wb"); if (!f) return;
	fprintf(f, "P6\n%d %d\n255\n", W, H);
	for (int y = H - 1; y >= 0; y--) for (int x = 0; x < W; x++) fwrite(&px[(y * W + x) * 4], 1, 3, f);
	fclose(f);
}

static int check(const char *what, int x, int y, int r, int g, int b, int tol)
{
	unsigned char *p = &px[(y * W + x) * 4];
	int ok = abs(p[0] - r) <= tol && abs(p[1] - g) <= tol && abs(p[2] - b) <= tol;
	printf("  %-28s (%2d,%2d) = %3d %3d %3d %3d  want %3d %3d %3d  %s\n", what, x, y, p[0], p[1], p[2], p[3], r, g, b, ok ? "ok" : "FAIL");
	return ok;
}

static GLuint shader(GLenum t, const char *src)
{
	GLuint s = glCreateShader(t); glShaderSource(s, 1, &src, NULL); glCompileShader(s);
	GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) { char log[512]; glGetShaderInfoLog(s, sizeof log, NULL, log); printf("shader: %s\n", log); }
	return s;
}

int main(int argc, char **argv)
{
	const char *node = argc > 1 ? argv[1] : "/dev/dri/renderD128";
	int fd = open(node, O_RDWR);
	if (fd < 0) { perror(node); return 1; }
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	if (!eglInitialize(dpy, &maj, &min)) { printf("eglInitialize failed %x\n", eglGetError()); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	static const EGLint cattr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, cattr);
	if (!ctx || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { printf("context failed %x\n", eglGetError()); return 1; }
	printf("GL_RENDERER: %s\nGL_VERSION: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	GLuint tex, fbo;
	glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	printf("fbo status %x\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
	glViewport(0, 0, W, H);
	int pass = 0, total = 0;

	printf("stage 1: clear\n");
	if (getenv("GLSMOKE_PRECLEAR")) {   /* clear once and wait first: is the first clear of a process special? */
		glClearColor(1.0f, 0.0f, 0.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); glFinish();
	}
	glClearColor(0.2f, 0.4f, 0.8f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 2; pass += check("clear corner", 0, 0, 51, 102, 204, 2); pass += check("clear center", 32, 32, 51, 102, 204, 2);

	printf("stage 2: constant-color triangle\n");
	GLuint p = glCreateProgram();
	glAttachShader(p, shader(GL_VERTEX_SHADER, "attribute vec2 pos; void main(){ gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER, "precision mediump float; void main(){ gl_FragColor = vec4(1.0, 0.5, 0.0, 1.0); }"));
	glBindAttribLocation(p, 0, "pos"); glLinkProgram(p); glUseProgram(p);
	static const GLfloat tri[] = { -1, -1,  1, -1,  -1, 1 };   /* lower-left half */
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri); glEnableVertexAttribArray(0);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 2; pass += check("inside triangle", 8, 8, 255, 128, 0, 3); pass += check("outside (clear)", 56, 56, 51, 102, 204, 2);

	printf("stage 2b: same triangle again, green clear\n");
	glClearColor(0.0f, 1.0f, 0.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 2; pass += check("again inside", 8, 8, 255, 128, 0, 3); pass += check("again outside green", 56, 56, 0, 255, 0, 3);
	glClearColor(0.2f, 0.4f, 0.8f, 1.0f);

	printf("stage 2c: magenta clear, glFinish, then the triangle\n");
	glClearColor(1.0f, 0.0f, 1.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT); glFinish();
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 2; pass += check("2c inside", 8, 8, 255, 128, 0, 3); pass += check("2c outside magenta", 56, 56, 255, 0, 255, 3);
	glClearColor(0.2f, 0.4f, 0.8f, 1.0f);

	printf("stage 2d: no clear, triangle again\n");
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 1; pass += check("2d inside", 8, 8, 255, 128, 0, 3);

	printf("stage 2e: triangle from vertices 3..5 (fresh indices)\n");
	static const GLfloat tri6[] = { 0,0, 0,0, 0,0,  -1, -1,  1, -1,  -1, 1 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri6);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 3, 3);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 2; pass += check("2e inside", 8, 8, 255, 128, 0, 3); pass += check("2e outside white", 56, 56, 255, 255, 255, 3);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
	glClearColor(0.2f, 0.4f, 0.8f, 1.0f);

	printf("stage 2f: triangle from a fresh VBO\n");
	{
		GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof tri, tri, GL_STATIC_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);
		glDrawArrays(GL_TRIANGLES, 0, 3);
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
		total += 2; pass += check("2f inside", 8, 8, 255, 128, 0, 3); pass += check("2f outside black", 56, 56, 0, 0, 0, 3);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri);
		glClearColor(0.2f, 0.4f, 0.8f, 1.0f);
	}

	printf("stage 3: per-vertex color\n");
	GLuint q = glCreateProgram();
	glAttachShader(q, shader(GL_VERTEX_SHADER, "attribute vec2 pos; attribute vec3 col; varying vec3 c; void main(){ c = col; gl_Position = vec4(pos, 0.0, 1.0); }"));
	glAttachShader(q, shader(GL_FRAGMENT_SHADER, "precision mediump float; varying vec3 c; void main(){ gl_FragColor = vec4(c, 1.0); }"));
	glBindAttribLocation(q, 0, "pos"); glBindAttribLocation(q, 1, "col"); glLinkProgram(q); glUseProgram(q);
	static const GLfloat quad[] = { -1, -1,  1, -1,  -1, 1,  1, 1 };
	static const GLfloat col[] = { 1, 0, 0,  1, 0, 0,  1, 0, 0,  1, 0, 0 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad); glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, col); glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 1; pass += check("full-screen red", 32, 32, 255, 0, 0, 3);

	printf("stage 4: constant color, triangle strip\n");
	glUseProgram(p);
	glDisableVertexAttribArray(1);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad); glEnableVertexAttribArray(0);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 1; pass += check("strip orange", 32, 32, 255, 128, 0, 3);

	printf("stage 5: per-vertex color, triangle list\n");
	glUseProgram(q);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, tri); glEnableVertexAttribArray(0);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, col); glEnableVertexAttribArray(1);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 1; pass += check("list red", 8, 8, 255, 0, 0, 3);

	printf("stage 6: one interleaved buffer (pos+col)\n");
	static const GLfloat il[] = { -1, -1, 0, 1, 0,   1, -1, 0, 1, 0,   -1, 1, 0, 1, 0 };
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 20, il);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 20, il + 2);
	glClear(GL_COLOR_BUFFER_BIT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px); dump();
	total += 1; pass += check("interleaved green", 8, 8, 0, 255, 0, 3);

	printf("RESULT %d/%d (glGetError %x)\n", pass, total, glGetError());
	return pass == total ? 0 : 2;
}
