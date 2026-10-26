#!/usr/bin/python3
"""Talk to the Xbox 360 SMC through /dev/smc (16-byte messages).
  smc.py powerup          -> power-on type (reply byte 1; 0x12 = eject button, what Peer Pressure checks)
  smc.py raw 01           -> send bytes, print the 16-byte reply
  smc.py reboot [reason]  -> 0x82 0x04 <reason> (kernel default 0x30); console reboots
"""
import os, sys
def xfer(b, reply=True):
    fd = os.open('/dev/smc', os.O_RDWR)
    try:
        os.write(fd, bytes(b).ljust(16, b'\0'))
        return os.read(fd, 16) if reply else None
    finally:
        os.close(fd)
a = sys.argv[1:] or ['powerup']
if a[0] == 'powerup':
    r = xfer([0x01]); print('reply', r.hex(' '), '-> power-on type 0x%02x' % r[1])
elif a[0] == 'raw':
    print(xfer([int(x, 16) for x in a[1:]]).hex(' '))
elif a[0] == 'reboot':
    reason = int(a[1], 16) if len(a) > 1 else 0x30
    os.sync(); xfer([0x82, 0x04, reason, 0x00], reply=False)
else:
    sys.exit(__doc__)
