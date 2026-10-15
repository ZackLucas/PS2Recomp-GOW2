#!/usr/bin/env python3
"""Bisect which part of the initial VRAM makes our GS disagree with PCSX2.

For a page range [a,b) (8 KiB pages): zero that range in the dump's initial VRAM, replay it in PCSX2
(nested X, software renderer, window capture) and in our GS with the same zeroing, and print the error
between the two. If zeroing a range makes the error collapse, the data that matters lives there.
usage: vram_bisect.py a-b [a-b ...]
"""
import os, struct, subprocess, sys, time
from PIL import Image, ImageChops, ImageStat, ImageGrab

ROOT = "/home/dev-lucas/Documentos/github/PS2Recomp"
R = "/home/dev-lucas/gow2/pcsx2-ref"
DUMP = f"{R}/God of War II_SCUS-97481_20261015000734.gs.zst"
APP = "/home/dev-lucas/Documentos/PCSX2/pcsx2-v2.9.127-linux-appimage-x64-Qt.AppImage"
DATA = "/tmp/claude-1000/pcsx2-data"
VRAM_OFF = 425

def make_dump(ranges, out):
    data = bytearray(subprocess.run(["zstd", "-dc", DUMP], check=True, capture_output=True).stdout)
    hsize, = struct.unpack_from("<I", data, 4)
    base = 8 + hsize + VRAM_OFF
    for a, b in ranges:
        data[base + a * 8192: base + b * 8192] = bytes((b - a) * 8192)
    open(out, "wb").write(data)

def pcsx2_frames(dump, n=6, gap=3.0):
    env = dict(os.environ, DISPLAY=":7", QT_QPA_PLATFORM="xcb")
    proc = subprocess.Popen(["setsid", APP, "-datapath", DATA, "-batch", "-logfile", f"{DATA}/logs/b.log", "--", dump],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        for _ in range(40):
            out = subprocess.run(["python3", f"{ROOT}/games/gow2/scripts/x11_nested.py", "list"], env=env, capture_output=True, text=True).stdout
            if "God of War II" in out: break
            time.sleep(1)
        time.sleep(7)
        frames = []
        for _ in range(n):
            g = ImageGrab.grab(xdisplay=":7").convert("RGB").crop((113, 48, 937, 665)).resize((512, 448), Image.LANCZOS)
            frames.append(g); time.sleep(gap)
        return frames
    finally:
        subprocess.run("ps -eo pid,comm | grep -i pcsx | awk '{print $1}' | xargs -r kill", shell=True)
        time.sleep(2)

def ours_frames(ranges, tag):
    env = dict(os.environ, GT_ZERO_PAGES=",".join(f"{a}-{b}" for a, b in ranges))
    subprocess.run([f"{ROOT}/build-o2/games/gow2/tools/gow2_gs_gpu_replay", DUMP, "--out", f"{R}/out/bis_{tag}_"],
                   env=env, capture_output=True)
    return [Image.open(f"{R}/out/bis_{tag}_gpu{f}.ppm").convert("RGB") for f in (1, 2, 3)]

def err(a, b): return sum(ImageStat.Stat(ImageChops.difference(a, b)).mean) / 3

if __name__ == "__main__":
    for spec in sys.argv[1:]:
        ranges = [tuple(int(v) for v in r.split("-")) for r in spec.split(",")] if spec != "none" else []
        tag = spec.replace(",", "_").replace("-", "to")
        dump = f"{R}/bis/vb_{tag}.gs"; make_dump(ranges, dump)
        pc = pcsx2_frames(dump); ours = ours_frames(ranges, tag)
        e = min(err(o, p) for o in ours for p in pc[2:])
        print(f"zeroed pages {spec:>14}: min error ours-vs-PCSX2 = {e:6.2f}", flush=True)
