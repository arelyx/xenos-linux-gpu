#!/bin/sh
# Bring up the GPU display path: xegpu module (pool, CP, DRM), EDRAM training,
# hand the display from the built-in xenos driver to xegpu_drm, enable GPU scanout.
# Safe to re-run: reloads the module. Stops and restarts Xfce if it was running.
set -e
DEV=0000:01:0f.0
had_x=0
systemctl is-active -q xfce-tty1 && { had_x=1; systemctl stop xfce-tty1; }
if lsmod | grep -q '^xegpu '; then rmmod xegpu; fi
sync; echo 3 > /proc/sys/vm/drop_caches; echo 1 > /proc/sys/vm/compact_memory
modprobe xegpu $XEGPU_OPTS
/opt/xegpu/user/tests/xeinit
if [ -e /sys/bus/pci/drivers/xenos/$DEV ]; then
	echo $DEV > /sys/bus/pci/drivers/xenos/unbind
fi
[ -e /sys/bus/pci/drivers/xegpu_drm/$DEV ] || echo $DEV > /sys/bus/pci/drivers/xegpu_drm/bind
echo 1 > /sys/module/xegpu/parameters/gpu_blit
[ $had_x = 1 ] && systemctl start xfce-tty1
echo "xegpu: display on xegpu_drm, GPU scanout on"
