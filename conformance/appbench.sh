#!/bin/bash
# appbench.sh <glamor|noaccel>: (on the console) time terminal workloads under a fresh Xorg.
# Seconds to scroll 100,000 lines through xterm (core and Xft fonts) and xfce4-terminal.
mode=${1:-glamor}
KEEP_X=1 /opt/deqp/xperf.sh $mode -dot >/dev/null
export DISPLAY=:0
t() { local s=$(date +%s.%N); timeout 300 "$@" >/dev/null 2>&1; echo "$(date +%s.%N) - $s" | bc; }
echo "$mode xterm-core $(t xterm -geometry 160x50 -e seq 1 100000)"
echo "$mode xterm-xft $(t xterm -geometry 160x50 -fa Monospace -fs 10 -e seq 1 100000)"
echo "$mode xfce4-terminal $(t xfce4-terminal --disable-server --geometry 160x50 -x seq 1 100000)"
pkill -x Xorg
