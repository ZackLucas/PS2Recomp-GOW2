#!/usr/bin/env python3
"""Builds vu1_run_one / pcsx2_vu1_inject inputs from a PCSX2 savestate's own VU1 state (sane numbers, unlike the
registers our runtime hands the VU1). Any (memory, registers, entry, TOP/ITOP) is a valid input for a deterministic
program, so the same state can be run in PCSX2 (injection) and in our VU1 and compared.

  vu1_ss_inputs.py <savestate-dir-with *_internal.dat> <tag> <outdir>   e.g. ~/gow2/pcsx2-ref/sstates menu outdir
Writes <outdir>/vu1_<hash>.bin (code) and <outdir>/in_<hash>_pc<N>_<k>.bin for entries 0/24/48 and TOP/ITOP combos.
"""
import os, struct, sys

VU1_ACC, VU1_VF, VU1_VI = 15632, 15648, 16160   # offsets in 'PCSX2 Internal Structures.dat' (version 36, menu state)

def main():
    sdir, tag, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    b = open(f"{sdir}/{tag}_internal.dat", "rb").read()
    code = open(f"{sdir}/{tag}_vu1MicroMem.bin", "rb").read()
    data = open(f"{sdir}/{tag}_vu1Memory.bin", "rb").read()
    h = {"menu": "a" * 16, "user": "b" * 16}.get(tag, "c" * 16)
    open(f"{out}/vu1_{h}.bin", "wb").write(code)
    vf = b[VU1_VF:VU1_VF + 512]
    vi_raw = [struct.unpack_from("<I", b, VU1_VI + k * 16)[0] for k in range(32)]
    vi = [struct.unpack("<h", struct.pack("<H", vi_raw[k] & 0xFFFF))[0] for k in range(16)]
    acc = b[VU1_ACC:VU1_ACC + 16]
    status, mac, clip, r = vi_raw[16], vi_raw[17], vi_raw[18], vi_raw[20]
    i_bits, q_bits, p_bits = vi_raw[21], vi_raw[22], vi_raw[23]
    st = bytearray(664)
    st[0:512] = vf
    struct.pack_into("<16i", st, 512, *vi)
    st[576:592] = acc
    struct.pack_into("<3I", st, 592, q_bits, p_bits, i_bits)
    struct.pack_into("<I", st, 604, r)
    struct.pack_into("<4I", st, 608, 0, mac, clip, status)
    k = 0
    for pc in (0, 24, 48):
        for top, itop in ((0x155, 0xF6), (0x2AB, 0x24B), (0x0, 0xF6)):
            hdr = struct.pack("<5I", 0x4E495556, pc, top, itop, 664)
            open(f"{out}/in_{h}_pc{pc}_{k % 3}_{top:x}.bin", "wb").write(hdr + bytes(st) + data)
            k += 1
    print(f"wrote inputs for {tag} (hash {h}) into {out}")

main()
