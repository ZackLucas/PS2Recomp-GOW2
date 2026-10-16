#!/usr/bin/env python3
"""Runs one captured VU1 call inside PCSX2 and returns the VU1 data memory PCSX2 ends up with.

PCSX2 gives no way to run a VU1 program on chosen input, so we edit a savestate (version 36, taken at the menu):
  * vu1MicroMem.bin / vu1Memory.bin  <- the captured code image and 16 KiB data memory
  * 'PCSX2 Internal Structures.dat'  <- VU1 ACC/VF/VI/q/p, TPC, start_pc, VU0.VI[29] (VPU_STAT busy bit)
  * eeHwRegs.bin                     <- VIF1 TOP/ITOP/TOPS/ITOPS (read by XTOP/XITOP)
  * eeMemory.bin                     <- the instruction at the EE pc becomes `b .` so the game cannot touch VU1 again
Then PCSX2 loads it (private data dir: no MTVU, no flag hack), F1 saves a new state, and we read its vu1Memory.bin.
Offsets were derived from PCSX2's vuMicroFreeze() (VUmicroMem.cpp) and verified against the menu savestate.

  pcsx2_vu1_inject.py <base.p2s> <in_<hash>_pc<N>_<k>.bin> <outdir>
"""
import os, re, shutil, struct, subprocess, sys, time, zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcsx2_exact as P

DATA2 = "/tmp/pcsx2-inject-data"
ISO = "/home/dev-lucas/Documentos/PCSX2/Jogos/God of War II (USA).iso"
SLOT = "SCUS-97481 (2F123FD8).01.p2s"

# --- offsets inside 'PCSX2 Internal Structures.dat' of the menu savestate (see header) ---
VU0_VI29 = 15054            # VU0.VI[29] = VPU_STAT, bit 8 = VU1 running
VU1_ACC, VU1_VF, VU1_VI = 15632, 15648, 16160
VU1_Q, VU1_P = 16672, 16688
VU1_START_PC = 16720
CPUREGS_PC = 322 + 32 + 680  # tag offset + 32-byte tag + offsetof(cpuRegisters, pc)


def make_env():
    ini = f"{DATA2}/PCSX2/inis/PCSX2.ini"
    if not os.path.exists(ini):
        os.makedirs(f"{DATA2}/PCSX2/inis", exist_ok=True)
        shutil.copy(f"{P.DATA}/PCSX2/inis/PCSX2.ini", ini)
    t = open(ini).read()
    for key, val in (("vuThread", "false"), ("vuFlagHack", "false"), ("vu1Instant", "false")):
        t = re.sub(rf"(?m)^{key}\s*=.*$", f"{key} = {val}", t)
    open(ini, "w").write(t)
    os.makedirs(f"{DATA2}/logs", exist_ok=True)
    for d in ("memcards", "bios", "cache", "gamesettings", "inputprofiles", "sstates"):
        os.makedirs(f"{DATA2}/PCSX2/{d}", exist_ok=True)


def kill_mine():
    for _ in range(20):
        pids = subprocess.run(["pgrep", "-f", f"pcsx2-v2.9.*-datapath {DATA2}"], capture_output=True, text=True).stdout.split()
        if not pids:
            break
        subprocess.run(["kill", "-9"] + pids)
        time.sleep(0.5)


def build_state(base, capture, out):
    cap = open(capture, "rb").read()
    magic, entry_pc, top, itop, ssz = struct.unpack("<5I", cap[:20])
    assert magic == 0x4E495556 and ssz == 664, "unexpected capture format"
    st = cap[20:20 + ssz]
    data = cap[20 + ssz:]
    vf = st[0:512]
    vi = struct.unpack("<16i", st[512:576])
    acc = st[576:592]
    q, p, i = struct.unpack("<3f", st[592:604])
    r, pc_, mac, clip, status = struct.unpack("<5I", st[604:624])
    code_img = open(os.path.join(os.path.dirname(capture), "vu1_" + os.path.basename(capture)[3:19] + ".bin"), "rb").read()

    zin = zipfile.ZipFile(base)
    blobs = {n: bytearray(zin.read(n)) for n in zin.namelist()}
    blobs["vu1MicroMem.bin"][:] = code_img[:16384]
    blobs["vu1Memory.bin"][:] = data[:16384]

    b = blobs["PCSX2 Internal Structures.dat"]
    b[VU1_ACC:VU1_ACC + 16] = acc
    b[VU1_VF:VU1_VF + 512] = vf
    for k in range(16):
        struct.pack_into("<I", b, VU1_VI + k * 16, vi[k] & 0xFFFF)
    for reg, val in ((16, status), (17, mac), (18, clip), (20, r)):
        struct.pack_into("<I", b, VU1_VI + reg * 16, val)
    struct.pack_into("<f", b, VU1_VI + 21 * 16, i)
    struct.pack_into("<f", b, VU1_VI + 22 * 16, q)
    struct.pack_into("<f", b, VU1_VI + 23 * 16, p)
    struct.pack_into("<f", b, VU1_Q, q)
    struct.pack_into("<f", b, VU1_P, p)
    struct.pack_into("<I", b, VU1_VI + 26 * 16, entry_pc // 8)   # TPC in pair units
    struct.pack_into("<I", b, VU1_START_PC, entry_pc)            # start_pc in bytes
    vpu = struct.unpack_from("<I", b, VU0_VI29)[0]
    struct.pack_into("<I", b, VU0_VI29, vpu | 0x100)             # VU1 busy

    hw = blobs["eeHwRegs.bin"]
    for off, val in ((0x3CE0, top), (0x3CC0, top), (0x3CD0, itop), (0x3C90, itop)):
        struct.pack_into("<I", hw, off, val)

    ee_pc = struct.unpack_from("<I", b, CPUREGS_PC)[0]
    ee = blobs["eeMemory.bin"]
    struct.pack_into("<II", ee, ee_pc & 0x1FFFFFF, 0x1000FFFF, 0)  # b . ; nop
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as zout:
        for n in zin.namelist():
            zout.writestr(n, bytes(blobs[n]))
    return dict(entry=entry_pc, top=top, itop=itop, ee_pc=ee_pc)


def run(state, outdir, wait_load=20):
    make_env()
    P.ensure_env()
    env = dict(os.environ, DISPLAY=":7", QT_QPA_PLATFORM="xcb")
    kill_mine()
    slot = f"{DATA2}/PCSX2/sstates/{SLOT}"
    for f in (slot, slot + ".backup"):
        if os.path.exists(f):
            os.remove(f)
    subprocess.Popen(["setsid", P.APP, "-datapath", DATA2, "-batch", "-logfile", f"{DATA2}/logs/inj.log",
                      "-statefile", state, "--", ISO], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(wait_load)
    subprocess.run(["python3", f"{P.ROOT}/games/gow2/scripts/x11_nested.py", "key", "F1"], env=env)
    time.sleep(4)
    kill_mine()
    if not os.path.exists(slot):
        return None
    z = zipfile.ZipFile(slot)
    os.makedirs(outdir, exist_ok=True)
    shutil.copy(slot, f"{outdir}/pcsx2_after.p2s")
    open(f"{outdir}/pcsx2_mem.bin", "wb").write(z.read("vu1Memory.bin"))
    return f"{outdir}/pcsx2_mem.bin"


if __name__ == "__main__":
    base, capture, outdir = sys.argv[1:4]
    os.makedirs(outdir, exist_ok=True)
    info = build_state(base, capture, f"{outdir}/injected.p2s")
    print("injected:", info)
    res = run(f"{outdir}/injected.p2s", outdir)
    print("pcsx2 result memory:", res or "NOT PRODUCED (state not saved)")
