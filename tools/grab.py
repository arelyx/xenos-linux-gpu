#!/usr/bin/python3
# dump the current scanout surface (tiled, 32bpp) as raw bytes; detile on the host with detile.py
import mmap, os, struct, sys
regs = 0x200ec800000
fd = os.open("/dev/mem", os.O_RDONLY | os.O_SYNC)
r = mmap.mmap(fd, 0x10000, mmap.MAP_SHARED, mmap.PROT_READ, offset=regs)
# 32-bit loads only: Xenos MMIO hangs the bus on byte accesses (mmap slicing reads bytes)
regs32 = memoryview(r).cast("I")
def rd(off):
    return int.from_bytes(regs32[off // 4].to_bytes(4, "little"), "big") if False else struct.unpack(">I", struct.pack("=I", regs32[off // 4]))[0]
addr, w, h = rd(0x6110), rd(0x6134), rd(0x6138)
size = w * ((h + 31) // 32 * 32) * 4
base = addr & ~0xffff; off = addr - base
m = mmap.mmap(fd, (size + off + 0xffff) & ~0xffff, mmap.MAP_SHARED, mmap.PROT_READ, offset=base)
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/screen.raw"
open(out, "wb").write(struct.pack(">III", addr, w, h) + m[off:off + size])
print("scanout %08x %dx%d -> %s" % (addr, w, h, out))
