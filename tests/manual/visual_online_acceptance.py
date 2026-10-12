"""Opt-in real model -> numbered public Canvas -> actual XTest -> fresh OCR.

Only public Canvas pixels leave the machine, from private Xvfb or an explicitly
selected host window. Credentials travel in the child
process environment to the existing SecretRef resolver, never in arguments or
reports. Failed selection/admission/completion remains evidence, not a retry.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tomllib
from urllib.parse import urlsplit
from PIL import Image, ImageDraw, ImageFont


def object_text(text):
    try:
        return json.loads(text)
    except (ValueError, TypeError):
        return {}


def main():
    parser = argparse.ArgumentParser()
    for name in ('provider', 'binary', 'manifest', 'font', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--reasoning', choices=('adaptive', 'none'), default='adaptive',
                        help='Explicit test setting; does not change product or provider defaults')
    args = parser.parse_args()
    cfg = tomllib.loads((args.provider / 'config.toml').read_text())
    provider = cfg['model_providers'][cfg['model_provider']]
    origin = urlsplit(provider['base_url'])
    if (origin.scheme != 'https' or origin.hostname not in ('api.minimaxi.com', 'api.minimax.io')
            or origin.username or origin.password or origin.port or origin.query or origin.fragment):
        raise RuntimeError('Restricted to the configured HTTPS MiniMax provider')
    key = json.loads((args.provider / 'auth.json').read_text())['OPENAI_API_KEY']
    if not isinstance(key, str) or not key:
        raise RuntimeError('Configured credential unavailable')
    env = os.environ.copy()
    env.update(MIRAGE_CANVAS_LIVE='1', MIRAGE_CANVAS_KEY=key,
               MIRAGE_CANVAS_ORIGIN='https://' + origin.hostname, MIRAGE_CANVAS_MODEL=cfg['model'],
               MIRAGE_CANVAS_REASONING=args.reasoning)
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(args.manifest.read_text())
    for artifact in manifest['artifacts'].values():
        artifact['path'] = str((args.manifest.parent / artifact['path']).resolve())
    # Opaque geometry numbers only; template identity must not reveal the shape.
    manifest.pop('templates', None)
    manifest['geometry_marks'] = True
    configured = args.output / 'online-manifest.json'
    configured.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    report = {'schema': 1, 'model': cfg['model'], 'dialect': 'anthropic.messages.v1',
              'reasoning': args.reasoning, 'origin': 'https://' + origin.hostname,
              'scope': ('Host public Canvas only' if env.get('MIRAGE_CANVAS_HOST') else
                        'Private Xvfb 3840x2160 with window ROI' if env.get('MIRAGE_CANVAS_ROI') == '1' else
                        'Private Xvfb 520x220') + '; public drawings; real model and Screen/XTest/PP-OCR',
              'source_manifest_sha256': hashlib.sha256(args.manifest.read_bytes()).hexdigest(),
              'manifest_sha256': hashlib.sha256(configured.read_bytes()).hexdigest(),
              'font_sha256': hashlib.sha256(args.font.read_bytes()).hexdigest(),
              'limits': {'owner_seconds': 30, 'reference_seconds': 5, 'observation_seconds': 5},
              'samples': [], 'passed': 0, 'selection_passed': 0, 'attempted': 0, 'total': 3}
    for name, swapped, mode in [('cross-right', False, 'update'),
                                ('cross-left', True, 'update'),
                                ('unchanged', True, 'unchanged')]:
        before = Image.new('RGB', (480, 180), 'white')
        draw = ImageDraw.Draw(before)
        for x, cross in [(65, swapped), (325, not swapped)]:
            draw.rectangle((x, 55, x + 70, 125), fill='black')
            draw.line((x + 20, 75, x + 50, 105), fill='white', width=3)
            if cross:
                draw.line((x + 50, 75, x + 20, 105), fill='white', width=3)
        after = Image.new('RGB', (480, 180), 'white')
        ImageDraw.Draw(after).text((28, 55), 'Task saved',
                                  font=ImageFont.truetype(str(args.font), 32), fill='black')
        samples = [args.output / f'{name}-{stage}.ppm' for stage in ('before', 'after')]
        before.save(samples[0]); after.save(samples[1])
        side = 'left' if swapped else 'right'
        operator = bool(env.get('MIRAGE_CANVAS_HOST') and env.get('MIRAGE_CANVAS_OPERATOR') == '1')
        if operator:
            print(f'{len(report["samples"])+1}/3 {name}: click the blank Canvas centre once '
                  'within 15 seconds, release, then leave it untouched until it closes.', flush=True)
        timed_out = False
        try:
            proc = subprocess.run([str(args.binary.resolve()), str(configured.resolve()),
                                   *[str(p.resolve()) for p in samples], 'Task saved', mode, side],
                                  env=env, text=True, capture_output=True, timeout=90)
            code, stdout, stderr = proc.returncode, proc.stdout, proc.stderr
        except subprocess.TimeoutExpired as error:
            def decoded(value):
                return value.decode(errors='replace') if isinstance(value, bytes) else value or ''
            code, stdout, stderr = -1, decoded(error.stdout), decoded(error.stderr)
            timed_out = True
        lines = [line[5:] for line in stdout.splitlines() if line.startswith('LIVE ')]
        data = object_text(lines[-1]) if lines else {}
        # Child diagnostics are bounded; credential values never become evidence.
        diagnostic = (stdout + stderr)[-16384:]
        if key in diagnostic or key in json.dumps(data):
            raise RuntimeError('Credential appeared in child output; evidence refused')
        tools = data.get('tools', [])
        marks = []
        chosen = None
        verified = []
        mark_calls = click_calls = verify_calls = 0
        for tool in tools:
            value = object_text(tool.get('output', ''))
            if tool['tool'] == 'desktop.vision.mark':
                mark_calls += 1
                marks = value.get('marks', [])
            if tool['tool'] == 'desktop.vision.click':
                click_calls += 1
                args_used = object_text(tool.get('input', ''))
                chosen = next((m for m in marks if m['mark'] == args_used.get('mark')), None)
                verified.append(value.get('verified') is (mode == 'update'))
            if tool['tool'] == 'desktop.action.verify':
                verify_calls += 1
                verified.append(value.get('verified') is (mode == 'update'))
        placed = data.get('origin', {})
        target_x = placed.get('x', 20) + (100 if swapped else 360)
        target_y = placed.get('y', 20) + 90
        selection = bool(chosen and chosen['x'] < target_x < chosen['x'] + chosen['width']
                         and chosen['y'] < target_y < chosen['y'] + chosen['height']
                         and all(not m.get('template_id') for m in marks)
                         and data.get('supplied_input_items', 0) >= 1)
        passed = (code == 0 and selection and data.get('pressed') and data.get('released')
                  and data.get('presses') == data.get('releases') == 1
                  and data.get('wrong_presses') == 0 and click_calls == 1 and verify_calls == 1
                  and len(verified) == 2 and all(verified)
                  and data.get('ok') is (mode == 'update')
                  and data.get('failed') is (mode == 'unchanged')
                  and not data.get('cancelled') and not data.get('supplied_input_dropped')
                  and not data.get('supply_degradations'))
        sample = {'sample': name, 'mode': mode, 'swapped': swapped, 'passed': bool(passed),
                  'selection_passed': selection, 'exit_code': code, 'timed_out': timed_out,
                  'mark_calls': mark_calls, 'click_calls': click_calls, 'verify_calls': verify_calls,
                  'selected': chosen, 'result': data,
                  'sample_sha256': [hashlib.sha256(p.read_bytes()).hexdigest() for p in samples]}
        report['samples'].append(sample)
        report['passed'] += bool(passed)
        report['selection_passed'] += selection
        report['attempted'] += 1
        (args.output / f'{name}.log').write_text(diagnostic)
        (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps({k: sample[k] for k in ('sample', 'passed', 'selection_passed', 'mark_calls',
                                                'click_calls', 'verify_calls')}), flush=True)
        if operator and not passed:
            print('Host online sample failed; stopped without another popup or retry.', flush=True)
            break
    print(f"online Canvas: {report['passed']}/{report['total']}; selection {report['selection_passed']}/{report['total']}")
    return 0 if report['passed'] == report['total'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
