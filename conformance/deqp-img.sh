#!/bin/bash
# deqp-img.sh <case> <outdir>: run one dEQP case on the console with image logging and
# extract every logged image as PNG into <outdir> (names: <ImageSet>_<Image>.png)
S="ssh -o ConnectTimeout=4 -o BatchMode=yes ${XBOX_SSH_OPTS:-} root@${XBOX_HOST:-10.0.0.206}"
case=$1; out=$2; mkdir -p $out
timeout 150 $S "cd /opt/deqp && LD_LIBRARY_PATH=/opt/mesa-xenos/lib EGL_PLATFORM=surfaceless timeout 100 ./deqp-gles2 --deqp-archive-dir=/opt/deqp -n $case --deqp-surface-type=pbuffer --deqp-surface-width=256 --deqp-surface-height=256 --deqp-gl-config-name=rgba8888d24s8ms0 --deqp-log-filename=/opt/deqp/runs/img.qpa >/dev/null 2>&1; echo timeouts=\$(cat /sys/module/xegpu/parameters/fence_timeouts)"
python3 - /srv/nfs/xenon/opt/deqp/runs/img.qpa $out <<'PY'
import sys, re, base64
q, out = sys.argv[1], sys.argv[2]
t = open(q, errors="replace").read()
for m in re.finditer(r'<Image Name="([^"]*)"[^>]*CompressionMode="PNG"[^>]*>(.*?)</Image>', t, re.S):
    p = f"{out}/{m.group(1)}.png"
    open(p, "wb").write(base64.b64decode(m.group(2)))
    print(p)
for m in re.finditer(r"<Text>([^<]*(?:fail|Fail|differ|error|Error)[^<]*)</Text>|<Result StatusCode=\"(\w+)\">([^<]*)", t):
    print(m.group(0)[:200])
PY
