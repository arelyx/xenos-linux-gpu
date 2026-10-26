/* One-shot GPU bring-up after boot: start the kernel-owned CP and train the EDRAM.
 * Xell reprograms the EDRAM clock, so this must run once per boot before any client renders. */
#include <stdio.h>
#include "../libxe/xe.h"
#include "../libxe/xe_linux.h"
#include "../libxe/edram.h"
int main(void)
{
	static struct XenosDevice xe_s; struct XenosDevice *xe = &xe_s;
	setvbuf(stdout, NULL, _IONBF, 0);
	if (xe_linux_open()) return 1;
	Xe_Init(xe);
	edram_init(xe);
	printf("xeinit: CP running, EDRAM trained\n");
	xe_linux_close();
	return 0;
}
