#!/bin/sh
# Run on the Xbox after booting the AltiVec kernel. Reports what the new kernel changed.
echo "kernel: $(uname -v)"
LD_SHOW_AUXV=1 /bin/true | grep -E "AT_HWCAP:"
grep -q altivec /proc/cpuinfo && echo "cpuinfo: altivec listed" || echo "cpuinfo: no altivec flag (ok on ppc, check HWCAP 0x10000000 bit)"
python3 -c "import ctypes; ctypes.CDLL('/usr/lib/powerpc64-linux-gnu/libLLVM.so.22.1'); print('libLLVM loads: yes')" || echo "libLLVM loads: NO"
lsmod | grep -E "xegpu|xenon_idle"
for p in gpu_blit blit_gpu_count blit_cpu_count irq_count; do echo "xegpu $p=$(cat /sys/module/xegpu/parameters/$p 2>/dev/null)"; done
systemctl is-active xegpu xfce-tty1
