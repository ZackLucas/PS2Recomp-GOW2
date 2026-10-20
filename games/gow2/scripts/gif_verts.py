#!/usr/bin/env python3
"""Decode a GIF packet dump (e.g. gow2_vu1_run_one *_packets.bin): tags and first N XYZ2/ST entries.
   gif_verts.py <packets.bin> [max_entries]"""
import struct, sys
b = open(sys.argv[1], 'rb').read(); o = 0; n = len(b); shown = 0
lim = int(sys.argv[2]) if len(sys.argv) > 2 else 40
while o + 16 <= n:
    lo, hi = struct.unpack_from('<QQ', b, o); o += 16
    nloop = lo & 0x7FFF; flg = (lo >> 58) & 3; nreg = (lo >> 60) & 15 or 16
    regs = [(hi >> (4 * i)) & 15 for i in range(nreg)]
    print(f'TAG nloop={nloop} flg={flg} nreg={nreg} prim={(lo>>47)&0x7ff:#x} regs={regs}')
    if flg == 0:
        for _ in range(nloop):
            for r in regs:
                q = struct.unpack_from('<4I', b, o); f = struct.unpack_from('<4f', b, o); o += 16
                if shown < lim:
                    if r in (4, 5): print(f'  XYZ2 x={(q[0]&0xffff)/16:.2f} y={(q[1]&0xffff)/16:.2f} z={q[2]} adc={(q[3]>>15)&1}'); shown += 1
                    elif r == 2: print(f'  ST  s={f[0]:.4f} t={f[1]:.4f} q={f[2]:.4f}')
    elif flg == 2: o += nloop * 16
    else: o += (nloop * nreg + 1) // 2 * 16
