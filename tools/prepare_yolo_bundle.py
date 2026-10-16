"""Attach audited local YOLO assets to an existing verified OCR bundle; no downloads."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil


def prepare(source, bundle, lock_path):
    lock = json.loads(lock_path.read_text())
    manifest_path = bundle / 'manifest.json'
    manifest = json.loads(manifest_path.read_text())
    # Validate every byte before publishing either a resource or the manifest.
    verified = {}
    for name in ('param', 'bin'):
        entry = lock['artifacts'][name]
        path = source / entry['path']
        if path.stat().st_size != entry['bytes']:
            raise ValueError('YOLO artifact size mismatch: ' + name)
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != entry['sha256']:
            raise ValueError('YOLO artifact digest mismatch: ' + name)
        verified[name] = data
    detector = {k: lock[k] for k in ('model_id', 'class_names', 'input_side',
                                    'preprocessing', 'confidence_milli')}
    detector['artifacts'] = {}
    target = bundle / 'yolo'
    target.mkdir(exist_ok=True)
    for name, data in verified.items():
        relative = 'yolo/' + lock['artifacts'][name]['path']
        (bundle / relative).write_bytes(data)
        detector['artifacts'][name] = {**lock['artifacts'][name], 'path': relative}
    shutil.copyfile(lock_path, target / 'resources.lock.json')
    notices = bundle / 'notices'
    notices.mkdir(exist_ok=True)
    (notices / 'YOLO-model.txt').write_text(
        f"{lock['model_id']}\nSource: {lock['source']}\nLicense: {lock['license']}\n"
        "UI candidate detection does not determine interactivity or enabled state.\n"
        f"Conversion: {lock['conversion']}\n")
    shutil.copyfile(Path(__file__).resolve().parents[1] / 'third_party/mira/LICENSE', notices / 'YOLO-AGPL-3.0.txt')
    manifest['detector'] = detector
    temporary = manifest_path.with_suffix('.tmp')
    temporary.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n')
    temporary.replace(manifest_path)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--bundle', type=Path, required=True)
    p.add_argument('--lock', type=Path, required=True)
    a = p.parse_args()
    prepare(a.source, a.bundle, a.lock)


if __name__ == '__main__':
    main()
