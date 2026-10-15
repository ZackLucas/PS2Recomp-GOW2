#!/usr/bin/env python3
"""Build small synthetic GS dumps (from a real dump's header/state, VRAM zeroed) to compare our GS with
PCSX2 on one feature at a time. The picture ends up in the original display buffer (p=232, CT24, 512x448).

experiments:
  t8_as_ct32   upload a pseudo-random T8 128x128 image, then view the same memory as a CT32 texture
  t4_as_ct32   upload a pseudo-random T4 128x128 image, then view the same memory as a CT32 texture
  ct32_as_t8   upload CT32 pseudo-random, then sample it as T8 through an identity-gray CT32 CLUT (CSM1)
  t8_clut      upload T8 indices 0..255 pattern + a colorful CT32 CLUT, sample as T8 128x128 with the CLUT
usage: gsdump_synth.py <real_dump.gs.zst> <experiment> <out.gs>
"""
import random, struct, subprocess, sys

src, exp, out = sys.argv[1:4]
data = bytearray(subprocess.run(["zstd", "-dc", src], check=True, capture_output=True).stdout)
hsize, = struct.unpack_from("<I", data, 4); ssize, = struct.unpack_from("<I", data, 12)
state0 = 8 + hsize; VRAM = state0 + 425
data[VRAM:VRAM + 4 * 1024 * 1024] = bytes(4 * 1024 * 1024)
regs0 = bytearray(data[state0 + ssize: state0 + ssize + 8192])
# Progressive output: no interlace/field mode and no field line offset, so presentation cannot confound the comparison.
struct.pack_into("<Q", regs0, 0x20, 0)                                  # SMODE2: INT=0, FFMD=0
dispfb = 232 | (8 << 9) | (1 << 15)                                     # p=232, FBW=8, CT24, DBX=DBY=0
struct.pack_into("<Q", regs0, 0x70, dispfb); struct.pack_into("<Q", regs0, 0x90, dispfb)
regs0 = bytes(regs0)
data[state0 + ssize: state0 + ssize + 8192] = regs0
head = bytes(data[:state0 + ssize + 8192])

def tag(nloop, flg, nreg=1, regs=0, eop=1): return struct.pack("<QQ", nloop | (eop << 15) | (flg << 58) | ((nreg & 15) << 60), regs)
def ad(*pairs): return b"".join(struct.pack("<QQ", v & (2**64 - 1), a) for a, v in pairs)
def transfer(payload): return bytes([0, 3]) + struct.pack("<I", len(payload)) + payload

def upload(dbp, dbw, dpsm, w, h, raw):
    raw = bytes(raw) + bytes((-len(raw)) % 16)
    setup = ad((0x50, (dbp << 32) | (dbw << 48) | (dpsm << 56)), (0x51, 0), (0x52, w | (h << 32)), (0x53, 0))
    return transfer(tag(4, 0, 1, 0xE, eop=0) + setup + tag(len(raw) // 16, 2) + raw)

def draw(tbp, tbw, tpsm, tw, th, w, h, cbp=0, cpsm=0, cld=0, nearest=True, dst=(232, 8, 1), sw=None, sh=None, sx=0, sy=0):
    off = 2048 * 16
    tex0 = tbp | (tbw << 14) | (tpsm << 20) | (tw << 26) | (th << 30) | (1 << 34) | (1 << 35) | (cbp << 37) | (cpsm << 51) | (0 << 55) | (0 << 56) | (cld << 61)
    regs = [(0x4C, dst[0] | (dst[1] << 16) | (dst[2] << 24)), (0x4E, 1 << 32), (0x47, (1 << 16) | (1 << 17)),
            (0x06, tex0), (0x14, 0 if nearest else (1 << 5) | (1 << 6)), (0x08, 5), (0x18, off | (off << 32)),
            (0x40, 0 | (511 << 16) | (0 << 32) | (447 << 48)), (0x00, 6 | (1 << 4) | (1 << 8)),
            (0x01, 0x80808080), (0x03, 0), (0x05, (off + sx * 16) | ((off + sy * 16) << 16)),
            (0x03, (w * 16) | ((h * 16) << 16)), (0x05, (off + (sx + (sw or w)) * 16) | ((off + (sy + (sh or h)) * 16) << 16))]
    return transfer(tag(len(regs), 0, 1, 0xE) + ad(*regs))

def tri_z(verts, zbp=120, ztst=1):
    # flat triangle into CT24 p=232, depth written to a Z24 buffer at zbp (ZMSK=0); verts = [(x16, y16, z), ...] in 1/16 pixel
    off = 2048 * 16
    regs = [(0x4C, 232 | (8 << 16) | (1 << 24)), (0x4E, zbp | (1 << 24)), (0x47, (1 << 16) | (ztst << 17)),
            (0x18, off | (off << 32)), (0x40, 0 | (511 << 16) | (0 << 32) | (447 << 48)), (0x00, 3), (0x01, 0x80808080)]
    for x, y, z in verts: regs.append((0x05, (off + x) | ((off + y) << 16) | (z << 32)))
    return transfer(tag(len(regs), 0, 1, 0xE) + ad(*regs))

rnd = random.Random(1234)
packets = b""
DBP = 12928   # page 404, the same spot as the problematic T8 texture
if exp == "t8_as_ct32":
    img = bytes(rnd.randrange(256) for _ in range(128 * 128))
    packets += upload(DBP, 2, 0x13, 128, 128, img)
    packets += draw(DBP, 1, 0x00, 6, 6, 64, 64)                  # 64x64 CT32 view of the 2 pages
elif exp == "ct32_plain":      # control: CT32 uploaded and read back as CT32 (must agree everywhere)
    img = bytes(rnd.randrange(256) for _ in range(64 * 64 * 4))
    packets += upload(DBP, 1, 0x00, 64, 64, img)
    packets += draw(DBP, 1, 0x00, 6, 6, 64, 64)
elif exp == "clut_map":        # reveal which palette pixel each T8 index uses: palette pixel (x,y) encodes (x,y)
    pal = b"".join(struct.pack("<I", (x * 16 + 8) | ((y * 16 + 8) << 8) | (0x40 << 16) | (0x80 << 24)) for y in range(16) for x in range(16))
    packets += upload(14000, 1, 0x00, 16, 16, pal)
    idx = bytes(y * 16 + x for y in range(16) for x in range(16))
    packets += upload(DBP, 1, 0x13, 16, 16, idx)               # T8 16x16, TBW=1, index = y*16+x
    packets += draw(DBP, 1, 0x13, 4, 4, 16, 16, cbp=14000, cpsm=0, cld=1)
elif exp == "clut_map4":       # same for 4-bit indices: 16 entries
    pal = b"".join(struct.pack("<I", (i * 16 + 8) | (0x40 << 8) | (0x40 << 16) | (0x80 << 24)) for i in range(16))
    packets += upload(14000, 1, 0x00, 8, 2, pal)               # 8x2 palette image, raster order
    idx = bytes(((2 * x + 1) & 15) << 4 | ((2 * x) & 15) for x in range(8)) * 2     # 16x2 pixels: indices 0..15 per row
    packets += upload(DBP, 1, 0x14, 16, 2, idx)
    packets += draw(DBP, 1, 0x14, 4, 1, 16, 2, cbp=14000, cpsm=0, cld=1)
elif exp == "t4_as_ct32":
    img = bytes(rnd.randrange(256) for _ in range(128 * 128 // 2))
    packets += upload(DBP, 2, 0x14, 128, 128, img)
    packets += draw(DBP, 1, 0x00, 6, 5, 64, 32)                  # 1 page = 64x32 CT32
elif exp == "ct32_as_t8":
    img = bytes(rnd.randrange(256) for _ in range(64 * 64 * 4))
    packets += upload(DBP, 1, 0x00, 64, 64, img)
    pal = b"".join(struct.pack("<I", i | (i << 8) | (i << 16) | (0x80 << 24)) for i in range(256))   # entry i -> gray i
    packets += upload(14000, 1, 0x00, 16, 16, pal)               # CLUT (CSM1) as a 16x16 CT32 image
    packets += draw(DBP, 2, 0x13, 7, 7, 128, 128, cbp=14000, cpsm=0, cld=1)
elif exp == "t8_clut":
    img = bytes((x + 16 * y) & 0xFF for y in range(128) for x in range(128))
    packets += upload(DBP, 2, 0x13, 128, 128, img)
    pal = b"".join(struct.pack("<I", (i * 7 & 0xFF) | ((i * 13 & 0xFF) << 8) | ((i * 29 & 0xFF) << 16) | (0x80 << 24)) for i in range(256))
    packets += upload(14000, 1, 0x00, 16, 16, pal)
    packets += draw(DBP, 2, 0x13, 7, 7, 128, 128, cbp=14000, cpsm=0, cld=1)
elif exp in ("spr_up_nearest", "spr_up_linear"):
    # 16x16 CT32 gradient (R=16*x, G=16*y, B=0x40) stretched by a sprite to 32x32 at (8,8): shows the sprite UV rule
    img = b"".join(struct.pack("<I", (16 * x) | ((16 * y) << 8) | (0x40 << 16) | (0x80 << 24)) for y in range(16) for x in range(16))
    packets += upload(DBP, 1, 0x00, 16, 16, img)
    packets += draw(DBP, 1, 0x00, 4, 4, 16, 16, nearest=(exp == "spr_up_nearest"), sw=32, sh=32, sx=8, sy=8)
elif exp.startswith("z_plane2"):
    # plane with gradient gx along x and gy along y (z_plane2_<gx>_<gy>, per pixel); Z24 read back as a texture
    gx, gy = (int(v) for v in (exp[8:].strip("_").split("_") + ["7", "5"])[:2]) if exp[8:] else (7, 5)
    x0, x1, y0, y1 = 8 * 16 + 5, 120 * 16 + 9, 8 * 16 + 3, 120 * 16 + 13
    zl = 0x400000
    packets += tri_z([(x0, y0, zl), (x1, y0, int(round(zl + (x1 - x0) / 16.0 * gx))), (x0, y1, int(round(zl + (y1 - y0) / 16.0 * gy)))])
    packets += draw(120 * 32, 8, 0x31, 9, 9, 128, 128)
elif exp.startswith("z_planex"):
    # plane growing along x only, g per pixel (default 7); the Z24 buffer is read back as a texture
    g = int(exp[8:] or 7)
    x0, x1 = 8 * 16 + 5, 120 * 16 + 9
    zl = 0x400000; zr = int(round(zl + (x1 - x0) / 16.0 * g))
    packets += tri_z([(x0, 8 * 16 + 3, zl), (x1, 8 * 16 + 7, zr), (x0, 120 * 16 + 13, zl)])
    packets += draw(120 * 32, 8, 0x31, 9, 9, 128, 128)
elif exp.startswith("z_isoy"):
    # same probe with the plane growing along y; boundary is a horizontal line. k counts in 1/256 pixel from the top vertex y=8+3/16
    k = int(exp[6:] or 0)
    y0, y1 = 8 * 16 + 3, 120 * 16 + 13
    g = 256.0 / 16.0
    zl = 0x400000; zr = int(round(zl + (y1 - y0) * g))
    packets += tri_z([(8 * 16 + 5, y0, zl), (8 * 16 + 5, y1, zr), (120 * 16 + 9, y0, zl)])
    off = 2048 * 16; K = 0x400000 + k
    regs = [(0x4C, 232 | (8 << 16) | (1 << 24)), (0x4E, 120 | (1 << 24) | (1 << 32)), (0x47, (1 << 16) | (2 << 17)),
            (0x18, off | (off << 32)), (0x40, 0 | (511 << 16) | (0 << 32) | (447 << 48)), (0x00, 6),
            (0x01, 0xFFFFFFFF), (0x05, off | (off << 16) | (K << 32)), (0x05, (off + 128 * 16) | ((off + 128 * 16) << 16) | (K << 32))]
    packets += transfer(tag(len(regs), 0, 1, 0xE) + ad(*regs))
elif exp.startswith("z_iso"):
    # iso-line probe: depth plane that grows along x only (256 per pixel), then a white sprite at constant Z with ZTST=GEQUAL.
    # White survives where plane Z <= K, so the vertical white/gray boundary shows where the model puts plane Z = K.
    # z_iso<k>: K = 0x400000 + k  (k counts in 1/256 pixel so that the boundary falls between pixels)
    k = int(exp[5:] or 0)
    x0, x1 = 8 * 16 + 5, 120 * 16 + 9                          # same subpixel positions as z_tri
    g = 256.0 / 16.0                                           # Z per 1/16 pixel
    zl = 0x400000; zr = int(round(zl + (x1 - x0) * g))
    packets += tri_z([(x0, 8 * 16 + 3, zl), (x1, 8 * 16 + 7, zr), (x0, 120 * 16 + 13, zl)])
    off = 2048 * 16; K = 0x400000 + k
    regs = [(0x4C, 232 | (8 << 16) | (1 << 24)), (0x4E, 120 | (1 << 24) | (1 << 32)), (0x47, (1 << 16) | (2 << 17)),
            (0x18, off | (off << 32)), (0x40, 0 | (511 << 16) | (0 << 32) | (447 << 48)), (0x00, 6),
            (0x01, 0xFFFFFFFF), (0x05, off | (off << 16) | (K << 32)), (0x05, (off + 128 * 16) | ((off + 128 * 16) << 16) | (K << 32))]
    packets += transfer(tag(len(regs), 0, 1, 0xE) + ad(*regs))
elif exp in ("z24_upload", "ct24_upload"):   # control: upload RGB bytes as Z24 / CT24, read back as the same format
    psm = 0x31 if exp == "z24_upload" else 0x01
    img = bytes(rnd.randrange(256) for _ in range(64 * 64 * 3))
    packets += upload(DBP, 1, psm, 64, 64, img)
    packets += draw(DBP, 1, psm, 6, 6, 64, 64)
elif exp in ("z_tri", "z_flat", "z_tri_ct24"):   # depth plane of one triangle, then view the Z24 buffer as a Z24 texture
    z = (0x400000,) * 3 if exp == "z_flat" else (0x100000, 0x500000, 0x300000)
    packets += tri_z([(8 * 16 + 5, 8 * 16 + 3, z[0]), (120 * 16 + 9, 8 * 16 + 7, z[1]), (8 * 16 + 11, 120 * 16 + 13, z[2])])
    packets += draw(120 * 32, 8, 1 if exp == 'z_tri_ct24' else 0x31, 9, 9, 128, 128)   # the Z buffer as a texture
else:
    raise SystemExit("unknown experiment")
packets += bytes([3]) + regs0 + bytes([1, 0])
open(out, "wb").write(head + packets)
print(f"wrote {out}: experiment {exp}")
