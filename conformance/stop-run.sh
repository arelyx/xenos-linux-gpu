#!/bin/bash
# stop-run.sh: stop running deqp-run.py harnesses and any power cycle they started.
# (matches process names exactly; "pkill -f <pattern>" also kills the shell that runs it)
for p in $(pgrep -x python3); do
	grep -qa deqp-run.py /proc/$p/cmdline 2>/dev/null && kill $p
done
pkill -x xbox-cycle
true
