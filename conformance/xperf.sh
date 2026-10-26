#!/bin/bash
# xperf.sh <glamor|noaccel> [x11perf tests...]: fresh Xorg with the given acceleration, run x11perf, print ops/s.
mode=${1:-glamor}; shift
tests=${*:--rect10 -rect500 -copywinwin10 -copywinwin500 -putimage10 -putimage500 -shmput500 -aa10text -aa24text -comppixwin500 -oddtilerect100 -scroll500}
systemctl stop xfce-tty1 2>/dev/null; pkill -9 -x Xorg; sleep 2
/opt/deqp/gl-ready.sh >/dev/null
echo 0 > /sys/module/xegpu/parameters/gpu_blit
[ "$mode" = noaccel ] && echo 1 > /sys/module/xegpu/parameters/gpu_blit
systemd-run --unit=xperf-$$ -p Environment=HOME=/root -p Environment=LD_LIBRARY_PATH=/opt/mesa-xenos/lib \
  -p Environment=PIXMAN_DISABLE=vmx $XENV /usr/bin/Xorg :0 vt1 -nolisten tcp -keeptty -configdir /etc/X11/$mode-test \
  -modulepath /opt/xegpu-xorg/modules,/usr/lib/xorg/modules -logfile /tmp/Xperf.log >/dev/null 2>&1
sleep 10
for t in $tests; do
  r=$(DISPLAY=:0 timeout 120 x11perf -repeat 2 -time 2 $t 2>/dev/null | grep -E "^ *[0-9]+ trep|reps" | tail -1)
  echo "$mode $t: $(echo "$r" | sed 's/^ *//' | cut -c1-90)"
done
echo "$mode stalls $(cat /sys/module/xegpu/parameters/fence_stalls) timeouts $(cat /sys/module/xegpu/parameters/fence_timeouts)"
