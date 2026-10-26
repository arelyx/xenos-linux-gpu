#!/bin/bash
# seq.sh "<cmd>"...  run console commands in order on a GL-ready console, power-cycling after any
# fence timeout.  XEGPU_OPTS overrides the module options; RELOAD=1 reloads before each command.
S="ssh -o ConnectTimeout=4 -o BatchMode=yes ${XBOX_SSH_OPTS:-} root@${XBOX_HOST:-10.0.0.206}"
CYCLE=$(dirname "$(readlink -f "$0")")/../irblaster/xbox-cycle
for c in "$@"; do
  until [ "$(timeout 150 $S "RELOAD=$RELOAD /opt/deqp/gl-ready.sh '${XEGPU_OPTS:-msm_compat=1}'" 2>/dev/null)" = ok ]; do $CYCLE force >/dev/null; done
  out=$(timeout 200 $S "export LD_LIBRARY_PATH=/opt/mesa-xenos/lib; timeout 60 $c 2>&1 | grep -v '^\$' | tail -3 | tr '\n' ' '; echo \"rc=\${PIPESTATUS[0]} timeouts=\$(cat /sys/module/xegpu/parameters/fence_timeouts)\"")
  echo "$c => ${out:-console froze}"
  case "$out" in *"timeouts=0"*) ;; *) $CYCLE force >/dev/null;; esac
done
