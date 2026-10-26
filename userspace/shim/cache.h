#ifndef SHIM_CACHE_H
#define SHIM_CACHE_H
/* xegpu buffers are mapped uncached (non-coherent DMA platform); a sync orders the stores. */
static inline void memdcbst(void *p, int len) { (void)p; (void)len; __asm__ volatile("sync" ::: "memory"); }
static inline void memdcbf(void *p, int len)  { (void)p; (void)len; __asm__ volatile("sync" ::: "memory"); }
static inline void memdcbt(void *p, int len)  { (void)p; (void)len; }
#endif
