#!/usr/bin/python3
"""Register state at each draw: replay type0 / SET_CONSTANT(type 4) writes from .rd dumps.
statediff.py A.rd[:nth_draw] B.rd[:nth_draw] [--init init.rd]  -> registers that differ at that draw"""
import os
import gzip, struct, sys, re
regs = {}
for m in re.finditer(r'XE_GPU_REGISTER\((0x[0-9A-Fa-f]+),\s*\w+,\s*(\w+)\)', open(os.environ.get('XENIA_GPU', 'xenia/src/xenia/gpu') + '/register_table.inc').read()):
    regs[int(m.group(1), 16)] = m.group(2)

def load(path):
    op = gzip.open if path.endswith('.gz') or open(path, 'rb').read(2) == b'\x1f\x8b' else open
    d = op(path, 'rb').read(); i = 0; bufs = {}; cmds = []; pending = None
    while i + 8 <= len(d):
        t, sz = struct.unpack('>II', d[i:i+8]); i += 8; body = d[i:i+sz]; i += sz
        if t == 3: pending = struct.unpack('>I', body[:4])[0]
        elif t == 12 and pending is not None: bufs[pending] = body; pending = None
        elif t == 6: cmds.append(struct.unpack('>II', body[:8]))
    return bufs, cmds

def words(bufs, a, n):
    for b, d in bufs.items():
        if b <= a < b + len(d):
            o = a - b; return list(struct.unpack('>%dI' % n, d[o:o+4*n]))
    return []

def run(path, upto, st):
    bufs, cmds = load(path); draws = [0]
    def go(a, n, depth=0):
        w = words(bufs, a, n); k = 0
        while k < len(w):
            h = w[k]; t = h >> 30
            if t == 0:
                c = ((h >> 16) & 0x3fff) + 1; r = h & 0x7fff
                one = (h >> 15) & 1
                for j in range(c): st[r if one else r + j] = w[k+1+j]
                k += 1 + c
            elif t == 3:
                c = ((h >> 16) & 0x3fff) + 1; op = (h >> 8) & 0x7f; b = w[k+1:k+1+c]
                if op == 0x2d and b and (b[0] >> 16) == 4:
                    for j, v in enumerate(b[1:]): st[0x2000 + (b[0] & 0x7ff) + j] = v
                elif op == 0x2d and b and (b[0] >> 16) == 1:
                    for j, v in enumerate(b[1:]): st[0x4800 + (b[0] & 0x7ff) + j] = v
                elif op in (0x32, 0x3f, 0x37) and depth < 3:
                    if go(b[0], b[1], depth + 1): return True
                elif op in (0x22, 0x36):
                    st['draw'] = (op, b)
                    if op == 0x22 or not only22: draws[0] += 1
                    if draws[0] == upto: return True
                elif op == 0x3b: st['INVALIDATE_STATE'] = b[0]
                elif op in (0x27, 0x2b): st.setdefault('shaders', []).append((op, b[:2]))
                k += 1 + c
            else: k += 1
        return False
    for a, n in cmds:
        if go(a, n): return st
    return st

def spec(s):
    p, _, n = s.partition(':'); return p, int(n) if n else 10**9

only22 = '--only22' in sys.argv
args = [a for a in sys.argv[1:] if not a.startswith('--')]
init = sys.argv[sys.argv.index('--init') + 1] if '--init' in sys.argv else None
if init: args.remove(init)
sts = []
for a in args:
    st = {}
    if init: run(init, 10**9, st)
    p, n = spec(a); sts.append(run(p, n, st))
A, B = sts
keys = sorted({k for k in list(A) + list(B) if isinstance(k, int) and (0x2000 <= k < 0x4000 or k in (0x0d00, 0x0d01, 0x0d02, 0x0d04, 0x0e42, 0x0c85, 0x0f01))})
for k in keys:
    a, b = A.get(k), B.get(k)
    if a != b:
        f = lambda v: '--------' if v is None else '%08x' % v
        print('%04x %-28s %s %s' % (k, regs.get(k, '?'), f(a), f(b)))
for k in ('draw', 'INVALIDATE_STATE', 'shaders'):
    print(k, 'A:', A.get(k), '\n   B:', B.get(k))
