#!/usr/bin/env python3
"""Shared helpers for exact (pixel for pixel) comparisons against the PCSX2 software renderer.

Environment is created on demand: a nested Xwayland :7, and a private PCSX2 data dir (PCSX2_EXACT_DATA, default
/tmp/pcsx2-exact-data) copied from the user's ini with IntegerScaling=true, linear_present_mode=0 and AspectRatio=Stretch.
In that mode the picture of a progressive dump is exactly 2x horizontally (origin x=14) and 345/128 per row
vertically (origin y=55); only the first ~214 rows are visible and the first 16 are covered by PCSX2's OSD text.

TRAP: stale PCSX2 instances pile up and show the picture of an older dump. kill_all() matches the AppImage path
in the command line with SIGKILL and waits until none is left."""
import os, re, shutil, struct, subprocess, sys, time
from PIL import Image, ImageGrab

ROOT = "/home/dev-lucas/Documentos/github/PS2Recomp"
R = "/home/dev-lucas/gow2/pcsx2-ref"
PCSX2_DIR = os.path.expanduser("~/Documentos/PCSX2")
APP = f"{PCSX2_DIR}/pcsx2-v2.9.127-linux-appimage-x64-Qt.AppImage"
DATA = os.environ.get("PCSX2_EXACT_DATA", "/tmp/pcsx2-exact-data")
WIN_OFF = (13, 30)            # picture origin relative to the PCSX2 window position
X0, Y0, SCALE_X, SCALE_Y = 13, 30, 2, 345.0 / 128.0   # defaults for a window at (0,0); capture() recalibrates
FIRST_ROW = 16

def ensure_env():
    if not os.path.exists("/tmp/.X11-unix/X7"):
        subprocess.Popen(["Xwayland", ":7", "-geometry", "1280x960", "-noreset"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        time.sleep(3)
    ini = f"{DATA}/PCSX2/inis/PCSX2.ini"
    if not os.path.exists(ini):
        os.makedirs(f"{DATA}/PCSX2/inis", exist_ok=True)
        shutil.copy(os.path.expanduser("~/.config/PCSX2/inis/PCSX2.ini"), ini)
        t = open(ini).read()
        for pat, rep in [(r"(?m)^SetupWizardIncomplete.*$", "SetupWizardIncomplete = false"), (r"(?m)^Renderer\s*=.*$", "Renderer = 13"),
                         (r"(?m)^Bios\s*=.*$", f"Bios = {PCSX2_DIR}/PS2_BIOS"), (r"(?m)^ConfirmShutdown.*$", "ConfirmShutdown = false"),
                         (r"(?m)^IntegerScaling.*$", "IntegerScaling = true"), (r"(?m)^linear_present_mode.*$", "linear_present_mode = 0"),
                         (r"(?m)^AspectRatio.*$", "AspectRatio = Stretch")]:
            t = re.sub(pat, rep, t)
        open(ini, "w").write(t)
    os.makedirs(f"{DATA}/logs", exist_ok=True)

def kill_all():
    for _ in range(20):
        pids = subprocess.run("ps -eo pid,args | awk '/pcsx2-v2.9/ && !/awk/ {print $1}'", shell=True, capture_output=True, text=True).stdout.split()
        if not pids: break
        subprocess.run(["kill", "-9"] + pids); time.sleep(0.5)
    time.sleep(1)

def capture(dump):
    """Show `dump` in PCSX2 (software renderer) and return the nested X screen as an RGB image."""
    ensure_env(); env = dict(os.environ, DISPLAY=":7", QT_QPA_PLATFORM="xcb"); kill_all()
    subprocess.Popen(["setsid", APP, "-datapath", DATA, "-batch", "-logfile", f"{DATA}/logs/t.log", "--", dump], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for _ in range(40):
        out = subprocess.run(["python3", f"{ROOT}/games/gow2/scripts/x11_nested.py", "list"], env=env, capture_output=True, text=True).stdout
        if "God of War II" in out: break
        time.sleep(1)
    m = re.search(r"pos=\((-?\d+),(-?\d+)\) size=1050x\d+ mapstate=2 name='God of War II'", out)
    global X0, Y0
    if m: X0, Y0 = int(m.group(1)) + WIN_OFF[0], int(m.group(2)) + WIN_OFF[1]
    time.sleep(7); g = ImageGrab.grab(xdisplay=":7").convert("RGB"); kill_all(); return g

def make_progressive(path):
    """SMODE2 = 0 (no interlace/field) and DISPFB = p=232 CT24 FBW=8 in the header registers and every Registers packet."""
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

def compare(ours_ppm, shot, w, h, first_row=FIRST_ROW):
    """Compare our presented image with the PCSX2 window grab pixel for pixel over the top-left w x h region."""
    op = Image.open(ours_ppm).convert("RGB").load(); gp = shot.load()
    tot = [0, 0, 0]; same = n = big = 0; hist = {}
    for y in range(first_row, h):
        Y = int(Y0 + (y + 0.5) * SCALE_Y)
        for x in range(w):
            a = op[x, y]; b = gp[X0 + SCALE_X * x + 1, Y]
            n += 1; same += (a == b); big += max(abs(a[c] - b[c]) for c in range(3)) > 8
            for c in range(3):
                d = a[c] - b[c]; tot[c] += abs(d); hist[d] = hist.get(d, 0) + 1
    return {"n": n, "identical": 100.0 * same / max(n, 1), "mean": [t / max(n, 1) for t in tot], "big": 100.0 * big / max(n, 1),
            "hist": sorted(hist.items(), key=lambda kv: -kv[1])[:6]}
