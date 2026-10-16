"""Validate explicit Mirage capture/mark evidence and render a local review gallery.

Run mirador_review_run.py first. This does not capture, infer, click, or download.
"""
import argparse
import html
import json
import math
from pathlib import Path


def overlap(a, b):
    area = max(0, min(a[0]+a[2], b[0]+b[2])-max(a[0], b[0])) * max(0, min(a[1]+a[3], b[1]+b[3])-max(a[1], b[1]))
    return area / (a[2]*a[3]+b[2]*b[3]-area) if area else 0.


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--results-dir', default='integrated-all')
    p.add_argument('--expect-samples', type=int, default=11)
    a = p.parse_args()
    repo = Path(__file__).resolve().parents[2]
    execution = json.loads((a.root / (a.results_dir+'-execution.json')).read_text())
    samples = execution['samples']
    assert len(samples) == a.expect_samples
    reports, gallery = {}, []
    for sample in samples:
        name = sample['name']
        assert sample['exit_code'] == 0 and sample['capture_pixels_exact'], name
        report = json.loads((a.root / a.results_dir / name / 'report.json').read_text())
        assert report['detector_backend_calls'] == 1, name
        refs = {r['ref']: r for r in report['regions']}
        for mark in report['marks']:
            assert mark['ref'] in refs and mark['number'] > 0
            assert report['regions'][mark['number']-1]['ref'] == mark['ref']
            if mark['selectable']:
                x, y, w, h = refs[mark['ref']]['bounds']
                assert x <= mark['anchor_x'] < x+w and y <= mark['anchor_y'] < y+h
        if name == 'blank':
            assert not refs and not report['marks']
        reports[name] = dict(image_geometry=report['image_geometry'],
                             regions=len(refs), marks=len(report['marks']),
                             detector_calls=report['detector_backend_calls'],
                             wall_seconds=sample['process_wall_seconds'])
        relative = a.results_dir+'/'+name
        gallery.append(f'<section><h2>{html.escape(name)}</h2><p>{len(refs)} fused regions; one real YOLO call</p>'
                       f'<div><img src="{relative}/raw.png"><img src="{relative}/marked.png"></div></section>')
    form = json.loads((a.root / a.results_dir / 'form-light/report.json').read_text())
    annotations = json.loads((repo / 'third_party/mirador/integrations/detector_yolo/eval/annotations.json').read_text())
    ox, oy, _, _ = form['image_geometry']
    coordinates, used = [], set()
    for target in annotations['ui-form-light']['targets']:
        x, y, w, h = target['bounds']
        gt = [x+ox, y+oy, w, h]
        score, region = max(((overlap(gt, r['bounds']), r) for r in form['regions'] if r['ref'] not in used), key=lambda x: x[0])
        used.add(region['ref'])
        mark = next(m for m in form['marks'] if m['ref'] == region['ref'])
        b = region['bounds']
        error = math.hypot(b[0]+b[2]/2-gt[0]-w/2, b[1]+b[3]/2-gt[1]-h/2)
        inside = gt[0] <= mark['anchor_x'] < gt[0]+w and gt[1] <= mark['anchor_y'] < gt[1]+h
        assert score >= .8 and error <= 2 and inside and mark['selectable'], target['name']
        coordinates.append(dict(name=target['name'], ref=region['ref'], iou=score,
                                center_error_px=error, anchor_inside_gt=inside))
    summary = dict(samples=reports, form_coordinates=coordinates,
                   scope='X11 1:1 pixel replay, origin (37,53), real OCR/YOLO/fusion/SoM; no desktop clicks')
    (a.root / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n')
    document = '<!doctype html><meta charset="utf-8"><title>Mirage YOLO integration review</title>'
    document += '<style>body{margin:24px;font:16px system-ui;background:#f3f5f7;color:#18202b}section{margin:24px 0}section div{display:flex;gap:16px}img{width:calc(50% - 8px);object-fit:contain;background:white;align-self:start}pre{white-space:pre-wrap}</style>'
    document += '<h1>Mirage YOLO integration</h1><p>Original pixels / actual product marks. Candidate boxes do not prove interactivity. Host image is a previously authorized task-body crop replayed on X11.</p>'
    document += '<pre>'+html.escape(json.dumps(coordinates,ensure_ascii=False,indent=2))+'</pre>'+''.join(gallery)
    (a.root / 'review.html').write_text(document)
    print(f'{len(samples)} captures/mark maps passed; 6/6 form boxes IoU >= .8, center error <= 2px, anchors inside targets')


if __name__ == '__main__':
    main()
