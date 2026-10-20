#!/usr/bin/env python3
"""Dump qwords of the 16 KiB VU1 data memory stored in a captured call (in_*.bin from PS2_VU1_CAPTURE*),
or in a raw PCSX2 vu1Memory.bin.   vu1_in_dump.py <file> <first_qw> <last_qw_exclusive> [--raw]"""
import struct, sys
b = open(sys.argv[1], 'rb').read()
raw = '--raw' in sys.argv
if raw:
    mem = b
    top = itop = 0
else:
    magic, pc, top, itop, sz = struct.unpack_from('<5I', b, 0)
    mem = b[20 + sz:]
    print('pc', pc, 'top', top, 'itop', itop)
for a in range(int(sys.argv[2]), int(sys.argv[3])):
    a &= 0x3ff
    f = struct.unpack_from('<4f', mem, a * 16); u = struct.unpack_from('<4I', mem, a * 16)
    print('%4d' % a, ' '.join('%11.4g' % x for x in f), ' ', ' '.join('%08x' % x for x in u))
