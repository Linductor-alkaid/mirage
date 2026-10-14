"""Opt-in live Messages -> Runtime -> IPC -> native pet, in private Xvfb/DBus.

One public arithmetic request, bounded by 120 seconds. Only the synthetic prompt
is supplied; no user history or desktop actions. Key is passed through the child
environment, never args/reports. Model thinking is counted in JSON; captures
contain only the synthetic public arithmetic prompt in a private display.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
import tomllib
from urllib.parse import urlsplit
from native_conversation_acceptance import Wire, stop
from native_desktop_launch import start_watcher, adopt_remaining, AdoptedProcess, launch
from pet_preview_acceptance import await_value


def session(args):
    from Xlib import display, X
    from PIL import Image, ImageChops
    children, wire = [], None
    env = os.environ.copy()
    report = dict(scope='private Xvfb/DBus; one public arithmetic turn; live provider and native pet',
                  user_history=False, desktop_actions=False, checks=[], observations={})

    def check(name, ok):
        report['checks'].append(dict(name=name, passed=bool(ok)))
        (args.output / 'online-results.json').write_text(json.dumps(report, indent=2) + '\n')
        print(name + ': ' + ('PASS' if ok else 'FAIL'), flush=True)
        assert ok, name

    try:
        assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0
        cfg = tomllib.loads((args.provider / 'config.toml').read_text())
        provider = cfg['model_providers'][cfg['model_provider']]
        origin = urlsplit(provider['base_url'])
        assert origin.scheme == 'https' and origin.hostname in ('api.minimaxi.com', 'api.minimax.io')
        assert not (origin.username or origin.password or origin.query or origin.fragment or origin.port)
        env['MIRAGE_PET_ACCEPTANCE_KEY'] = json.loads((args.provider / 'auth.json').read_text())['OPENAI_API_KEY']
        assert env['MIRAGE_PET_ACCEPTANCE_KEY']
        runtime_dir = Path(env['XDG_RUNTIME_DIR']) / 'mirage'
        runtime_dir.mkdir(mode=0o700, exist_ok=True)
        endpoint = str(runtime_dir / 'mirage-service.sock')
        config_dir = Path(env['XDG_CONFIG_HOME']) / 'mirage'
        config_dir.mkdir(mode=0o700, exist_ok=True)
        config_path = config_dir / 'service.json'
        config_path.write_text(json.dumps(dict(schema=1, socket=endpoint, model=dict(
            enabled=True, display_name='Public arithmetic acceptance',
            dialect='anthropic.messages.v1', model=cfg['model'],
            endpoint='https://' + origin.hostname, api_prefix='/anthropic/v1',
            credential_env='MIRAGE_PET_ACCEPTANCE_KEY', context_window_tokens=128000))))
        config_path.chmod(0o600)
        report['observations'].update(model=cfg['model'], dialect='anthropic.messages.v1')
        if not args.desktop_launch:
            start_watcher(env, children, args.output)
        wm_record = args.output / 'wm.json'
        wm_record.unlink(missing_ok=True)
        children.append(subprocess.Popen([sys.executable, str(Path(__file__).with_name('x11_wm_fixture.py')),
            '--record', str(wm_record), '--advertise-compositor'], env=env,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        await_value(wm_record.exists, bool)
        if args.desktop_launch:
            tray, ui, actual_endpoint = launch(args.build, env, children, args.output)
            assert actual_endpoint == endpoint
        else:
            with (args.output / 'product.log').open('w') as log:
                tray = subprocess.Popen([str(args.build / 'apps/tray/mirage-tray'), '--config', str(config_path),
                    '--socket', endpoint, '--state-dir', env['XDG_STATE_HOME'], '--no-recovery',
                    '--shell', str(args.build / 'apps/native/mirage-native')], env=env,
                    stdout=log, stderr=log)
            children.append(tray)
        await_value(lambda: Path(endpoint).exists() or tray.poll() is not None, bool, 20)
        check('tray runtime starts', tray.poll() is None)
        wire = Wire(endpoint)
        hello = wire.call('hello')
        check('live model and pet bubble faces available', hello.get('chat') and hello.get('pet_bubbles'))
        product = await_value(lambda: wire.call('product.control', action='status', exit_epoch=0),
                              lambda v: v.get('pet_state') == 'running' and v.get('frontend_pid', 0) > 0, 20)
        children.append(AdoptedProcess(product['pet_pid']))
        d = display.Display()
        def find_pet():
            for w in d.screen().root.query_tree().children:
                pid = w.get_full_property(d.intern_atom('_NET_WM_PID'), X.AnyPropertyType)
                if pid is not None and int(pid.value[0]) == product['pet_pid']:
                    return w
        window = await_value(find_pet, bool)
        def capture(name):
            g = window.get_geometry()
            raw = window.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff)
            value = Image.frombytes('RGBA', (g.width, g.height), raw.data, 'raw', 'BGRA')
            value.save(args.output / (name + '.png'))
            return value
        # Let font/entrance resources become ready before the single paid turn.
        time.sleep(3)
        sid = wire.call('session.open')['session_id']
        wire.call('events.subscribe', pet_status=True, chat_preview=False)
        wire.events.clear()
        wire.call('session.chat', session_id=sid,
                  text='请核算17加25。只回复一句中文：计算结果是42。不要调用工具。',
                  agent=False, access='chat_only', reasoning='adaptive')
        wire.events.clear() # pet.status polls are full current snapshots; no raw thought backlog
        counts = dict(thinking=0, dialog=0, model_preview=0, answer_summary=0)
        final = None
        thought_image = None
        movement = False
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            status = wire.call('pet.status')
            bubble = status.get('bubble')
            if bubble:
                counts[bubble['kind']] += 1
                if bubble['content_origin'] in counts:
                    counts[bubble['content_origin']] += 1
                if bubble['kind'] == 'thinking' and bubble['content_origin'] == 'model_preview':
                    time.sleep(.06)
                    if thought_image is None:
                        thought_image = capture('thinking-live')
                    elif not movement:
                        image = capture('thinking-later')
                        # Only the clipped text viewport is compared, not animated actor pixels.
                        movement = bool(ImageChops.difference(thought_image.crop((24, 32, 240, 52)),
                                                             image.crop((24, 32, 240, 52))).getbbox())
                if bubble.get('final'):
                    final = bubble
                    time.sleep(.08)
                    capture('dialog-final')
                    break
            wire.events.clear()
            time.sleep(.08)
        report['observations']['snapshots'] = counts
        check('public thought reaches bubble before canonical settlement', counts['model_preview'] > 0)
        check('thought viewport changes while streaming', movement)
        check('canonical answer becomes a short dialog bubble', final is not None and
              final['kind'] == 'dialog' and final['content_origin'] == 'answer_summary' and
              '42' in final['text'])
        report['observations']['summary'] = final['text']
        report['observations']['final_remaining_ms'] = final['remaining_ms']
        time.sleep(5.2)
        check('canonical bubble expires', not wire.call('pet.status').get('bubble'))
        history = wire.call('session.chat.history', session_id=sid, limit=1)['turns'][-1]
        check('canonical history stays complete', history['status'] == 'ok' and '42' in history['reply_text'])
        check('raw previews do not persist as turn fields', not any('preview' in key for key in history))
        d.close()
        wire.call('service.shutdown')
        wire.sock.close(); wire = None
        tray.wait(timeout=12)
        check('product shutdown completes', tray.returncode == 0)
    finally:
        adopt_remaining(args.build, env, children)
        if wire: wire.sock.close()
        stop(children)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--provider', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--desktop-launch', action='store_true', help='Exercise generated desktop entry, matching the installed entry')
    p.add_argument('--session', action='store_true')
    args = p.parse_args()
    args.build, args.provider, args.output = [v.resolve() for v in (args.build, args.provider, args.output)]
    args.output.mkdir(parents=True, exist_ok=True)
    if args.session:
        assert os.environ.get('MIRAGE_PET_PRIVATE_SESSION') == '1'
        session(args)
        return
    with tempfile.TemporaryDirectory(prefix='mirage-pet-online-') as temporary:
        root, env = Path(temporary), os.environ.copy()
        for name, leaf in [('XDG_CONFIG_HOME','config'), ('XDG_STATE_HOME','state'),
                           ('XDG_RUNTIME_DIR','runtime'), ('XDG_DATA_HOME','data')]:
            path = root / leaf; path.mkdir(mode=0o700); env[name] = str(path)
        env.update(MIRAGE_PET_PRIVATE_SESSION='1', XDG_SESSION_TYPE='x11', LIBGL_ALWAYS_SOFTWARE='1')
        env.pop('WAYLAND_DISPLAY', None)
        read_fd, write_fd = os.pipe()
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen','0','1500x1000x24',
                                 '-nolisten','tcp'], env=env, pass_fds=[write_fd],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 5)[0]
            env['DISPLAY'] = ':' + os.read(read_fd, 32).decode().strip()
            result = subprocess.run(['dbus-run-session','--',sys.executable,str(Path(__file__).resolve()),
                '--build',str(args.build),'--provider',str(args.provider),'--output',str(args.output),
                '--session', *(['--desktop-launch'] if args.desktop_launch else [])], env=env, timeout=170)
            assert result.returncode == 0
        finally:
            os.close(read_fd); xvfb.terminate(); xvfb.wait(timeout=5)

if __name__ == '__main__': main()
