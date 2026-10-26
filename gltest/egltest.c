/* What glamor does: desktop GL context first, then GLES2, on the KMS node, no config, surfaceless. */
#include <stdio.h>
#include <fcntl.h>
#include <gbm.h>
#include <epoxy/egl.h>

#include <epoxy/gl.h>
int main(int argc, char **argv)
{
	int fd = open(argc > 1 ? argv[1] : "/dev/dri/card0", O_RDWR);
	struct gbm_device *g = gbm_create_device(fd);
	EGLDisplay d = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_MESA, g, NULL);
	printf("init %d\n", eglInitialize(d, NULL, NULL));
	printf("ext: %s\n", eglQueryString(d, EGL_EXTENSIONS));
	for (int api = 0; api < 2; api++) {
		eglBindAPI(api ? EGL_OPENGL_ES_API : EGL_OPENGL_API);
		EGLint es[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
		EGLContext c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, api ? es : NULL);
		printf("%s: ctx %p err %x\n", api ? "GLES" : "GL", c, eglGetError());
		if (!c) continue;
		printf("  makecurrent %d err %x\n", eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c), eglGetError());
		printf("  version %s, epoxy %d, renderer %s\n", glGetString(GL_VERSION), epoxy_gl_version(), glGetString(GL_RENDERER));
		eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroyContext(d, c);
	}
	return 0;
}
