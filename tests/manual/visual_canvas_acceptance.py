"""Real Xvfb Screen -> PP-OCR -> SoM -> Mira scripted selection -> XTest -> fresh OCR.

External weights only; no download or packaging. The unchanged negative case must
suppress the scripted success answer. This verifies a controlled Canvas, not an
online model's visual reasoning or physical host DPI.
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
results = []
for name, before, after, size, mode in [
    ("english", "Submit task", "Task saved", 32, "update"),
    ("chinese", "提交任务", "任务完成", 32, "update"),
    ("small", "保存文件", "文件已保存", 16, "update"),
    ("mixed", "提交 Submit", "完成 Done", 24, "update"),
    ("unchanged", "Submit task", "Task saved", 32, "unchanged"),
]:
    samples = []
    for stage, text in [("before", before), ("after", after)]:
        image = Image.new("RGB", (480, 180), "white")
        draw = ImageDraw.Draw(image)
        draw.text((28, 55), text, font=ImageFont.truetype(str(args.font), size), fill="black")
        path = args.output / f"{name}-{stage}.ppm"
        image.save(path)
        samples.append(path)
    proc = subprocess.run([
        str(args.binary.resolve()), str(args.manifest.resolve()),
        *[str(path.resolve()) for path in samples], after, mode,
    ], text=True, capture_output=True, timeout=90)
    (args.output / f"{name}.log").write_text(proc.stdout + proc.stderr)
    evidence = []
    summary = ""
    for line in proc.stdout.splitlines():
        if line.startswith("EVIDENCE "):
            _, tool, payload = line.split(" ", 2)
            evidence.append({"tool": tool, "result": json.loads(payload)})
        if line.startswith("CANVAS "):
            summary = line
    passed = proc.returncode == 0 and bool(summary) and len(evidence) == 2
    results.append({
        "sample": name, "before": before, "expected": after, "mode": mode,
        "exit_code": proc.returncode, "passed": passed, "summary": summary,
        "evidence": evidence,
        "sample_sha256": [hashlib.sha256(path.read_bytes()).hexdigest() for path in samples],
    })
report = {
    "manifest_sha256": hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
    "font_sha256": hashlib.sha256(args.font.read_bytes()).hexdigest(),
    "passed": sum(item["passed"] for item in results), "total": len(results),
    "source": "private Xvfb ScreenProvider capture, 520x220, window 480x180 at (20,20)",
    "model": "scripted Mira provider; live visual model not tested",
    "samples": results,
}
(args.output / "report.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n")
print(json.dumps(report, ensure_ascii=False))
raise SystemExit(0 if report["passed"] == report["total"] else 1)
