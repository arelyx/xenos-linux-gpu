/* gldepthtex <2d|rect> [internalformat]: upload a GL_DEPTH_COMPONENT24 texture with depth = x/W from the CPU,
 * sample it (r channel) into an RGBA8 FBO and print what the shader saw at a few x.
 * Checks the CPU upload layout of depth textures against what the texture unit reads. */
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
int main(int argc, char **argv)
{
	int rect = argc > 1 && !strcmp(argv[1], "rect"), W = getenv("W") ? atoi(getenv("W")) : 64, H = getenv("H") ? atoi(getenv("H")) : W;
	int fd = open("/dev/dri/renderD128", O_RDWR);
	struct gbm_device *gbm = gbm_create_device(fd);
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	EGLint maj, min;
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_API);
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, NULL);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	GLenum tgt = rect ? GL_TEXTURE_RECTANGLE_ARB : GL_TEXTURE_2D;
	static float dv[256 * 256];
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) dv[y * W + x] = (x + 0.5f) / W;
	GLuint t, fbo, c;
	glGenTextures(1, &t); glBindTexture(tgt, t);
	GLint ifmt = argc > 2 ? (int)strtol(argv[2], 0, 0) : GL_DEPTH_COMPONENT24;
	if (ifmt == GL_DEPTH24_STENCIL8)
		glTexImage2D(tgt, 0, ifmt, W, H, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, NULL);
	else
		glTexImage2D(tgt, 0, ifmt, W, H, 0, getenv("LUM") ? GL_LUMINANCE : GL_DEPTH_COMPONENT, GL_FLOAT, dv);
	if (getenv("RENDER") || ifmt == GL_DEPTH24_STENCIL8) {   /* write depth = x/W with the GPU instead */
		GLuint rf;
		glGenFramebuffers(1, &rf); glBindFramebuffer(GL_FRAMEBUFFER, rf);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, tgt, t, 0);
		glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
		glViewport(0, 0, W, H);
		glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS);
		glBegin(GL_QUADS); glVertex3f(-1, -1, -1); glVertex3f(1, -1, 1); glVertex3f(1, 1, 1); glVertex3f(-1, 1, -1); glEnd();
		glDisable(GL_DEPTH_TEST);
		static float rb[256];
		glReadPixels(0, 1, W, 1, GL_DEPTH_COMPONENT, GL_FLOAT, rb);
		printf("readback after render: x0 %.3f, x%d %.3f\n", rb[0], W / 2, rb[W / 2]);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glFinish();
	}
	glTexParameteri(tgt, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(tgt, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(tgt, GL_DEPTH_TEXTURE_MODE, GL_LUMINANCE);
	glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glGenRenderbuffers(1, &c); glBindRenderbuffer(GL_RENDERBUFFER, c);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, W, W);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, c);
	glViewport(0, 0, W, W);
	const char *vs = "void main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}";
	const char *fs2 = "uniform sampler2D s;void main(){gl_FragColor=vec4(texture2D(s,gl_TexCoord[0].xy).r);}";
	char fsrbuf[256];
	snprintf(fsrbuf, sizeof(fsrbuf), "#extension GL_ARB_texture_rectangle : require\nuniform sampler2DRect s;void main(){gl_FragColor=vec4(texture2DRect(s,gl_TexCoord[0].xy*%d.0).r);}", W);
	const char *fsr = fsrbuf;
	GLuint p = glCreateProgram(), v = glCreateShader(GL_VERTEX_SHADER), f = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(v, 1, &vs, NULL); glCompileShader(v);
	glShaderSource(f, 1, rect ? &fsr : &fs2, NULL); glCompileShader(f);
	glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p); glUseProgram(p);
	glBegin(GL_QUADS);
	glTexCoord2f(0, 0); glVertex2f(-1, -1); glTexCoord2f(1, 0); glVertex2f(1, -1);
	glTexCoord2f(1, 1); glVertex2f(1, 1); glTexCoord2f(0, 1); glVertex2f(-1, 1);
	glEnd();
	static unsigned char px[256 * 256 * 4];
	glReadPixels(0, 0, W, W, GL_RGBA, GL_UNSIGNED_BYTE, px);
	printf("%s: x:want/got", rect ? "rect" : "2d");
	for (int x = 0; x < W; x += W / 8) printf(" %d:%d/%d", x, (int)((x + 0.5f) / W * 255 + 0.5f), px[(10 * W + x) * 4]);
	printf("\nerr %x\n", glGetError());
	return 0;
}
