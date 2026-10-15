#!/usr/bin/env bash
# Run a GS dump in PCSX2 (software renderer) inside a private nested X server and save its screenshot.
# usage: pcsx2_oracle.sh dump.gs[.zst] out.png
# Needs: PCSX2 AppImage + BIOS configured (PCSX2_DIR), Xwayland, python3 with libX11/libXtst.
set -u
DUMP="$1"; OUT="$2"
PCSX2_DIR="${PCSX2_DIR:-$HOME/Documentos/PCSX2}"
APPIMAGE="$(ls "$PCSX2_DIR"/pcsx2-*.AppImage | head -1)"
DATA="${PCSX2_ORACLE_DATA:-/tmp/pcsx2-oracle-data}"
HERE="$(cd "$(dirname "$0")" && pwd)"
export DISPLAY=:7

if [ ! -S /tmp/.X11-unix/X7 ]; then
  (Xwayland :7 -geometry 1280x960 -noreset >/dev/null 2>&1 &)
  sleep 3
fi
mkdir -p "$DATA/PCSX2/inis" "$DATA/PCSX2/snaps"
if [ ! -f "$DATA/PCSX2/inis/PCSX2.ini" ]; then
  cp "$HOME/.config/PCSX2/inis/PCSX2.ini" "$DATA/PCSX2/inis/PCSX2.ini"
  sed -i -E 's/^SetupWizardIncomplete.*/SetupWizardIncomplete = false/; s/^Renderer\s*=.*/Renderer = 13/; s#^Bios\s*=.*#Bios = '"$PCSX2_DIR"'/PS2_BIOS#; s/^ConfirmShutdown.*/ConfirmShutdown = false/' "$DATA/PCSX2/inis/PCSX2.ini"
fi
rm -f "$DATA/PCSX2/snaps/"*.png

QT_QPA_PLATFORM=xcb setsid "$APPIMAGE" -datapath "$DATA" -batch -logfile "$DATA/emu.log" -- "$DUMP" >/dev/null 2>&1 &
PGID=$!
for i in $(seq 1 60); do   # wait for the render window
  python3 "$HERE/x11_nested.py" list 2>/dev/null | grep -q "God of War II" && break
  sleep 1
done
sleep 6                    # let the replay present a few frames
# NOTE: PCSX2's F8 screenshot does NOT return the displayed frame while replaying a GS dump (it ignores the
# dump's VRAM and packets). The window itself is truthful, so grab the nested X display and crop the render area.
PY="${PYTHON:-/home/dev-lucas/gow2/tools/bin/python3}"
"$PY" - "$OUT" <<'PYEOF'
import sys
from PIL import ImageGrab
img = ImageGrab.grab(xdisplay=":7").convert("RGB")
# render area of the 1050x666 window: below the two menu bars, above the status bar
x0, y0, x1, y1 = 0, 48, 1050, 665
img.crop((x0, y0, x1, y1)).save(sys.argv[1])
PYEOF
kill -- -"$PGID" 2>/dev/null
echo "ok $OUT"
