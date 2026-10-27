/* glprime: what X's DRI3 does with a client's back buffer. Allocate a 320x240 XRGB8888
 * render-target BO with GBM on the render node, export it as a dma-buf, and import it with
 * GBM on the card node (as glamor_pixmap_from_fds does), printing each step. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <gbm.h>
#include <errno.h>
#include <string.h>
#include <xf86drm.h>
int main(void)
{
	int rfd = open("/dev/dri/renderD128", O_RDWR), cfd = open("/dev/dri/card0", O_RDWR);
	struct gbm_device *r = gbm_create_device(rfd), *c = gbm_create_device(cfd);
	printf("devices %p %p (%s)\n", (void *)r, (void *)c, c ? gbm_device_get_backend_name(c) : "-");
	struct gbm_bo *bo = gbm_bo_create(r, 320, 240, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
	if (!bo) { puts("create failed"); return 1; }
	int fd = gbm_bo_get_fd(bo);
	printf("bo stride %u modifier %llx fd %d\n", gbm_bo_get_stride(bo), (unsigned long long)gbm_bo_get_modifier(bo), fd);
	unsigned h1 = 0, h2 = 0;
	int e1 = drmPrimeFDToHandle(cfd, fd, &h1), e1n = errno;
	int e2 = drmPrimeFDToHandle(rfd, fd, &h2), e2n = errno;
	printf("prime import card0 %d (%s) handle %u, render %d (%s) handle %u\n", e1, e1 ? strerror(e1n) : "ok", h1,
	       e2, e2 ? strerror(e2n) : "ok", h2);
	struct gbm_import_fd_data d = { .fd = fd, .width = 320, .height = 240, .stride = gbm_bo_get_stride(bo),
					.format = GBM_FORMAT_XRGB8888 };
	struct gbm_bo *ib = gbm_bo_import(c, GBM_BO_IMPORT_FD, &d, GBM_BO_USE_RENDERING);
	printf("import (stride %u): %p\n", d.stride, (void *)ib);
	if (ib) printf("imported stride %u\n", gbm_bo_get_stride(ib));
	return 0;
}
