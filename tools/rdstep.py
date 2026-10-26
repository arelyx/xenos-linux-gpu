#!/usr/bin/python3
"""Turn a freedreno .rd dump into a replayable blob for xereplay, applying edits.

Usage: rdstep.py in.rd out.xrp edit[,edit...]   (edit list may be empty: "-")

Every buffer in the dump gets a 64 KB slot in one arena; address fields that the
command stream is known to carry (IB pointers, MEM_WRITE, fetch constants,
RB_COPY_DEST_BASE, EVENT_WRITE_SHD) become arena relocations. Edits rewrite the
parsed packets, so each step differs from the last by one named change.
"""
import gzip, struct, sys

SLOT = 0x10000

def u32s(b): return list(struct.unpack('>%dI' % (len(b) // 4), b[:len(b) // 4 * 4]))

class Rd:
    def __init__(self, path):
        d = gzip.open(path).read(); i = 0; pending = None
        self.bufs = []          # [(old_base, words)]
        self.cmds = []
        while i + 8 <= len(d):
            t, sz = struct.unpack('>II', d[i:i+8]); i += 8; body = d[i:i+sz]; i += sz
            if t == 3: pending = struct.unpack('>I', body[:4])[0]
            elif t == 12 and pending is not None:
                self.bufs.append((pending, u32s(body))); pending = None
            elif t == 6: self.cmds.append(struct.unpack('>II', body[:8]))
        self.slot_of = {b: k for k, (b, _) in enumerate(self.bufs)}
    def find(self, addr):
        for k, (b, w) in enumerate(self.bufs):
            if b <= addr < b + 4 * len(w): return k, (addr - b) // 4
        raise KeyError('%08x not in dump' % addr)

def T3(op, body, pred=0): return [0xc0000000 | ((len(body) - 1) << 16) | (op << 8) | pred] + body
def T0(reg, vals): return [((len(vals) - 1) << 16) | reg] + vals

# packets: ('t0', reg, [vals]) | ('t3', op, [body], hdr) | ('ib', op, slot, [packets]) | ('raw', [words])
def parse(rd, slot, off, n, depth=0):
    w = rd.bufs[slot][1][off:off+n]; out = []; k = 0
    while k < len(w):
        h = w[k]; typ = h >> 30
        if typ == 0:
            cnt = ((h >> 16) & 0x3fff) + 1
            out.append(['t0', h & 0x7fff, w[k+1:k+1+cnt]]); k += 1 + cnt
        elif typ == 3:
            cnt = ((h >> 16) & 0x3fff) + 1; op = (h >> 8) & 0x7f; body = w[k+1:k+1+cnt]
            if op in (0x32, 0x3f, 0x37) and depth < 2:   # INDIRECT_BUFFER(_PFD)
                s, o = rd.find(body[0])
                out.append(['ib', op, s, parse(rd, s, o, body[1], depth + 1), o])
            else:
                out.append(['t3', op, body, h])
            k += 1 + cnt
        else:
            out.append(['raw', [h]]); k += 1
    return out

def walk(pk):
    for p in pk:
        yield p
        if p[0] == 'ib': yield from walk(p[3])

def set_ctx(pk, reg, fn):
    """apply fn(old) to every write of context register reg (0x2000-based)"""
    n = 0
    for p in walk(pk):
        if p[0] == 't3' and p[1] == 0x2d and p[2] and (p[2][0] >> 16) == 4:
            idx = p[2][0] & 0x7ff
            for j in range(1, len(p[2])):
                if 0x2000 + idx + j - 1 == reg: p[2][j] = fn(p[2][j]); n += 1
    return n

class Out:
    def __init__(self, rd):
        self.rd = rd
        self.words = []
        for b, w in rd.bufs: self.words += w + [0] * (SLOT // 4 - len(w))
        self.relocs = set()
        self.extra = len(rd.bufs)
    def new_slot(self, words):
        s = self.extra; self.extra += 1
        self.words += words + [0] * (SLOT // 4 - len(words)); return s
    def rel(self, addr):
        s, o = self.rd.find(addr); return s * SLOT + o * 4
    def emit(self, pk, slot, off):
        """serialize packets into slot at word offset off, return length in dwords"""
        out = []
        for p in pk:
            if ALIGN and p[0] in ('t0', 't0r') and 0x4000 <= p[1] < 0x4a00:
                # libxenon Xe_pAlign: constant data must start 16-byte aligned (type0 header at dword 3 mod 4)
                while (off + len(out)) % 4 != 3: out.append(0x80000000)
            if ALIGN and p[0] == 't3' and p[1] == 0x2d and p[2] and (p[2][0] >> 16) in (0, 1, 2, 3):
                # SET_CONSTANT: header + offset dword, so the header goes at 2 mod 4
                while (off + len(out)) % 4 != 2: out.append(0x80000000)
            if p[0] == 't0': out += T0(p[1], p[2])
            elif p[0] == 't0r':
                for j in p[3]: self.relocs.add(slot * SLOT // 4 + off + len(out) + 1 + j)
                out += T0(p[1], p[2])
            elif p[0] == 'raw': out += p[1]
            elif p[0] == 'ib':
                n = self.emit(p[3], p[2], p[4])
                out += [0xc0013f00 if p[1] == 0x3f else 0xc0000000 | (1 << 16) | (p[1] << 8)]
                self.relocs.add(slot * SLOT // 4 + off + len(out)); out += [p[2] * SLOT + p[4] * 4, n]
            else:
                op, body, h = p[1], list(p[2]), p[3]
                at = slot * SLOT // 4 + off + len(out) + 1
                if op == 0x27 and len(p) > 4 and p[4] == 'memps':
                    self.relocs.add(at)
                elif op == 0x3d:                       # MEM_WRITE addr
                    body[0] = self.relx(body[0]); self.relocs.add(at)
                elif op == 0x58:                     # EVENT_WRITE_SHD ev, addr|mode, data
                    body[1] = self.relx(body[1] & ~3) | (body[1] & 3); self.relocs.add(at + 1)
                elif op == 0x2d and body and (body[0] >> 16) == 1:   # fetch constants
                    idx = body[0] & 0x7ff
                    for j in range(1, len(body)):
                        d = idx + j - 1
                        # vertex fetch: dword pairs, type 3 in the low bits of the even dword
                        if (body[j] & 3) == 3 and d % 2 == 0 and body[j] > 0x10000:
                            try:
                                body[j] = self.relx(body[j] & ~3) | 3; self.relocs.add(at + j)
                            except KeyError:
                                pass   # not an address of a dumped buffer (texture constant word)
                elif op == 0x2d and body and (body[0] >> 16) == 4:
                    idx = body[0] & 0x7ff
                    for j in range(1, len(body)):
                        if 0x2000 + idx + j - 1 == 0x2319 and body[j]:      # RB_COPY_DEST_BASE
                            body[j] = self.dest_override if getattr(self, 'dest_override', None) is not None else self.relx(body[j])
                            self.relocs.add(at + j)
                hh = (h & 0xc000ffff) | ((len(body) - 1) << 16) if len(body) else h
                out += [hh] + body
        base = slot * SLOT // 4 + off
        self.words[base:base + len(out)] = out
        return len(out)
    def relx(self, a):
        return self.rel(a)

LIBX_VS = [0x10011002, 0x00001200, 0xc4000000, 0x00000000, 0x1003c200, 0x22000000,
           0x00080000, 0x00253b48, 0x00000002, 0xc80f803e, 0x00000000, 0xc2000000,
           0x00000000, 0x00000000, 0x00000000]

def f32(x): return struct.unpack('>I', struct.pack('>f', x))[0]

def libx_resolve(out, dest_rel, w, h, dest_info, clear, ccolor=0xff0000ff):
    """libxenon Xe_ResolveInto, packet for packet (xe.c), for a w x h 8888 target"""
    vb = out.new_slot([f32(-.5), f32(-.5), f32(w - .5), f32(0), f32(w - .5), f32(h - .5)])
    p = []
    T = lambda reg, *v: p.append(['t0', reg, list(v)])
    T(0x2080, 0, 0, w | (h << 16))                       # Xe_pSetSurfaceClip
    T(0x2000, 160 | (160 << 18), 8, 0)                   # Xe_pSetEDRAMLayout: pitch 160, color base 8, depth 0
    T(0x2104, 0xf)
    T(0x210f, 0x44000000, 0x44000000, 0xc3b40000, 0x43b40000, 0x3f800000, 0)
    p.append(['t0r', 0x2318, [0x00100000 | (clear << 8), dest_rel, w | (h << 16), dest_info], [1]])
    T(0x231d, 0xffffff00); T(0x231e, ccolor); T(0x231f, ccolor)   # Xe_pSetClearValues
    T(0x2208, 6); T(0x2208, 6); T(0x2200, 0x8777); T(0x05c8, 0x20000)
    p.append(['t3', 0x3b, [0x100], 0xc0003b00])
    p.append(['t3', 0x2b, [0, len(LIBX_VS)] + LIBX_VS, 0xc0102b00])
    T(0x2180, 0x00010002, 0)
    T(0x2203, 0)
    T(0x2100, 0xffff, 0, 0)
    T(0x2204, 0x10000, 0x10000, 0x300)
    T(0x2312, 0xffff)
    T(0x200d, 0)
    p.append(['t0r', 0x4800, [vb * SLOT | 3, 0x1000001a, 0, 0, 0, 0], [0]])
    T(0x5000, 0, 0, 0)
    p.append(['t3', 0x36, [0x00030088], 0xc0003600])
    p.append(['t3', 0x46, [6], 0xc0004600])
    T(0x2007, 0)
    p.append(['t0r', 0x0a31, [0x03000100], []])
    p.append(['t0r', 0x0a2f, [w * h * 4, dest_rel], [1]])
    p.append(['t3', 0x3c, [3, 0xa31, 0, 0x80000000, 8], 0xc0043c00])
    T(0x057e, 0x10001); T(0x2318, 0); T(0x231b, 0)
    return p

ALIGN = False
def main():
    global ALIGN
    rd = Rd(sys.argv[1]); outp = sys.argv[2]
    edits = [e for e in (sys.argv[3] if len(sys.argv) > 3 else '-').split(',') if e and e != '-']
    lo, n = rd.cmds[-1]
    s, o = rd.find(lo)
    pk = parse(rd, s, o, n)
    out = Out(rd)
    ibs = [p for p in pk if p[0] == 'ib']
    # the resolve destination: last RB_COPY_DEST_BASE write
    dest = None
    for p in walk(pk):
        if p[0] == 't3' and p[1] == 0x2d and p[2] and (p[2][0] >> 16) == 4:
            idx = p[2][0] & 0x7ff
            for j in range(1, len(p[2])):
                if 0x2000 + idx + j - 1 == 0x2319 and p[2][j]: dest = p[2][j]
    dest_rel = out.rel(dest) if dest else 0
    guard_slots = 0
    if 'guard' in edits:   # resolve destination in the middle of 32 empty 64 KB slots, to see where a resolve writes
        first = out.extra
        for _ in range(32): out.new_slot([])
        dest_rel = (first + 16) * SLOT; out.dest_override = dest_rel; guard_slots = 32
        edits.remove('guard')
    for e in edits:
        if e == 'align': ALIGN = True
        elif e == 'noclear': pk.remove(ibs[0])
        elif e == 'noresolve': pk.remove(ibs[1])
        elif e == 'nofence': pk[:] = [p for p in pk if not (p[0] == 't3' and p[1] == 0x58)]
        elif e == 'fencefix':   # EVENT_WRITE_SHD fence -> WAIT_UNTIL idle + MEM_WRITE (byte-swapped value)
            for k in range(len(pk) - 1, -1, -1):
                p = pk[k]
                if p[0] == 't3' and p[1] == 0x58:
                    v = p[2][2]; v = struct.unpack('<I', struct.pack('>I', v))[0]
                    pk[k:k+1] = [['t0', 0x05c8, [0x20000]], ['t3', 0x3d, [p[2][1] & ~3, v], 0xc0013d00]]
        elif e == 'wfi2wu':     # CP_WAIT_FOR_IDLE -> libxenon's WAIT_UNTIL write
            for q in [pk] + [p[3] for p in walk(pk) if p[0] == 'ib']:
                for k, p in enumerate(q):
                    if p[0] == 't3' and p[1] == 0x26: q[k] = ['t0', 0x05c8, [0x20000]]
        elif e == 'stride4':    # Xenos vertex fetch stride/offset are dwords, freedreno wrote bytes
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x2b and p[2][0] == 0:
                    sh = p[2]
                    for i in range(2, (len(sh) - 2) // 3):   # past the two CF slots
                        w0, w2 = sh[2 + 3*i], sh[4 + 3*i]
                        if (w0 & 0x1f) == 0 and (w0 >> 19) & 1:
                            st, of = w2 & 0xff, (w2 >> 8) & 0x7fffff
                            sh[4 + 3*i] = (w2 & 0x80000000) | ((of // 4) << 8) | (st // 4)
                            print('stride4: slot %d stride %d->%d offset %d->%d' % (i, st, st // 4, of, of // 4))
        elif e.startswith('noreg'):   # noreg2209+220a+220b: drop single-register writes
            regs = [int(x, 16) for x in e[5:].split('+')]
            for q in [pk] + [p[3] for p in walk(pk) if p[0] == 'ib']:
                q[:] = [p for p in q if not (p[0] == 't3' and p[1] == 0x2d and len(p[2]) == 2
                        and (p[2][0] >> 16) == 4 and 0x2000 + (p[2][0] & 0x7ff) in regs)]
        elif e.startswith('set'):     # set2200=0: write a context register at the start of every IB
            r, v = e[3:].split('=')
            for p in walk(pk):
                if p[0] == 'ib': p[3].insert(0, ['t3', 0x2d, [0x00040000 | (int(r, 16) - 0x2000), int(v, 16)], 0xc0012d00])
        elif e.startswith('rep'):     # rep2181=4: replace every write of a context register
            r, v = e[3:].split('=')
            print(e, set_ctx(pk, int(r, 16), lambda old, v=int(v, 16): v))
        elif e == 'tri':        # RECTLIST -> TRIANGLELIST
            for p in walk(pk):
                if p[0] == 't3' and p[1] in (0x22, 0x36): p[2][-1] = (p[2][-1] & ~0x3f) | 4
        elif e == 'prefetch0':  # vertex fetch prefetch_count (dword0 bits 27-29) -> 0, as libxenon's shaders
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x2b and p[2][0] == 0:
                    sh = p[2]
                    for i in range(2, (len(sh) - 2) // 3):
                        w0 = sh[2 + 3*i]
                        if (w0 & 0x1f) == 0 and (w0 >> 19) & 1: sh[2 + 3*i] = w0 & ~(7 << 27)
        elif e == 'psregs':
            print('psregs', set_ctx(pk, 0x2180, lambda v: v & ~0x0000c0c0))
        elif e == 'xvs':   # clear IB: libxenon's resolve VS (screen-space float2 passthrough, fetch slot 0), VTE 0x300
            ib = ibs[0][3]
            vb = out.new_slot([f32(-.5), f32(-.5), f32(64 - .5), f32(0), f32(64 - .5), f32(64 - .5)])
            for k, p in enumerate(ib):
                if p[0] == 't3' and p[1] == 0x2b and p[2][0] == 0:
                    ib[k] = ['t3', 0x2b, [0, len(LIBX_VS)] + LIBX_VS, 0xc0002b00]
            for k in range(len(ib) - 1, -1, -1):
                if ib[k][0] == 't3' and ib[k][1] in (0x22, 0x36):
                    ib.insert(k, ['t0r', 0x4800, [vb * SLOT | 3, 0x1000001a], [0]])
            print('xvs', set_ctx(pk, 0x2206, lambda v: 0x300), set_ctx(pk, 0x2180, lambda v: 0x10030002))
        elif e == 'memps':   # PS through IM_LOAD from memory (as libxenon does) instead of IM_LOAD_IMMEDIATE
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x2b and p[2][0] == 1:
                    code = p[2][2:]; slot = out.new_slot(code)
                    p[1] = 0x27; p[2] = [slot * SLOT | 1, len(code)]; p[3] = 0xc0012700; p.append('memps')
        elif e == 'ib3f':      # INDIRECT_BUFFER_PFD (0x37) -> INDIRECT_BUFFER (0x3f), the only form libxenon uses
            for p in walk(pk):
                if p[0] == 'ib': p[1] = 0x3f
        elif e.startswith('inval'):   # inval300: every INVALIDATE_STATE mask -> 0x300 (what libxenon's frames use)
            v = int(e[5:], 16)
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x3b: p[2] = [v]
        elif e == 'libxmisc':  # before each draw: 0x200d = 0, 0x5000-0x5002 = 0, VPORT_Z scale 1 / offset 0, as libxenon writes
            for q in [p[3] for p in walk(pk) if p[0] == 'ib']:
                for k in range(len(q) - 1, -1, -1):
                    if q[k][0] == 't3' and q[k][1] in (0x22, 0x36):
                        q[k:k] = [['t0', 0x200d, [0]], ['t0', 0x5000, [0, 0, 0]], ['t0', 0x2113, [0x3f800000, 0]]]
        elif e == 'vsparam':   # freedreno's solid VS allocates an interpolator but never exports one: add the export
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x2b and p[2][0] == 0 and p[2][1] == 12:
                    sh = p[2][2:]
                    assert sh[3:6] == [0x00000000, 0x1003c200, 0x20000002], sh[3:6]
                    sh[4] = (sh[4] & 0xffff0000) | 0xc200    # unchanged ALLOC position (low half)
                    sh[4] = (0x2003 << 16) | 0xc200          # CF3: EXEC_END address 3, count 2
                    sh += [0x000f8000, 0x00000000, 0xc2000000]   # slot 4: export r0 -> interpolator 0
                    p[2] = [0, len(sh)] + sh; p[3] = 0xc0002b00
                    print('vsparam: VS now exports position + param 0')
        elif e.startswith('vsorder'):  # rebuild freedreno's solid VS in libxenon's CF order:
            # EXEC fetch, ALLOC position, EXEC export pos, ALLOC param, EXEC_END export param(s)
            nparam = int(e[7:] or '1')
            def pair(E, O): return [E & 0xffffffff, (E >> 32) | ((O & 0xffff) << 16), O >> 16]
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x2b and p[2][0] == 0 and p[2][1] == 12:
                    old = p[2][2:]; fetch = old[6:9]; posx = old[9:12]
                    first = 3                      # 6 CFs = 3 slots
                    cf = [0x1000_00031000 | first, 0xc200_00000000, 0x1000_00001000 | (first + 1),
                          0xc400_00000000, 0x2000_00000000 | ((nparam) << 12) | (first + 2), 0]
                    sh = pair(cf[0], cf[1]) + pair(cf[2], cf[3]) + pair(cf[4], cf[5]) + fetch + posx
                    for j in range(nparam): sh += [0x000f8000 | j, 0x00000000, 0xc2000000]
                    p[2] = [0, len(sh)] + sh; p[3] = 0xc0002b00
                    print('vsorder: VS rebuilt, %d dwords, %d param export(s)' % (len(sh), nparam))
        elif e == 'alut0':     # ALU constants: SET_CONSTANT type 0 hangs Xenos (26 Oct); write 0x4000+ with type0 like libxenon
            for q in [pk] + [p[3] for p in walk(pk) if p[0] == 'ib']:
                for k, p in enumerate(q):
                    if p[0] == 't3' and p[1] == 0x2d and p[2] and (p[2][0] >> 16) == 0:
                        q[k] = ['t0', 0x4000 + (p[2][0] & 0x7ff), p[2][1:]]
        elif e == 'alublock':  # ALU constants as whole 16-constant (64-dword) blocks, like libxenon; a 3-constant write hangs
            for q in [pk] + [p[3] for p in walk(pk) if p[0] == 'ib']:
                for k, p in enumerate(q):
                    if p[0] == 't3' and p[1] == 0x2d and p[2] and (p[2][0] >> 16) == 0:
                        idx = p[2][0] & 0x7ff; vals = p[2][1:]
                        b0 = idx // 64 * 64; b1 = (idx + len(vals) + 63) // 64 * 64
                        data = [0] * (b1 - b0); data[idx - b0: idx - b0 + len(vals)] = vals
                        q[k] = ['t0', 0x4000 + b0, data]
                        print('alublock: %d dwords at %d -> %d dwords at %d' % (len(vals), idx, len(data), b0))
        elif e == 'nocleardraw':   # keep the clear IB's state, drop its draw packet
            ibs[0][3][:] = [p for p in ibs[0][3] if not (p[0] == 't3' and p[1] in (0x22, 0x36))]
        elif e.startswith('ibkeep'):   # ibkeep3-9: keep only clear-IB packets with index in [3, 9)
            a_, b_ = [int(x) for x in e[6:].split('-')]
            ibs[0][3][:] = ibs[0][3][a_:b_]
        elif e == 'fetchblock':   # fetch constants as whole 6-dword groups (libxenon's granularity); a 2-dword write breaks resolves
            for q in [pk] + [p[3] for p in walk(pk) if p[0] == 'ib']:
                for k, p in enumerate(q):
                    if p[0] == 't3' and p[1] == 0x2d and p[2] and (p[2][0] >> 16) == 1:
                        idx = p[2][0] & 0x7ff; vals = p[2][1:]
                        b0 = idx // 6 * 6; b1 = (idx + len(vals) + 5) // 6 * 6
                        data = [0] * (b1 - b0); data[idx - b0: idx - b0 + len(vals)] = vals
                        q[k] = ['t3', 0x2d, [0x00010000 | b0] + data, 0xc0002d00]
                        print('fetchblock: %d dwords at %d -> %d at %d' % (len(vals), idx, len(data), b0))
        elif e.startswith('head'):    # head<N>: keep the first N top-level packets
            pk[:] = pk[:int(e[4:])]
        elif e.startswith('noop'):    # noop<hex>: drop every top-level type3 packet with that opcode
            op = int(e[4:], 16); pk[:] = [p for p in pk if not (p[0] == 't3' and p[1] == op)]
        elif e == 'noclip': print('noclip', set_ctx(pk, 0x2204, lambda v: v | 0x10000))
        elif e.startswith('noreg'):   # noreg2209+220a+220b: drop single-register writes
            regs = [int(x, 16) for x in e[5:].split('+')]
            for q in [pk] + [p[3] for p in walk(pk) if p[0] == 'ib']:
                q[:] = [p for p in q if not (p[0] == 't3' and p[1] == 0x2d and len(p[2]) == 2
                        and (p[2][0] >> 16) == 4 and 0x2000 + (p[2][0] & 0x7ff) in regs)]
        elif e.startswith('set'):     # set2200=0: write a context register at the start of every IB
            r, v = e[3:].split('=')
            for p in walk(pk):
                if p[0] == 'ib': p[3].insert(0, ['t3', 0x2d, [0x00040000 | (int(r, 16) - 0x2000), int(v, 16)], 0xc0012d00])
        elif e.startswith('rep'):     # rep2181=4: replace every write of a context register
            r, v = e[3:].split('=')
            print(e, set_ctx(pk, int(r, 16), lambda old, v=int(v, 16): v))
        elif e == 'tri':        # RECTLIST -> TRIANGLELIST
            for p in walk(pk):
                if p[0] == 't3' and p[1] in (0x22, 0x36): p[2][-1] = (p[2][-1] & ~0x3f) | 4
        elif e == 'prefetch0':  # vertex fetch prefetch_count (dword0 bits 27-29) -> 0, as libxenon's shaders
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x2b and p[2][0] == 0:
                    sh = p[2]
                    for i in range(2, (len(sh) - 2) // 3):
                        w0 = sh[2 + 3*i]
                        if (w0 & 0x1f) == 0 and (w0 >> 19) & 1: sh[2 + 3*i] = w0 & ~(7 << 27)
        elif e == 'psregs': print('psregs', set_ctx(pk, 0x2180, lambda v: v & ~0x0000c000))
        elif e == 'colorbase8': print('colorbase8', set_ctx(pk, 0x2001, lambda v: (v & ~0xfff) | 8))
        elif e == 'xdraw':
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x22 and len(p[2]) == 2: p[1] = 0x36; p[2] = [p[2][1]]; p[3] = 0xc0003600
        elif e == 'evflush':
            for q in [pk] + [p[3] for p in walk(pk) if p[0] == 'ib']:
                for k in range(len(q) - 1, -1, -1):
                    if q[k][0] == 't3' and q[k][1] in (0x22, 0x36): q.insert(k + 1, ['t3', 0x46, [6], 0])
        elif e == 'vfbit28':
            for p in walk(pk):
                if p[0] == 't3' and p[1] == 0x2d and p[2] and (p[2][0] >> 16) == 1:
                    for j in range(1, len(p[2]) - 1):
                        if (p[2][j] & 3) == 3 and (p[2][j] & ~3) > 0x10000: p[2][j + 1] |= 0x10000000
        elif e.startswith('xresolve'):
            # replace freedreno's resolve IB with libxenon's resolve of the same destination
            info = 0x01000300
            ib = ibs[1]; ib[3] = libx_resolve(out, dest_rel, 64, 64, info, 0x3 if e == 'xresolve_clear' else 0)
        elif e == 'xonly':
            # nothing of freedreno's but the dump's buffers: libxenon resolve-with-clear alone
            pk[:] = [['ib', 0x3f, ibs[1][2], libx_resolve(out, dest_rel, 64, 64, 0x01000300, 3), ibs[1][4]]]
        else: sys.exit('unknown edit ' + e)
    ndw = out.emit(pk, s, o)
    words = out.words
    rel = sorted(out.relocs)
    # readback: the destination slot
    rb_off = (dest_rel & ~(SLOT - 1)) if dest else 0
    rb_len = SLOT
    if guard_slots: rb_off = (dest_rel - 16 * SLOT); rb_len = 32 * SLOT    # read back the whole guard area
    hdr = [0x58525031, len(words), len(rel), s * SLOT + o * 4, ndw, rb_off, rb_len]
    with open(outp, 'wb') as f:
        f.write(struct.pack('>%dI' % len(hdr), *hdr))
        f.write(struct.pack('>%dI' % len(words), *words))
        f.write(struct.pack('>%dI' % len(rel), *rel))
    print('%s: %d edits %s, entry +%x %d dw, %d relocs, arena %d KB, readback +%x' %
          (outp, len(edits), edits, s * SLOT + o * 4, ndw, len(rel), len(words) // 256, rb_off))
    if '-v' in sys.argv: dump(words, s * SLOT // 4 + o, ndw)

def dump(words, at, n, depth=0):
    k = at
    while k < at + n:
        h = words[k]; t = h >> 30; pad = '  ' * depth
        if t == 3:
            cnt = ((h >> 16) & 0x3fff) + 1; op = (h >> 8) & 0x7f
            print(pad + '%02x %s' % (op, ' '.join('%08x' % x for x in words[k+1:k+1+min(cnt, 8)])))
            if op in (0x3f, 0x37, 0x32): dump(words, words[k+1] // 4, words[k+2], depth + 1)
            k += 1 + cnt
        elif t == 0:
            cnt = ((h >> 16) & 0x3fff) + 1
            print(pad + 'W %04x %s' % (h & 0x7fff, ' '.join('%08x' % x for x in words[k+1:k+1+cnt]))); k += 1 + cnt
        else: print(pad + 'raw %08x' % h); k += 1

if __name__ == "__main__":
    main()
