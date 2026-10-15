#!/usr/bin/env python3
"""Bisect the packet stream: for each packet index K, cut the dump after packet K, append a blit of the
scene buffer into the display buffer, show it in PCSX2 (window capture) and in our replay, and print the error.
usage: trunc_bisect.py K [K ...]"""
import os, subprocess, sys, time
from PIL import Image, ImageChops, ImageStat, ImageGrab

ROOT = "/home/dev-lucas/Documentos/github/PS2Recomp"; R = "/home/dev-lucas/gow2/pcsx2-ref"
DUMP = f"{R}/God of War II_SCUS-97481_20261015000734.gs.zst"
APP = "/home/dev-lucas/Documentos/PCSX2/pcsx2-v2.9.127-linux-appimage-x64-Qt.AppImage"; DATA = "/tmp/claude-1000/pcsx2-data"

def pcsx2(dump):
    env = dict(os.environ, DISPLAY=":7", QT_QPA_PLATFORM="xcb")
    subprocess.run("ps -eo pid,comm | grep -i pcsx | awk '{print $1}' | xargs -r kill", shell=True); time.sleep(2)
    subprocess.Popen(["setsid", APP, "-datapath", DATA, "-batch", "-logfile", f"{DATA}/logs/t.log", "--", dump], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        if "God of War II" in subprocess.run(["python3", f"{ROOT}/games/gow2/scripts/x11_nested.py", "list"], env=env, capture_output=True, text=True).stdout: break
        time.sleep(1)
    time.sleep(7)
    g = ImageGrab.grab(xdisplay=":7").convert("RGB").crop((113, 48, 937, 665)).resize((512, 448), Image.LANCZOS)
    subprocess.run("ps -eo pid,comm | grep -i pcsx | awk '{print $1}' | xargs -r kill", shell=True); time.sleep(2)
    return g

def err(a, b): return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3

BLIT = os.environ.get("TB_BLIT", "8,8,0"); WH = os.environ.get("TB_WH", "512,448"); TAG = os.environ.get("TB_TAG", "")
for k in sys.argv[1:]:
    dump = f"{R}/bis/tb{TAG}_{k}.gs"
    subprocess.run(["python3", "-I", f"{ROOT}/games/gow2/scripts/gsdump_trunc.py", DUMP, dump, "--packets", str(int(k) + 1), "--blit", BLIT, "--blit-wh", WH],
                   capture_output=True, check=True)
    pc = pcsx2(dump); pc.save(f"{R}/bis/tb{TAG}_{k}_pcsx2.png")
    subprocess.run([f"{ROOT}/build-o2/games/gow2/tools/gow2_gs_gpu_replay", dump, "--frames", "1", "--out", f"{R}/bis/tb{TAG}_{k}_"], capture_output=True)
    ours = Image.open(f"{R}/bis/tb{TAG}_{k}_gpu0.ppm").convert("RGB"); ours.save(f"{R}/bis/tb{TAG}_{k}_ours.png")
    w, h = (int(v) for v in WH.split(","))
    box = (0, 0, min(512, w), min(448, h))      # only the blitted region is meaningful
    print(f"cut after packet {int(k):5d} [{BLIT} {WH}]: error ours-vs-PCSX2 = {err(ours.crop(box), pc.crop(box)):6.2f}", flush=True)
