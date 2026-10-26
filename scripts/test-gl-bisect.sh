#!/bin/sh
# One freedreno run where every draw/clear/resolve is its own submit (FD_MESA_DEBUG=flush), with
# command dumps. If the GPU hangs, the last completed fence says which submit hung.
# Log: /opt/xegpu/test-gl-bisect.log, dumps: /opt/xegpu/rd/
exec > /opt/xegpu/test-gl-bisect.log 2>&1
XEGPU_OPTS="msm_compat=1" /opt/xegpu/xegpu-up.sh
echo 0 > /sys/module/xegpu/parameters/gpu_blit     # keep the display on the CPU path during the test
mkdir -p /opt/xegpu/rd; rm -f /opt/xegpu/rd/*
LD_LIBRARY_PATH=/opt/mesa-xenos/lib GBM_BACKENDS_PATH=/opt/mesa-xenos/lib/gbm \
  XENOS_DEBUG=1 GLSMOKE_PRECLEAR=1 GLSMOKE_DUMP=/opt/xegpu/rd FD_MESA_DEBUG=flush,nobin FD_RD_DUMP=enable,full FD_RD_DUMP_PATH=/opt/xegpu/rd timeout 60 /opt/xegpu/gltest/glsmoke
echo "glsmoke rc=$?"
echo "fence emitted vs done:"; dmesg | grep -E "xegpu" | tail -5
cat /sys/module/xegpu/parameters/gpu_hung
ls /opt/xegpu/rd
