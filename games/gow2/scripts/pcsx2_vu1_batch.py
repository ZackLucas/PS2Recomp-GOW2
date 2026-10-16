#!/usr/bin/env python3
"""For each captured VU1 call: run it in PCSX2 (savestate injection), run it in our interpreter/native VU1, compare
the resulting 16 KiB VU1 data memory. Prints one line per case.

  pcsx2_vu1_batch.py <base.p2s> <vu1progs dir> <outdir> in_<hash>_pc<N>_<k> [...]
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pcsx2_vu1_inject as I
import pcsx2_exact as P

RUN_ONE = f"{P.ROOT}/build-o2/games/gow2/tools/gow2_vu1_run_one"


def diff(a, b):
    return sum(1 for x, y in zip(a, b) if x != y)


def qdiff(a, b):
    return sum(1 for q in range(1024) if a[q * 16:q * 16 + 16] != b[q * 16:q * 16 + 16])


def main():
    base, vdir, outdir = sys.argv[1:4]
    total = same = 0
    for name in sys.argv[4:]:
        cap = f"{vdir}/{name}.bin"
        d = f"{outdir}/{name}"
        os.makedirs(d, exist_ok=True)
        r = subprocess.run([RUN_ONE, cap, f"{d}/ours"], capture_output=True, text=True).stdout
        I.build_state(base, cap, f"{d}/injected.p2s")
        res = I.run(f"{d}/injected.p2s", d) or I.run(f"{d}/injected.p2s", d)  # one retry: PCSX2 sometimes misses the save
        if not res:
            print(f"{name}: PCSX2 produced no state"); continue
        px = open(res, "rb").read()
        inp = open(cap, "rb").read()[20 + 664:]
        ours = open(f"{d}/ours_interp_mem.bin", "rb").read()
        nat = open(f"{d}/ours_native_mem.bin", "rb").read() if os.path.exists(f"{d}/ours_native_mem.bin") else None
        total += 1
        ok = px == ours
        same += ok
        print(f"{name}: PCSX2 changed {qdiff(inp, px)} qwords, ours(interp) changed {qdiff(inp, ours)}; "
              f"PCSX2 vs interp: {diff(px, ours)} bytes / {qdiff(px, ours)} qwords differ; "
              f"vs native: {'n/a' if nat is None else str(diff(px, nat)) + ' bytes'}  -> {'IDENTICAL' if ok else 'DIFFERENT'}", flush=True)
    print(f"SUMMARY: {same}/{total} identical")


main()
