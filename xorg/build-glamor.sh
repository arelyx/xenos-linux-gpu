#!/bin/sh
set -e
cd /root/xsrc/xorg-server-21.1.25
[ -d b ] || meson setup b -Dxorg=true -Dglamor=true -Dxephyr=false -Dxnest=false -Dxvfb=false -Dxwin=false -Dxquartz=false -Dudev=true -Dsystemd_logind=true -Ddri1=false -Ddri2=true -Ddri3=true -Dglx=true -Dsecure-rpc=false -Dxselinux=false -Dxcsecurity=true -Ddtrace=false -Dlisten_tcp=false -Dbuildtype=release
ninja -C b hw/xfree86/glamor_egl/libglamoregl.so
ls -la b/hw/xfree86/glamor_egl/libglamoregl.so
