"""Real-window settings audit. Used only by the private acceptance driver.

XTest actions operate the shipped app; assertions observe service ACKs and
pixels. Public synthetic keys only, no inference/network calls. Two bounded
passes: discovery then confirmation. No product hooks or user state access.
"""
import copy
import json
import time


def masked_key_pixels(image):
    # Focus changes on Save hide the caret. Compare the interior masks, remove
    # only a full-height caret column; input border/hover are outside this crop.
    text = image.crop((580, 401, 1000, 424)).copy()
    pixels = text.load()
    for x in range(text.width):
        if sum(sum(pixels[x, y][:3]) < 480 for y in range(text.height)) > 12:
            for y in range(text.height):
                pixels[x, y] = (255, 255, 255)
    return text.tobytes()

def run(click, paste, key, capture, wire, output, saved, expected_fixed, service_lifecycle=None):
    checks = []

    def check(name, passed, details=''):
        checks.append(dict(name=name, passed=bool(passed), details=details))
        (output / 'audit-results.json').write_text(json.dumps(dict(
            scope='real Release window / XTest / private Service and keyring',
            expected_fixed=expected_fixed, inference_requests=0, checks=checks), indent=2))
        print(name + ': ' + ('PASS' if passed else 'FAIL'), flush=True)

    def document():
        return json.loads(wire.call('model.get')['model_settings'])

    def enter(x, y, value):
        click(x, y)
        key('a', ctrl=True)
        paste(value)

    def save():
        click(1060, 744)
        time.sleep(.3)
        return document()

    def refresh():
        click(977, 152)
        time.sleep(.25)

    def cancel():
        click(610, 744)
        time.sleep(.25)

    def add_model(value, count):
        click(1045, 456)
        enter(750, 516 + count * 40, value)
        click(750, 556 + count * 40)

    def header(img):
        return img.crop((604, 209, 900, 235)).tobytes()

    def dimmed(img):
        return img.getpixel((10, 100))[0] < 215

    first = document()
    check('preset key-only saved and acknowledged', first['model']['api_key_configured'])
    enter(750, 556, '64000')
    add_model('fixture-second', 1)
    enter(750, 596, '96000')
    click(750, 494)
    enter(750, 596, '32000')
    capture('audit-budget-before-delete')
    click(1076, 534)  # Delete the other model, keeping the selected model.
    after = save()
    check('deleting other model preserves unsaved selected budget',
          len(after['models']) == 1 and after['models'][0]['context_window_tokens'] == 32000,
          'reported budget=' + str(after['models'][0].get('context_window_tokens')))
    a = copy.deepcopy(after['models'][0])
    b = copy.deepcopy(a)
    b.update(display_name='audit-model-b', provider_id='audit-b', provider_name='Sandbox B',
             model='fixture-b', enabled=False, dialect='openai.chat-completions.v1',
             context_window_tokens=65536)
    seed = dict(schema=1, model=a, models=[a, b])
    wire.call('model.set', settings=json.dumps(seed))
    refresh()
    click(420, 248)
    baseline_b = capture('audit-b-selected')
    click(1080, 220)
    click(1000, 262)
    enter(750, 220, 'changed B')
    cancel()
    check('cancel restores selected service rather than active service',
          header(capture('audit-b-cancelled')) == header(baseline_b))
    # Reset the editor independently of the preceding navigation outcome.
    refresh()
    click(420, 248)
    add_model('fixture-b2', 1)
    after = save()
    check('adding a model keeps disabled service disabled',
          len([m for m in after['models'] if m['provider_id'] == 'audit-b']) == 2 and
          all(not m.get('enabled', False) for m in after['models'] if m['provider_id'] == 'audit-b'))
    # The selected service is B, active endpoint remains a harmless fixture.
    capture('audit-two-models')
    enter(750, 596, '999')
    before = document()
    after = save()
    check('invalid budget does not alter acknowledged configuration', before == after)
    enter(750, 596, '68000')
    after = save()
    check('valid correction can be saved after validation failure',
          any(m['context_window_tokens'] == 68000 for m in after['models'] if m['provider_id'] == 'audit-b'))
    click(1045, 456)
    enter(750, 596, '   fixture-b2   ')
    click(750, 636)
    after = save()
    check('trimmed duplicate model rejected without catalog change',
          len([m for m in after['models'] if m['provider_id'] == 'audit-b']) == 2)
    # Escape closes the add-model editor before leaving the settings page.
    key('Escape')
    refresh()
    selected = capture('audit-before-inline-escape')
    click(1045, 456)
    key('Escape')
    escaped = capture('audit-after-inline-escape')
    check('escape closes inline editor without leaving settings',
          escaped.crop((320, 96, 530, 130)).tobytes() == baseline_b.crop((320, 96, 530, 130)).tobytes())
    if not expected_fixed:
        # Current Escape leaves settings. Re-enter the model page.
        click(226, 756)
        click(90, 226)
        cancel()
        refresh()
    else:
        cancel()
    # B may contain an extra invalid duplicate on the old build; reset it.
    seed = dict(schema=1, model=a, models=[a, b])
    wire.call('model.set', settings=json.dumps(seed))
    refresh()
    click(420, 248)
    click(750, 348)
    opened = capture('audit-protocol-open')
    click(860, 170)
    closed = capture('audit-protocol-outside')
    check('outside click dismisses protocol menu',
          opened.crop((568, 380, 1088, 496)).tobytes() != closed.crop((568, 380, 1088, 496)).tobytes())
    key('Escape')
    if expected_fixed:
        # Menu was already dismissed; escape navigates back. Re-enter.
        click(226, 756)
        click(90, 226)
    click(420, 248)
    enter(750, 556, '72000')
    click(420, 212)
    prompt = capture('audit-dirty-navigation')
    check('dirty service switch offers explicit recovery choices', dimmed(prompt))
    if expected_fixed:
        click(650, 480)  # Keep editing.
        after_keep = capture('audit-dirty-kept')
        check('keep editing retains current service', header(after_keep) == header(baseline_b))
    cancel()
    refresh()
    click(420, 248)
    click(1080, 220)
    click(1000, 300)
    deletion = capture('audit-delete-provider')
    check('service deletion requires confirmation',
          dimmed(deletion) and len(document()['models']) == 2)
    if expected_fixed:
        key('Escape')
        check('cancel delete keeps acknowledged service', len(document()['models']) == 2)
        click(1080, 220)
        click(1000, 300)
        click(760, 480)
        time.sleep(.3)
        check('confirm delete removes only the target service',
              len(document()['models']) == 1 and document()['models'][0]['provider_id'] == a['provider_id'])
    # Recover the same initial acknowledged profile for credential controls.
    wire.call('model.set', settings=json.dumps(dict(schema=1, model=a, models=[a])))
    refresh()
    click(1070, 381)  # Stage key removal.
    cancel()
    check('cancel key removal retains original credential', document()['model']['credential_ref'] == a['credential_ref'])
    click(1070, 381)
    after = save()
    check('save key removal clears acknowledged credential', not after['model'].get('api_key_configured', False))
    enter(750, 412, ' fixturekey \t')
    after = save()
    check('pasted key tolerates surrounding clipboard whitespace', after['model'].get('api_key_configured', False))
    if expected_fixed:
        # Independent real-window verification of every connection control.
        for row, dialect in [(438, 'openai.responses.v1'),
                             (402, 'openai.chat-completions.v1'),
                             (474, 'anthropic.messages.v1')]:
            click(750, 348)
            click(750, row)
            after = save()
            check('protocol selection persists ' + dialect, after['model']['dialect'] == dialect)
        click(1035, 220)
        after = save()
        check('service switch persists disabled state', not after['model'].get('enabled', False))
        click(1035, 220)
        after = save()
        check('service switch can enable again', after['model'].get('enabled', False))
        before = document()
        enter(750, 284, 'not-a-url')
        after = save()
        check('invalid connection does not overwrite acknowledged configuration', before == after)
        capture('audit-invalid-url')
        enter(750, 284, '  https://api.minimaxi.com/anthropic/v1  ')
        after = save()
        check('connection correction saves after failure',
              after['model']['endpoint'] == 'https://api.minimaxi.com')
        click(1080, 220)
        click(1000, 262)
        enter(750, 220, '   ')
        before = document()
        after = save()
        check('blank service name rejected without catalog change', before == after)
        enter(750, 220, '  Renamed fixture  ')
        after = save()
        check('service rename updates acknowledged catalog',
              after['model']['provider_name'] == 'Renamed fixture' and
              after['models'][0]['provider_name'] == 'Renamed fixture')
        enter(750, 412, 'replacementfixturekey')
        hidden = capture('audit-key-hidden')
        click(1080, 412)
        visible = capture('audit-key-visible')
        check('eye button reveals only current key draft',
              hidden.crop((568, 396, 1050, 428)).tobytes() != visible.crop((568, 396, 1050, 428)).tobytes())
        click(1080, 412)
        old_ref = document()['model']['credential_ref']
        after = save()
        check('replacement key produces new credential reference', after['model']['credential_ref'] != old_ref)
        click(1045, 152)
        draft = capture('audit-new-service')
        enter(750, 220, 'Empty fixture')
        enter(750, 284, 'https://example.com/v1')
        after = save()
        check('empty service saves disabled without inventing a model',
              after['model']['provider_name'] == 'Empty fixture' and not after['model'].get('enabled', False) and
              not after['model'].get('model'))
        click(1045, 456)
        # Empty placeholder row takes 48px instead of a model row's 40px.
        enter(750, 564, 'fixture-empty')
        click(750, 604)
        after = save()
        check('first model can be added to an empty disabled service',
              after['model'].get('model') == 'fixture-empty' and not after['model'].get('enabled', False))
        click(1076, 494)
        after = save()
        check('last model deletion retains disabled service',
              after['model']['provider_name'] == 'Empty fixture' and not after['model'].get('enabled', False) and
              not after['model'].get('model'))
        if service_lifecycle:
            original = document()
            enter(750, 412, 'offlinefixturekey')
            staged = capture('audit-key-before-outage')
            service_lifecycle('stop')
            click(1060, 744)
            failed = capture('audit-offline-save')
            check('failed save retains current key draft',
                  masked_key_pixels(staged) == masked_key_pixels(failed))
            cancel()
            cancelled = capture('audit-offline-cancel')
            check('offline cancel restores acknowledged editor',
                  staged.crop((568, 396, 1050, 428)).tobytes() != cancelled.crop((568, 396, 1050, 428)).tobytes())
            enter(750, 412, 'reconnectfixturekey')
            wire = service_lifecycle('restart')
            check('failed and cancelled saves leave disk configuration unchanged', original == document())
            refresh()
            time.sleep(.5)
            after = save()
            check('reconnect preserves editable key draft and can save',
                  after['model'].get('api_key_configured', False) and
                  after['model'].get('credential_ref') != original['model'].get('credential_ref'))
    capture('audit-final-state')
    if expected_fixed:
        assert all(c['passed'] for c in checks), 'settings audit contains failed checks'
    return checks
