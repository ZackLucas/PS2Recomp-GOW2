#!/usr/bin/env python3
"""Write a truncated copy of a PCSX2 GS dump so PCSX2 itself can be used as an oracle mid-frame.

usage: gsdump_trunc.py dump.gs[.zst] out.gs --packets N [--frame F] [--display FBP,FBW,PSM]

Keeps the header, the initial state and the first N packets of frame F (0-based; packets before
frame F are dropped, so the replay starts from frame F's own initial state is NOT possible: the
state blob is the dump's start, so use --frame 0 unless you also want earlier frames replayed).
Then appends a VSync and a Registers block whose DISPFB1/DISPFB2 point at the given buffer
(default 8,8,0 = the main scene buffer of the menu), so the displayed image is that buffer.
"""
import struct, subprocess, sys, argparse

ap = argparse.ArgumentParser()
ap.add_argument("dump"); ap.add_argument("out")
ap.add_argument("--packets", type=int, required=True, help="number of leading packets to keep (all frames counted)")
ap.add_argument("--display", default=None, help="patch DISPFB1/2 to FBP,FBW,PSM (PCSX2 ignores this in dump replay; prefer --blit)")
ap.add_argument("--blit", default="8,8,0", help="FBP,FBW,PSM of a buffer to copy into the original display buffer (232, CT24, 512x448) with a final sprite")
ap.add_argument("--fill", default=None, help="R,G,B: instead of a blit, fill the display buffer with a solid color (oracle self-test)")
ap.add_argument("--blit-wh", default="512,448", help="W,H of the source region to copy (power-of-two texture dims are derived)")
ap.add_argument("--show-fbp", type=int, default=232); ap.add_argument("--show-fbw", type=int, default=8); ap.add_argument("--show-psm", type=int, default=1)
a = ap.parse_args()

data = subprocess.run(["zstd", "-dc", a.dump], check=True, capture_output=True).stdout if a.dump.endswith(".zst") else open(a.dump, "rb").read()
marker, hsize = struct.unpack_from("<II", data, 0)
assert marker == 0xFFFFFFFF
state_size, = struct.unpack_from("<I", data, 12)
p0 = 8 + hsize + state_size
regs0 = bytearray(data[p0:p0 + 8192]); p = p0 + 8192

def disp_value(fbp, fbw, psm):
    return fbp | (fbw << 9) | (psm << 15)
def patch(regs):
    if not a.display:
        return bytes(regs)
    fbp, fbw, psm = (int(v, 0) for v in a.display.split(","))
    dv = disp_value(fbp, fbw, psm)
    r = bytearray(regs)
    struct.pack_into("<Q", r, 0x70, dv)   # DISPFB1
    struct.pack_into("<Q", r, 0x90, dv)   # DISPFB2
    return bytes(r)

def gif_blit(src_fbp, src_fbw, src_psm, dst_fbp, dst_fbw, dst_psm, w=512, h=448, fill=None):
    """One PATH3 transfer: a textured full-screen sprite copying src (as texture) into dst (as frame)."""
    ad = []
    def reg(addr, val): ad.append((val & 0xFFFFFFFFFFFFFFFF, addr))
    off = 2048 * 16
    reg(0x4C, dst_fbp | (dst_fbw << 16) | (dst_psm << 24))                # FRAME_1
    reg(0x4E, 1 << 32)                                                     # ZBUF_1: ZMSK
    reg(0x47, (1 << 16) | (1 << 17))                                       # TEST_1: ZTE, ZTST=ALWAYS
    tw = max(1, (w - 1).bit_length()); th = max(1, (h - 1).bit_length())
    reg(0x06, (src_fbp * 32) | (src_fbw << 14) | (src_psm << 20) | (tw << 26) | (th << 30) | (1 << 34) | (1 << 35))  # TEX0_1 DECAL
    reg(0x14, 0)                                                           # TEX1_1: nearest
    reg(0x08, 1 | (1 << 2))                                                # CLAMP_1: clamp/clamp
    reg(0x18, off | (off << 32))                                           # XYOFFSET_1
    reg(0x40, 0 | ((w - 1) << 16) | (0 << 32) | ((h - 1) << 48))           # SCISSOR_1
    reg(0x1A, 1)                                                           # PRMODECONT=1: attributes come from PRIM (the stream may leave it 0)
    if fill is None:
        reg(0x00, 6 | (1 << 4) | (1 << 8))                                 # PRIM: sprite, TME, FST
        reg(0x01, 0x80 | (0x80 << 8) | (0x80 << 16) | (0x80 << 24))        # RGBAQ
    else:
        reg(0x00, 6)                                                       # PRIM: sprite, no texture
        reg(0x01, fill[0] | (fill[1] << 8) | (fill[2] << 16) | (0x80 << 24))
    reg(0x03, 0 | (0 << 16))                                               # UV top-left
    reg(0x05, off | (off << 16))                                           # XYZ2 top-left
    reg(0x03, (w * 16) | ((h * 16) << 16))                                 # UV bottom-right
    reg(0x05, (off + w * 16) | ((off + h * 16) << 16) | (0 << 32))         # XYZ2 bottom-right (kick)
    tag_lo = len(ad) | (1 << 15) | (0 << 58) | (1 << 60)                   # NLOOP, EOP, FLG=PACKED, NREG=1
    body = struct.pack("<QQ", tag_lo, 0x0E)
    for v, addr in ad:
        body += struct.pack("<QQ", v, addr)
    return bytes([0, 3]) + struct.pack("<I", len(body)) + body

kept = bytearray(); n = 0
while p < len(data) and n < a.packets:
    pid = data[p]
    if pid == 0:
        size, = struct.unpack_from("<I", data, p + 2); end = p + 6 + size
    elif pid == 1: end = p + 2
    elif pid == 2: end = p + 5
    elif pid == 3: end = p + 1 + 8192
    else: raise SystemExit(f"bad packet id {pid} at {p:#x}")
    kept += data[p:end]; p = end; n += 1

out = bytearray(data[:p0]) + patch(regs0) + kept
if a.fill:
    out += gif_blit(0, 1, 0, a.show_fbp, a.show_fbw, a.show_psm, fill=tuple(int(v) for v in a.fill.split(",")))
elif a.blit and a.blit.lower() != "none":
    bf, bw_, bp_ = (int(v, 0) for v in a.blit.split(","))
    bwid, bhei = (int(v) for v in a.blit_wh.split(','))
    out += gif_blit(bf, bw_, bp_, a.show_fbp, a.show_fbw, a.show_psm, w=bwid, h=bhei)
# Same order as the original dumps: the Registers block precedes the VSync that presents it.
out += bytes([3]) + patch(regs0)
out += bytes([1, 0])                                   # VSync (field 0)
open(a.out, "wb").write(out)
print(f"wrote {a.out}: {n} packets kept, blit {a.blit} -> display buffer")
