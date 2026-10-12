"""Fixed public text corpus, real backend test binary; no model/network access.

Exit 1 means exact OCR acceptance failed. This is separate from CTest's
synthetic composition fixture and does not validate physical DPI transforms.
Each sample must return the exact expected text AND one text region whose
bounds contain the rendered ground-truth text box (position gate).
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from PIL import Image, ImageDraw, ImageFont

parser = argparse.ArgumentParser()
parser.add_argument("--binary", type=Path, required=True)
parser.add_argument("--manifest", type=Path, required=True)
parser.add_argument("--font", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--position-tolerance", type=int, default=3,
                    help="how many pixels a region border may fall short of the text box")
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
results = []
for name, text, size, scale in [("english", "Submit task", 32, 1),
                              ("chinese", "提交任务", 32, 1),
                              ("small", "保存文件", 16, 1),
                              ("mixed-2x", "提交 Submit", 24, 2)]:
    image = Image.new("RGB", (480, 180), "white")
    draw = ImageDraw.Draw(image)
    draw.text((28, 55), text, font=ImageFont.truetype(str(args.font), size), fill="black")
    text_box = draw.textbbox((28, 55), text, font=ImageFont.truetype(str(args.font), size))
    if scale != 1:
        image = image.resize((480 * scale, 180 * scale), Image.Resampling.LANCZOS)
        text_box = tuple(value * scale for value in text_box)
    sample = args.output / (name + ".ppm")
    image.save(sample)
    proc = subprocess.run([str(args.binary.resolve()), str(args.manifest.resolve()), str(sample.resolve()), text],
                          text=True, capture_output=True, timeout=50)
    (args.output / (name + ".log")).write_text(proc.stdout + proc.stderr)
    observed = "".join(line.removeprefix("OCR=") for line in proc.stdout.splitlines() if line.startswith("OCR="))
    regions = []
    for line in proc.stdout.splitlines():
        match = re.match(r"REGION (-?\d+),(-?\d+),(-?\d+),(-?\d+) confidence=(\S+) text=(.*)", line)
        if match:
            regions.append({"bounds": [int(match.group(i)) for i in range(1, 5)],
                            "confidence": float(match.group(5)), "text": match.group(6)})
    text_regions = [region for region in regions if region["text"]]
    # Position gate: exactly one text line, and its box must contain the
    # rendered text box within the tolerance (unclip may only grow it).
    tolerance = args.position_tolerance
    contains = False
    if len(text_regions) == 1:
        x, y, width, height = text_regions[0]["bounds"]
        contains = (x - tolerance <= text_box[0] and y - tolerance <= text_box[1]
                    and x + width + tolerance >= text_box[2] and y + height + tolerance >= text_box[3])
    results.append({"sample": name, "expected": text, "observed": observed,
                    "text_box": list(text_box), "regions": regions,
                    "text_region_count": len(text_regions),
                    "exact_match": proc.returncode == 0 and observed == text,
                    "position_match": contains,
                    "exit_code": proc.returncode, "pixels": list(image.size),
                    "sample_sha256": hashlib.sha256(sample.read_bytes()).hexdigest()})
report = {"manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
          "font_sha256": hashlib.sha256(args.font.read_bytes()).hexdigest(),
          "passed": sum(item["exact_match"] and item["position_match"] for item in results),
          "total": len(results), "samples": results}
(args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print(json.dumps(report, ensure_ascii=False))
raise SystemExit(0 if report["passed"] == report["total"] else 1)
