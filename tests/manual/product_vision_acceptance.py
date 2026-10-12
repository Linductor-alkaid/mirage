"""M7-12 external owner: private XDG/DBus/Xvfb, actual launcher/tray/native/OCR.

Optional --provider uses the previously authorized MiniMax configuration for one native turn.
The default run needs no credential. The opt-in provider reads only its configured key
into the private child environment; it never logs it. No weights are downloaded and
the user's desktop is untouched.
"""
import argparse
import ctypes
import json
import os
import select
import subprocess
import sys
import tempfile
import time
import tomllib
from urllib.parse import urlsplit
from pathlib import Path
from native_conversation_acceptance import Wire, stop
from native_desktop_launch import AdoptedProcess, adopt_remaining, start_watcher


def session(args):
    from Xlib import display, X, XK, Xatom
    from Xlib.ext import xtest
    from PIL import Image, ImageDraw, ImageFont
    env = os.environ.copy()
    children = []
    wire = None
    results = dict(scope='Linux private Xvfb; actual launcher/tray/native and PP-OCR; optional one authorized MiniMax native turn', checks=[])

    class ProductWire(Wire):
        def receive(self, timeout=5):
            try:
                value = super().receive(timeout)
            except UnicodeDecodeError:
                (args.output / 'invalid-ipc-frame.bin').write_bytes(self.buffer)
                raise
            if value.get('id') and value.get('ok') is False:
                results['last_ipc_error'] = value.get('error')
            return value

    def check(name, value):
        results['checks'].append(dict(name=name, passed=bool(value)))
        (args.output / 'report.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
        print(name + ': ' + ('PASS' if value else 'FAIL'), flush=True)
        assert value, name

    def wait_value(fn, predicate=bool, seconds=20):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            value = fn()
            if predicate(value):
                return value
            time.sleep(.05)
        raise TimeoutError('private product acceptance condition')

    def command(*args_):
        return subprocess.run([str(args.build / 'apps/mirage'), *args_], env=env, capture_output=True, text=True, timeout=25)

    try:
        assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0
        start_watcher(env, children, args.output)
        settings_path = Path(env['XDG_CONFIG_HOME']) / 'mirage/service.json'
        settings_path.parent.mkdir(mode=0o700)
        initial = dict(schema=1, model=dict(enabled=False, display_name='产品启用验收', supports_images=True))
        if args.provider:
            cfg = tomllib.loads((args.provider / 'config.toml').read_text())
            provider = cfg['model_providers'][cfg['model_provider']]
            origin = urlsplit(provider['base_url'])
            if (origin.scheme != 'https' or origin.hostname not in ('api.minimaxi.com', 'api.minimax.io')
                    or origin.username or origin.password or origin.port or origin.query or origin.fragment):
                raise RuntimeError('Restricted to previously authorized HTTPS MiniMax provider')
            credential = json.loads((args.provider / 'auth.json').read_text())['OPENAI_API_KEY']
            if not isinstance(credential, str) or not credential:
                raise RuntimeError('Configured credential unavailable')
            env['MIRAGE_PRODUCT_LIVE_KEY'] = credential
            initial['model'].update(enabled=True, endpoint='https://' + origin.hostname,
                                    api_prefix='/anthropic/v1', dialect='anthropic.messages.v1',
                                    model=cfg['model'], credential_env='MIRAGE_PRODUCT_LIVE_KEY')
            results['model'] = cfg['model']
            results['dialect'] = 'anthropic.messages.v1'
        settings_path.write_text(json.dumps(initial))
        settings_path.chmod(0o600)
        endpoint = str(Path(env['XDG_RUNTIME_DIR']) / 'mirage/mirage-service.sock')

        def launch(override=None):
            args_ = ['start'] + (['--ocr-manifest', str(override)] if override is not None else [])
            result = command(*args_)
            if result.returncode:
                (args.output / 'startup-error.txt').write_text(result.stdout + result.stderr)
                with (args.output / 'tray-startup-error.txt').open('w') as log:
                    debug = subprocess.Popen([str(args.build / 'apps/tray/mirage-tray'), '--no-shell'], env=env, stdout=log, stderr=log)
                    children.append(debug)
                    try:
                        debug.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        debug.terminate()
                        debug.wait(timeout=5)
            check('actual launcher start' + (' with OCR override' if override else ''), result.returncode == 0)
            client = ProductWire(endpoint)
            state = client.call('product.control', action='status', exit_epoch=0)
            parent_pid = int(Path(f"/proc/{state['frontend_pid']}/stat").read_text().split(') ', 1)[1].split()[1])
            children.extend([AdoptedProcess(state['frontend_pid']), AdoptedProcess(parent_pid)])
            return client, state

        check('relative startup path rejected before launch', command('start', '--ocr-manifest', 'relative.json').returncode == 2)
        check('missing startup path rejected before launch', command('start', '--ocr-manifest').returncode == 2)
        check('headless launcher rejects product OCR override', command('start', '--no-tray', '--no-shell', '--ocr-manifest', '').returncode == 2)
        wire, owner = launch()
        config = wire.call('vision.get')
        if args.bundled_default:
            check('bundled OCR runs without vision configuration', config['vision_supported'] and config['vision_running'] and not config['restart_required'])
            check('bundled manifest automatically selected', config['active_manifest'] == str(args.build / 'share/mirage/vision/ppocrv5-mobile-v1/manifest.json'))
        else:
            check('compiled OCR supported but initially disabled', config['vision_supported'] == (not args.unsupported_build) and not config['vision_running'] and not config['restart_required'])
        D = display.Display()
        root = D.screen().root

        def find_window(pid):
            for w_ in root.query_tree().children:
                prop = w_.get_full_property(D.intern_atom('_NET_WM_PID'), Xatom.CARDINAL)
                if prop and prop.value[0] == pid and w_.get_attributes().map_state == X.IsViewable:
                    return w_
            return None

        w = wait_value(lambda: find_window(owner['frontend_pid']))
        w.configure(x=10, y=10, width=1180, height=820)
        w.set_input_focus(X.RevertToParent, X.CurrentTime)
        D.sync()
        time.sleep(.3)

        def key(name, ctrl=False, shift=False):
            modifiers = ['Control_L'] if ctrl else []
            if shift:
                modifiers.append('Shift_L')
            for modifier in modifiers:
                xtest.fake_input(D, X.KeyPress, D.keysym_to_keycode(XK.string_to_keysym(modifier)))
            code = D.keysym_to_keycode(XK.string_to_keysym(name))
            xtest.fake_input(D, X.KeyPress, code)
            xtest.fake_input(D, X.KeyRelease, code)
            for modifier in reversed(modifiers):
                xtest.fake_input(D, X.KeyRelease, D.keysym_to_keycode(XK.string_to_keysym(modifier)))
            D.sync()
            time.sleep(.09)

        def type_ascii(text):
            names = {'/': 'slash', '.': 'period', '-': 'minus', ' ': 'space'}
            for c in text:
                key(names.get(c, c.lower()), shift=c.isupper())

        def click(x, y):
            origin = root.translate_coords(w, 0, 0)
            xtest.fake_input(D, X.MotionNotify, x=origin.x + x, y=origin.y + y)
            xtest.fake_input(D, X.ButtonPress, 1)
            xtest.fake_input(D, X.ButtonRelease, 1)
            D.sync()
            time.sleep(.2)

        def capture(name):
            g = w.get_geometry()
            raw = w.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff)
            Image.frombytes('RGB', (g.width, g.height), raw.data, 'raw', 'BGRX').save(args.output / (name + '.png'))

        def check_canvas(label):
            sample = Image.new('RGB', (500, 120), 'white')
            ImageDraw.Draw(sample).text((30, 4), '任务已保存', fill='black',
                font=ImageFont.truetype(str(args.build / 'apps/native/assets/NotoSansSC-Regular.otf'), 72))
            canvas = root.create_window(650, 45, 500, 120, 0, D.screen().root_depth,
                X.InputOutput, X.CopyFromParent, background_pixel=D.screen().white_pixel,
                override_redirect=True)
            canvas.set_wm_name('M7 Product Evidence')
            gc = canvas.create_gc()
            canvas.map()
            canvas.put_image(gc, 0, 0, 500, 120, X.ZPixmap, D.screen().root_depth, 0, sample.tobytes('raw', 'BGRX'))
            D.sync()
            try:
                observation = wire.call('desktop.observe', semantic=False, visual=True)
                (args.output / ('observation-' + ('default' if label.startswith('default') else 'custom') + '.json')).write_text(json.dumps(observation, ensure_ascii=False, indent=2))
                check(label, bool(observation.get('visual_snapshot_ref')) and any(r.get('source') == 'ocr' and '任务已保存' in r.get('text', '') for r in observation.get('visual_regions', [])))
            finally:
                gc.free()
                canvas.destroy()
                w.set_input_focus(X.RevertToParent, X.CurrentTime)
                D.sync()

        if args.bundled_default:
            check_canvas('default bundle recognizes public Canvas text')
        key('comma', ctrl=True)
        click(100, 282)
        time.sleep(.8)
        capture('settings-disabled-light')
        # Content column starts at x=320 at this size; form path row y=242.
        click(470, 243)
        key('a', ctrl=True)
        type_ascii(str(args.manifest))
        time.sleep(.3)
        click(490, 334)
        config = wait_value(lambda: wire.call('vision.get'), lambda c: c['vision_manifest'] == str(args.manifest))
        check('native save persists without hot pipeline changes', config['restart_required'] and config['vision_running'] == args.bundled_default)
        saved = json.loads(settings_path.read_text())
        check('native save preserves model configuration', all(saved['model'].get(k, False if isinstance(v, bool) else '') == v for k, v in initial['model'].items()))
        capture('settings-saved-light')
        wire.call('product.control', action='quit', exit_epoch=0)
        wire.sock.close()
        wire = None
        wait_value(lambda: Path(endpoint).exists(), lambda x: not x)
        for old_child in children[-2:]:
            old_child.wait(timeout=10)
        if args.unsupported_build:
            check('OCR configuration refuses unsupported build at restart', command('start', '--wait', '5').returncode == 1)
            wire, owner = launch('')
            config = wire.call('vision.get')
            check('unsupported build recovery shows disabled status', not config['vision_supported'] and not config['vision_running'] and config['restart_required'])
            wire.call('vision.set', ocr_manifest='')
            check('unsupported build can clear pending resource path', not wire.call('vision.get')['restart_required'])
            wire.call('product.control', action='quit', exit_epoch=0)
            return
        wire, owner = launch()
        config = wire.call('vision.get')
        check('normal product restart consumes saved manifest', config['vision_running'] and config['active_manifest'] == str(args.manifest) and not config['restart_required'])
        refused = command('start', '--ocr-manifest', str(args.manifest))
        check('startup override refuses existing tray', refused.returncode == 1 and 'requires restarting' in refused.stderr)
        check('ordinary startup still reuses tray', command('start').returncode == 0)
        w = wait_value(lambda: find_window(owner['frontend_pid']))
        w.configure(x=10, y=10, width=1180, height=820)
        w.set_input_focus(X.RevertToParent, X.CurrentTime)
        D.sync()
        time.sleep(.3)
        key('comma', ctrl=True)
        click(100, 282)
        capture('settings-running-light')
        check_canvas('actual product observation recognizes public Canvas text')

        click(100, 170)
        click(1040, 188)
        click(100, 282)
        time.sleep(.4)
        capture('settings-running-dark')
        w.configure(width=860, height=620)
        D.sync()
        time.sleep(.4)
        capture('settings-min-dark')
        click(100, 170)
        click(344, 215)
        click(100, 282)
        capture('settings-min-light')
        w.configure(width=1180, height=820)
        D.sync()
        time.sleep(.4)
        click(86, 94)  # Explicit settings navigation before composing.
        if args.provider:
            # One real model turn, from the actual native composer on public test pixels.
            capture('conversation-before-submit')
            click(490, 370)
            type_ascii('M7PRODUCT Call desktop.vision.ocr and desktop.vision.mark once each. Read only. Report two short Chinese texts visible. Do not click or type.')
            key('Return')
            capture('conversation-after-submit')
            def submitted_history():
                for item in wire.call('session.list')['sessions']:
                    sid = item.get('session_id', item.get('id'))
                    candidate = wire.call('session.chat.history', session_id=sid, limit=40)
                    if candidate.get('turns'):
                        return candidate
            history = wait_value(submitted_history, seconds=10)
            session_id = history['session_id']
            check('actual native composer submission admitted', bool(history['turns']))
            try:
                history = wait_value(lambda: wire.call('session.chat.history', session_id=session_id, limit=40), lambda h: bool(h.get('turns')) and h['turns'][-1]['status'] in ('ok', 'failed'), 120)
            except TimeoutError:
                history = wire.call('session.chat.history', session_id=session_id, limit=40)
                (args.output / 'native-history-before-cancel.json').write_text(json.dumps(history, ensure_ascii=False, indent=2))
                wire.call('session.chat.cancel', session_id=session_id)
                history = wait_value(lambda: wire.call('session.chat.history', session_id=session_id, limit=40), lambda h: bool(h.get('turns')) and h['turns'][-1]['status'] in ('ok', 'failed'), 20)
                (args.output / 'native-history-cancelled.json').write_text(json.dumps(history, ensure_ascii=False, indent=2))
                raise TimeoutError('real model native turn exceeded observation budget; cancelled')
            (args.output / 'native-history.json').write_text(json.dumps(history, ensure_ascii=False, indent=2))
            capture('conversation-result')
            turn = history['turns'][-1]
            check('real native model turn completes', turn['status'] == 'ok')
            tools = [part for part in turn.get('parts', []) if part.get('kind') == 'tool']
            check('real native conversation invokes OCR and mark', all(any(p.get('name') == name and p.get('status') == 'complete' for p in tools) for name in ('desktop.vision.ocr', 'desktop.vision.mark')))
            check('read-only task issues no desktop mutations', not any(p.get('name') in ('desktop.vision.click', 'desktop.vision.scroll', 'desktop.vision.drag') or p.get('name', '').startswith('desktop.input.') for p in tools))
            results['native_turn'] = dict(status=turn['status'], tools=[dict(name=p['name'], status=p['status']) for p in tools])
        wire.call('vision.set', ocr_manifest='')
        check('clearing manifest waits for restart', wire.call('vision.get')['restart_required'])
        wire.call('product.control', action='quit', exit_epoch=0)
        wire.sock.close()
        wire = None
        wait_value(lambda: Path(endpoint).exists(), lambda x: not x)
        for old_child in children[-2:]:
            old_child.wait(timeout=10)
        wire, owner = launch(args.manifest)
        config = wire.call('vision.get')
        check('explicit launcher override forwarded despite saved disable', config['vision_running'] and config['active_manifest'] == str(args.manifest) and config['vision_manifest'] == '' and config['restart_required'])
        wire.call('product.control', action='quit', exit_epoch=0)
        wire.sock.close()
        wire = None
        wait_value(lambda: Path(endpoint).exists(), lambda x: not x)
        for old_child in children[-2:]:
            old_child.wait(timeout=10)
        bad = json.loads(settings_path.read_text())
        bad['vision'] = {'ocr_manifest': '/tmp/mirage-nonexistent-resource.json'}
        settings_path.write_text(json.dumps(bad))
        check('invalid persisted resource refuses startup', command('start', '--wait', '5').returncode == 1)
        wire, owner = launch('')
        config = wire.call('vision.get')
        check('empty startup override recovers settings access', not config['vision_running'] and config['active_manifest'] == '' and config['restart_required'])
        wire.call('vision.set', ocr_manifest='')
        check('recovery can persist disabled OCR', not wire.call('vision.get')['restart_required'])
        wire.call('product.control', action='quit', exit_epoch=0)
        if args.tamper_bundle:
            # Only a private install copy may be damaged; never the developer/user package.
            assert args.bundled_default and str(args.build).startswith('/tmp/mirage-bundled-installed')
            wire.sock.close()
            wire = None
            wait_value(lambda: Path(endpoint).exists(), lambda x: not x)
            default_settings = json.loads(settings_path.read_text())
            default_settings.pop('vision', None)
            settings_path.write_text(json.dumps(default_settings))
            asset = args.build / 'share/mirage/vision/ppocrv5-mobile-v1/rec_bin'
            original = asset.read_bytes()
            try:
                asset.write_bytes(bytes([original[0] ^ 1]) + original[1:])
                check('damaged bundled weight refuses normal startup', command('start', '--wait', '5').returncode == 1)
                wire, owner = launch('')
                check('explicit empty override recovers damaged bundle', not wire.call('vision.get')['vision_running'])
                wire.call('product.control', action='quit', exit_epoch=0)
                wire.sock.close()
                wire = None
                wait_value(lambda: Path(endpoint).exists(), lambda x: not x)
                asset.unlink()
                check('missing bundled weight refuses normal startup', command('start', '--wait', '5').returncode == 1)
            finally:
                asset.write_bytes(original)
        results['resource_manifest'] = str(args.manifest)
    finally:
        if wire:
            wire.sock.close()
        adopt_remaining(args.build, env, children)
        stop(children)
        (args.output / 'report.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--provider', type=Path, help='Previously authorized MiniMax provider directory; makes one native agent turn')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--screen-size', choices=['1500x1000x24', '3200x2000x24'], default='1500x1000x24')
    parser.add_argument('--xvfb', default='Xvfb')
    parser.add_argument('--session', action='store_true')
    parser.add_argument('--tamper-bundle', action='store_true', help='Private installed copy only: damage/missing assets and recovery')
    parser.add_argument('--bundled-default', action='store_true', help='Default packaged resources, no manual vision setup')
    parser.add_argument('--unsupported-build', action='store_true', help='Negative OCR-off product startup and recovery')
    args = parser.parse_args()
    args.build, args.manifest, args.output = args.build.resolve(), args.manifest.resolve(), args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.session:
        assert os.environ.get('MIRAGE_ACCEPTANCE_PRIVATE_SESSION') == '1'
        session(args)
        return
    with tempfile.TemporaryDirectory(prefix='mirage-product-vision-') as tmp:
        task_root = Path(tmp)
        env = os.environ.copy()
        for name, folder in [('XDG_RUNTIME_DIR', 'run'), ('XDG_CONFIG_HOME', 'config'), ('XDG_STATE_HOME', 'state'), ('XDG_DATA_HOME', 'data')]:
            path = task_root / folder
            path.mkdir(mode=0o700)
            env[name] = str(path)
        env.pop('MIRAGE_NATIVE_SOCKET', None)
        env['MIRAGE_ACCEPTANCE_PRIVATE_SESSION'] = '1'
        env['LC_ALL'] = 'C.UTF-8'
        read_fd, write_fd = os.pipe()
        xvfb = subprocess.Popen([args.xvfb, '-displayfd', str(write_fd), '-screen', '0', args.screen_size, '-nolisten', 'tcp'], pass_fds=(write_fd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 5)[0]
            env['DISPLAY'] = ':' + os.read(read_fd, 32).decode().strip()
            env['XDG_SESSION_TYPE'] = 'x11'
            subprocess.run(['dbus-run-session', '--', sys.executable, __file__, '--session', '--build', str(args.build), '--manifest', str(args.manifest), '--output', str(args.output)] + (['--provider', str(args.provider.resolve())] if args.provider else []) + (['--unsupported-build'] if args.unsupported_build else []) + (['--bundled-default'] if args.bundled_default else []) + (['--tamper-bundle'] if args.tamper_bundle else []) + ['--screen-size', args.screen_size], env=env, check=True, timeout=300)
        finally:
            os.close(read_fd)
            xvfb.terminate()
            xvfb.wait(timeout=5)


if __name__ == '__main__':
    main()
