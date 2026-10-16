"""Verify optional detector resource packaging without real model weights or network."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile

root = Path(sys.argv[1])
spec = importlib.util.spec_from_file_location('prepare_yolo_bundle', root / 'tools/prepare_yolo_bundle.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='mirage-yolo-bundle-') as temporary:
    path = Path(temporary)
    source, bundle = path / 'source', path / 'bundle'
    source.mkdir()
    bundle.mkdir()
    lock = json.loads((root / 'packaging/vision/yolo.lock.json').read_text())
    for name in ('param', 'bin'):
        data = ('synthetic-' + name).encode()
        (source / lock['artifacts'][name]['path']).write_bytes(data)
        lock['artifacts'][name].update(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
    locked = path / 'lock.json'
    locked.write_text(json.dumps(lock))
    original = {'schema': 1, 'model_id': 'synthetic-ocr', 'artifacts': {}}
    (bundle / 'manifest.json').write_text(json.dumps(original))
    module.prepare(source, bundle, locked)
    actual = json.loads((bundle / 'manifest.json').read_text())
    assert actual['detector']['preprocessing'] == 'bilinear114'
    assert actual['model_id'] == original['model_id']
    assert (bundle / 'notices/YOLO-AGPL-3.0.txt').stat().st_size > 10000
    assert (bundle / 'yolo/resources.lock.json').read_bytes() == locked.read_bytes()
    previous = (bundle / 'manifest.json').read_bytes()
    for name in ('param', 'bin'):
        file = source / lock['artifacts'][name]['path']
        content = file.read_bytes()
        file.write_bytes(bytes([content[0] ^ 1]) + content[1:])
        try:
            module.prepare(source, bundle, locked)
        except ValueError:
            pass
        else:
            raise AssertionError('tampered artifact accepted')
        assert (bundle / 'manifest.json').read_bytes() == previous
        file.write_bytes(content)
print('YOLO bundle: layout, notices, deterministic lock and tamper rejection passed')
