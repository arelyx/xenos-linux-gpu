#!/bin/sh
# Bring xegpu up in msm-compatible mode, run the UAPI and GLES2 smoke tests, then restore normal mode.
# Logs to /opt/xegpu/test-gl.log
exec > /opt/xegpu/test-gl.log 2>&1
set -x
XEGPU_OPTS="msm_compat=1" /opt/xegpu/xegpu-up.sh
ls -la /dev/dri/
cat /sys/kernel/debug/dri/*/name 2>/dev/null
mkdir -p /opt/xegpu/rd; rm -f /opt/xegpu/rd/*
cd /opt/xegpu/user && make tests/msmtest >/dev/null && ./tests/msmtest
[ "$RUN_FREEDRENO" = 1 ] && LD_LIBRARY_PATH=/opt/mesa-xenos/lib LIBGL_DRIVERS_PATH=/opt/mesa-xenos/lib/dri GBM_BACKENDS_PATH=/opt/mesa-xenos/lib/gbm \
  FD_MESA_DEBUG=nobin FD_RD_DUMP=enable,full FD_RD_DUMP_PATH=/opt/xegpu/rd timeout 30 /opt/xegpu/gltest/glsmoke
echo "glsmoke rc=$?"
dmesg | grep -i -E "xegpu|msm|oops|bug" | tail -10
/opt/xegpu/xegpu-up.sh
