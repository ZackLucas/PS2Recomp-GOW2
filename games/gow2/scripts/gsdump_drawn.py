#!/usr/bin/env python3
"""Per texture (tbp, tfx): triangles that draw and vertices suppressed by ADC in one segment of a PCSX2 GS dump.
   gsdump_drawn.py <dump.gs[.zst]> [segment] [tbp ...]   (no tbp: all textures)
Same split as the runtime's PS2_GS_FRAMESUM (drawn / adc); gsdump.py draws counts every vertex."""
import collections, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gsdump

d = gsdump.Dump(gsdump.load(sys.argv[1]))
seg = int(sys.argv[2]) if len(sys.argv) > 2 else 0
only = {int(t) for t in sys.argv[3:]}
tex0 = {}
prim = 0
queued = 0
drawn, adc = collections.Counter(), collections.Counter()
for addr, val, kick, _ in gsdump.canonical_stream(d, seg):
    if addr == 0x00:
        prim, queued = val & 0x7ff, 0
    elif addr in (0x06, 0x07):
        tex0[addr - 6] = val
    elif addr == 0x05:
        t0 = tex0.get((prim >> 9) & 1, 0)
        key = (t0 & 0x3fff, (t0 >> 35) & 3) if (prim >> 4) & 1 else (-1, -1)
        queued += 1
        ptype = prim & 7
        if not kick:
            adc[key] += 1
        elif (ptype in (4, 5) and queued >= 3) or (ptype == 3 and queued % 3 == 0):
            drawn[key] += 1
for key in sorted(set(drawn) | set(adc)):
    if not only or key[0] in only:
        print('tbp=%d tfx=%d drawn=%d adc=%d' % (key[0], key[1], drawn[key], adc[key]))
print('total drawn', sum(drawn.values()), 'adc', sum(adc.values()))
