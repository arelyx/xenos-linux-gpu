/* xcopytest: random fills and copies within a window and within a pixmap, checked
 * against a CPU model with XGetImage. Exercises glamor's same-pixmap copies
 * (glTextureBarrierNV between draws that sample what earlier draws wrote). */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 320
#define H 240
static unsigned model[H][W], tmp[H][W];

static void fill(unsigned c, int x, int y, int w, int h)
{
	for (int j = y; j < y + h; j++)
		for (int i = x; i < x + w; i++)
			model[j][i] = c;
}

static void copy(int sx, int sy, int w, int h, int dx, int dy)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			tmp[j][i] = model[sy + j][sx + i];
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			model[dy + j][dx + i] = tmp[j][i];
}

static int check(Display *d, Drawable dr, const char *what)
{
	XImage *im = XGetImage(d, dr, 0, 0, W, H, AllPlanes, ZPixmap);
	int bad = 0, fx = -1, fy = -1;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			if ((XGetPixel(im, x, y) & 0xffffff) != model[y][x]) {
				if (!bad++) { fx = x; fy = y; }
			}
	printf("%s: %d bad pixels", what, bad);
	if (bad)
		printf(" (first at %d,%d: got %06lx want %06x)", fx, fy, XGetPixel(im, fx, fy) & 0xffffff, model[fy][fx]);
	printf("\n");
	XDestroyImage(im);
	return bad != 0;
}

static XImage *img;

static int run(Display *d, Drawable dr, GC gc, int n, unsigned seed, const char *what)
{
	srand(seed);
	XSetForeground(d, gc, 0x202020);
	XFillRectangle(d, dr, gc, 0, 0, W, H);
	fill(0x202020, 0, 0, W, H);
	for (int k = 0; k < n; k++) {
		int w = 1 + rand() % 80, h = 1 + rand() % 60;
		int x = rand() % (W - w), y = rand() % (H - h);
		if (rand() % 4 == 0) {
			/* PutImage: small ones take Mesa's CPU path, large ones the GPU blit */
			if (rand() % 4 == 0) {
				w = 100 + rand() % (W - 100);
				h = 100 + rand() % (H - 100);
				x = rand() % (W - w + 1);
				y = rand() % (H - h + 1);
			}
			for (int j = 0; j < h; j++)
				for (int i = 0; i < w; i++) {
					unsigned c = (rand() & 0xffffff);
					XPutPixel(img, i, j, c);
					model[y + j][x + i] = c;
				}
			XPutImage(d, dr, gc, img, 0, 0, x, y, w, h);
		} else if (rand() % 3 == 0) {
			unsigned c = (rand() & 0xffffff);
			XSetForeground(d, gc, c);
			XFillRectangle(d, dr, gc, x, y, w, h);
			fill(c, x, y, w, h);
		} else {
			/* near the source half the time, so copies overlap */
			int dx = rand() % 2 ? rand() % (W - w) : x + rand() % 21 - 10;
			int dy = rand() % 2 ? rand() % (H - h) : y + rand() % 21 - 10;
			if (dx < 0) dx = 0; if (dy < 0) dy = 0;
			if (dx > W - w) dx = W - w; if (dy > H - h) dy = H - h;
			XCopyArea(d, dr, dr, gc, x, y, w, h, dx, dy);
			copy(x, y, w, h, dx, dy);
		}
	}
	return check(d, dr, what);
}

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 2000, fails = 0;
	Display *d = XOpenDisplay(NULL);
	if (!d) { fprintf(stderr, "no display\n"); return 2; }
	Window win = XCreateSimpleWindow(d, DefaultRootWindow(d), 10, 10, W, H, 0, 0, 0);
	XSelectInput(d, win, ExposureMask);
	XMapWindow(d, win);
	for (XEvent e;;) { XNextEvent(d, &e); if (e.type == Expose) break; }
	GC gc = XCreateGC(d, win, 0, NULL);
	XSetGraphicsExposures(d, gc, False);
	img = XCreateImage(d, DefaultVisual(d, DefaultScreen(d)), DefaultDepth(d, DefaultScreen(d)), ZPixmap, 0,
			   malloc(W * H * 4), W, H, 32, 0);
	Pixmap pm = XCreatePixmap(d, win, W, H, DefaultDepth(d, DefaultScreen(d)));
	for (unsigned s = 1; s <= 3; s++) {
		char what[64];
		snprintf(what, sizeof what, "window seed %u", s);
		fails += run(d, win, gc, n, s, what);
		snprintf(what, sizeof what, "pixmap seed %u", s);
		fails += run(d, pm, gc, n, s + 100, what);
	}
	printf("%s\n", fails ? "FAIL" : "PASS");
	return fails != 0;
}
