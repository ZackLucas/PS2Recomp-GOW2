#!/usr/bin/env python3
"""Read a bounded IOP trace prefix, stopping at the first 989 ERROR 84.

Usage: python a22_summary.py TRACE_LOG --output SUMMARY_JSON
Keep game traces and the resulting JSON outside the code repository.
"""
import argparse
import json
import re
from pathlib import Path


def fields(line):
    result = {}
    for key, value in re.findall(r"(?:^| )(\w+)=([^\s]+)", line):
        if key.endswith("words"):
            result[key] = [int(word, 0) for word in value.split(",") if word]
        else:
            try:
                result[key] = int(value, 16 if value.startswith("0x") else 10)
            except ValueError:
                result[key] = value
    return result


def summarize(path):
    summary = {"schema": 1, "trace": str(path), "modules": [], "trace_events": 0}
    last_rpc = None
    for line_number, line in enumerate(path.open(errors="replace"), 1):
        if line_number > 20000 or len(line) > 16384:
            raise ValueError("trace prefix exceeds safety bound")
        if "[IOP trace]" in line:
            event = fields(line)
            summary["trace_events"] += 1
            kind = event.get("event")
            if kind == "module":
                summary["modules"].append(event)
            elif kind == "rpc_begin":
                last_rpc = event
            elif kind == "rpc_payload" and last_rpc:
                last_rpc["payload_words"] = event.get("words", [])
            elif kind == "get_other_data_stub":
                summary["last_ee_to_iop_request"] = event
                summary["preceding_rpc"] = dict(last_rpc) if last_rpc else None
            elif kind in ("iop_dma", "spu_dma", "sif_transfer"):
                summary["last_transport_event"] = event
            elif kind == "pc":
                driver = next((m for m in summary["modules"] if "989NOMID" in m["path"]), None)
                if driver and event.get("pc") == driver["base"] + 0x2018 and event.get("r4") == 84:
                    summary["driver_error_entry"] = event
                    summary["driver_caller_offset"] = event["r31"] - driver["base"] - 8
            elif kind == "printf":
                summary["last_printf"] = event
        if re.search(r"\b989 ERROR 84\b", line):
            summary["first_error"] = {"line": line_number, "text": line.strip()}
            transfer = summary.get("last_ee_to_iop_request", {})
            error = summary.get("driver_error_entry", {})
            if transfer and error:
                offset = error["r19"] - transfer["destination"]
                words = transfer.get("source_words", [])
                if offset >= 0 and offset % 4 == 0 and offset // 4 < len(words):
                    summary["header_check"] = {
                        "offset_in_transfer": offset,
                        "ee_source_address": transfer["source"] + offset,
                        "iop_header_address": error["r19"],
                        "ee_type": words[offset // 4],
                        "iop_type": error["s3_word"],
                        "accepted_types": [1, 3],
                    }
            return summary
    raise ValueError("first 989 ERROR 84 not found in bounded prefix")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    summary = summarize(args.trace)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps(summary.get("header_check", summary["first_error"])))


if __name__ == "__main__":
    main()
