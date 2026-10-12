"""4K private display, bounded window ROI capture, same-source click and OCR.

Reuses public geometry/text/template fixtures. Does not upload pixels or relax
capture/analysis/deadline budgets. Default full-display capture must fail while
each scoped 480x180 analysis succeeds.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
for key in ('binary', 'manifest', 'font', 'output'):
    parser.add_argument('--' + key, type=Path, required=True)
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
manifest = json.loads(args.manifest.read_text())
for artifact in manifest['artifacts'].values():
    artifact['path'] = str((args.manifest.parent / artifact['path']).resolve())
manifest['roi_capture'] = True
configured = args.output / 'regional-manifest.json'
configured.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
env = os.environ.copy()
for key in ('MIRAGE_CANVAS_HOST', 'MIRAGE_CANVAS_OCCLUDE'):
    env.pop(key, None)
env.update(MIRAGE_CANVAS_LIVE='0', MIRAGE_CANVAS_ROI='1', MIRAGE_CANVAS_POS='1000,700')
reports = {}
for kind, script, count in [('geometry', 'visual_geometry_acceptance.py', 3),
                             ('template', 'visual_template_acceptance.py', 4),
                             ('text', 'visual_canvas_acceptance.py', 5)]:
    output = args.output / kind
    proc = subprocess.run(['python3', str(Path(__file__).with_name(script)), '--binary',
                           str(args.binary.resolve()), '--manifest', str(configured.resolve()),
                           '--font', str(args.font.resolve()), '--output', str(output.resolve())],
                          env=env, capture_output=True, text=True, timeout=360)
    (args.output / (kind + '.log')).write_text(proc.stdout + proc.stderr)
    report = json.loads((output / 'report.json').read_text())
    report['source'] = 'Private Xvfb 3840x2160; fixed scoped 480x180 window at (1000,700); ROI before capture/analysis'
    (output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    assert proc.returncode == 0 and report['passed'] == report['total'] == count, kind
    for sample in report['samples']:
        assert 'roi_capture=1' in sample['summary'], (kind, sample['summary'])
    reports[kind] = report
    print(f'{kind}: {count}/{count}', flush=True)
summary = {'schema': 1, 'display': {'width':3840, 'height':2160},
           'capture': {'width':480, 'height':180, 'bytes':345600, 'origin_x':1000, 'origin_y':700},
           'limits': {'capture_bytes':8388608, 'owner_seconds':30, 'observation_seconds':5},
           'full_capture_rejected_before_analysis': True, 'reports': reports,
           'passed': 12, 'total': 12, 'online': False}
(args.output / 'report.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
print('4K regional Canvas: 12/12')
