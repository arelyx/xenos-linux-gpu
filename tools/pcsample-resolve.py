#!/usr/bin/env python3
# pcsample-resolve.py SAMPLES LIB... : flat profile from tools/pcsample.c output.
# Run on the host against the NFS root; LIB paths are as seen on the console.
# resolve pcsample output ("main lib fileoff") to functions: file offset ->
# vaddr through the LOAD segments, then the nearest preceding symbol
import subprocess, bisect, collections, sys, re
samples = [l.split() for l in open(sys.argv[1]) if l.startswith('1 ')]
def segs(path):
    out = subprocess.run(['readelf', '-lW', path], capture_output=True, text=True).stdout
    r = []
    for l in out.splitlines():
        p = l.split()
        if p and p[0] == 'LOAD':
            r.append((int(p[1], 16), int(p[2], 16), int(p[4], 16)))  # off, vaddr, filesz
    return r
def syms(path):
    # ELFv1 (big-endian ppc64): a function symbol is a descriptor in .opd
    # whose first doubleword is the code address
    secs = subprocess.run(['readelf', '-SW', path], capture_output=True, text=True).stdout
    opd = None
    for l in secs.splitlines():
        m = re.search(r'\]\s+\.opd\s+\S+\s+([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)', l)
        if m: opd = tuple(int(x, 16) for x in m.groups())
    data = open(path, 'rb').read()
    s = set()
    for flag in ([], ['-D']):
        out = subprocess.run(['nm', '-n', '--defined-only'] + flag + [path], capture_output=True, text=True).stdout
        for l in out.splitlines():
            p = l.split()
            if len(p) < 3: continue
            a = int(p[0], 16)
            if opd and opd[0] <= a < opd[0] + opd[2]:
                fo = a - opd[0] + opd[1]
                s.add((int.from_bytes(data[fo:fo + 8], 'big'), p[2]))
            elif p[1].lower() == 't' and '.plt_call.' not in p[2]:
                s.add((a, p[2]))
    s = sorted(s)
    return [a for a, n in s], [n for a, n in s]
for lib in sys.argv[2:]:
    path = '/srv/nfs/xenon' + lib
    sg = segs(path); addrs, names = syms(path)
    c = collections.Counter()
    for _, l, off in samples:
        if l != lib: continue
        o = int(off, 16); va = o
        for so, sv, sz in sg:
            if so <= o < so + sz: va = o - so + sv; break
        i = bisect.bisect_right(addrs, va) - 1
        c[names[i] if i >= 0 else '?'] += 1
    print(f'== {lib.split("/")[-1]} ({sum(c.values())} of {len(samples)} main-thread samples)')
    for n, k in c.most_common(16): print(f'{k:5d} {100*k/len(samples):5.1f}%  {n}')
