#!/bin/bash
# xg-one.sh "<client cmd>": fresh Xorg + glamor (no dumps), run one client, report stalls
systemctl stop xfce-tty1 2>/dev/null; pkill -9 -x Xorg; sleep 2
/opt/deqp/gl-ready.sh >/dev/null
systemd-run --unit=xgo-$$ -p Environment=HOME=/root -p Environment=LD_LIBRARY_PATH=/opt/mesa-xenos/lib \
  -p Environment=PIXMAN_DISABLE=vmx -p StandardError=file:/tmp/Xgo.err -p StandardOutput=file:/tmp/Xgo.out /usr/bin/Xorg :0 vt1 -nolisten tcp -keeptty -configdir /etc/X11/glamor-test \
  -modulepath /opt/xegpu-xorg/modules,/usr/lib/xorg/modules -logfile /tmp/Xgo.log -retro >/dev/null 2>&1
sleep 10
export DISPLAY=:0
($1 >/dev/null 2>&1 &); sleep 12
echo "$1: timeouts $(cat /sys/module/xegpu/parameters/fence_timeouts) stalls $(cat /sys/module/xegpu/parameters/fence_stalls)"
