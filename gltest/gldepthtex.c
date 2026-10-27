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
	glTexImage2D(tgt, 0, argc > 2 ? (int)strtol(argv[2], 0, 0) : GL_DEPTH_COMPONENT24, W, H, 0, getenv("LUM") ? GL_LUMINANCE : GL_DEPTH_COMPONENT, GL_FLOAT, dv);
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
