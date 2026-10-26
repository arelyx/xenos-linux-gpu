#!/usr/bin/python3
"""Minimal FATX (Xbox 360) reader plus an in-place file rewriter, for switching the
Peer Pressure dashboard from Linux without Aurora.

  fatxcfg.py [--dev /dev/sda] info
  fatxcfg.py [--dev /dev/sda] ls  /PeerPressure/System
  fatxcfg.py [--dev /dev/sda] cat /PeerPressure/System/Config.ini
  fatxcfg.py [--dev /dev/sda] put /PeerPressure/System/Config.ini new.ini   (writes!)
  fatxcfg.py [--dev /dev/sda] dash aurora|xell                             (writes!)

Writes only ever touch the file's existing data clusters and the size field of its
directory entry; the FAT and every other structure are left alone. The new file must
fit in the clusters the file already owns. The old contents are saved to
./fatx-backup-<name>-<time> first and the result is read back and compared.
"""
import os, struct, sys, time

DATA_PART = 0x130EB0000          # Xbox 360 retail HDD layout: Partition1 (Hdd1:)

class Fatx:
    def __init__(self, dev, off=DATA_PART, write=False):
        self.f = open(dev, 'r+b' if write else 'rb', buffering=0)
        self.off = off
        hdr = self.pread(off, 0x1000)
        if hdr[:4] != b'XTAF':
            raise SystemExit('no XTAF at 0x%x on %s (got %r)' % (off, dev, hdr[:4]))
        self.spc = struct.unpack('>I', hdr[8:12])[0]
        self.root = struct.unpack('>I', hdr[12:16])[0]
        self.csize = self.spc * 512
        self.f.seek(0, 2); devsize = self.f.tell()
        psize = devsize - off
        self.nclusters = psize // self.csize
        self.esize = 2 if self.nclusters < 0xfff0 else 4
        fat_bytes = (self.nclusters + 1) * self.esize
        fat_bytes = (fat_bytes + 0xfff) & ~0xfff
        self.fat_off = off + 0x1000
        self.data_off = self.fat_off + fat_bytes
    def pread(self, at, n):
        self.f.seek(at); b = self.f.read(n)
        if len(b) != n: raise IOError('short read at 0x%x' % at)
        return b
    def pwrite(self, at, b):
        self.f.seek(at); self.f.write(b); os.fsync(self.f.fileno())
    def next(self, c):
        b = self.pread(self.fat_off + c * self.esize, self.esize)
        v = struct.unpack('>H' if self.esize == 2 else '>I', b)[0]
        end = 0xfff0 if self.esize == 2 else 0xfffffff0
        return None if v == 0 or v >= end else v
    def chain(self, c):
        out = []
        while c is not None and len(out) < 1 << 20:
            out.append(c); c = self.next(c)
        return out
    def caddr(self, c): return self.data_off + (c - 1) * self.csize
    def entries(self, first):
        for c in self.chain(first):
            blk = self.pread(self.caddr(c), self.csize)
            for i in range(0, self.csize, 64):
                e = blk[i:i+64]; n = e[0]
                if n in (0x00, 0xff): return
                if n == 0xe5 or n > 42: continue
                yield dict(name=e[2:2+n].decode('latin-1'), attr=e[1], first=struct.unpack('>I', e[44:48])[0],
                           size=struct.unpack('>I', e[48:52])[0], at=self.caddr(c) + i)
    def lookup(self, path):
        cur = self.root; ent = None
        for part in [p for p in path.replace('\\', '/').split('/') if p]:
            ent = next((e for e in self.entries(cur) if e['name'].lower() == part.lower()), None)
            if ent is None: raise SystemExit('not found: %s (at %s)' % (path, part))
            cur = ent['first']
        return ent
    def read(self, ent):
        data = b''.join(self.pread(self.caddr(c), self.csize) for c in self.chain(ent['first']))
        return data[:ent['size']]
    def rewrite(self, ent, new):
        clusters = self.chain(ent['first'])
        cap = len(clusters) * self.csize
        if len(new) > cap: raise SystemExit('new file %d bytes > %d bytes owned' % (len(new), cap))
        old = self.read(ent)
        bk = 'fatx-backup-%s-%d' % (ent['name'], time.time_ns())
        open(bk, 'wb').write(old); print('old contents saved to', bk)
        pad = new + b'\0' * (cap - len(new))
        for k, c in enumerate(clusters):
            self.pwrite(self.caddr(c), pad[k*self.csize:(k+1)*self.csize])
        self.pwrite(ent['at'] + 48, struct.pack('>I', len(new)))
        ent2 = dict(ent, size=len(new))
        if self.read(ent2) != new: raise SystemExit('READ-BACK MISMATCH, restore from ' + bk)
        print('wrote %d bytes, read back ok' % len(new))

CFG = '/PeerPressure/System/Config.ini'
DASH = {'aurora': r'\Device\Harddisk0\Partition1\PeerPressure\Dashboard\Aurora.xex',
        'xell':   r'\Device\Harddisk0\Partition1\PeerPressure\OtherOS\BootXell.xex'}

def main():
    a = sys.argv[1:]; dev = '/dev/sda'
    if a[:1] == ['--dev']: dev = a[1]; a = a[2:]
    cmd = a[0] if a else 'info'
    fx = Fatx(dev, write=cmd in ('put', 'dash'))
    if cmd == 'info':
        print('XTAF ok: cluster %d bytes, %d clusters, FAT entries %d bytes, root cluster %d' % (fx.csize, fx.nclusters, fx.esize, fx.root))
    elif cmd == 'ls':
        e = fx.lookup(a[1]) if len(a) > 1 and a[1] != '/' else dict(first=fx.root)
        for x in fx.entries(e['first']):
            print('%s %10d  %s' % ('d' if x['attr'] & 0x10 else '-', x['size'], x['name']))
    elif cmd == 'cat':
        sys.stdout.buffer.write(fx.read(fx.lookup(a[1])))
    elif cmd == 'put':
        fx.rewrite(fx.lookup(a[1]), open(a[2], 'rb').read())
    elif cmd == 'dash':
        ent = fx.lookup(CFG); old = fx.read(ent).decode('latin-1')
        lines = old.split('\n'); hit = 0
        for i, l in enumerate(lines):
            if l.strip().lower().startswith('dashboardpath'):
                lines[i] = 'DashboardPath = "%s"' % DASH[a[1]] + ('\r' if l.endswith('\r') else ''); hit += 1
        if hit != 1: raise SystemExit('expected one DashboardPath line, found %d' % hit)
        fx.rewrite(ent, '\n'.join(lines).encode('latin-1'))
        print('dashboard ->', DASH[a[1]])
    else:
        raise SystemExit(__doc__)

main()
