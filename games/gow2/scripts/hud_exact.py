#!/usr/bin/env python3
"""Exact PCSX2 x ours comparison of a lower band of a real gameplay frame.
usage: hud_exact.py <dump.gs.zst> <ours_prefix_t..._gpu> <dby> [nframes]
Replays the dump in PCSX2 (software renderer) with DISPFB.DBY=<dby> so the visible window shows rows dby.., then
compares with our GPU frames (<prefix>0..2.ppm), choosing the best frame."""
import struct, subprocess, sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcsx2_exact as P
from PIL import Image

def make_progressive_dby(path, dby):
    d = bytearray(open(path, "rb").read())
    hsize, = struct.unpack_from("<I", d, 4); ssize, = struct.unpack_from("<I", d, 12)
    off = 8 + hsize + ssize
    def patch(o):
        struct.pack_into("<Q", d, o + 0x20, 0)
        fb = 232 | (8 << 9) | (1 << 15) | (dby << 43); struct.pack_into("<Q", d, o + 0x70, fb); struct.pack_into("<Q", d, o + 0x90, fb)
    patch(off); p = off + 8192
    while p < len(d):
        k = d[p]
        if k == 0: p += 6 + struct.unpack_from("<I", d, p + 2)[0]
        elif k == 1: p += 2
        elif k == 2: p += 5
        elif k == 3: patch(p + 1); p += 1 + 8192
        else: break
    open(path, "wb").write(d)

def main():
    dump, prefix, dby = sys.argv[1], sys.argv[2], int(sys.argv[3])
    tmp = f"/tmp/hud_{os.getpid()}.gs"
    open(tmp, "wb").write(subprocess.run(["zstd", "-dc", dump], check=True, capture_output=True).stdout)
    make_progressive_dby(tmp, dby)
    shot = P.capture(tmp); shot.save(f"/tmp/hud_shot_{dby}.png"); sp = shot.load()
    best = None
    SY = P.SCALE_Y   # window shows ~214 rows starting at DBY
    regions = {"banda": (0, 512, dby + 16, dby + 190), "contador": (360, 512, dby + 16, min(dby + 190, 330))}
    for f in range(int(os.environ.get("NF","3"))):
        op = Image.open(f"{prefix}{f}.ppm").convert("RGB").load()
        res = {}
        for name, (x0, x1, y0, y1) in regions.items():
            n = same = big = 0; tot = 0
            for y in range(y0, y1):
                Y = int(P.Y0 + (y - dby + 0.5) * SY)
                for x in range(x0, x1):
                    a = op[x, y]; b = sp[P.X0 + 2 * x + 1, Y]
                    n += 1; same += (a == b); m = max(abs(a[c] - b[c]) for c in range(3)); big += m > 8; tot += m
            res[name] = (tot / n, 100 * same / n, 100 * big / n)
        print(f"  frame {f}: banda {res['banda'][0]:.2f} ident {res['banda'][1]:.1f}% >8 {res['banda'][2]:.1f}% | contador {res['contador'][0]:.2f} ident {res['contador'][1]:.1f}%")
        if best is None or res["banda"][0] < best[1]["banda"][0]: best = (f, res)
    print("our frame", best[0])
    for name, (m, i, b) in best[1].items(): print(f"  {name:9s} mean|d|={m:.2f}  identical={i:.1f}%  >8: {b:.1f}%")
main()
