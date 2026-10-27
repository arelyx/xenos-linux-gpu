/* xresize [steps]: map a window and grow it from 200x150 to the whole screen (and back),
 * drawing into it after each step, as dragging a window corner does. */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <X11/Xlib.h>
int main(int argc, char **argv)
{
	int steps = argc > 1 ? atoi(argv[1]) : 60;
	Display *d = XOpenDisplay(NULL);
	if (!d) return 1;
	int s = DefaultScreen(d), W = DisplayWidth(d, s), H = DisplayHeight(d, s);
	Window w = XCreateSimpleWindow(d, RootWindow(d, s), 50, 50, 200, 150, 0, 0, 0x336699);
	XSelectInput(d, w, ExposureMask | StructureNotifyMask);
	XMapWindow(d, w);
	GC gc = XCreateGC(d, w, 0, NULL);
	XSync(d, False);
	for (int pass = 0; pass < 2; pass++)
		for (int i = 0; i <= steps; i++) {
			int k = pass ? steps - i : i;
			int ww = 200 + (W + 100 - 200) * k / steps, hh = 150 + (H + 60 - 150) * k / steps;
			XResizeWindow(d, w, ww, hh);
			XSetForeground(d, gc, 0x10101 * (k * 4 & 255));
			XFillRectangle(d, w, gc, 0, 0, ww / 2, hh / 2);
			XDrawString(d, w, gc, 20, 20, "resize", 6);
			XSync(d, False);
			usleep(30000);
			printf("%dx%d\n", ww, hh); fflush(stdout);
		}
	return 0;
}
