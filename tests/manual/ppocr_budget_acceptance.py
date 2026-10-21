"""M8-07 controlled acceptance driver: deterministic dense scene + real
weight bundle through the product image-OCR pipeline (budgeted path).

Renders a byte-identical 1600x1000 desktop-style scene (>= 96 text lines,
CJK + Latin mix) so the frozen reference selection (K = 64) binds, then runs
the acceptance binary: three consecutive read_image_ocr calls, each bounded
by the DEC-076 five-second deadline. Exit 1 on any gate failure; the report
records per-call timing, completion diagnostics and the cross-call digest.
No weights or pixels are downloaded; nothing is uploaded.
"""
import argparse
import hashlib
import json
import re
import subprocess
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

LINE = 40   # vertical pitch of one text row (px); dense enough that the
            # frozen K=64 selection binds on the 3x23 grid det segmentates

def scene_plan(width, height):
    rows = (height - 60) // LINE
    col_pitch = (width - 100) // 3
    texts = []
    for row in range(rows):
        for col in range(3):
            index = row * 3 + col
            texts.append((30 + col * col_pitch, 24 + row * LINE,
                          f"{index:02d}窗口 保存{index % 10}"))
    return texts

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--font", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--calls", type=int, default=3)
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=1000)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)

    image = Image.new("RGB", (args.width, args.height), (245, 245, 245))
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype(str(args.font), 22)
    for x, y, text in scene_plan(args.width, args.height):
        draw.text((x, y), text, font=font, fill=(20, 20, 20))
    scene = args.output / "dense_scene.ppm"
    image.save(scene)
    scene_sha = hashlib.sha256(scene.read_bytes()).hexdigest()

    proc = subprocess.run(
        [str(args.binary.resolve()), str(args.manifest.resolve()), str(scene.resolve()),
         str(args.calls)],
        text=True, capture_output=True, timeout=300)
    (args.output / "acceptance.log").write_text(proc.stdout + proc.stderr)
    calls = []
    for line in proc.stdout.splitlines():
        match = re.match(
            r"CALL (\d+) ok=(\d) ms=(\d+) regions=(\d+) total=(\d+)"
            r"(?: partial=(\d) stop=(\S+) detected=(\d+) recognized=(\d+) deferred=(\d+)"
            r" det_limit_hit=(\d))? digest=([0-9a-f]+) GATE=(\w+)", line)
        if match:
            g = match.groups()
            calls.append({
                "index": int(g[0]), "ok": bool(int(g[1])), "wall_ms": int(g[2]),
                "regions": int(g[3]), "total_region_count": int(g[4]),
                "partial": bool(int(g[5])) if g[5] is not None else None,
                "stop_reason": g[6], "detected": int(g[7]) if g[7] else None,
                "recognized": int(g[8]) if g[8] else None,
                "deferred": int(g[9]) if g[9] else None,
                "det_limit_hit": bool(int(g[10])) if g[10] is not None else None,
                "digest": g[11], "gate": g[12],
            })
    registry = re.search(r"REGISTRY untouched=(\d)", proc.stdout)
    passed_line = re.search(r"BUDGET_ACCEPTANCE (PASS|FAIL)", proc.stdout)

    budget_ms = 5000
    checks = {
        "binary_exit_zero": proc.returncode == 0,
        "binary_pass": bool(passed_line and passed_line.group(1) == "PASS"),
        "call_count": len(calls) == args.calls,
        "every_call_within_budget": all(c["wall_ms"] <= budget_ms for c in calls),
        "every_call_ok": all(c["ok"] for c in calls),
        "every_call_budgeted_diagnostics": all(c["partial"] is not None for c in calls),
        "recognition_budget_binds": all(c["stop_reason"] == "rec_line_limit" and c["partial"]
                                        and c["recognized"] == 64
                                        and c["deferred"] == c["detected"] - c["recognized"]
                                        for c in calls),
        "dense_scene_consumable": all(c["regions"] >= 32 for c in calls),
        "digest_identical_across_calls": len({c["digest"] for c in calls}) == 1,
        "registry_untouched": bool(registry and registry.group(1) == "1"),
    }
    report = {
        "scene_sha256": scene_sha, "manifest_sha256":
            hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
        "font_sha256": hashlib.sha256(args.font.read_bytes()).hexdigest(),
        "scene_lines": len(scene_plan(args.width, args.height)),
        "budget_ms_per_call": budget_ms, "calls": calls, "checks": checks,
        "passed": all(checks.values()),
    }
    (args.output / "report.json").write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n")
    print(json.dumps(report, ensure_ascii=False))
    raise SystemExit(0 if report["passed"] else 1)

if __name__ == "__main__":
    main()
