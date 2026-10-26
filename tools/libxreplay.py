#!/usr/bin/python3
"""Turn a libxe fake-backend recording (one arena buffer + N submits) into an xereplay blob.
Every command word whose value lies inside the arena is relocated (the fake arena sits at
0x1A000000 so such values are unambiguous). Optional splices from a freedreno .rd replace
or insert packets; see --help.
usage: libxreplay.py in.rd out.xrp [--submits a,b,..] [--dump]"""
import struct, sys

def load(path):
    d = open(path, 'rb').read(); i = 0; base = None; buf = None; cmds = []
    while i + 8 <= len(d):
        t, sz = struct.unpack('>II', d[i:i+8]); b = d[i+8:i+8+sz]; i += 8 + sz
        if t == 3: base = struct.unpack('>I', b[:4])[0]
        elif t == 12: buf = b
        elif t == 6: cmds.append(struct.unpack('>II', b[:8]))
    return base, buf, cmds

def main():
    a = sys.argv[1:]; inp, outp = a[0], a[1]
    base, buf, cmds = load(inp)
    if '--submits' in a: cmds = [cmds[int(k)] for k in a[a.index('--submits') + 1].split(',')]
    buf += b'\0' * (-len(buf) % 4)
    words = list(struct.unpack('>%dI' % (len(buf) // 4), buf))
    size = len(buf); relocs = set()
    def scan(addr, n):
        o = (addr - base) // 4; k = o
        while k < o + n:
            h = words[k]; t = h >> 30
            if t == 0:
                c = ((h >> 16) & 0x3fff) + 1
                for j in range(1, c + 1):
                    if base <= words[k + j] < base + size: relocs.add(k + j)
                k += 1 + c
            elif t == 3:
                c = ((h >> 16) & 0x3fff) + 1
                for j in range(1, c + 1):
                    if base <= words[k + j] < base + size: relocs.add(k + j)
                k += 1 + c
            else: k += 1
    dest = None
    for addr, n in cmds:
        scan(addr, n)
        o = (addr - base) // 4; k = o
        while k < o + n:
            h = words[k]
            if h >> 30 == 0 and (h & 0x7fff) == 0x2318 and ((h >> 16) & 0x3fff) >= 1 and words[k + 2]: dest = words[k + 2]
            k += 1 + (((h >> 16) & 0x3fff) + 1 if h >> 30 in (0, 3) else 0)
    FD_PS = [0x00000000, 0x1001c400, 0x20000002, 0x000f8000, 0x00000000, 0x02000000]   # freedreno solid-color PS
    if '--fdps' in a:
        for addr, n in cmds:
            o = (addr - base) // 4; k = o
            while k < o + n:
                h = words[k]; c = ((h >> 16) & 0x3fff) + 1 if h >> 30 in (0, 3) else 0
                if h >> 30 == 3 and ((h >> 8) & 0x7f) == 0x27 and (words[k + 1] & 3) == 1:
                    sa = (words[k + 1] & ~3) - base
                    words[sa // 4: sa // 4 + 6] = FD_PS; words[k + 2] = 6
                    print('fdps: PS at +%x replaced' % sa)
                k += 1 + c
    fetch0 = None; fetchreloc = []
    for addr, n in cmds:   # last type0 write covering 0x48be (fetch const 31, third vertex fetch = libxenon stream 0)
        o = (addr - base) // 4; k = o
        while k < o + n:
            h = words[k]; c = ((h >> 16) & 0x3fff) + 1 if h >> 30 in (0, 3) else 0
            if h >> 30 == 0 and (h & 0x7fff) <= 0x48be < (h & 0x7fff) + c - 1:
                j = 0x48be - (h & 0x7fff); fetch0 = [words[k + 1 + j], words[k + 2 + j]]
            k += 1 + c
    if '--fdvb' in a:   # freedreno's 3-vertex float3 buffer and its fetch constant value (size 9 dwords, endian 2)
        f32 = lambda x: struct.unpack('>I', struct.pack('>f', x))[0]
        vbw = [f32(v) for v in (-1, 1, 1, 1, 1, 1, -1, -1, 1)]
        vbaddr = base + len(words) * 4; words += vbw + [0] * ((-len(vbw)) % 16)
        fetch0 = [vbaddr | 3, int(a[a.index('--fdvb') + 1], 16)]
        size = len(words) * 4
    if '--fdfetch' in a: print('stream 0 fetch constant', ['%08x' % x for x in fetch0])
    if '--draw' in a:   # replace each indexed draw (DRAW_INDX with index buffer) with an auto-index draw, NOP-padded
        init = int(a[a.index('--draw') + 1], 16); pred = 0 if '--nopred' in a else None
        for addr, n in cmds:
            o = (addr - base) // 4; k = o
            while k < o + n:
                h = words[k]; c = ((h >> 16) & 0x3fff) + 1 if h >> 30 in (0, 3) else 0
                if h >> 30 == 3 and ((h >> 8) & 0x7f) == 0x22 and c == 4:
                    p0 = h & 1 if pred is None else 0
                    words[k:k+5] = [0xc0012200 | p0, 0, init, 0x80000000, 0x80000000]
                    if '--setjson' in a:   # registers written right before the draw: IB = [writes..., draw]
                        import json
                        regs = {int(r, 16): v for r, v in json.load(open(a[a.index('--setjson') + 1])).items()}
                        skip = set(int(x, 16) for x in a[a.index('--skip') + 1].split(',')) if '--skip' in a else set()
                        ib = []
                        for r, v in sorted(regs.items()):
                            if r in skip: continue
                            if '--setconst' in a and r >= 0x2000: ib += [0xc0012d00, 0x00040000 | (r - 0x2000), v]   # SET_CONSTANT type 4
                            else: ib += [r, v]          # type0, one register
                        if '--fdfetch' in a:   # copy the cube's stream-0 fetch constant (0x48be/0x48bf) into slot 26 via SET_CONSTANT type 1
                            ib += [0xc0022d00, 0x0001009c, fetch0[0], fetch0[1]]; fetchreloc.append(len(ib) - 2)
                        ib += [0xc0012200, 0, init]
                        ibaddr2 = base + len(words) * 4
                        for fr in fetchreloc: relocs.add(len(words) + fr)
                        words += ib + [0] * ((-len(ib)) % 8)
                        words[k:k+3] = [0xc0013f00, ibaddr2, len(ib)]; relocs.add(k + 1)
                    relocs.discard(k + 3); print('draw: word %d -> auto-index %08x pred %d' % (k, init, p0))
                k += 1 + c
    size = len(words) * 4
    if '--fdvs' in a:   # freedreno's solid VS (fetch retargeted to libxenon's stream 0 = const 31 sel 2, stride 12)
        vs = [0x00000000, 0x1002c400, 0x10000003, 0x00000000, 0x1003c200, 0x20000002,
              0x19a80000, 0x00392a88, 0x0000000c, 0x000f803e, 0x00000000, 0xc2000000]
        if '--fdfetch' not in a:
            vs[6] = (vs[6] & ~(0x7f << 20)) | (31 << 20) | (2 << 25)
        if '--fdvb' in a: vs[8] = 3          # stride 3 dwords
        if '--fdvsparam' in a:
            vs[4] = 0x2003c200; vs += [0x000f8000, 0x00000000, 0xc2000000]
        if '--fdvsorder' in a:   # libxenon CF order: EXEC fetch, ALLOC pos, EXEC pos, ALLOC param, EXEC_END params
            npar = int(a[a.index('--fdvsorder') + 1])
            pair = lambda E, O: [E & 0xffffffff, (E >> 32) | ((O & 0xffff) << 16), O >> 16]
            cf = [0x100000031003, 0xc20000000000, 0x100000001004, 0xc40000000000, 0x200000000005 | (npar << 12), 0]
            vs = pair(cf[0], cf[1]) + pair(cf[2], cf[3]) + pair(cf[4], cf[5]) + vs[6:12]
            for j in range(npar): vs += [0x000f8000 | j, 0x00000000, 0xc2000000]
        ibw = [0xc0002b00 | ((len(vs) + 1) << 16), 0, len(vs)] + vs
        ibaddr = base + len(words) * 4; words += ibw + [0] * ((-len(ibw)) % 8)
        for addr, n in cmds:
            o = (addr - base) // 4; k = o
            while k < o + n:
                h = words[k]; c = ((h >> 16) & 0x3fff) + 1 if h >> 30 in (0, 3) else 0
                if h >> 30 == 3 and ((h >> 8) & 0x7f) == 0x27 and (words[k + 1] & 3) == 0:
                    words[k:k+3] = [0xc0013f00, ibaddr, len(ibw)]; relocs.add(k + 1)
                    print('fdvs: VS IM_LOAD at word %d -> IB with freedreno VS' % k)
                k += 1 + c
        size = len(words) * 4
    if '--immps' in a:   # PS through IM_LOAD_IMMEDIATE (as freedreno does) via an IB in place of the IM_LOAD
        ibw = [0xc0072b00, 1, 6] + FD_PS     # IM_LOAD_IMMEDIATE type 1 (pixel), start 0, 6 dwords
        if '--ibmemps' in a:                 # control: same IB wrapper, but the IB holds the original IM_LOAD
            ibw = None
        ibaddr = base + len(words) * 4
        if ibw: words += ibw + [0] * ((-len(ibw)) % 8)
        for addr, n in cmds:
            o = (addr - base) // 4; k = o
            while k < o + n:
                h = words[k]; c = ((h >> 16) & 0x3fff) + 1 if h >> 30 in (0, 3) else 0
                if h >> 30 == 3 and ((h >> 8) & 0x7f) == 0x27 and (words[k + 1] & 3) == 1:
                    if ibw is None:   # copy this IM_LOAD into the IB (its address word is relocated there)
                        ib2 = base + len(words) * 4; words += words[k:k+3] + [0] * 5; relocs.add((ib2 - base) // 4 + 1)
                        words[k:k+3] = [0xc0013f00, ib2, 3]; relocs.add(k + 1); relocs.discard(k + 1) if False else None
                        print('ibmemps: IM_LOAD at word %d moved into an IB' % k); k += 1 + c; continue
                    words[k:k+3] = [0xc0013f00, ibaddr, len(ibw)]; relocs.add(k + 1)
                    print('immps: IM_LOAD at word %d -> IB' % k)
                k += 1 + c
        size = len(words) * 4
    if '--prefixframe' in a:   # run raw words (freedreno's preamble) at the start of every frame submit (not the init)
        pre = list(struct.unpack('>%dI' % (len(open(a[a.index('--prefixframe') + 1], 'rb').read()) // 4),
                                 open(a[a.index('--prefixframe') + 1], 'rb').read()))
        for addr, n in cmds[1:]:
            k = (addr - base) // 4; h = words[k]
            c = ((h >> 16) & 0x3fff) + 1
            assert h >> 30 == 0 and c + 1 >= 3, 'first frame packet must be a type0 write'
            moved = words[k:k + 1 + c]
            ibaddr3 = base + len(words) * 4
            for j in range(1 + c):
                if k + j in relocs: relocs.discard(k + j); relocs.add(len(words) + len(pre) + j)
            ibw = pre + moved; words += ibw + [0] * ((-len(ibw)) % 8)
            words[k:k + 1 + c] = [0xc0013f00, ibaddr3, len(ibw)] + [0x80000000] * (c - 2)
            relocs.add(k + 1)
            print('prefixframe: %d words before the frame at +%x' % (len(pre), addr - base))
        size = len(words) * 4
    entry = len(words)
    nentry = 0
    for addr, n in cmds:   # inline the submits into one buffer: kernel ring -> entry (IB1) -> at most one more level
        o = (addr - base) // 4
        for j in range(n):
            if o + j in relocs: relocs.add(len(words))
            words.append(words[o + j])
        nentry += n
    for i in relocs: words[i] -= base
    words += [0] * 16
    rb = (dest - base) if dest else 0
    hdr = [0x58525031, len(words), len(relocs), entry * 4, nentry, rb, 64 * 64 * 4 if dest else 0]
    with open(outp, 'wb') as f:
        f.write(struct.pack('>%dI' % len(hdr), *hdr)); f.write(struct.pack('>%dI' % len(words), *words))
        f.write(struct.pack('>%dI' % len(relocs), *sorted(relocs)))
    print('%s: %d submits, %d relocs, arena %d KB, resolve dest +%x' % (outp, len(cmds), len(relocs), len(words) // 256, rb))

main()
