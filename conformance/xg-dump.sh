#!/bin/bash
# xg-dump.sh: start Xorg + glamor with freedreno command-stream dumps in /opt/deqp/rdx,
# run a few X clients, report fence state.  The last dump before a stall is the suspect.
systemctl stop xfce-tty1 2>/dev/null; pkill -9 -x Xorg; sleep 2
/opt/deqp/gl-ready.sh >/dev/null
mkdir -p /opt/deqp/rdx; rm -f /opt/deqp/rdx/*
systemd-run --unit=xgd-$$ -p Environment=HOME=/root -p Environment=LD_LIBRARY_PATH=/opt/mesa-xenos/lib \
  -p Environment=LIBGL_DRIVERS_PATH=/opt/mesa-xenos/lib/dri -p Environment=GBM_BACKENDS_PATH=/opt/mesa-xenos/lib/gbm \
  -p Environment=PIXMAN_DISABLE=vmx -p Environment=FD_RD_DUMP= -p Environment=FD_RD_DUMP_PATH=/opt/deqp/rdx \
  /usr/bin/Xorg :0 vt1 -nolisten tcp -keeptty -configdir /etc/X11/glamor-test \
  -modulepath /opt/xegpu-xorg/modules,/usr/lib/xorg/modules -logfile /tmp/Xg3.log $XARGS
sleep 12
export DISPLAY=:0
for c in "xlogo -geometry 200x200+900+100" "xclock -geometry 300x300+100+100" "xeyes -geometry 300x200+500+100" "xterm -geometry 80x24+100+450"; do
  ($c >/dev/null 2>&1 &); sleep 4
  echo "$c: dumps $(ls /opt/deqp/rdx | wc -l) timeouts $(cat /sys/module/xegpu/parameters/fence_timeouts) stalls $(cat /sys/module/xegpu/parameters/fence_stalls 2>/dev/null)"
done
