"""No-text Canvas -> actual Mirador geometry/SoM -> XTest -> fresh PP-OCR condition.

Two closed button silhouettes, no a11y/detector/template labels. Scripted selection
exercises independent numbered targets; the unchanged negative suppresses success.
Weights stay external. No physical-host/online-model/semantic-button claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--manifest", type=Path, required=True)
parser.add_argument("--font", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
# Resolve existing artifact paths without copying/downloading model weights.
manifest = json.loads(args.manifest.read_text())
for artifact in manifest["artifacts"].values():
    artifact["path"] = str((args.manifest.parent / artifact["path"]).resolve())
manifest["geometry_marks"] = True
configured = args.output / "geometry-manifest.json"
configured.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
results = []
for name, side, mode in [("left", "left", "update"), ("right", "right", "update"),
                         ("unchanged", "right", "unchanged")]:
    before = Image.new("RGB", (480, 180), "white")
    draw = ImageDraw.Draw(before)
    # Filled silhouettes yield closed contours without ambiguous text or fake boxes.
    draw.rectangle((65, 55, 135, 125), fill="black")
    draw.rectangle((325, 55, 395, 125), fill="black")
    after = Image.new("RGB", (480, 180), "white")
    expected = "Task saved"
    ImageDraw.Draw(after).text((28, 55), expected,
                              font=ImageFont.truetype(str(args.font), 32), fill="black")
    samples = [args.output / f"{name}-{stage}.ppm" for stage in ("before", "after")]
    before.save(samples[0])
    after.save(samples[1])
    proc = subprocess.run([str(args.binary.resolve()), str(configured.resolve()),
                           *[str(p.resolve()) for p in samples], expected, mode, side],
                          text=True, capture_output=True, timeout=90)
    (args.output / f"{name}.log").write_text(proc.stdout + proc.stderr)
    evidence = []
    summary = ""
    target = None
    for line in proc.stdout.splitlines():
        if line.startswith("EVIDENCE "):
            _, tool, payload = line.split(" ", 2)
            evidence.append({"tool": tool, "result": json.loads(payload)})
        if line.startswith("CANVAS "):
            summary = line
        if line.startswith("TARGET "):
            target = json.loads(line[7:])
    passed = (proc.returncode == 0 and "geometry=1" in summary and target is not None
              and len(evidence) == 2)
    results.append({"sample": name, "side": side, "mode": mode, "expected": expected,
                    "exit_code": proc.returncode, "passed": passed, "target": target,
                    "summary": summary, "evidence": evidence,
                    "sample_sha256": [hashlib.sha256(p.read_bytes()).hexdigest() for p in samples]})
report = {"manifest_sha256": hashlib.sha256(configured.read_bytes()).hexdigest(),
          "source_manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
          "font_sha256": hashlib.sha256(args.font.read_bytes()).hexdigest(),
          "passed": sum(r["passed"] for r in results), "total": len(results),
          "source": "private Xvfb 520x220; Canvas 480x180 at (20,20); no initial text/a11y",
          "model": "scripted Mira provider; live visual model not tested", "samples": results}
(args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print(f"geometry Canvas: {report['passed']}/{report['total']}")
raise SystemExit(0 if report["passed"] == report["total"] else 1)
