#!/usr/bin/env python3
"""Rebuild the sine table that the game computes at boot (loop at 0x119808, table at 0x3d9280, 4096 floats) under two
roundings of its only hardware FPU operation, x = (float)k * 0x39c90fdb (mul.s), and compare with RAM dumps.
The rest of the loop is software double (libgcc fp-bit: integer instructions), so it rounds the same on every side.
   sin_table_round.py <ee_ram.bin> [<ee_ram.bin> ...]   e.g. the runtime's state_600_ee.bin, PCSX2's eeMemory.bin
Per dump: how many entries match each model (RN = round to nearest, RZ = toward zero, PCSX2's FPU.Roundmode = 3)."""
import math, struct, sys
from fractions import Fraction

C = struct.unpack('<f', struct.pack('<I', 0x39c90fdb))[0]


def f2b(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def mul(a, b, toward_zero):
    exact = Fraction(a) * Fraction(b)
    r = f2b(float(exact))  # the product of two floats is exact in double; packing rounds to nearest
    if toward_zero and r and abs(Fraction(struct.unpack('<f', struct.pack('<I', r))[0])) > abs(exact):
        r -= 1  # the values here are positive
    return struct.unpack('<f', struct.pack('<I', r))[0]


models = {name: [f2b(math.sin(mul(float(k), C, rz))) for k in range(4096)] for name, rz in (('RN', False), ('RZ', True))}
for path in sys.argv[1:]:
    with open(path, 'rb') as f:
        f.seek(0x3d9280)
        table = struct.unpack('<4096I', f.read(4096 * 4))
    for name, m in models.items():
        hist = {}
        for k in range(4096):
            d = abs(m[k] - table[k])
            hist[d] = hist.get(d, 0) + 1
        print(f"{path}: model {name}: {hist.get(0, 0)}/4096 equal; ulp histogram {dict(sorted(hist.items())[:6])}")
