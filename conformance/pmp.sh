#!/bin/bash
# pmp.sh <pid> <samples>: poor man's profiler with eu-stack; aggregates function-name stacks
pid=$1; n=${2:-60}
for i in $(seq $n); do eu-stack -p $pid -1 2>/dev/null | awk '/^#/{print $3}' | head -14 | tr '\n' ' '; echo; sleep 0.05; done > /tmp/pmp.txt
echo "== innermost"; awk '{print $1}' /tmp/pmp.txt | sort | uniq -c | sort -rn | head -12
echo "== frames anywhere in the stack (inclusive)"; tr ' ' '\n' < /tmp/pmp.txt | grep -v '^$' | sort | uniq -c | sort -rn | head -40
