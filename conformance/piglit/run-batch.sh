#!/bin/bash
# run-batch.sh <batch file> <result file> <per-test timeout>: run piglit tests on the console.
# Batch lines: name<TAB>shell-quoted command. Result lines: name<TAB>result
# ("running" is written before each test, so a hang leaves it as the last line).
bf=$1; rf=$2; tmo=$3
export PIGLIT_PLATFORM=gbm PIGLIT_SOURCE_DIR=/usr/lib/powerpc64-linux-gnu/piglit LD_LIBRARY_PATH=/opt/mesa-xenos/lib
export PATH=/usr/lib/powerpc64-linux-gnu/piglit/bin:$PATH
cd /usr/lib/powerpc64-linux-gnu/piglit
while IFS=$'\t' read -r name cmd; do
  printf '%s\trunning\n' "$name" >> "$rf"; sync "$rf" 2>/dev/null
  out=$(eval timeout "$tmo" $cmd 2>&1); rc=$?
  r=$(printf '%s\n' "$out" | sed -n 's/.*PIGLIT: {"result": "\([a-z]*\)".*/\1/p' | tail -1)
  [ -z "$r" ] && { [ $rc = 124 ] && r=timeout || r="crash($rc)"; }
  sed -i '$d' "$rf"; printf '%s\t%s\n' "$name" "$r" >> "$rf"
done < "$bf"
