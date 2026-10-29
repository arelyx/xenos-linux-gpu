#!/bin/bash
# run-batch.sh <batch file> <result file> <per-test timeout>: run piglit tests on the console.
# Batch lines: name<TAB>shell-quoted command. Result lines: name<TAB>result
# ("running" is written before each test, so a console freeze leaves it as the last line).
# After each test the GPU's fence counters are checked: the first test after which they
# move, or the GPU is marked hung, is recorded as "hang" and the batch stops there.
bf=$1; rf=$2; tmo=$3
P=/sys/module/xegpu/parameters
# EGL_PLATFORM: tests that call eglGetDisplay(EGL_DEFAULT_DISPLAY) have no X here
# (glsl-bug-110796 failed eglInitialize instead of skipping), as in Mesa CI
export EGL_PLATFORM=surfaceless PIGLIT_PLATFORM=gbm PIGLIT_SOURCE_DIR=/usr/lib/powerpc64-linux-gnu/piglit LD_LIBRARY_PATH=/opt/mesa-xenos/lib
export PATH=/usr/lib/powerpc64-linux-gnu/piglit/bin:$PATH
cd /usr/lib/powerpc64-linux-gnu/piglit
state() { echo "$(cat $P/fence_timeouts) $(cat $P/fence_stalls) $(cat $P/gpu_hung)"; }
s0=$(state)
while IFS=$'\t' read -r name cmd; do
  printf '%s\trunning\n' "$name" >> "$rf"; sync "$rf" 2>/dev/null
  out=$(eval timeout "$tmo" $cmd 2>&1); rc=$?
  r=$(printf '%s\n' "$out" | sed -n 's/.*PIGLIT: {"result": "\([a-z]*\)".*/\1/p' | tail -1)
  [ -z "$r" ] && { [ $rc = 124 ] && r=timeout || r="crash($rc)"; }
  [ "$(state)" != "$s0" ] && r=hang
  sed -i '$d' "$rf"; printf '%s\t%s\n' "$name" "$r" >> "$rf"
  [ $r = hang ] && break
done < "$bf"
