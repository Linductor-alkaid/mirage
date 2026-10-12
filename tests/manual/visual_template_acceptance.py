"""Local verified patches -> Mirador index over geometry -> numbered X11 click -> fresh OCR.

No template enrollment from the live desktop, downloads or semantic roles. Script
selects by appearance ID across position swaps; unchanged negative suppresses success.
"""
import argparse, hashlib, json, subprocess
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
for key in ("binary", "manifest", "font", "output"):
    parser.add_argument("--" + key, type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)

def draw_button(draw, x, cross):
    draw.rectangle((x, 55, x + 70, 125), fill="black")
    draw.line((x + 20, 75, x + 50, 105), fill="white", width=3)
    if cross:
        draw.line((x + 50, 75, x + 20, 105), fill="white", width=3)

base = Image.new("RGB", (480, 180), "white")
draw = ImageDraw.Draw(base)
draw_button(draw, 65, False)
draw_button(draw, 325, True)
manifest = json.loads(args.manifest.read_text())
for a in manifest["artifacts"].values():
    a["path"] = str((args.manifest.parent / a["path"]).resolve())
manifest["geometry_marks"] = True
manifest["templates"] = []
for x, identity in [(65, "line-icon"), (325, "cross-icon")]:
    # Same tight contour extent as Mirador's pixel-ridge proposal: includes its
    # single-pixel white border. No live capture used to enroll or tune patches.
    patch = base.crop((x - 1, 54, x + 71, 126)).convert("RGBA")
    pixels = patch.tobytes("raw", "BGRA")
    asset = args.output / (identity + ".bgra")
    asset.write_bytes(pixels)
    manifest["templates"].append({"id": identity, "format": "bgra8", "path": str(asset.resolve()),
                                 "width": patch.width, "height": patch.height,
                                 "bytes": len(pixels), "sha256": hashlib.sha256(pixels).hexdigest()})
configured = args.output / "template-manifest.json"
configured.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
results = []
for name, swapped, identity, mode in [("line", False, "line-icon", "update"),
                                     ("cross", False, "cross-icon", "update"),
                                     ("swapped", True, "cross-icon", "update"),
                                     ("unchanged", True, "cross-icon", "unchanged")]:
    before = Image.new("RGB", (480, 180), "white")
    draw = ImageDraw.Draw(before)
    draw_button(draw, 65, swapped)
    draw_button(draw, 325, not swapped)
    after = Image.new("RGB", (480, 180), "white")
    expected = "Task saved"
    ImageDraw.Draw(after).text((28, 55), expected,
                              font=ImageFont.truetype(str(args.font), 32), fill="black")
    samples = [args.output / f"{name}-{stage}.ppm" for stage in ("before", "after")]
    before.save(samples[0]); after.save(samples[1])
    side = "left" if (identity == "cross-icon") == swapped else "right"
    proc = subprocess.run([str(args.binary.resolve()), str(configured.resolve()),
                           *[str(p.resolve()) for p in samples], expected, mode, side, identity],
                          text=True, capture_output=True, timeout=90)
    (args.output / f"{name}.log").write_text(proc.stdout + proc.stderr)
    evidence, target, summary = [], None, ""
    for line in proc.stdout.splitlines():
        if line.startswith("EVIDENCE "):
            _, tool, payload = line.split(" ", 2)
            evidence.append({"tool": tool, "result": json.loads(payload)})
        elif line.startswith("TARGET "):
            target = json.loads(line[7:])
        elif line.startswith("CANVAS "):
            summary = line
    passed = (proc.returncode == 0 and target and target.get("template_id") == identity
              and f"template={identity}" in summary and len(evidence) == 2)
    results.append({"sample": name, "swapped": swapped, "identity": identity, "mode": mode,
                    "passed": bool(passed), "exit_code": proc.returncode, "summary": summary,
                    "target": target, "evidence": evidence,
                    "sample_sha256": [hashlib.sha256(p.read_bytes()).hexdigest() for p in samples]})
report = {"manifest_sha256": hashlib.sha256(configured.read_bytes()).hexdigest(),
          "source_manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
          "font_sha256": hashlib.sha256(args.font.read_bytes()).hexdigest(),
          "assets": [{k:v for k,v in a.items() if k != "path"} for a in manifest["templates"]],
          "source": "private Xvfb 520x220; no a11y; local public button illustrations",
          "model": "scripted Mira provider selects template ID, online visual reasoning not tested",
          "passed": sum(r["passed"] for r in results), "total": len(results), "samples": results}
(args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print(f"template Canvas: {report['passed']}/{report['total']}")
raise SystemExit(0 if report["passed"] == report["total"] else 1)
