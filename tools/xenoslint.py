#!/usr/bin/env python3
"""xenoslint: list registers and PM4 opcodes used by .rd dumps, checked against Xenia's
Xenos register table (src/xenia/gpu/register_table.inc) and PM4 opcode list (xenos.h).

usage: xenoslint.py [--diff A.rd... -- B.rd...] dumps...
Prints registers written that Xenia does not know (likely Adreno-only -> lands on some other
Xenos register), and with --diff, the registers/opcodes/values used by B but never by A."""
import os, re, sys, collections
sys.path.insert(0, os.path.dirname(__file__))
from rdstep import Rd, parse, walk

XG = os.environ.get("XENIA_GPU", os.path.join(os.path.dirname(__file__), "../xenia/src/xenia/gpu"))   # a xenia checkout
REGS = {}
for m in re.finditer(r"XE_GPU_REGISTER\((0x[0-9A-Fa-f]+),\s*\w+,\s*(\w+)\)", open(f"{XG}/register_table.inc").read()):
    REGS[int(m.group(1), 16)] = m.group(2)
OPS = {}
for m in re.finditer(r"PM4_(\w+)\s*=\s*(0x[0-9a-fA-F]+)", open(f"{XG}/xenos.h").read()):
    OPS[int(m.group(2), 16)] = m.group(1)

def rname(r): return REGS.get(r, f"?{r:04x}")

def usage(path):
    """-> {reg: Counter(values)}, Counter(opcodes)"""
    rd = Rd(path)
    regs, ops = collections.defaultdict(collections.Counter), collections.Counter()
    for cs, cn in rd.cmds:
        try:
            s, o = rd.find(cs)
        except KeyError:
            continue
        for p in walk(parse(rd, s, o, cn)):
            if p[0] == 't0':
                for j, v in enumerate(p[2]):
                    regs[p[1] + j][v] += 1
            elif p[0] == 't3':
                ops[p[1]] += 1
                b = p[2]
                if p[1] == 0x2d and b and (b[0] >> 16) == 4:      # SET_CONSTANT, registers
                    for j, v in enumerate(b[1:]):
                        regs[0x2000 + (b[0] & 0x7ff) + j][v] += 1
            elif p[0] == 'ib':
                ops[p[1]] += 1
    return regs, ops

def merged(paths):
    R, O = collections.defaultdict(collections.Counter), collections.Counter()
    for p in paths:
        r, o = usage(p)
        for k, c in r.items(): R[k].update(c)
        O.update(o)
    return R, O

def main(a):
    if "--diff" in a:
        i, j = a.index("--diff"), a.index("--")
        A, B = merged(a[i+1:j]), merged(a[j+1:])
        print("== opcodes used by B but never by A")
        for op in sorted(set(B[1]) - set(A[1])):
            print(f"  {op:02x} {OPS.get(op, '?')}  x{B[1][op]}")
        print("== registers written by B but never by A")
        for r in sorted(set(B[0]) - set(A[0])):
            print(f"  {r:04x} {rname(r):40s} values {[hex(v) for v in list(B[0][r])[:6]]}")
        print("== registers both write, with values only B uses")
        for r in sorted(set(B[0]) & set(A[0])):
            new = [v for v in B[0][r] if v not in A[0][r]]
            if new and not (0x4000 <= r < 0x4900):      # skip shader constants
                print(f"  {r:04x} {rname(r):40s} B-only {[hex(v) for v in new[:6]]}  A has {[hex(v) for v in list(A[0][r])[:4]]}")
        return
    R, O = merged(a)
    print("== opcodes"); [print(f"  {op:02x} {OPS.get(op, '?'):28s} x{n}") for op, n in sorted(O.items())]
    print("== registers Xenia does not know")
    for r in sorted(R):
        if r not in REGS and not (0x4000 <= r < 0x4900):
            print(f"  {r:04x}  values {[hex(v) for v in list(R[r])[:6]]}")

if __name__ == "__main__":
    main(sys.argv[1:])
