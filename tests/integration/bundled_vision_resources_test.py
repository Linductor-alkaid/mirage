"""Locked offline resource reconstruction and tamper rejection (DEC-068)."""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

source, repository, bundled = map(Path, sys.argv[1:])
lock = repository / 'packaging/vision/resources.lock.json'
data = json.loads(lock.read_text())
checks = 0

def check(value):
    global checks
    checks += 1
    assert value, f'bundled resource check {checks}'

with tempfile.TemporaryDirectory(prefix='mirage-bundle-test-') as temporary:
    root = Path(temporary)
    inputs, output = root / 'source', root / 'output'
    for relative in data['source_artifacts']:
        path = inputs / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source / relative, path)
    command = [sys.executable, str(repository / 'tools/prepare_vision_bundle.py'),
               '--source', str(inputs), '--output', str(output), '--lock', str(lock)]
    check(subprocess.run(command, capture_output=True, timeout=20).returncode == 0)
    expected_manifest = json.loads((bundled / 'manifest.json').read_text())
    expected_manifest.pop('detector', None)  # Optional, independently locked YOLO bundle.
    check(json.loads((output / 'manifest.json').read_text()) == expected_manifest)
    for name, expected in data['output_artifacts'].items():
        content = (output / name).read_bytes()
        check(len(content) == expected['bytes'] and hashlib.sha256(content).hexdigest() == expected['sha256'])
    # Each input, including dictionary and parameter graph, is authenticated.
    for relative in data['source_artifacts']:
        path = inputs / relative
        content = path.read_bytes()
        path.write_bytes(bytes([content[0] ^ 1]) + content[1:])
        check(subprocess.run(command, capture_output=True, timeout=20).returncode != 0)
        path.write_bytes(content)
    wrong = root / 'wrong.lock.json'
    data['output_artifacts']['rec_bin']['sha256'] = '0' * 64
    wrong.write_text(json.dumps(data))
    command[-1] = str(wrong)
    check(subprocess.run(command, capture_output=True, timeout=20).returncode != 0)
print(f'Bundled resources: {checks} checks')
