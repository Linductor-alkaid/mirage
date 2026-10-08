"""Private Xvfb acceptance: non-anthropic thinking renders collapsed and expands.

Runs the real composer against a --provider config whose wire_api is "chat"
(openai.chat-completions.v1). After the upgrade to mira PR#84 the model's
reasoning output must reach the conversation as a collapsed "thinking" process
row that expands on click (MIRA-20261008-001 follow-up; DEC-052 semantics).
"""

import json
import time

from PIL import ImageChops


def run(click, paste, key, capture, wire, output):
    metadata = wire.call('model.get')
    settings = json.loads(metadata['model_settings'])
    active = settings['model']
    assert active['dialect'] == 'openai.chat-completions.v1', (
        'expected a chat-completions provider, got ' + active['dialect'])
    assert active.get('model'), 'provider config carries no model id'
    before = len(wire.call('session.list')['sessions'])

    click(470, 361)
    paste('Reply with only two letters: OK')
    key('Return')

    deadline = time.monotonic() + 120
    terminal = None
    sid = None
    while time.monotonic() < deadline:
        sessions = wire.call('session.list')['sessions']
        if sid is None and len(sessions) > before:
            sid = max(sessions, key=lambda value: value['created_at_ms'])['id']
        if sid is not None:
            # The protocol rejects session.chat.history without a session_id
            # (protocol_error + close), so the new session must be resolved first.
            turns = wire.call('session.chat.history', session_id=sid, limit=10)['turns']
            if turns and turns[-1]['status'] != 'pending':
                terminal = turns[-1]
                break
        time.sleep(0.5)
    assert terminal is not None, 'live turn did not settle'
    assert terminal['status'] == 'ok', 'live turn failed: ' + terminal.get('error', '')
    thinking = [part for part in (terminal.get('parts') or [])
                if part.get('kind') == 'thinking']
    assert thinking and thinking[0].get('text'), (
        'settled turn carries no thinking part; dialect reasoning output was dropped')
    time.sleep(1.2)  # let the independent UI consume the terminal event
    capture('thinking-collapsed')

    # The collapsed row is the first row of the settled reply block (label
    # "> 思考过程" with "已完成" right-aligned). First click a neutral canvas
    # spot so the composer loses focus: otherwise its caret vanishing on the
    # first scan click produces a large composer-band diff that masquerades as
    # an expansion. A genuine expansion must light up the content region
    # (diff bbox starting above y=500), not the composer band. Scan both the
    # row band (x=700) and the row label (x=430) across the reply-block rows.
    click(700, 450)
    time.sleep(0.3)
    expanded_at = None
    for y in range(120, 260, 8):
        for x in (700, 430):
            base = capture('thinking-scan')
            click(x, y)
            time.sleep(0.3)
            after = capture('thinking-scan')
            bbox = ImageChops.difference(base, after).getbbox()
            if (bbox and bbox[2] - bbox[0] > 160 and bbox[3] - bbox[1] > 24
                    and bbox[1] < 500):
                expanded_at = {'x': x, 'y': y, 'bbox': list(bbox)}
                break
        if expanded_at:
            break
    assert expanded_at is not None, 'no collapsed thinking row expanded in the reply column'
    capture('thinking-expanded')

    result = {
        'scope': 'real native composer, private tray/Xvfb/DBus; live chat-completions '
                 'thinking display (MIRA-20261008-001 follow-up)',
        'provider_model': active.get('provider_name', '') + ' / ' + active['model'],
        'dialect': active['dialect'],
        'reasoning_options': active.get('reasoning_options', []),
        'thinking_parts': len(thinking),
        'thinking_chars': sum(len(part.get('text', '')) for part in thinking),
        'thinking_head': thinking[0]['text'][:140],
        'reply_head': terminal.get('reply_text', '')[:60],
        'expanded_at': expanded_at,
        'sessions_after': len(wire.call('session.list')['sessions']),
    }
    assert result['sessions_after'] == before + 1, 'unexpected session count'
    (output / 'dialect-thinking-results.json').write_text(
        json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False), flush=True)
