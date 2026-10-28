"""Paired frozen-binary OCR regression corpus; no downloads/host capture.

Reports both ground truth accuracy and exact text/one-pixel box non-regression.
Baseline errors remain errors; matching them does not prove universal accuracy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from PIL import Image, ImageDraw, ImageFont


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('baseline', 'binary', 'manifest', 'font', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    cases = []
    for scale in (1, 2):
        for size in (16, 24, 32):
            for text in ('Submit task', '提交任务', '提交 Submit', 'Count: 12345'):
                cases.append((text, size, scale, 1))
    cases += [('保存文件', 16, 1, 4), ('Submit task', 24, 2, 4), ('', 24, 1, 0)]
    results = []
    for number, (text, size, scale, lines) in enumerate(cases):
        image = Image.new('RGB', (640 * scale, 360 * scale), 'white')
        draw = ImageDraw.Draw(image)
        font = ImageFont.truetype(str(args.font), size * scale)
        for line in range(lines):
            draw.text((32 * scale, (40 + line * (size + 12)) * scale), text,
                      font=font, fill='black')
        path = args.output / f'{number:02}.ppm'
        image.save(path)
        row = dict(case=number, text=text, font_size=size, scale=scale, lines=lines,
                   expected=text * lines, image_sha256=sha(path), arms={})
        for arm, binary in [('baseline', args.baseline), ('production', args.binary)]:
            command = [str(binary.resolve()), str(args.manifest.resolve()), str(path.resolve())]
            run = subprocess.run(command, capture_output=True, text=True, timeout=60)
            (args.output / f'{number:02}-{arm}.log').write_text(run.stdout + run.stderr)
            recognized = ''.join(x[4:] for x in run.stdout.splitlines() if x.startswith('OCR='))
            regions = []
            for line in run.stdout.splitlines():
                match = re.match(r'REGION (-?\d+),(-?\d+),(-?\d+),(-?\d+) confidence=(\S+) text=(.*)', line)
                if match and match[6]:
                    regions.append(dict(text=match[6], bounds=[int(match[i]) for i in range(1, 5)]))
            regions.sort(key=lambda r: (r['text'], r['bounds']))
            row['arms'][arm] = dict(exit_code=run.returncode, text=recognized, regions=regions,
                                    exact_ground_truth=recognized == row['expected'])
        before, after = row['arms']['baseline'], row['arms']['production']
        row['non_regression'] = (before['exit_code'] == after['exit_code'] == 0
                                 and before['text'] == after['text']
                                 and len(before['regions']) == len(after['regions'])
                                 and all(a['text'] == b['text'] and
                                         all(abs(x-y) <= 1 for x, y in zip(a['bounds'], b['bounds']))
                                         for a, b in zip(before['regions'], after['regions'])))
        results.append(row)
        print(number, row['non_regression'], before['exact_ground_truth'], after['exact_ground_truth'], flush=True)
        report = dict(samples=results, total=len(cases), completed=len(results),
                      non_regression_passed=sum(r['non_regression'] for r in results),
                      baseline_exact=sum(r['arms']['baseline']['exact_ground_truth'] for r in results),
                      production_exact=sum(r['arms']['production']['exact_ground_truth'] for r in results),
                      baseline_sha256=sha(args.baseline), binary_sha256=sha(args.binary),
                      font_sha256=sha(args.font), manifest_sha256=sha(args.manifest))
        (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2))
    return 0 if all(r['non_regression'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
