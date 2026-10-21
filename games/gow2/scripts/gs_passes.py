"""PCSX2 GS dump as a sequence of passes (runs of vertices sharing PRIM, FRAME, TEX0 base, ALPHA, TEST, ZBUF), one line each.
   gs_passes.py <dump.gs[.zst]> <segment> [pass index to list vertex by vertex]
Same format as the runtime probe PS2_GS_DRAWSEQ=N[:K] (PS2_GS_DRAWSEQ_TBP=<tbp> lists the vertices), to diff both sides."""
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gsdump
d = gsdump.Dump(gsdump.load(sys.argv[1])); seg = int(sys.argv[2]); detail = int(sys.argv[3]) if len(sys.argv) > 3 else -1
REG = {0x06:'t0',0x07:'t0',0x42:'al',0x43:'al',0x47:'te',0x48:'te',0x4c:'fr',0x4d:'fr',0x4e:'zb',0x4f:'zb',0x18:'xo',0x19:'xo',0x14:'t1',0x15:'t1'}
st = {}; prim = 0; rgbaq = 0; uv = 0; stq = 0
cur = None; runs = []
def key(c):
    t0 = st.get(('t0',c),0); fr = st.get(('fr',c),0)
    return (prim & 0x7ff, (fr & 0x1ff), (fr >> 16) & 0x3f, (t0 & 0x3fff) if prim & 0x10 else -1, st.get(('al',c),0), st.get(('te',c),0), st.get(('zb',c),0) & 0x1ff, (fr>>32))
for addr, val, kick, _ in gsdump.canonical_stream(d, seg):
    if addr == 0: prim = val; continue
    if addr == 1: rgbaq = val; continue
    if addr == 2: stq = val; continue
    if addr == 3: uv = val; continue
    if addr in REG: st[(REG[addr], addr & 1 if addr not in (0x06,0x07,0x14,0x15) else (addr - 6) & 1 if addr in (0x06,0x07) else (addr-0x14)&1)] = val
    if addr in (0x4c,0x4d,0x42,0x43,0x47,0x48,0x4e,0x4f): st[(REG[addr], (addr - {0x4c:0x4c,0x4d:0x4c,0x42:0x42,0x43:0x42,0x47:0x47,0x48:0x47,0x4e:0x4e,0x4f:0x4e}[addr]))] = val
    if addr in (0x18,0x19): st[('xo', addr-0x18)] = val
    if addr != 5: continue
    c = (prim >> 9) & 1
    k = key(c)
    if cur is None or cur[0] != k:
        t0 = st.get(('t0', c), 0)
        cur = [k, 0, 0, [], (t0 >> 37) & 0x3fff if prim & 0x10 else 0]; runs.append(cur)  # CLUT of the first vertex
    cur[1] += 1; cur[2] += (0 if kick else 1)
    xo = st.get(('xo',c),0)
    x = ((val & 0xffff) - (xo & 0xffff))/16; y = (((val>>16)&0xffff) - ((xo>>32)&0xffff))/16
    cur[3].append((x, y, (val>>32)&0xffffffff, kick, rgbaq & 0xffffffff, uv, stq))
for i, (k, nv, nadc, vs, cbp) in enumerate(runs):
    p, fbp, fbw, tbp, al, te, zbp, fbmsk = k
    xs = [v[0] for v in vs]; ys = [v[1] for v in vs]
    print(f"#{i:3d} prim={p:03x}(t{p&7} tme{(p>>4)&1} abe{(p>>6)&1} fst{(p>>8)&1} ctx{(p>>9)&1}) fbp={fbp} fbw={fbw} fbmsk={fbmsk:08x} tbp={tbp} cbp={cbp} alpha={al:010x} test={te:x} zbp={zbp} verts={nv} adc={nadc} x={min(xs):.1f}..{max(xs):.1f} y={min(ys):.1f}..{max(ys):.1f}")
    if i == detail:
        for v in vs: print(f"     x={v[0]:7.2f} y={v[1]:7.2f} z={v[2]:08x} kick={int(v[3])} rgba={v[4]:08x} uv={v[5]:08x} stq={v[6]:016x}")
