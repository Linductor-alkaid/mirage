"""Operator-assisted public Canvas on the current Xorg desktop.

Local scripted provider only. Prepare a scoped ROI manifest, stop on the first
failed sample, and preserve operator/preflight/tool evidence. No credentials or
network. Own-window fixture readback never replaces product root capture.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('binary', 'manifest', 'font', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    parser.add_argument('--display', default=os.environ.get('DISPLAY', ''))
    parser.add_argument('--allow-xwayland', action='store_true',
                        help='Explicit diagnostic only; does not establish Xorg/Wayland support')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'schema': 1, 'work_item': 'M7-07', 'display': args.display,
              'session_type': os.environ.get('XDG_SESSION_TYPE', ''),
              'source': 'Operator-assisted public 480x180 Canvas; root ScreenProvider ROI capture',
              'model': 'Scripted Mira provider; no network or credentials',
              'limits': {'operator_seconds': 15, 'preparation_events': 32,
                         'owner_seconds': 30, 'reference_seconds': 5,
                         'observation_seconds': 5, 'parent_seconds': 90},
              'samples': [], 'passed': 0, 'attempted': 0, 'total': 3}
    def save():
        (args.output / 'report.json').write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    if not re.fullmatch(r':\d+(?:\.\d+)?', args.display):
        report['preflight_error'] = 'Local X11 DISPLAY required'
    elif report['session_type'] != 'x11' and not args.allow_xwayland:
        report['preflight_error'] = 'Xorg session required; current session is not x11'
    if 'preflight_error' in report:
        save()
        print(report['preflight_error'], flush=True)
        return 2
    manifest = json.loads(args.manifest.read_text())
    for artifact in manifest['artifacts'].values():
        artifact['path'] = str((args.manifest.parent / artifact['path']).resolve())
    manifest.pop('templates', None)
    manifest.update(roi_capture=True, geometry_marks=True)
    configured = args.output / 'host-manifest.json'
    configured.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    report['source_manifest_sha256'] = hashlib.sha256(args.manifest.read_bytes()).hexdigest()
    report['manifest_sha256'] = hashlib.sha256(configured.read_bytes()).hexdigest()
    report['font_sha256'] = hashlib.sha256(args.font.read_bytes()).hexdigest()
    before = Image.new('RGB', (480, 180), 'white')
    draw = ImageDraw.Draw(before)
    for x in (65, 325):
        draw.rectangle((x, 55, x + 70, 125), fill='black')
    after = Image.new('RGB', (480, 180), 'white')
    ImageDraw.Draw(after).text((28, 55), 'Task saved',
                              font=ImageFont.truetype(str(args.font), 32), fill='black')
    inputs = [args.output / 'before.ppm', args.output / 'after.ppm']
    before.save(inputs[0]); after.save(inputs[1])
    report['sample_sha256'] = [hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs]
    env = os.environ.copy()
    for key in ('MIRAGE_CANVAS_OCCLUDE', 'MIRAGE_CANVAS_NEGATIVE', 'MIRAGE_CANVAS_KEY',
                'MIRAGE_CANVAS_MODEL', 'MIRAGE_CANVAS_ORIGIN'):
        env.pop(key, None)
    env.update(MIRAGE_CANVAS_HOST=args.display, MIRAGE_CANVAS_OPERATOR='1',
               MIRAGE_CANVAS_ROI='1', MIRAGE_CANVAS_LIVE='0')
    for name, side, mode, pos in [('right', 'right', 'update', '20,20'),
                                  ('left', 'left', 'update', '60,60'),
                                  ('unchanged', 'right', 'unchanged', '60,60')]:
        print(f'{len(report["samples"])+1}/3 {name}: wait for Canvas; check two black squares; '
              'click the blank centre once within 15 seconds, release, then leave it untouched.',
              flush=True)
        env['MIRAGE_CANVAS_POS'] = pos
        timed_out = False
        try:
            proc = subprocess.run([str(args.binary.resolve()), str(configured.resolve()),
                                   *[str(p.resolve()) for p in inputs], 'Task saved', mode, side],
                                  env=env, capture_output=True, text=True, timeout=90)
            code, stdout, stderr = proc.returncode, proc.stdout, proc.stderr
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            code, stdout, stderr = -1, decoded(error.stdout), decoded(error.stderr)
            timed_out = True
        log = stdout + stderr
        (args.output / (name + '.log')).write_text(log)
        evidence, summary = [], ''
        for line in stdout.splitlines():
            if line.startswith('EVIDENCE '):
                _, tool, payload = line.split(' ', 2)
                evidence.append({'tool': tool, 'result': json.loads(payload)})
            if line.startswith('CANVAS '):
                summary = line
        passed = (code == 0 and 'roi_capture=1' in summary and 'backend_calls=4' in summary
                  and 'pressed=1 released=1 images=1' in summary and len(evidence) == 2
                  and 'Canvas public drawable matches fixture=1' in stderr
                  and 'Canvas operator focus release confirmed' in stderr)
        for entry in evidence:
            result = entry['result']
            verification = result.get('verification', result)
            passed = (passed and result.get('verified') is (mode == 'update')
                      and verification.get('reason') ==
                      ('condition_matched' if mode == 'update' else 'condition_not_matched'))
        report['samples'].append({'sample': name, 'requested_origin': pos, 'mode': mode,
                                  'exit_code': code, 'timed_out': timed_out, 'passed': passed,
                                  'summary': summary, 'evidence': evidence,
                                  'fixture_matches': 'Canvas public drawable matches fixture=1' in stderr,
                                  'operator_release': 'Canvas operator focus release confirmed' in stderr,
                                  'diagnostic': stderr[-4096:],
                                  'log_sha256': hashlib.sha256(log.encode()).hexdigest()})
        report['passed'] += int(passed)
        report['attempted'] += 1
        save()
        print(f'{name}: {"passed" if passed else "failed; stopped"}', flush=True)
        if not passed:
            print(stderr[-4096:], flush=True)
            return 1
    print('Host Canvas: 3/3', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
