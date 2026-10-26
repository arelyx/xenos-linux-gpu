#!/bin/bash
# gl-ready.sh [xegpu opts]: print "ok" if the GPU is usable for GL tests (loading the msm interface
# if needed), "hung" if it must be power-cycled first.  Runs on the console.
P=/sys/module/xegpu/parameters
opts=${1:-msm_compat=1}
if [ "$(cat $P/gpu_hung 2>/dev/null)" = Y ] || [ "$(cat $P/fence_timeouts 2>/dev/null || echo 1)" != 0 ] || [ "$(cat $P/fence_stalls 2>/dev/null || echo 0)" != 0 ]; then echo hung; exit; fi
if [ "$(cat $P/msm_compat)" != 1 ] || [ -n "$RELOAD" ]; then
  systemctl stop xfce-tty1 2>/dev/null; pkill -9 -x Xorg; sleep 2
  XEGPU_OPTS="$opts" /opt/xegpu/xegpu-up.sh >/dev/null 2>&1
  echo 0 > $P/gpu_blit
  [ "$(cat $P/gpu_hung)" = Y ] && { echo hung; exit; }
fi
echo ok
