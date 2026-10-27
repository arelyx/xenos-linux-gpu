#!/bin/bash
# perfcheck.sh [baseline] [out]: run x11perf (glamor), glrt and xcopytest on the console and
# compare against a baseline from an earlier run. Flags any test more than 7% slower.
# Stops the desktop session (icewm-glamor or xfce-glamor) for the run and starts it again afterwards.
# perfcheck.sh --compare <baseline> <run>: only compare two saved runs.
here=$(dirname "$0")
if [ "$1" = --compare ]; then base=$2; out=$3; else
  base=${1:-$here/results/perf/baseline.txt}
  out=${2:-$(mktemp)}
host=${XBOX_HOST:-10.0.0.206}
ssh_opts=${XBOX_SSH_OPTS:--i ~/.ssh/lake}
tests="-rect10 -rect500 -copywinwin10 -copywinwin500 -putimage10 -putimage500 -shmput500 -getimage10 -getimage500 -aa10text -aa24text -comppixwin500 -oddtilerect100 -scroll500"
ssh $ssh_opts root@$host "
  sess=; for u in icewm-glamor xfce-glamor; do systemctl is-active -q \$u && sess=\$u; done; [ -n \"\$sess\" ] && systemctl stop \$sess
  KEEP_X=1 XENV='$XENV' /opt/deqp/xperf.sh glamor $tests
  echo \"xcopytest \$(DISPLAY=:0 timeout 300 /opt/xegpu/xtest/xcopytest 2>&1 | tail -1)\"
  pkill -x Xorg; sleep 2
  for i in 1 2 3; do LD_LIBRARY_PATH=/opt/mesa-xenos/lib /opt/xegpu/gltest/glrt 64 2000 draw; done | sort -k4 -n | sed -n 2p | sed 's/^/glrt-draw-us /'
  [ -n \"\$sess\" ] && systemctl start \$sess
  true" | tee "$out"
fi
[ -f "$base" ] || { echo "no baseline $base; save $out as one"; exit 0; }
python3 - "$base" "$out" <<'PY'
import re, sys
def load(p):
    d = {}
    for l in open(p):
        m = re.match(r'glamor (-\S+): .*\(\s*([\d.]+)/sec\)', l)
        if m: d[m[1]] = float(m[2])
        m = re.search(r'glrt-draw-us.*?([\d.]+) us', l)
        if m: d['glrt(us, lower is better)'] = -float(m[1])
    return d
b, n = load(sys.argv[1]), load(sys.argv[2])
bad = 0
for k in b:
    if k not in n: print(f'{k:28} missing'); bad += 1; continue
    r = n[k] / b[k]
    tol = 0.85 if k == '-putimage500' else 0.93   # putimage500 varies 167-182 between runs
    flag = '  SLOWER' if r < tol else ('  faster' if r > 1.07 else '')
    bad += r < tol
    print(f'{k:28} {abs(b[k]):>12.1f} -> {abs(n[k]):>12.1f}  x{r:.2f}{flag}')
ok = any(l.startswith('xcopytest') and 'PASS' in l for l in open(sys.argv[2]))
print('xcopytest', 'PASS' if ok else 'FAIL')
sys.exit(1 if bad or not ok else 0)
PY
