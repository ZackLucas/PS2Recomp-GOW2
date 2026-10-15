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

def draw(tbp, tbw, tpsm, tw, th, w, h, cbp=0, cpsm=0, cld=0, nearest=True, dst=(232, 8, 1)):
    off = 2048 * 16
    tex0 = tbp | (tbw << 14) | (tpsm << 20) | (tw << 26) | (th << 30) | (1 << 34) | (1 << 35) | (cbp << 37) | (cpsm << 51) | (0 << 55) | (0 << 56) | (cld << 61)
    regs = [(0x4C, dst[0] | (dst[1] << 16) | (dst[2] << 24)), (0x4E, 1 << 32), (0x47, (1 << 16) | (1 << 17)),
            (0x06, tex0), (0x14, 0 if nearest else (1 << 5) | (1 << 6)), (0x08, 5), (0x18, off | (off << 32)),
            (0x40, 0 | (511 << 16) | (0 << 32) | (447 << 48)), (0x00, 6 | (1 << 4) | (1 << 8)),
            (0x01, 0x80808080), (0x03, 0), (0x05, off | (off << 16)),
            (0x03, (w * 16) | ((h * 16) << 16)), (0x05, (off + w * 16) | ((off + h * 16) << 16))]
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
else:
    raise SystemExit("unknown experiment")
packets += bytes([3]) + regs0 + bytes([1, 0])
open(out, "wb").write(head + packets)
print(f"wrote {out}: experiment {exp}")
