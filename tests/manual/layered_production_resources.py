#!/usr/bin/env python3
"""Run paired 5/10 Hz resource gates on owned 640/1600 pixel replays.

Each arm warms for five seconds, then measures sixty seconds. Both arms use
identical OCR/YOLO models; this compares scheduling, not the old OCR-only product.
No host capture, input, downloads or model API calls are made.
"""
import argparse
import hashlib
import json
import platform
import subprocess
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "manifest", "corpus-640", "corpus-1600", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--widths", type=int, choices=(640, 1600), nargs="+", default=[640, 1600])
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cpu = next((line.split(":", 1)[1].strip() for line in
                Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")), "unknown")
    matrix = dict(binary_sha256=digest(args.binary), manifest_sha256=digest(args.manifest),
                  system=platform.platform(), cpu=cpu, warmup_seconds=5, seconds=60, arms=[], pairs=[])
    for width, corpus in ((640, args.corpus_640), (1600, args.corpus_1600)):
        if width not in args.widths:
            continue
        images = [corpus / (name + ".ppm") for name in ("base", "changed", "blank")]
        for hz in (5, 10):
            pair = {}
            for mode in ("legacy", "layered"):
                stem = f"{width}-{mode}-{hz}"
                path = args.output / (stem + ".json")
                path.unlink(missing_ok=True)
                command = [str(args.binary.resolve()), str(args.manifest.resolve()),
                           str(path.resolve()), mode, str(hz), "60"]
                command += [str(image.resolve()) for image in images]
                proc = subprocess.run(command, text=True, capture_output=True, timeout=110)
                (args.output / (stem + ".log")).write_text(proc.stdout + proc.stderr)
                report = json.loads(path.read_text()) if path.exists() else {}
                pair[mode] = report
                matrix["arms"].append(dict(width=width, hz=hz, mode=mode, command=command,
                                           exit_code=proc.returncode, report=report,
                                           sample_sha256={p.name: digest(p) for p in images}))
                (args.output / "matrix.json").write_text(json.dumps(matrix, indent=2))
                print(stem, "exit", proc.returncode, "cpu", report.get("cpu_percent"),
                      "rss", report.get("peak_rss_kib"), flush=True)
            legacy, layered = pair["legacy"].get("peak_rss_kib"), pair["layered"].get("peak_rss_kib")
            delta = layered - legacy if legacy is not None and layered is not None else None
            matrix["pairs"].append(dict(width=width, hz=hz, rss_delta_kib=delta,
                                        rss_delta_pass=delta is not None and delta <= 64 * 1024))
    matrix["production_pass"] = (all(arm["exit_code"] == 0 for arm in matrix["arms"]
                                     if arm["mode"] == "layered") and
                                 all(pair["rss_delta_pass"] for pair in matrix["pairs"]))
    (args.output / "matrix.json").write_text(json.dumps(matrix, indent=2))
    return 0 if matrix["production_pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
