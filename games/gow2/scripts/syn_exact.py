#!/usr/bin/env python3
"""Exact comparison of a synthetic dump (made by gsdump_synth.py) against PCSX2.
usage: syn_exact.py <experiment> [W H]   (dump in $R/syn/<experiment>.gs, our image from gow2_gs_gpu_replay)"""
import subprocess, sys
sys.path.insert(0, "/home/dev-lucas/Documentos/github/PS2Recomp/games/gow2/scripts")
import pcsx2_exact as X
name = sys.argv[1]; w = int(sys.argv[2]) if len(sys.argv) > 2 else 128; h = int(sys.argv[3]) if len(sys.argv) > 3 else 128
O = f"{X.R}/syn"
import os
if os.environ.get("SYN_CPU"):      # CPU model image (gow2_gs_replay) instead of the GPU one
    subprocess.run([f"{X.ROOT}/build-o2/games/gow2/tools/gow2_gs_replay", f"{O}/{name}.gs", "--vram-offset", "425", "--frames", "1", "--out", f"{O}/{name}_cpu"], capture_output=True)
    img = f"{O}/{name}_cpu0.ppm"
else:
    subprocess.run([f"{X.ROOT}/build-o2/games/gow2/tools/gow2_gs_gpu_replay", f"{O}/{name}.gs", "--frames", "1", "--out", f"{O}/{name}_"], capture_output=True)
    img = f"{O}/{name}_gpu0.ppm"
if os.environ.get("SYN_REUSE") and os.path.exists(f"{O}/{name}_raw.png"):
    from PIL import Image; shot = Image.open(f"{O}/{name}_raw.png").convert("RGB")
    import re as _re   # calibration stored by the capture that made the raw image: default window offsets
else:
    shot = X.capture(f"{O}/{name}.gs"); shot.save(f"{O}/{name}_raw.png")
r = X.compare(img, shot, w, h)
print(f"{name}: {r['n']} px, identical {r['identical']:.1f}%, mean abs R/G/B {r['mean'][0]:.2f} {r['mean'][1]:.2f} {r['mean'][2]:.2f}, >8 off {r['big']:.2f}%, diffs {r['hist']}")
