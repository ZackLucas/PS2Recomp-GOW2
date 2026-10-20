#!/usr/bin/env python3
"""Counts PACKED GIF vertices that kick a draw (ADC=0) and those suppressed by ADC=1 in raw GIF data.
   gif_adc_count.py <packets.bin> [...]   (our vu1_run_one *_interp_packets.bin)
   or imported: count(bytes) -> dict"""
import struct, sys, collections

def count(buf, stats=None):
    st = stats if stats is not None else collections.Counter()
    o = 0
    n = len(buf)
    while o + 16 <= n:
        lo, hi = struct.unpack_from('<QQ', buf, o); o += 16
        nloop = lo & 0x7FFF; flg = (lo >> 58) & 3; nreg = (lo >> 60) & 15 or 16
        regs = [(hi >> (4 * i)) & 15 for i in range(nreg)]
        st['tags'] += 1
        if flg == 0:
            for _ in range(nloop):
                for r in regs:
                    if o + 16 > n: return st
                    q0, q1 = struct.unpack_from('<QQ', buf, o); o += 16
                    if r in (4, 5):                  # XYZF2 / XYZ2
                        adc = (q1 >> 47) & 1
                        st['xyz2_adc' if adc else 'xyz2_kick'] += 1
                    elif r in (0xC, 0xD):            # XYZF3 / XYZ3 (never draws)
                        st['xyz3'] += 1
                    elif r == 0xE:
                        st['ad'] += 1
                    st['qw'] += 1
        elif flg == 2:
            o += nloop * 16
        else:
            o += nloop * 16 * 1
    return st

if __name__ == '__main__':
    tot = collections.Counter()
    for p in sys.argv[1:]:
        s = count(open(p, 'rb').read())
        print(p.split('/')[-1], dict(s))
        tot.update(s)
    print('TOTAL', dict(tot))
