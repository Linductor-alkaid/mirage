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
        # Keys are local drafts even while the catalog is unknown.
        class MaskedKey:
            def __init__(self, image):
                self.region = image.crop((590, 401, 1000, 424))
                self.caret = {x for x in range(self.region.width)
                              if sum(max(self.region.getpixel((x, y))) < 160
                                     for y in range(self.region.height)) > 12}

            def __eq__(self, other):
                # Ignore caret columns in both images, including when the caret
                # overlaps a mask after an invalid edit restores the raw value.
                skip = self.caret | other.caret
                return all(self.region.getpixel((x, y)) == other.region.getpixel((x, y))
                           for x in range(self.region.width) if x not in skip
                           for y in range(self.region.height))

        masked = MaskedKey

        click(750, 412)
        for char in 'fixturetypedkey':
            key(char)
        typed = capture('preset-offline-key-typed')
        entered = masked(typed) != masked(preview)
        check('offline preset accepts continuous keyboard key input', entered)
        if not entered:
            assert not fixed, 'offline key input is blocked'
            return
        key('BackSpace')
        check('offline key supports deletion', masked(capture('preset-offline-key-deleted')) != masked(typed))
        key('a', ctrl=True)
        paste('fixtureclipboardkey')
        pasted = capture('preset-offline-key-pasted')
        check('offline key supports clipboard replacement', masked(pasted) != masked(typed))
        click(1077, 412)
        check('offline key visibility can be toggled', masked(capture('preset-offline-key-visible')) != masked(pasted))
        click(1077, 412)
        check('offline key is masked again', masked(capture('preset-offline-key-hidden')) == masked(pasted))
        click(750, 412)
        key('a', ctrl=True)
        paste('invalid key')
        check('offline invalid paste preserves valid draft', masked(capture('preset-offline-key-invalid')) == masked(pasted))
        click(1060, 744)
        check('offline key never saves before the catalog ACK',
              not json.loads(wire.call('model.get')['model_settings']).get('models'))
        click(420, 288)  # OpenAI; protect the unsaved key.
        check('offline key navigation asks before discarding',
              capture('preset-offline-key-navigation').getpixel((10, 100))[0] < 215)
        click(650, 480)
        check('offline keep editing retains key', masked(capture('preset-offline-key-kept')) == masked(pasted))
        click(420, 288)
        click(760, 480)
        click(420, 324)  # MiniMax again; discarded key must be gone.
        check('offline discard clears key draft', masked(capture('preset-offline-key-discarded')) == masked(preview))
        click(750, 412)
        paste('fixturereconnectkey')
        pending_key = capture('preset-offline-key-pending')
        profile = dict(display_name='saved-fixture', provider_id='preset:minimax',
                       provider_name='Saved MiniMax', endpoint='https://example.com', api_prefix='/v1',
                       model='saved-fixture-minimax', dialect='openai.chat-completions.v1', enabled=False)
        other = dict(profile, display_name='saved-other', provider_id='other-service',
                     provider_name='Saved other', model='saved-other', endpoint='https://other.example.com')
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
        check('connection retains unsaved key draft',
              masked(capture('preset-reconnected-key-draft')) == masked(pending_key))
        old_reference = catalog['models'][0]['credential_ref']
        other_before = catalog['models'][1]
        click(1060, 744)
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            updated = json.loads(wire.call('model.get')['model_settings'])
            edited = next(m for m in updated['models'] if m['provider_id'] == 'preset:minimax')
            if edited.get('credential_ref') != old_reference:
                break
            time.sleep(.1)
        check('reconnected key can be saved and acknowledged',
              edited.get('credential_ref') != old_reference and edited.get('api_key_configured', False))
        check('key save preserves saved connection and other service',
              len(updated['models']) == 2 and edited['endpoint'] == 'https://example.com' and
              edited['model'] == 'saved-fixture-minimax' and
              next(m for m in updated['models'] if m['provider_id'] == 'other-service') == other_before)
        capture('preset-reconnected-key-saved')
        current = 2
        saved = {2: 'https://example.com/v1'}
    baseline = json.loads(wire.call('model.get')['model_settings'])
    for index, (preset_id, name, url) in enumerate(presets):
        count = 2 + (0 if current in saved else 1) if saved else 1
        unavailable = set(saved)
        if current is not None:
            unavailable.add(current)
        if index in saved:
            order = list(dict.fromkeys(m['provider_id'] for m in baseline['models']))
            y = 212 + order.index('preset:' + preset_id) * 36
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
