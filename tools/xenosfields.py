#!/usr/bin/env python3
"""xenosfields: decode register values in .rd dumps with Xenia's Xenos bitfield layouts
(src/xenia/gpu/registers.h) and flag bits set in fields Xenia marks as padding.

usage: xenosfields.py [-a] dumps...     (-a: print every decoded write, not only suspicious ones)"""
import os, re, sys, collections
sys.path.insert(0, os.path.dirname(__file__))
from xenoslint import merged, REGS

XG = os.environ.get("XENIA_GPU", os.path.join(os.path.dirname(__file__), "../xenia/src/xenia/gpu"))   # a xenia checkout
src = open(f"{XG}/registers.h").read()
LAYOUT = {}   # reg name -> [(field, lo, width)]
for m in re.finditer(r"union alignas\(uint32_t\) (\w+) \{(.*?)\n\};", src, re.S):
    name, body = m.group(1), m.group(2)
    fields, pos = [], 0
    for f in re.finditer(r"^\s*[\w:]+\s+(\w+)\s*:\s*(\d+);\s*(?://\s*\+(\d+))?", body, re.M):
        w = int(f.group(2))
        lo = int(f.group(3)) if f.group(3) else pos
        fields.append((f.group(1), lo, w)); pos = lo + w
    if fields:
        LAYOUT[name] = fields

def decode(name, v):
    out, pad = [], 0
    for f, lo, w in LAYOUT[name]:
        x = (v >> lo) & ((1 << w) - 1)
        if f.startswith("_pad") or f.startswith("pad"):
            if x: pad |= x << lo
        elif x:
            out.append(f"{f}={x:#x}")
    return out, pad

args = sys.argv[1:]
allp = "-a" in args
args = [a for a in args if a != "-a"]
R, _ = merged(args)
for r in sorted(R):
    n = REGS.get(r)
    if n not in LAYOUT:
        continue
    for v in R[r]:
        f, pad = decode(n, v)
        if pad or allp:
            print(f"{r:04x} {n:28s} {v:#010x}  {' '.join(f)}" + (f"   PAD BITS {pad:#x}" if pad else ""))
print(f"({len(LAYOUT)} Xenia layouts; registers written without a layout: "
      f"{sorted(REGS.get(r, hex(r)) for r in R if REGS.get(r) not in LAYOUT and not 0x4000 <= r < 0x4900)})")
