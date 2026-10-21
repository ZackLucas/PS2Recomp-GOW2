#!/usr/bin/env python3
"""Per present block of the runtime's PS2_GS_FRAMESUM=N[:K] log: triangles that draw and vertices suppressed by ADC,
   per texture (tbp, tfx), in the same format as gsdump_drawn.py, plus the other primitives (sprites, lines, points).
   framesum_drawn.py <log> [tbp ...]   (no tbp: all textures)"""
import collections, re, sys

only = {int(t) for t in sys.argv[2:]}
block = None
blocks = []
for line in open(sys.argv[1], errors='replace'):
    m = re.match(r'\[framesum\] present (\d+):', line)
    if m:
        block = dict(present=int(m.group(1)), drawn=collections.Counter(), adc=collections.Counter(), other=0)
        blocks.append(block)
        continue
    m = re.match(r'\[framesum\]\s+(\d+) adc=(\d+)\s+(\d+) ctx\d (.*)', line)
    if not m or block is None:
        continue
    count, adc, ptype, rest = int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4)
    t = re.search(r'tbp=(\d+) .*tfx=(\d+)', rest)
    key = (int(t.group(1)), int(t.group(2))) if t else (-1, -1)
    block['adc'][key] += adc
    if ptype in (3, 4, 5):
        block['drawn'][key] += count
    else:
        block['other'] += count
for b in blocks:
    print('present %d' % b['present'])
    for key in sorted(set(b['drawn']) | set(b['adc'])):
        if not only or key[0] in only:
            print('tbp=%d tfx=%d drawn=%d adc=%d' % (key[0], key[1], b['drawn'][key], b['adc'][key]))
    print('total drawn', sum(b['drawn'].values()), 'adc', sum(b['adc'].values()), 'other', b['other'])
