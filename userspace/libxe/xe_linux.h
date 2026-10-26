#ifndef XE_LINUX_H
#define XE_LINUX_H
#include <stdint.h>
#include <stddef.h>
#include "xegpu_uapi.h"

/* open /dev/xegpu, map registers. returns 0 on success */
int xe_linux_open(void);
void xe_linux_close(void);
volatile unsigned int *xe_linux_regs(void);
const struct xegpu_info *xe_linux_info(void);
/* contiguous GPU-visible memory; *phys receives the GPU address */
void *xe_linux_alloc(uint32_t *phys, int size, int align);
void xe_linux_free(void *ptr);
/* CPU pointer for a GPU address we allocated, or NULL */
void *xe_linux_phys_to_virt(uint32_t phys);
uint32_t xe_linux_virt_to_phys(const void *p);
/* halt the CP, stop writeback, restore the scanout registers */
void xe_linux_quiesce(void);
/* kernel-owned CP */
int xe_linux_cp_init(void);
unsigned long long xe_linux_submit(uint32_t gpu_addr, uint32_t ndw);
int xe_linux_wait(unsigned long long fence, unsigned timeout_ms);
/* command stream recording (for kernel replay) */
int xe_linux_record_begin(size_t arena_size);
uint32_t *xe_linux_record_take(size_t *n);
void xe_linux_record_arena(uint8_t **virt, uint32_t *phys, size_t *used);
void xe_linux_record_end(void);
#endif
