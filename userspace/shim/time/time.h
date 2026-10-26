#ifndef SHIM_TIME_TIME_H
#define SHIM_TIME_TIME_H
#include <time.h>
#include <stdint.h>
static inline void udelay(int us)
{
	struct timespec a, b;
	clock_gettime(CLOCK_MONOTONIC, &a);
	do {
		clock_gettime(CLOCK_MONOTONIC, &b);
	} while ((b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000 < us);
}
static inline void mdelay(int ms) { udelay(ms * 1000); }
static inline void delay(int s) { udelay(s * 1000000); }
#endif
