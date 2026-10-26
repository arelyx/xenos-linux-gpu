Kernel fixes found while bringing up Linux 6.18 + Free60 xenon0.30 on a Trinity slim, 25 Oct 2026.
Candidates for https://github.com/Free60Project/linux-kernel-xbox360.

0001 sis190 TX wake race      tested on hardware
0002 Xenon AltiVec            built; tested on hardware (Debian libLLVM loads)
idle (not a patch yet)        Xenon has no ppc_md.power_save, so idle SMT threads spin at
                              medium priority and take ~45% of the core from their sibling.
                              A power_save that spins at low SMT priority (or 1,1,1) with
                              interrupts enabled until need_resched() gives 1.85x single-thread
                              speed with an idle sibling, no change with both busy. Tested as a
                              module (kernel/xenon_idle/xenon_idle.c); belongs in platforms/xenon/setup.c.
                              Also fixes /proc/stat booking idle time as irq time.
fence IRQ                     Xenos CP raises RB_INT with the a2xx CP_INTERRUPT opcode 0x40;
                              Xenia's PM4_INTERRUPT 0x54 raises nothing on this hardware.
kexec                         default_machine_kexec on Xenon hangs right after the jump (tested).
VMX128 gap                    With AltiVec advertised, standard AltiVec code mostly works, but the
                              Xenon VMX128 unit lacks the integer multiply/sum family (vmsumubm,
                              vmsumshm, vmsumuhm, vmhaddshs, vmladduhm, vmuleub, vmulouh, vsum4ubs
                              tested: SIGILL). pixman's VMX paths use them (Xorg crashes; workaround
                              PIXMAN_DISABLE=vmx). Proper fix: emulate these in the program-check
                              handler, or don't advertise AltiVec. Probe: xegpu/vmxprobe.
