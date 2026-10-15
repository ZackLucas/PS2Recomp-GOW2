#!/usr/bin/env python3
"""Exact (pixel-for-pixel) comparison of a truncated dump: ours vs PCSX2 software renderer.
Uses a private PCSX2 data dir with IntegerScaling=true/linear_present_mode=0/Stretch, where the picture is exactly
2x horizontally and 345/128 vertically (nearest), so every source pixel is read back at its own screen position.
usage: trunc_exact.py K[,K...]   env TB_BLIT=FBP,FBW,PSM  TB_WH=W,H  TB_TAG=name
prints the mean absolute error per channel over the WxH region and the share of pixels that are identical."""
import os, shutil, subprocess, sys, time
from PIL import Image, ImageGrab
ROOT = "/home/dev-lucas/Documentos/github/PS2Recomp"; R = "/home/dev-lucas/gow2/pcsx2-ref"
DUMP = f"{R}/God of War II_SCUS-97481_20261015000734.gs.zst"
APP = "/home/dev-lucas/Documentos/PCSX2/pcsx2-v2.9.127-linux-appimage-x64-Qt.AppImage"
SRC = "/tmp/claude-1000/pcsx2-data"; DATA = "/tmp/claude-1000/pcsx2-data-exact"
if not os.path.isdir(DATA):
    shutil.copytree(SRC, DATA, symlinks=True)
ini = f"{DATA}/PCSX2/inis/PCSX2.ini"; t = open(ini).read()
t = t.replace("IntegerScaling = false", "IntegerScaling = true").replace("linear_present_mode = 1", "linear_present_mode = 0")
import re; t = re.sub(r"(?m)^AspectRatio = .*$", "AspectRatio = Stretch", t); open(ini, "w").write(t)

def kill():
    """Kill every PCSX2 instance (matched by the AppImage path in the command line) and wait until none is left;
    plain `comm` matching and SIGTERM both let stale instances pile up and contaminate captures."""
    for _ in range(20):
        pids = subprocess.run("ps -eo pid,args | awk '/pcsx2-v2.9/ && !/awk/ {print $1}'", shell=True, capture_output=True, text=True).stdout.split()
        if not pids: break
        subprocess.run(["kill", "-9"] + pids); time.sleep(0.5)
    time.sleep(1)
def capture(dump):
    env = dict(os.environ, DISPLAY=":7", QT_QPA_PLATFORM="xcb"); kill()
    subprocess.Popen(["setsid", APP, "-datapath", DATA, "-batch", "-logfile", f"{DATA}/logs/t.log", "--", dump], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        if "God of War II" in subprocess.run(["python3", f"{ROOT}/games/gow2/scripts/x11_nested.py", "list"], env=env, capture_output=True, text=True).stdout: break
        time.sleep(1)
    time.sleep(7); g = ImageGrab.grab(xdisplay=":7").convert("RGB"); kill(); return g

def region_origin(g):
    """Locate the picture: top-left screen pixel of source (0,0) is calibrated against the white-free layout; the
    horizontal origin is 14 and the vertical one 55 for the 1050x640 window (see PROGRESS.md)."""
    return 14, 55

import struct
def make_progressive(path):
    """SMODE2 = 0 (no interlace/field) in the header registers and in every Registers packet, DISPFB = p=232 CT24 FBW=8."""
    d = bytearray(open(path, "rb").read())
    hsize, = struct.unpack_from("<I", d, 4); ssize, = struct.unpack_from("<I", d, 12)
    off = 8 + hsize + ssize
    def patch(o):
        struct.pack_into("<Q", d, o + 0x20, 0)
        fb = 232 | (8 << 9) | (1 << 15); struct.pack_into("<Q", d, o + 0x70, fb); struct.pack_into("<Q", d, o + 0x90, fb)
    patch(off); p = off + 8192
    while p < len(d):
        kind = d[p]
        if kind == 0: p += 6 + struct.unpack_from("<I", d, p + 2)[0]
        elif kind == 1: p += 2
        elif kind == 2: p += 5
        elif kind == 3: patch(p + 1); p += 1 + 8192
        else: break
    open(path, "wb").write(d)

BLIT = os.environ.get("TB_BLIT", "8,8,0"); WH = os.environ.get("TB_WH", "512,448"); TAG = os.environ.get("TB_TAG", "_ex")
w, h = (int(v) for v in WH.split(","))
for k in sys.argv[1].split(","):
    dump = f"{R}/bis/tx{TAG}_{k}.gs"
    subprocess.run(["python3", "-I", f"{ROOT}/games/gow2/scripts/gsdump_trunc.py", DUMP, dump, "--packets", str(int(k) + 1), "--blit", BLIT, "--blit-wh", WH], capture_output=True, check=True)
    make_progressive(dump)
    g = capture(dump); g.save(f"{R}/bis/tx{TAG}_{k}_pcsx2raw.png")
    subprocess.run([f"{ROOT}/build-o2/games/gow2/tools/gow2_gs_gpu_replay", dump, "--frames", "1", "--out", f"{R}/bis/tx{TAG}_{k}_"], capture_output=True)
    ours = Image.open(f"{R}/bis/tx{TAG}_{k}_gpu0.ppm").convert("RGB"); op = ours.load(); gp = g.load()
    x0, y0 = region_origin(g); sc_y = 345.0 / 128.0
    tot = [0, 0, 0]; same = 0; n = 0; big = 0
    for y in range(16, h):          # skip the PCSX2 on-screen text (Dump Frame / Packet Number) in the first rows
        Y = int(y0 + (y + 0.5) * sc_y)
        for x in range(w):
            a = op[x, y]; b = gp[x0 + 2 * x + 1, Y]
            for c in range(3): tot[c] += abs(a[c] - b[c])
            same += (a == b); n += 1; big += (max(abs(a[c] - b[c]) for c in range(3)) > 8)
    print(f"cut {int(k):5d} [{BLIT} {WH}] exact: mean abs err R/G/B = {tot[0]/n:5.2f} {tot[1]/n:5.2f} {tot[2]/n:5.2f}   identical pixels {100.0*same/n:5.1f}%   >8 off {100.0*big/n:5.2f}%", flush=True)
