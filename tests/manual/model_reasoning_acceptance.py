"""Opt-in three-request MiniMax-M3 reasoning/channel acceptance (DEC-082).

Uses an existing Mirage SecretRef read-only through the production transport.
Only public arithmetic is submitted; no user history, desktop tools, authorization
or raw model content is collected. Config/state/socket are temporary and private.
This bounded child-process driver does not implement product task concurrency.
"""
import argparse
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from native_conversation_acceptance import Wire, stop


def run(build, configuration):
    model = json.loads(configuration.read_text())['model']
    if model.get('dialect') != 'anthropic.messages.v1' or model.get('model') != 'MiniMax-M3':
        raise ValueError('this probe requires a configured MiniMax-M3 Messages model')
    public_fields = ['enabled', 'dialect', 'provider_id', 'provider_name', 'display_name',
                     'endpoint', 'api_prefix', 'model', 'credential_ref', 'supports_images',
                     'context_window_tokens']
    results = []
    with tempfile.TemporaryDirectory(prefix='mirage-reasoning-') as directory:
        root = Path(directory)
        env = os.environ.copy()
        for name, leaf in [('XDG_CONFIG_HOME', 'config'), ('XDG_STATE_HOME', 'state'),
                           ('XDG_CACHE_HOME', 'cache'), ('XDG_RUNTIME_DIR', 'runtime')]:
            value = root / leaf
            value.mkdir(mode=0o700)
            env[name] = str(value)
        endpoint = str(root / 'runtime' / 'service.sock')
        config = root / 'config' / 'service.json'
        config.write_text(json.dumps({'schema': 1, 'socket': endpoint, 'model':
                                      {k: model[k] for k in public_fields if k in model}}))
        config.chmod(0o600)
        children = []
        wire = None
        try:
            child = subprocess.Popen([str(build / 'apps/mirage-service'), '--config', str(config),
                                      '--socket', endpoint, '--state-dir', str(root / 'state'),
                                      '--no-recovery'], env=env, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL)
            children.append(child)
            deadline = time.monotonic() + 15
            while (not Path(endpoint).exists() and child.poll() is None
                   and time.monotonic() < deadline):
                time.sleep(.1)
            wire = Wire(endpoint)
            for choice in ['', 'none', 'adaptive']:
                sid = wire.call('session.open')['session_id']
                start = time.monotonic()
                wire.call('session.chat', session_id=sid, text='17 加 25 等于多少？请只回答 42。',
                          agent=False, access='chat_only', reasoning=choice)
                deadline = time.monotonic() + 130
                turn = None
                while time.monotonic() < deadline:
                    turns = wire.call('session.chat.history', session_id=sid, limit=1)['turns']
                    if turns and turns[-1]['status'] != 'pending':
                        turn = turns[-1]
                        break
                    time.sleep(.5)
                if turn is None:
                    wire.call('session.chat.cancel', session_id=sid)
                    raise RuntimeError('synthetic turn did not settle within its deadline')
                thought = [p for p in turn.get('parts', []) if p.get('kind') == 'thinking']
                results.append({'choice': choice or 'default', 'status': turn['status'],
                                'exact_answer': turn.get('reply_text', '').strip() == '42',
                                'reply_chars': len(turn.get('reply_text', '')),
                                'thinking_parts': len(thought),
                                'thinking_chars': sum(len(p.get('text', '')) for p in thought),
                                'elapsed_seconds': round(time.monotonic() - start, 2)})
                print(json.dumps(results[-1]), flush=True)
        finally:
            if wire:
                wire.sock.close()
            stop(children)
    return {'synthetic_prompt': '17 + 25; answer only 42', 'desktop_access': False,
            'user_history_supplied': False, 'results': results,
            'passed': all(r['status'] == 'ok' and r['exact_answer'] and
                          bool(r['thinking_parts']) == (r['choice'] != 'none') for r in results)}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build/native-release'))
    parser.add_argument('--config', type=Path,
                        default=Path.home() / '.config/mirage/service.json')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    summary = run(args.build.resolve(), args.config.resolve())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n')
    raise SystemExit(0 if summary['passed'] else 1)
