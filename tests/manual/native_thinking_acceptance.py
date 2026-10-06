"""Private Xvfb acceptance for the live MiniMax composer controls (opt-in paid).

Uses the authorized provider through native_conversation_acceptance's isolated
tray, keyring, desktop and clipboard fixture. No keys or private user content
are written to evidence. Tests two small live turns and actual UI choices.
"""
import json
import time


def run(click, paste, key, capture, wire, output):
    engine_prompt = '请先实际调用 wait 工具等待 1 毫秒，然后只回复 THINKING_OK。'
    metadata = wire.call('model.get')
    settings = json.loads(metadata['model_settings'])
    assert settings['model']['model'] == 'MiniMax-M3'
    assert settings['model']['reasoning_options'] == ['', 'none', 'adaptive']
    before = len(wire.call('session.list')['sessions'])
    click(962, 416)
    capture('thinking-options')
    click(850, 290)  # Empty-composer popup: default / off / on.
    capture('thinking-enabled')
    records = []
    sid = None
    for number, prompt in enumerate([engine_prompt, '只回复 OFF_OK。']):
        if number:
            click(1026, 744)
            capture('thinking-message-options')
            click(890, 574)  # Settled-composer popup: off.
            capture('thinking-disabled')
        click(470, 361 if number == 0 else 690)
        paste(prompt)
        key('Return')
        deadline = time.monotonic() + 100
        terminal = None
        previews = 0
        while time.monotonic() < deadline:
            if sid is None:
                sessions = wire.call('session.list')['sessions']
                if len(sessions) > before:
                    sid = max(sessions, key=lambda value: value['created_at_ms'])['id']
            if sid:
                turns = wire.call('session.chat.history', session_id=sid, limit=10)['turns']
                previews += sum(event.get('event') == 'session.chat_preview'
                                and event.get('session_id') == sid and bool(event.get('text'))
                                for event in wire.events)
                wire.events.clear()
                if len(turns) > number and turns[-1]['status'] != 'pending':
                    terminal = turns[-1]
                    break
            time.sleep(0.2)
        assert terminal and terminal['status'] == 'ok', 'live turn failed'
        assert terminal['user_text'] == prompt
        assert ('THINKING_OK' if number == 0 else 'OFF_OK') in terminal['reply_text']
        assert previews > 0, 'no text preview observed before terminal history'
        time.sleep(0.4)  # Allow the independent UI to consume its terminal event.
        capture('thinking-completed' if number == 0 else 'thinking-off-completed')
        records.append({'option': 'adaptive' if number == 0 else 'none',
                        'status': terminal['status'], 'reply_matches': True,
                        'progress_events_observed': previews})
    assert len(wire.call('session.list')['sessions']) == before + 1
    result = {'scope': 'real native composer, private tray/Xvfb/DBus/keyring; live MiniMax-M3',
              'choices_from_service': ['', 'none', 'adaptive'], 'same_session': True, 'turns': records}
    (output / 'thinking-results.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result, ensure_ascii=False), flush=True)
