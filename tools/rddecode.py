#!/usr/bin/python3
# Decode a freedreno .rd dump's command stream as Xenos PM4, naming registers from Xenia's table.
import os
import gzip, struct, sys, re
regs = {}
for m in re.finditer(r'XE_GPU_REGISTER\((0x[0-9A-Fa-f]+),\s*\w+,\s*(\w+)\)', open(os.environ.get('XENIA_GPU', 'xenia/src/xenia/gpu') + '/register_table.inc').read()):
    regs[int(m.group(1), 16)] = m.group(2)
ops = {}
for m in re.finditer(r'PM4_(\w+)\s*=\s*(0x[0-9a-fA-F]+)', open(os.environ.get('XENIA_GPU', 'xenia/src/xenia/gpu') + '/xenos.h').read()):
    ops.setdefault(int(m.group(2), 16), m.group(1))
d = gzip.open(sys.argv[1]).read(); i = 0; bufs = {}; cmds = []; pending = None
while i + 8 <= len(d):
    t, sz = struct.unpack('>II', d[i:i+8]); i += 8; body = d[i:i+sz]; i += sz
    if t == 3:   # GPUADDR: u32 lo, u32 size, u32 hi
        lo, size = struct.unpack('>II', body[:8]); pending = lo
    elif t == 12 and pending is not None:
        bufs[pending] = body; pending = None
    elif t == 6:
        lo, size = struct.unpack('>II', body[:8]); cmds.append((lo, size))
def words_at(addr, n):
    for base, b in bufs.items():
        if base <= addr < base + len(b):
            off = addr - base
            return [struct.unpack('>I', b[off+4*k:off+4*k+4])[0] for k in range(n)]
    return None
def decode(addr, n, depth=0):
    w = words_at(addr, n); pad = '  ' * depth
    if w is None: print(pad + "?? %08x not in dump" % addr); return
    k = 0
    while k < len(w):
        h = w[k]; typ = h >> 30
        if typ == 0:
            cnt = ((h >> 16) & 0x3fff) + 1; reg = h & 0x7fff
            vals = w[k+1:k+1+cnt]
            for j, v in enumerate(vals):
                print(pad + "  W %-28s (%04x) = %08x" % (regs.get(reg + j, '?'), reg + j, v))
            k += 1 + cnt
        elif typ == 2:
            k += 1
        elif typ == 3:
            cnt = ((h >> 16) & 0x3fff) + 1; op = (h >> 8) & 0x7f
            body = w[k+1:k+1+cnt]
            name = ops.get(op, '%02x' % op)
            if name == 'SET_CONSTANT' and body:
                t = body[0] >> 16; idx = body[0] & 0x7ff
                if t == 4:   # registers
                    for j, v in enumerate(body[1:]):
                        print(pad + "  C %-28s (%04x) = %08x" % (regs.get(0x2000 + idx + j, '?'), 0x2000 + idx + j, v))
                else:
                    print(pad + "  SET_CONSTANT type %d idx %d: %s" % (t, idx, ' '.join('%08x' % x for x in body[1:9])))
            else:
                print(pad + "  %-16s %s" % (name, ' '.join('%08x' % x for x in body[:8]) + (' ...' if len(body) > 8 else '')))
            if name in ('INDIRECT_BUFFER', 'INDIRECT_BUFFER_PFD') and depth < 2:
                decode(body[0], body[1], depth + 1)
            k += 1 + cnt
        else:
            print(pad + "  type1 %08x" % h); k += 1
for lo, size in cmds:
    print("cmdstream @%08x, %d dwords" % (lo, size)); decode(lo, size)
