"""Real XTest preset selection; public URLs only, no inference requests."""
import json
import re
import time
from pathlib import Path


def run(click, paste, key, capture, clipboard, wire, lifecycle, output, offline, fixed):
    source = (Path(__file__).resolve().parents[2] / 'apps/native/provider_presets.hpp').read_text()
    presets = re.findall(r'ProviderPreset\{\s*"([^\"]+)",\s*"([^\"]+)",\s*"([^\"]+)"', source)
    assert len(presets) == 11
    checks = []

    def check(name, passed):
        checks.append(dict(name=name, passed=bool(passed)))
        (output / 'preset-results.json').write_text(json.dumps(dict(
            scope='actual native window, XTest/public URL clipboard, private Service/keyring',
            offline_start=offline, checks=checks, inference_requests=0), indent=2))
        print(name + ': ' + ('PASS' if passed else 'FAIL'), flush=True)

    def public_url():
        click(750, 284)
        key('a', ctrl=True)
        return clipboard()

    def title(image):
        return image.crop((604, 209, 900, 235)).tobytes()

    current = None
    dirty = False
    saved = {}
    if offline:
        before = capture('preset-offline-start')
        click(420, 288)
        selected = capture('preset-offline-selected')
        chosen = title(before) != title(selected)
        check('preset can be selected before initial service connection', chosen)
        if not chosen:
            assert not fixed, 'offline preset is blocked'
            return
        click(420, 324)  # MiniMax after OpenAI has become the local preview row.
        preview = capture('preset-offline-switched')
        check('offline preview can switch to another preset', title(preview) != title(selected))
        # Fields stay locked while the acknowledged catalog is unknown.
        click(750, 412)
        paste('fixture-preview-must-not-enter')
        check('offline preview does not accept key edits',
              # Exclude the insertion caret at x=580, which changes on focus.
              preview.crop((590, 401, 1000, 424)).tobytes() ==
              capture('preset-offline-key-locked').crop((590, 401, 1000, 424)).tobytes())
        profile = dict(display_name='saved-fixture', provider_id='preset:minimax',
                       provider_name='Saved MiniMax', endpoint='https://example.com', api_prefix='/v1',
                       model='saved-fixture-minimax', dialect='openai.chat-completions.v1', enabled=False)
        other = dict(profile, display_name='saved-other', provider_id='other-service',
                     provider_name='Saved other', model='saved-other')
        wire.call('model.set', settings=json.dumps(dict(schema=1, model=profile, models=[profile, other])),
                  api_key='fixturekey')
        lifecycle('stop')
        wire = lifecycle('restart_for_ui')
        click(977, 152)
        time.sleep(.5)
        check('connection restores selected saved preset configuration', public_url() == 'https://example.com/v1')
        catalog = json.loads(wire.call('model.get')['model_settings'])
        check('preview retains saved catalog and credentials without writing',
              len(catalog['models']) == 2 and catalog['models'][0].get('api_key_configured', False))
        current = 2
        saved = {2: 'https://example.com/v1'}
    baseline = json.loads(wire.call('model.get')['model_settings'])
    for index, (preset_id, name, url) in enumerate(presets):
        count = 2 + (0 if current in saved else 1) if saved else 1
        unavailable = set(saved)
        if current is not None:
            unavailable.add(current)
        if index in saved:
            y = 212
        else:
            visible = [i for i in range(len(presets)) if i not in unavailable]
            y = 252 + count * 36 + visible.index(index) * 36
        click(420, y)
        if dirty:
            dialog = capture('preset-discard-dialog')
            check('dirty preset navigation shows recovery choices ' + name,
                  dialog.getpixel((10, 100))[0] < 215)
            if index == 1:
                click(650, 480)
                check('keep editing retains selected preset', public_url() == presets[current][2])
                click(420, y)
            click(760, 480)
        observed = public_url()
        check('preset selection applies public URL ' + name, observed == saved.get(index, url))
        current = index
        dirty = index not in saved
    check('preset selection does not persist or delete configurations',
          json.loads(wire.call('model.get')['model_settings']) == baseline)
    capture('preset-selection-final')
    assert all(c['passed'] for c in checks), 'preset selection checks failed'
