#!/usr/bin/env python3
"""Summarize bounded SPU2 PCM/log captures without loading either file whole.

PCM is stereo s16le at 48 kHz (the validated Linux x86-64 capture format).
The optional WAV starts 100 ms before the first nonzero frame; no gain or
resampling is applied. Keep all outputs containing game samples outside git.
"""
import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import wave


def summarize(pcm, log=None):
    count = nonzero = squares = peak = clipped = 0
    first = None
    digest = hashlib.sha256()
    with pcm.open("rb") as source:
        while data := source.read(65536):
            if len(data) % 4:
                raise ValueError("PCM must contain complete stereo frames")
            digest.update(data)
            samples = array("h")
            samples.frombytes(data)
            if sys.byteorder != "little":
                samples.byteswap()
            for i, value in enumerate(samples):
                if value:
                    nonzero += 1
                    if first is None:
                        first = (count + i) // 2
                peak = max(peak, abs(value))
                clipped += value in (-32768, 32767)
                squares += value * value
            count += len(samples)
    result = dict(pcm=str(pcm.resolve()), sha256=digest.hexdigest(),
                  format="s16le stereo 48000 Hz", frames=count // 2,
                  seconds=count / 96000, nonzero_samples=nonzero,
                  first_nonzero_frame=first, peak=peak,
                  rms=math.sqrt(squares / count) if count else 0,
                  clipped_samples=clipped)
    if log:
        errors = polls = 0
        transfers = []
        positions = {}
        with log.open(errors="replace") as source:
            for line in source:
                errors += "989 ERROR 52" in line
                if "event=spu_dma core=" in line:
                    fields = dict(re.findall(r"\b(\w+)=([^\s]+)", line))
                    transfers.append(dict(core=int(fields["core"]), direction=fields["direction"],
                                          copied=fields["copied"] == "1", bytes=int(fields["bytes"]),
                                          tsa=int(fields["tsa"], 16),
                                          hashes_match=fields["iop_hash"] == fields["spu_hash"]))
                if "event=pc pc=0x3dfa0 " not in line:
                    continue
                regs = {k: int(v, 16) for k, v in re.findall(r"\b(r\d+)=0x([0-9a-f]+)", line)}
                if "r21" not in regs or "r19" not in regs:
                    continue
                polls += 1
                position = regs["r21"]
                key = hex(regs["r19"])
                entry = positions.setdefault(key, dict(polls=0, first=position,
                                                       last=position, min=position, max=position,
                                                       changes=0))
                entry["polls"] += 1
                entry["changes"] += position != entry["last"]
                entry["last"] = position
                entry["min"] = min(entry["min"], position)
                entry["max"] = max(entry["max"], position)
        result.update(log=str(log.resolve()), error52=errors, nax_polls=polls,
                      nax_by_selector=positions, traced_dma=transfers)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("pcm", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--wav", type=Path)
    parser.add_argument("--seconds", type=float, default=8)
    args = parser.parse_args()
    if args.seconds <= 0 or args.seconds > 60:
        parser.error("--seconds must be in (0, 60]")
    result = summarize(args.pcm, args.log)
    if args.wav:
        start = max(0, (result["first_nonzero_frame"] or 0) - 4800)
        remaining = min(int(args.seconds * 48000), result["frames"] - start)
        written = remaining
        with args.pcm.open("rb") as source, wave.open(str(args.wav), "wb") as target:
            source.seek(start * 4)
            target.setparams((2, 2, 48000, 0, "NONE", "not compressed"))
            while remaining:
                frames = min(remaining, 16384)
                target.writeframesraw(source.read(frames * 4))
                remaining -= frames
        result["wav"] = dict(path=str(args.wav.resolve()), start_frame=start,
                             frames=written, processing="trim only; original gain and sample rate")
    output = json.dumps(result, indent=2) + "\n"
    if args.json:
        args.json.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
