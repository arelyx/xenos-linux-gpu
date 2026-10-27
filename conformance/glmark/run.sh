#!/bin/bash
# run.sh <out>: each default glmark2 scene in its own process, off-screen 1280x720, 5 s each.
# A scene that times out or leaves a fence timeout is recorded as HANG and the console is power-cycled.
out=${1:-glmark.txt}
SSH="ssh -o ConnectTimeout=5 -o BatchMode=yes ${XBOX_SSH_OPTS} root@${XBOX_HOST:-10.0.0.206}"
CYCLE=${XBOX_CYCLE:-$(dirname "$0")/../../irblaster/xbox-cycle}
scenes=(build:use-vbo=false build:use-vbo=true texture:texture-filter=nearest texture:texture-filter=linear
 texture:texture-filter=mipmap shading:shading=gouraud shading:shading=blinn-phong-inf shading:shading=phong
 shading:shading=cel bump:bump-render=high-poly bump:bump-render=normals bump:bump-render=height
 "effect2d:kernel=0,1,0;1,-4,1;0,1,0;" "effect2d:kernel=1,1,1,1,1;1,1,1,1,1;1,1,1,1,1;"
 pulsar:light=false:quads=5:texture=false desktop:blur-radius=5:effect=blur:passes=1:separable=true:windows=4
 desktop:effect=shadow:windows=4 buffer:columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=map
 buffer:columns=200:interleave=false:update-dispersion=0.9:update-fraction=0.5:update-method=subdata
 buffer:columns=200:interleave=true:update-dispersion=0.9:update-fraction=0.5:update-method=map
 ideas:speed=duration jellyfish terrain shadow refract conditionals:fragment-steps=0:vertex-steps=0
 conditionals:fragment-steps=5:vertex-steps=0 conditionals:fragment-steps=0:vertex-steps=5
 function:fragment-complexity=low:fragment-steps=5 function:fragment-complexity=medium:fragment-steps=5
 loop:fragment-loop=false:fragment-steps=5:vertex-steps=5 loop:fragment-steps=5:fragment-uniform=false:vertex-steps=5
 loop:fragment-steps=5:fragment-uniform=true:vertex-steps=5)
for s in "${scenes[@]}"; do
  grep -qF "[$s]" "$out.done" 2>/dev/null && continue
  until $SSH true 2>/dev/null; do sleep 10; done
  $SSH "systemctl stop xfce-glamor 2>/dev/null; pkill -x Xorg; /opt/deqp/gl-ready.sh >/dev/null; t0=\$(cat /sys/module/xegpu/parameters/fence_timeouts)
    r=\$(JSIMD_FORCENONE=1 LD_LIBRARY_PATH=/opt/mesa-xenos/lib timeout 60 glmark2-es2-drm --off-screen -b '$s:duration=5' 2>&1 | grep -E '^\[|rror|bort' | tail -2)
    rc=\$?; t1=\$(cat /sys/module/xegpu/parameters/fence_timeouts); echo \"\$r\"; [ \$t1 != \$t0 ] && echo FENCE_TIMEOUT" > /tmp/glm.$$ 2>&1
  r=$(cat /tmp/glm.$$)
  if grep -q FENCE_TIMEOUT /tmp/glm.$$ || ! grep -q "FPS" /tmp/glm.$$ && [ -z "$r" ]; then
    echo "[$s] HANG $(echo $r | tr '\n' ' ')" >> "$out"
    "$CYCLE" force
  elif grep -q FPS /tmp/glm.$$; then echo "$(grep FPS /tmp/glm.$$ | head -1)" >> "$out"
  else echo "[$s] FAIL $(echo $r | tr '\n' ' ')" >> "$out"; fi
  echo "$s" | sed "s/^/[/; s/$/]/" >> "$out.done"; tail -1 "$out"
done
rm -f /tmp/glm.$$
