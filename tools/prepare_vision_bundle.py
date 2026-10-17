"""Fixed model-contract adaptation for DEC-068; never downloads or trains.

Input is nihui/ncnn-android-ppocrv5 at the audited exact commit. Runtime and
models stay outside the repository. Keep its source notices with the bundle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

PIN = "671ac4a72299a86ddee160131ba88fed748df425"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--lock", type=Path, required=True)
    args = parser.parse_args()
    lock = json.loads(args.lock.read_text(encoding="utf-8"))
    commit = lock["commit"]
    if commit != PIN:
        raise SystemExit("source pin differs from the audited conversion")
    for relative, expected in lock["source_artifacts"].items():
        if digest((args.source / relative).read_bytes()) != expected:
            raise SystemExit("source artifact digest mismatch: " + relative)
    original = args.source / "app/src/main/assets"
    outputs = {}
    provenance = {"source_commit": commit, "artifacts": {}}
    for stage in ("det", "rec"):
        param = (original / f"PP_OCRv5_mobile_{stage}.ncnn.param").read_bytes()
        weights = (original / f"PP_OCRv5_mobile_{stage}.ncnn.bin").read_bytes()
        provenance["artifacts"][stage] = {
            "param_sha256": digest(param), "bin_sha256": digest(weights)
        }
        lines = param.decode().splitlines()
        layers, blobs = map(int, lines[1].split())
        lines[1] = f"{layers + 1} {blobs + 1}"
        if stage == "det":
            # Mirador supplies z = pixel/127.5 - 1. The model's own training
            # normalization is (pixel/255 - mean)/std. Fuse their exact affine
            # conversion into a Scale artifact, retaining every trained weight.
            lines[2] = "Input mirage_input 0 1 x"
            lines.insert(3, "Scale mirage_det_norm 1 1 x in0 0=3 1=1")
            lines = [re.sub(r"\bout0\b", "out", line) for line in lines]
            means, deviations = (.485, .456, .406), (.229, .224, .225)
            scale = [.5 / s for s in deviations]
            bias = [(.5 - m) / s for m, s in zip(means, deviations)]
            weights = struct.pack("<6f", *(scale + bias)) + weights
        else:
            lines = [re.sub(r"\bin0\b", "x", line) for line in lines]
            # The SPI decodes logits and computes softmax confidence itself.
            # Remove the weightless exported Softmax to avoid applying it twice.
            if not lines[-1].startswith("Softmax") or "231 out0" not in lines[-1]:
                raise SystemExit("recognizer output differs from the audited export")
            lines.pop()
            lines[1] = f"{layers} {blobs}"
            lines[-1] = re.sub(r"\b231\b", "mirage_logits", lines[-1])
            # Raw output is classes x timesteps; the SPI requires CHW with
            # width=classes, height=1, channels=timesteps. No scores change.
            lines.append("Reshape mirage_ctc_layout 1 1 mirage_logits out 0=18385 1=1 2=-1")
        outputs[f"{stage}_param"] = ("\n".join(lines) + "\n").encode()
        outputs[f"{stage}_bin"] = weights
    dictionary_path = args.source / "app/src/main/jni/ppocrv5_dict.h"
    dictionary_source = dictionary_path.read_text(encoding="utf-8")
    entries = re.findall(r'^\s*("(?:[^"\\]|\\.)*")\s*,?\s*$', dictionary_source, re.M)
    chars = [json.loads(item) for item in entries]
    if len(chars) != 18383 or any("\n" in item or not item for item in chars):
        raise SystemExit("dictionary shape differs from the audited model")
    # Paddle CTCLoss adds a final ASCII space class to the 18,383-entry dictionary.
    chars.append(" ")
    outputs["charset"] = ("\n".join(chars) + "\n").encode()
    provenance["dictionary_source_sha256"] = digest(dictionary_path.read_bytes())
    args.output.mkdir(parents=True, exist_ok=True)
    artifacts = {}
    for name, data in outputs.items():
        expected = lock["output_artifacts"][name]
        if len(data) != expected["bytes"] or digest(data) != expected["sha256"]:
            raise SystemExit("converted artifact differs from verified version: " + name)
        (args.output / name).write_bytes(data)
        artifacts[name] = {"path": name, "sha256": digest(data), "bytes": len(data)}
    manifest = {
        "schema": 1, "model_id": "PP-OCRv5-mobile-ncnn-mirador-contract-v1",
        # DEC-068: preserve desktop line detail at the same height.
        # Width remains dynamic; 640 is a cap, not padding for every short line.
        "det_side": 960, "rec_height": 48, "rec_width": 640, "artifacts": artifacts,
        "geometry_marks": True, "roi_capture": True,
    }
    (args.output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    (args.output / "conversion.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(args.output / "manifest.json")


if __name__ == "__main__":
    main()
