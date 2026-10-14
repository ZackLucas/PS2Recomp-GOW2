import struct, sys

DEST = lambda d: ''.join(c for c, b in zip('xyzw', (8, 4, 2, 1)) if d & b)
BC = 'xyzw'

def s11(v):
    return v - 0x800 if v & 0x400 else v

def upper(i):
    flags = ''.join(f for f, b in (('I', 31), ('E', 30), ('M', 29), ('D', 28), ('T', 27)) if i >> b & 1)
    d = DEST(i >> 21 & 0xF); ft = i >> 16 & 0x1F; fs = i >> 11 & 0x1F; fd = i >> 6 & 0x1F
    op = i & 0x3F; bc = BC[i & 3]
    names = {0x1C: 'MULq', 0x1D: 'MAXi', 0x1E: 'MULi', 0x1F: 'MINIi', 0x20: 'ADDq', 0x21: 'MADDq', 0x22: 'ADDi',
             0x23: 'MADDi', 0x24: 'SUBq', 0x25: 'MSUBq', 0x26: 'SUBi', 0x27: 'MSUBi', 0x28: 'ADD', 0x29: 'MADD',
             0x2A: 'MUL', 0x2B: 'MAX', 0x2C: 'SUB', 0x2D: 'MSUB', 0x2E: 'OPMSUB', 0x2F: 'MINI'}
    bcn = ['ADD', 'SUB', 'MADD', 'MSUB', 'MAX', 'MINI', 'MUL']
    if op < 0x1C:
        s = f'{bcn[op >> 2]}{bc}.{d} vf{fd}, vf{fs}, vf{ft}{bc}'
    elif op in names:
        s = f'{names[op]}.{d} vf{fd}, vf{fs}, vf{ft}'
    elif op >= 0x3C:
        x = (i >> 6 & 0x1F) << 2 | (i & 3)
        bca = ['ADDA', 'SUBA', 'MADDA', 'MSUBA']
        sp = {0x10: 'ITOF0', 0x11: 'ITOF4', 0x12: 'ITOF12', 0x13: 'ITOF15', 0x14: 'FTOI0', 0x15: 'FTOI4',
              0x16: 'FTOI12', 0x17: 'FTOI15', 0x1C: 'MULAq', 0x1D: 'ABS', 0x1E: 'MULAi', 0x1F: 'CLIP',
              0x20: 'ADDAq', 0x21: 'MADDAq', 0x22: 'ADDAi', 0x23: 'MADDAi', 0x24: 'SUBAq', 0x25: 'MSUBAq',
              0x26: 'SUBAi', 0x27: 'MSUBAi', 0x28: 'ADDA', 0x29: 'MADDA', 0x2A: 'MULA', 0x2C: 'SUBA',
              0x2D: 'MSUBA', 0x2E: 'OPMULA', 0x2F: 'NOP'}
        if x < 0x10:
            s = f'{bca[x >> 2]}{BC[x & 3]}.{d} ACC, vf{fs}, vf{ft}{BC[x & 3]}'
        elif 0x18 <= x < 0x1C:
            s = f'MULA{BC[x & 3]}.{d} ACC, vf{fs}, vf{ft}{BC[x & 3]}'
        elif x == 0x2F:
            s = 'NOP'
        elif x in sp:
            s = f'{sp[x]}.{d} vf{ft}, vf{fs}'
        else:
            s = f'?up{x:02x}'
    else:
        s = f'?up{op:02x}'
    return s + (f' [{flags}]' if flags else '')

def lower(i, pc):
    op = i >> 25; d = DEST(i >> 21 & 0xF); t = i >> 16 & 0x1F; s_ = i >> 11 & 0x1F; imm = s11(i & 0x7FF)
    tgt = lambda: f'0x{(pc + 8 + imm * 8) & 0x3FFF:04x}'
    if op == 0x00: return f'LQ.{d} vf{t}, {imm}(vi{s_})'
    if op == 0x01: return f'SQ.{d} vf{s_}, {imm}(vi{t})'
    if op == 0x04: return f'ILW.{d} vi{t}, {imm}(vi{s_})'
    if op == 0x05: return f'ISW.{d} vi{t}, {imm}(vi{s_})'
    if op in (0x08, 0x09):
        u = (i >> 21 & 0xF) << 11 | (i & 0x7FF)
        return f'{"IADDIU" if op == 8 else "ISUBIU"} vi{t}, vi{s_}, 0x{u:x}'
    fl = {0x10: 'FCEQ', 0x11: 'FCSET', 0x12: 'FCAND', 0x13: 'FCOR'}
    if op in fl: return f'{fl[op]} vi1, 0x{i & 0xFFFFFF:x}'
    fs = {0x14: 'FSEQ', 0x15: 'FSSET', 0x16: 'FSAND', 0x17: 'FSOR'}
    if op in fs: return f'{fs[op]} vi{t}, 0x{(i >> 10 & 0x800) | (i & 0x7FF):x}'
    fm = {0x18: 'FMEQ', 0x1A: 'FMAND', 0x1B: 'FMOR'}
    if op in fm: return f'{fm[op]} vi{t}, vi{s_}'
    if op == 0x1C: return f'FCGET vi{t}'
    if op == 0x20: return f'B {tgt()}'
    if op == 0x21: return f'BAL vi{t}, {tgt()}'
    if op == 0x24: return f'JR vi{s_}'
    if op == 0x25: return f'JALR vi{t}, vi{s_}'
    br = {0x28: 'IBEQ', 0x29: 'IBNE'}
    if op in br: return f'{br[op]} vi{t}, vi{s_}, {tgt()}'
    br1 = {0x2C: 'IBLTZ', 0x2D: 'IBGTZ', 0x2E: 'IBLEZ', 0x2F: 'IBGEZ'}
    if op in br1: return f'{br1[op]} vi{s_}, {tgt()}'
    if op == 0x40:
        f = i & 0x3F; dd = i >> 6 & 0x1F
        if f == 0x30: return f'IADD vi{dd}, vi{s_}, vi{t}'
        if f == 0x31: return f'ISUB vi{dd}, vi{s_}, vi{t}'
        if f == 0x32: return f'IADDI vi{t}, vi{s_}, {dd - 32 if dd & 16 else dd}'
        if f == 0x34: return f'IAND vi{dd}, vi{s_}, vi{t}'
        if f == 0x35: return f'IOR vi{dd}, vi{s_}, vi{t}'
        if f >= 0x3C:
            x = dd << 2 | (i & 3)
            n = {0x30: f'MOVE.{d} vf{t}, vf{s_}', 0x31: f'MR32.{d} vf{t}, vf{s_}',
                 0x34: f'LQI.{d} vf{t}, (vi{s_}++)', 0x35: f'SQI.{d} vf{s_}, (vi{t}++)',
                 0x36: f'LQD.{d} vf{t}, (--vi{s_})', 0x37: f'SQD.{d} vf{s_}, (--vi{t})',
                 0x38: f'DIV Q, vf{s_}{BC[i >> 21 & 3]}, vf{t}{BC[i >> 23 & 3]}',
                 0x39: f'SQRT Q, vf{t}{BC[i >> 23 & 3]}', 0x3A: f'RSQRT Q, vf{s_}{BC[i >> 21 & 3]}, vf{t}{BC[i >> 23 & 3]}',
                 0x3B: 'WAITQ', 0x3C: f'MTIR vi{t}, vf{s_}{BC[i >> 21 & 3]}', 0x3D: f'MFIR.{d} vf{t}, vi{s_}',
                 0x3E: f'ILWR.{d} vi{t}, (vi{s_})', 0x3F: f'ISWR.{d} vi{t}, (vi{s_})',
                 0x40: f'RNEXT.{d} vf{t}, R', 0x41: f'RGET.{d} vf{t}, R', 0x42: f'RINIT R, vf{s_}{BC[i >> 21 & 3]}',
                 0x43: f'RXOR R, vf{s_}{BC[i >> 21 & 3]}', 0x64: f'MFP.{d} vf{t}, P', 0x68: f'XTOP vi{t}',
                 0x69: f'XITOP vi{t}', 0x6C: f'XGKICK vi{s_}', 0x70: f'ESADD P, vf{s_}', 0x71: f'ERSADD P, vf{s_}',
                 0x72: f'ELENG P, vf{s_}', 0x73: f'ERLENG P, vf{s_}', 0x76: f'ESUM P, vf{s_}', 0x78: f'ESQRT P, vf{s_}{BC[i >> 21 & 3]}',
                 0x79: f'ERSQRT P, vf{s_}{BC[i >> 21 & 3]}', 0x7A: f'ERCPR P, vf{s_}{BC[i >> 21 & 3]}', 0x7B: 'WAITP',
                 0x7C: f'ESIN P, vf{s_}{BC[i >> 21 & 3]}', 0x7D: f'EATAN P, vf{s_}{BC[i >> 21 & 3]}',
                 0x7E: f'EEXP P, vf{s_}{BC[i >> 21 & 3]}'}
            return n.get(x, f'?lo2_{x:02x}')
        return f'?lo1_{f:02x}'
    return f'?lo{op:02x}'

path = sys.argv[1]
start = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0
end = int(sys.argv[3], 16) if len(sys.argv) > 3 else 0x4000
code = open(path, 'rb').read()
for pc in range(start, end, 8):
    lo, up = struct.unpack_from('<II', code, pc)
    u = upper(up)
    l = f'LOI 0x{lo:08x}' if up >> 31 & 1 else lower(lo, pc)
    print(f'{pc:04x}: {u:40s} {l}')
