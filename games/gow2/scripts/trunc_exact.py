#!/usr/bin/env python3
"""Exact (pixel for pixel) comparison of a truncated dump: ours vs PCSX2 software renderer (see pcsx2_exact.py).
usage: trunc_exact.py K[,K...]   env TB_BLIT=FBP,FBW,PSM  TB_WH=W,H  TB_TAG=name
prints the mean absolute error per channel over the WxH region (first rows skipped), the share of identical pixels and
the share more than 8 levels off."""
import os, subprocess, sys
sys.path.insert(0, "/home/dev-lucas/Documentos/github/PS2Recomp/games/gow2/scripts")
import pcsx2_exact as X
DUMP = f"{X.R}/God of War II_SCUS-97481_20261015000734.gs.zst"
BLIT = os.environ.get("TB_BLIT", "8,8,0"); WH = os.environ.get("TB_WH", "512,214"); TAG = os.environ.get("TB_TAG", "_ex")
w, h = (int(v) for v in WH.split(","))
for k in sys.argv[1].split(","):
    dump = f"{X.R}/bis/tx{TAG}_{k}.gs"
    subprocess.run(["python3", "-I", f"{X.ROOT}/games/gow2/scripts/gsdump_trunc.py", DUMP, dump, "--packets", str(int(k) + 1), "--blit", BLIT, "--blit-wh", WH], capture_output=True, check=True)
    X.make_progressive(dump)
    shot = X.capture(dump); shot.save(f"{X.R}/bis/tx{TAG}_{k}_pcsx2raw.png")
    subprocess.run([f"{X.ROOT}/build-o2/games/gow2/tools/gow2_gs_gpu_replay", dump, "--frames", "1", "--out", f"{X.R}/bis/tx{TAG}_{k}_"], capture_output=True)
    r = X.compare(f"{X.R}/bis/tx{TAG}_{k}_gpu0.ppm", shot, w, h)
    print(f"cut {int(k):5d} [{BLIT} {WH}] exact: mean abs err R/G/B = {r['mean'][0]:5.2f} {r['mean'][1]:5.2f} {r['mean'][2]:5.2f}   identical {r['identical']:5.1f}%   >8 off {r['big']:5.2f}%", flush=True)
