#!/bin/bash
# run-watched.sh <caselist> <qpa> <timeout>   (runs on the console)
# Runs deqp-gles2 and watches dmesg for missed fences.  Writes <qpa>.events:
#   "miss <case>"  when the kernel's missed-fence count first rises while <case> is running
#                  (deqp is then killed: a missed fence means the GPU is wedged)
#   "exit <rc>"    when deqp ends
cl=$1; qpa=$2; to=$3; ev=$qpa.events
: > $ev
cnt() { echo $(( $(cat /sys/module/xegpu/parameters/fence_timeouts) + $(cat /sys/module/xegpu/parameters/fence_stalls 2>/dev/null || echo 0) )); }
base=$(cnt)
cd /opt/deqp
LD_LIBRARY_PATH=/opt/mesa-xenos/lib EGL_PLATFORM=surfaceless \
timeout -s KILL $to ./deqp-gles2 --deqp-archive-dir=/opt/deqp --deqp-caselist-file=$cl --deqp-log-filename=$qpa \
  --deqp-surface-type=pbuffer --deqp-surface-width=256 --deqp-surface-height=256 \
  --deqp-gl-config-name=rgba8888d24s8ms0 --deqp-log-images=disable --deqp-log-shader-sources=disable \
  --deqp-watchdog=enable >$qpa.stdout 2>&1 &
pid=$!
while kill -0 $pid 2>/dev/null; do
  n=$(cnt)
  if [ "$n" -gt "$base" ]; then
    cur=$(grep '^#beginTestCaseResult' $qpa 2>/dev/null | tail -1 | cut -d' ' -f2)
    echo "miss $cur" >> $ev
    kill -9 $pid; break
  fi
  sleep 0.3
done
wait $pid; echo "exit $?" >> $ev
