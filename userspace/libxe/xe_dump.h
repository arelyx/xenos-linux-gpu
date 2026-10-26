/* de-tile a 32bpp Xenos tiled surface and write it as a binary PPM */
#ifndef XE_DUMP_H
#define XE_DUMP_H
#include <stdio.h>
#include <stdint.h>
static inline int xe_tiled_index(int x, int y, int width)
{
	/* same formula as the xenos DRM driver / libxenon console */
	return ((((y & ~31) * width) + (x & ~31) * 32) +
		(((x & 3) + ((y & 1) << 2) + ((x & 28) << 1) + ((y & 30) << 5)) ^ ((y & 8) << 2)));
}
/* order: 0 = word is 0xRRGGBBAA (resolve of ARGB clear), 1 = word is 0xBBGGRRAA-ish (swab32 ARGB) */
static inline int xe_dump_ppm(const char *path, const uint32_t *px, int w, int h, int pitch_px, int order)
{
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	fprintf(f, "P6\n%d %d\n255\n", w, h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			uint32_t v = px[xe_tiled_index(x, y, pitch_px)];
			unsigned char rgb[3];
			if (order == 0) { rgb[0] = v >> 24; rgb[1] = v >> 16; rgb[2] = v >> 8; }
			else            { rgb[0] = v >> 8;  rgb[1] = v >> 16; rgb[2] = v >> 24; }
			fwrite(rgb, 1, 3, f);
		}
	fclose(f);
	return 0;
}
#endif
