// SPDX-License-Identifier: GPL-2.0
/*
 * Experiment: a low-priority idle loop for the Xenon PPE threads.
 *
 * Without ppc_md.power_save, an idle hardware thread bounces through the
 * generic idle loop at medium SMT priority and steals dispatch slots from
 * its busy sibling. This spins at low priority with interrupts enabled
 * until there is work, so the sibling gets the core.
 */
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/irqflags.h>
#include <asm/machdep.h>

static int mode = 1;       /* 1: or 1,1,1 (low)  2: or 31,31,31 (very low) */
module_param(mode, int, 0644);

static void xenon_lowprio_idle(void)
{
	raw_local_irq_enable();
	if (READ_ONCE(mode) == 2) {
		while (!need_resched())
			asm volatile("or 31,31,31" ::: "memory");
	} else {
		while (!need_resched())
			asm volatile("or 1,1,1" ::: "memory");
	}
	asm volatile("or 2,2,2" ::: "memory");
}

static int __init xenon_idle_init(void)
{
	if (ppc_md.power_save) {
		pr_info("xenon_idle: a power_save hook is already installed, not touching it\n");
		return -EBUSY;
	}
	ppc_md.power_save = xenon_lowprio_idle;
	wake_up_all_idle_cpus();
	pr_info("xenon_idle: low-priority idle installed (mode %d)\n", mode);
	return 0;
}

static void __exit xenon_idle_exit(void)
{
	ppc_md.power_save = NULL;
	smp_mb();
	wake_up_all_idle_cpus();
	msleep(100);   /* let every thread leave the hook before the text goes away */
	pr_info("xenon_idle: removed\n");
}

module_init(xenon_idle_init);
module_exit(xenon_idle_exit);
MODULE_LICENSE("GPL");
