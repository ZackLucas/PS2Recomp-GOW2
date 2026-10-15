#!/usr/bin/env python3
"""Reader/inspector for PCSX2 GS dumps (.gs / .gs.zst).

Layout (derived from the file, validated by stream-length consistency):
  u32 0xFFFFFFFF, u32 header_size
  header (header_size bytes): u32 state_version, state_size, serial_offset,
      serial_size, crc, ss_width, ss_height, ss_offset, ss_size ; serial ; RGBA screenshot
  state blob (state_size bytes): GS freeze data (registers + 4MB VRAM)
  8192 bytes: GS privileged registers
  packets until EOF: id(u8)
      0 Transfer : u8 path, u32 size, data
      1 VSync    : u8 field
      2 ReadFIFO : u32 size
      3 Registers: 8192 bytes
"""
import struct, subprocess, sys, collections

def load(path):
    if path.endswith(".zst"):
        return subprocess.run(["zstd", "-dc", path], check=True, capture_output=True).stdout
    return open(path, "rb").read()

class Dump:
    def __init__(self, data):
        self.data = data
        marker, hsize = struct.unpack_from("<II", data, 0)
        assert marker == 0xFFFFFFFF, "not a headered GS dump"
        base = 8
        (self.state_version, self.state_size, so, ss, self.crc,
         self.ss_w, self.ss_h, sso, sss) = struct.unpack_from("<9I", data, base)
        self.serial = data[base+so:base+so+ss].decode()
        self.screenshot = data[base+sso:base+sso+sss]
        p = base + hsize
        self.state = data[p:p+self.state_size]; p += self.state_size
        self.regs = data[p:p+8192]; p += 8192
        self.packets_start = p
        self.packets = []
        n = len(data)
        while p < n:
            pid = data[p]; p += 1
            if pid == 0:
                path = data[p]; size, = struct.unpack_from("<I", data, p+1); p += 5
                self.packets.append((0, path, data[p:p+size])); p += size
            elif pid == 1:
                self.packets.append((1, data[p], None)); p += 1
            elif pid == 2:
                size, = struct.unpack_from("<I", data, p); p += 4
                self.packets.append((2, size, None))
            elif pid == 3:
                self.packets.append((3, 0, data[p:p+8192])); p += 8192
            else:
                raise ValueError(f"unknown packet id {pid} at {p-1:#x}")
        assert p == n, "stream did not end exactly at EOF"

REG_NAMES = {0x00:"PRIM",0x01:"RGBAQ",0x02:"ST",0x03:"UV",0x04:"XYZF2",0x05:"XYZ2",0x06:"TEX0_1",0x07:"TEX0_2",
 0x08:"CLAMP_1",0x09:"CLAMP_2",0x0a:"FOG",0x0c:"XYZF3",0x0d:"XYZ3",0x14:"TEX1_1",0x15:"TEX1_2",0x16:"TEX2_1",
 0x17:"TEX2_2",0x18:"XYOFFSET_1",0x19:"XYOFFSET_2",0x1a:"PRMODECONT",0x1b:"PRMODE",0x1c:"TEXCLUT",0x22:"SCANMSK",
 0x34:"MIPTBP1_1",0x35:"MIPTBP1_2",0x36:"MIPTBP2_1",0x37:"MIPTBP2_2",0x3b:"TEXA",0x3d:"FOGCOL",0x3f:"TEXFLUSH",
 0x40:"SCISSOR_1",0x41:"SCISSOR_2",0x42:"ALPHA_1",0x43:"ALPHA_2",0x44:"DIMX",0x45:"DTHE",0x46:"COLCLAMP",
 0x47:"TEST_1",0x48:"TEST_2",0x49:"PABE",0x4a:"FBA_1",0x4b:"FBA_2",0x4c:"FRAME_1",0x4d:"FRAME_2",0x4e:"ZBUF_1",
 0x4f:"ZBUF_2",0x50:"BITBLTBUF",0x51:"TRXPOS",0x52:"TRXREG",0x53:"TRXDIR",0x54:"HWREG",0x60:"SIGNAL",
 0x61:"FINISH",0x62:"LABEL",0x0e:"A+D"}

def walk_gif(buf, stats):
    """Walk GIF tags in one Transfer; fill stats counters."""
    p, n = 0, len(buf)
    while p + 16 <= n:
        lo, hi = struct.unpack_from("<QQ", buf, p); p += 16
        nloop = lo & 0x7fff; eop = (lo >> 15) & 1; pre = (lo >> 46) & 1
        flg = (lo >> 58) & 3; nreg = (lo >> 60) & 0xf or 16
        stats["tags"] += 1; stats["flg%d" % flg] += 1
        if flg == 0:
            for _ in range(nloop):
                for r in range(nreg):
                    if p + 16 > n: return p
                    reg = (hi >> (4*r)) & 0xf
                    lo2, hi2 = struct.unpack_from("<QQ", buf, p); p += 16
                    if reg == 0xe:
                        a = hi2 & 0xff; stats["ad:"+REG_NAMES.get(a, hex(a))] += 1
                        if a in (0x04,0x05,0x0c,0x0d): stats["vertex"] += 1
                    else:
                        stats["packed:"+REG_NAMES.get(reg, hex(reg))] += 1
                        if reg in (0x4,0x5): stats["vertex"] += 1
        elif flg == 1:
            q = (nloop * nreg + 1) // 2
            for _ in range(q * 1): p += 16
            stats["reglist_regs"] += nloop * nreg
        else:
            p += nloop * 16; stats["image_qw"] += nloop
        if p > n: break
    return p

def main():
    d = Dump(load(sys.argv[1]))
    print(f"serial={d.serial} crc={d.crc:#010x} state_version={d.state_version} state_size={d.state_size}")
    print(f"screenshot {d.ss_w}x{d.ss_h}  packets_start={d.packets_start:#x}  packets={len(d.packets)}")
    kinds = collections.Counter(p[0] for p in d.packets)
    print("packet kinds:", dict(kinds), " transfers by path:",
          dict(collections.Counter(p[1] for p in d.packets if p[0] == 0)))
    total = sum(len(p[2]) for p in d.packets if p[0] == 0)
    print(f"GIF payload: {total} bytes ({total//16} qwords)")
    st = collections.Counter()
    for p in d.packets:
        if p[0] == 0: walk_gif(p[2], st)
    for k, v in sorted(st.items(), key=lambda kv: -kv[1])[:40]: print(f"  {k:28s} {v}")

if __name__ == "__main__" and not (len(sys.argv) > 2 and sys.argv[1] == "draws"):
    main()


# ---- draw summary: track GS register state across the GIF stream and report each draw's setup ----
PSM = {0:"CT32",1:"CT24",2:"CT16",10:"CT16S",19:"T8H"if False else "?",20:"T4",19:"T8",27:"T8H",36:"T4HL",44:"T4HH",48:"Z32",49:"Z24",50:"Z16",58:"Z16S",
       0x13:"T8",0x14:"T4",0x1b:"T8H",0x24:"T4HL",0x2c:"T4HH",0x30:"Z32",0x31:"Z24",0x32:"Z16",0x3a:"Z16S"}
PRIMS = ["POINT","LINE","LSTRIP","TRI","TSTRIP","TFAN","SPRITE","?"]

def iter_register_writes(d, seg=0):
    """Yield (reg_addr, value) in GIF order for the seg-th frame (segments split by VSync packets)."""
    cur = 0
    remaining = 0; flg = 0; nreg = 0; regs = 0; loop_left = 0; r = 0
    for k, a, b in d.packets:
        if k == 1:
            cur += 1
            continue
        if k != 0 or cur != seg:
            continue
        p, n = 0, len(b)
        while p < n:
            if remaining == 0:
                lo, hi = struct.unpack_from("<QQ", b, p); p += 16
                nloop = lo & 0x7fff; flg = (lo >> 58) & 3; nreg = ((lo >> 60) & 0xf) or 16
                regs = hi; prim_pre = (lo >> 46) & 1
                if prim_pre and flg < 2: yield (0x00, (lo >> 47) & 0x7ff)
                if flg == 0: remaining = nloop * nreg
                elif flg == 1: remaining = (nloop * nreg + 1) // 2
                else: remaining = nloop
                loop_left = remaining; r = 0
                continue
            lo, hi = struct.unpack_from("<QQ", b, p); p += 16
            if flg == 0:
                reg = (regs >> (4 * (r % nreg))) & 0xf; r += 1
                if reg == 0xe: yield (hi & 0xff, lo)
                elif reg == 0xf: pass
                else: yield ({0:0x00,1:0x01,2:0x02,3:0x03,4:0x04,5:0x05,6:0x06,7:0x07,8:0x08,9:0x09,0xa:0x0a,0xc:0x0c,0xd:0x0d}.get(reg, 0x100+reg), (lo, hi))
            elif flg == 1:
                for half in (lo, hi):
                    reg = (regs >> (4 * (r % nreg))) & 0xf; r += 1
                    yield (reg, half)
            remaining -= 1

def draw_summary(d, seg=0):
    st = {"FRAME_1":0,"FRAME_2":0,"TEX0_1":0,"TEX0_2":0,"ALPHA_1":0,"ALPHA_2":0,"TEST_1":0,"TEST_2":0,"ZBUF_1":0,"ZBUF_2":0,"TEX1_1":0,"TEX1_2":0,"CLAMP_1":0,"CLAMP_2":0}
    prim = 0
    regmap = {0x06:"TEX0_1",0x07:"TEX0_2",0x42:"ALPHA_1",0x43:"ALPHA_2",0x47:"TEST_1",0x48:"TEST_2",0x4c:"FRAME_1",0x4d:"FRAME_2",
              0x4e:"ZBUF_1",0x4f:"ZBUF_2",0x14:"TEX1_1",0x15:"TEX1_2",0x08:"CLAMP_1",0x09:"CLAMP_2"}
    verts = 0; need = {0:1,1:2,2:2,3:3,4:3,5:3,6:2}
    combos = collections.Counter(); order = []
    for reg, val in iter_register_writes(d, seg):
        if reg == 0x00:
            prim = val if isinstance(val, int) else val[0]; verts = 0
        elif reg in regmap:
            st[regmap[reg]] = val if isinstance(val, int) else val[0]
        elif reg in (0x04, 0x05, 0x0c, 0x0d):
            kick = reg in (0x04, 0x05)
            verts += 1
            if kick and verts >= need.get(prim & 7, 3):
                c = (prim >> 9) & 1
                f = st["FRAME_%d" % (c+1)]; t = st["TEX0_%d" % (c+1)]; a = st["ALPHA_%d" % (c+1)]
                te = st["TEST_%d" % (c+1)]; z = st["ZBUF_%d" % (c+1)]; t1 = st["TEX1_%d" % (c+1)]
                cl = st["CLAMP_%d" % (c+1)]
                tme = (prim >> 4) & 1; abe = (prim >> 6) & 1; fst = (prim >> 8) & 1
                key = ("%s ctx%d FB(p=%d bw=%d %s) %s%s%s%s" % (PRIMS[prim & 7], c+1, f & 0x1ff, (f>>16)&0x3f, PSM.get((f>>24)&0x3f, (f>>24)&0x3f),
                       ("TEX(tbp=%d tbw=%d %s tw=%d th=%d tcc=%d tfx=%d cpsm=%d csm=%d cld=%d mmin=%d mmag=%d) " % (
                            t & 0x3fff, (t>>14)&0x3f, PSM.get((t>>20)&0x3f, (t>>20)&0x3f), 1<<((t>>26)&0xf), 1<<((t>>30)&0xf), (t>>34)&1, (t>>35)&3,
                            (t>>51)&0xf, (t>>55)&1, (t>>61)&7, (t1>>6)&7, (t1>>5)&1) if tme else "notex "),
                       ("ABE(a=%d b=%d c=%d d=%d fix=%#x) " % (a&3,(a>>2)&3,(a>>4)&3,(a>>6)&3,(a>>32)&0xff) if abe else ""),
                       "ATE" if te & 1 else "", " ZTE" if (te>>16)&1 else "") + (" CLAMP(wms=%d wmt=%d minu=%d maxu=%d minv=%d maxv=%d)" % (
                       cl&3, (cl>>2)&3, (cl>>4)&0x3ff, (cl>>14)&0x3ff, (cl>>24)&0x3ff, (cl>>34)&0x3ff) if tme else ""))
                combos[key] += 1
                if key not in order: order.append(key)
                if prim & 7 in (3, 4, 5): verts = 0 if (prim & 7) == 3 else verts   # tri strip/fan keep sliding window
                if prim & 7 in (0, 1, 6): verts = 0
    return combos, order

def cmd_draws(path, seg):
    d = Dump(load(path))
    combos, order = draw_summary(d, seg)
    print(f"segment {seg}: {sum(combos.values())} primitives, {len(combos)} distinct setups")
    for k in order: print(f"  {combos[k]:6d}  {k}")

if __name__ == "__main__" and len(sys.argv) > 2 and sys.argv[1] == "draws":
    cmd_draws(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 0)
