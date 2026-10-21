#!/usr/bin/env python3
"""Summarize bounded GoW2 IOP traces up to the first 989 ERROR 52.

Use PS2_TRACE_IOP_RPC=1 and select a PC in the streaming check with
PS2_TRACE_IOP_PC. Optional PS2_TRACE_IOP_PEEK=HEX_ADDRESS:WORD_COUNT
adds at most 32 IOP RAM words at each selected PC. Keep logs and JSON
outside the code repository. The instruction offsets below describe
the SCUS-97481 modules; module load addresses come from each trace.
"""
import argparse
import json
import re
from collections import deque
from pathlib import Path

from a22_summary import fields


def summarize(path):
    result = {"trace": str(path), "modules": [],
              "bounds": [], "counters": [], "libsd_reads": [], "stream_starts": []}
    driver = libsd = smpd = None
    current_rpc = None
    recent_dma = deque(maxlen=4)
    for line_number, line in enumerate(path.open(errors="replace"), 1):
        if line_number > 20000 or len(line) > 16384:
            raise ValueError("trace prefix exceeds safety bound")
        if "[IOP trace]" in line:
            event = fields(line)
            kind = event.get("event")
            if kind == "module":
                result["modules"].append(event)
                if "989NOMID" in event.get("path", ""):
                    driver = event["base"]
                if "LIBSD" in event.get("path", ""):
                    libsd = event["base"]
                if "SMPD" in event.get("path", ""):
                    smpd = event["base"]
            elif kind == "rpc_begin":
                current_rpc = dict(event)
            elif kind == "rpc_payload" and current_rpc is not None:
                current_rpc["payload_words"] = event.get("words", [])
            elif kind == "rpc_return" and current_rpc is not None:
                current_rpc["response_words"] = event.get("words", [])
            elif kind == "spu_dma":
                recent_dma.append(event)
            elif kind == "printf":
                result["last_printf"] = event
            elif kind == "pc":
                pc = event.get("pc")
                if smpd is not None and pc == smpd + 0x201C and len(result["stream_starts"]) < 16:
                    result["stream_starts"].append({"snapshot": event,
                        "preceding_rpc": dict(current_rpc) if current_rpc else None})
                if driver is not None and pc == driver + 0x133A0 and len(result["bounds"]) < 64:
                    result["bounds"].append({"seq": event["seq"], "state": event["r20"],
                        "selector": event["r19"], "position": event["r21"],
                        "first_half_begin": event["r2"] + 1, "first_half_end": event["r3"]})
                elif driver is not None and pc == driver + 0x133F0 and len(result["counters"]) < 64:
                    result["counters"].append({"seq": event["seq"], "state": event["r20"],
                        "selector": event["r19"], "position": event["r21"], "count": event["r2"]})
                elif libsd is not None and libsd + 0x3B6C <= pc <= libsd + 0x3B74:
                    if len(result["libsd_reads"]) < 12:
                        result["libsd_reads"].append(event)
                if "peek_words" in event and "memory_peek" not in result:
                    result["memory_peek"] = {"address": event["peek_address"], "words": event["peek_words"]}
        match = re.search(r"\b989 ERROR 52 ([0-9a-fA-F]+)\b", line)
        if match:
            result["first_error"] = {"line": line_number, "text": line.strip(),
                                     "handle": int(match.group(1), 16)}
            result["recent_spu_dma"] = list(recent_dma)
            result["last_rpc"] = current_rpc
            return result
    raise ValueError("first 989 ERROR 52 not found in bounded prefix")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("traces", type=Path, nargs="+")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = {"schema": 1, "runs": [summarize(path) for path in args.traces]}
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps([run["first_error"] for run in result["runs"]]))


if __name__ == "__main__":
    main()
