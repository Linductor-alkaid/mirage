"""M9-28/33 controlled real-weight replay; no downloads or desktop capture.

Renders independently known text and a blank control. The binary's report
contains both passing and failing gates; this driver never rewrites a failure.
A few controlled samples do not establish recall, video understanding or Hz.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from PIL import Image, ImageDraw, ImageFont


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "manifest", "font", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--source-width", type=int, choices=(640, 1280, 1600), default=640)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    ratio = args.source_width / 640
    font = ImageFont.truetype(str(args.font), round(32 * ratio))
    position = (round(50 * ratio), round(130 * ratio))
    entries = []
    for name, text in (("base", "Status: READY"), ("changed", "Status: SAVED"), ("blank", "")):
        image = Image.new("RGB", (args.source_width, round(360 * ratio)), "white")
        draw = ImageDraw.Draw(image)
        draw.text(position, text, font=font, fill="black")
        path = args.output / (name + ".ppm")
        image.save(path)
        image.save(args.output / (name + ".png"))
        entries.append(dict(name=name, text=text, sha256=digest(path),
                            bounds=list(draw.textbbox(position, text, font=font)) if text else []))
    command = [str(args.binary.resolve()), str(args.manifest.resolve()),
               str((args.output / "report.json").resolve())]
    command += [str((args.output / (name + ".ppm")).resolve()) for name in ("base", "changed", "blank")]
    (args.output / "report.json").unlink(missing_ok=True)
    proc = subprocess.run(command, text=True, capture_output=True, timeout=180)
    (args.output / "execution.log").write_text(proc.stdout + proc.stderr)
    record = dict(exit_code=proc.returncode, command=command, samples=entries,
                  source_width=args.source_width,
                  manifest_sha256=digest(args.manifest), font_sha256=digest(args.font),
                  binary_sha256=digest(args.binary), scope="controlled image replay; no host desktop capture")
    (args.output / "execution.json").write_text(json.dumps(record, ensure_ascii=False, indent=2))
    print(proc.stdout, end="")
    if proc.stderr:
        print(proc.stderr, end="")
    if (args.output / "report.json").exists():
        report = json.loads((args.output / "report.json").read_text())
        print(json.dumps(report["gates"], ensure_ascii=False))
    return proc.returncode


if __name__ == "__main__":
    raise SystemExit(main())
