#!/usr/bin/python3
# detile a grab.py dump into a PNG
import struct, sys
from PIL import Image
d = open(sys.argv[1], "rb").read()
addr, w, h = struct.unpack(">III", d[:12]); px = d[12:]
img = Image.new("RGB", (w, h)); p = img.load()
for y in range(h):
    for x in range(w):
        ndx = ((((y & ~31) * w) + (x & ~31) * 32) + (((x & 3) + ((y & 1) << 2) + ((x & 28) << 1) + ((y & 30) << 5)) ^ ((y & 8) << 2)))
        b, g, r = px[ndx * 4], px[ndx * 4 + 1], px[ndx * 4 + 2]
        p[x, y] = (r, g, b)
img.save(sys.argv[2]); print("saved", sys.argv[2], w, h)
