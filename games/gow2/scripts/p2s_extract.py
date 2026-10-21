#!/usr/bin/env python3
"""Extract members of a PCSX2 savestate (.p2s: a zip whose members use zstd, method 93, which zipfile cannot open).
   p2s_extract.py <savestate.p2s> <outdir> [member ...]   e.g. eeMemory.bin Scratchpad.bin vu1Memory.bin
Needs the zstd command. Compare eeMemory.bin/Scratchpad.bin with the runtime's PS2_STATE_DUMP files."""
import os, struct, subprocess, sys, zipfile

p2s, out = sys.argv[1], sys.argv[2]
want = set(sys.argv[3:])
os.makedirs(out, exist_ok=True)
with zipfile.ZipFile(p2s) as z, open(p2s, 'rb') as f:
    for info in z.infolist():
        if want and info.filename not in want:
            continue
        f.seek(info.header_offset)
        name_len, extra_len = struct.unpack_from('<HH', f.read(30), 26)
        f.seek(info.header_offset + 30 + name_len + extra_len)
        raw = f.read(info.compress_size)
        data = raw if info.compress_type == 0 else subprocess.run(['zstd', '-dc'], input=raw, check=True, capture_output=True).stdout
        with open(os.path.join(out, info.filename), 'wb') as o:
            o.write(data)
        print(info.filename, len(data))
