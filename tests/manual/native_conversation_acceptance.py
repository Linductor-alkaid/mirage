"""Opt-in Linux acceptance test for the real Mirage Release window.

Requires python-xlib, Pillow, Xvfb, DBus, IBus/libpinyin and system PyGObject.
--provider selects an already authorized local Mira model_provider directory;
it makes one paid model request unless --settings-only is selected.
The settings-only mode checks paste/save and typed replacement without inference.
Credentials are read only into the service
child environment, never printed or stored in the temporary settings file.
All windows, clipboard input, service state and IM settings are private.
This is a bounded external test driver, not a product task/executor path.
"""
import argparse
import json
import os
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import tomllib
from pathlib import Path
from urllib.parse import urlsplit

def stop(children):
    for child in reversed(children):
        if child.poll() is None:
            child.terminate()
            try:
                child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait(timeout=3)

class Wire:

    def __init__(self, endpoint):
        self.sock = socket.socket(socket.AF_UNIX)
        self.sock.settimeout(5)
        self.sock.connect(endpoint)
        self.buffer = b''
        self.seq = 0
        self.events = []

    def receive(self, timeout=5):
        deadline = time.monotonic() + timeout
        while len(self.buffer) < 4 or len(self.buffer) < 4 + struct.unpack('<I', self.buffer[:4])[0]:
            left = deadline - time.monotonic()
            if left <= 0:
                raise TimeoutError('IPC deadline')
            self.sock.settimeout(left)
            data = self.sock.recv(65536)
            if not data:
                raise RuntimeError('IPC closed')
            self.buffer += data
            if len(self.buffer) >= 4 and struct.unpack('<I', self.buffer[:4])[0] > 1048576:
                raise RuntimeError('oversized frame')
        size = struct.unpack('<I', self.buffer[:4])[0]
        obj = json.loads(self.buffer[4:4 + size])
        self.buffer = self.buffer[4 + size:]
        return obj

    def call(self, op, **fields):
        self.seq += 1
        data = json.dumps(dict(v=1, id=self.seq, op=op, **fields)).encode()
        self.sock.sendall(struct.pack('<I', len(data)) + data)
        deadline = time.monotonic() + 5
        while True:
            obj = self.receive(max(0.001, deadline - time.monotonic()))
            if obj.get('id') == self.seq:
                if not obj.get('ok'):
                    raise RuntimeError('IPC request rejected: ' + op)
                return obj
            if len(self.events) >= 128:
                raise RuntimeError('acceptance observer backlog exceeded')
            self.events.append(obj)

def session(args):
    from Xlib import display, X, XK, Xatom, protocol
    from Xlib.ext import xtest
    from PIL import Image, ImageChops
    children = []
    wire = None
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    os.environ['IBUS_ADDRESS'] = 'unix:path=' + os.environ['XDG_RUNTIME_DIR'] + '/ibus-bus'
    try:
        children.append(subprocess.Popen(['ibus-daemon', '--address=' + os.environ['IBUS_ADDRESS'], '--xim', '--panel=/usr/libexec/ibus-ui-gtk3', '--emoji-extension=disable'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        time.sleep(3)
        children.append(subprocess.Popen(['/usr/libexec/ibus-engine-libpinyin', '--ibus'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        D = display.Display()
        root = D.screen().root
        root.change_property(D.intern_atom('RESOURCE_MANAGER'), D.intern_atom('STRING'), 8, b'Xft.dpi: 96\n')
        D.sync()
        config_path = Path(os.environ['XDG_CONFIG_HOME']) / 'service.json'
        config = {'schema': 1, 'socket': str(Path(os.environ['XDG_RUNTIME_DIR']) / 'mirage.sock')}
        cfg = tomllib.loads((args.provider / 'config.toml').read_text())
        provider = cfg['model_providers'][cfg['model_provider']]
        origin = urlsplit(provider['base_url'])
        model = {'enabled': True, 'display_name': '验证用模型', 'endpoint': origin.scheme + '://' + origin.netloc, 'api_prefix': origin.path.rstrip('/') or '/v1', 'model': cfg['model'], 'dialect': 'openai.responses.v1' if provider['wire_api'] == 'responses' else 'openai.chat-completions.v1', 'credential_env': 'MIRAGE_ACCEPTANCE_KEY', 'context_window_tokens': 128000}
        if not args.model_settings:
            config['model'] = model
        config_path.write_text(json.dumps(config))
        config_path.chmod(384)
        env = os.environ.copy()
        test_key = json.loads((args.provider / 'auth.json').read_text())['OPENAI_API_KEY']
        if args.model_settings:
            keyring = subprocess.Popen(['gnome-keyring-daemon', '--foreground', '--components=secrets', '--unlock'], stdin=subprocess.PIPE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            keyring.stdin.write(b'\n')
            if keyring.stdin and not keyring.stdin.closed:
                keyring.stdin.close()
            children.append(keyring)
            time.sleep(0.3)
            # Use the unlocked, ephemeral collection of this private daemon.
            # No alias or credential on the user's session bus is touched.
            subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.freedesktop.secrets', '--object-path', '/org/freedesktop/secrets', '--method', 'org.freedesktop.Secret.Service.SetAlias', 'default', '/org/freedesktop/secrets/collection/session'], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=5)
        else:
            env['MIRAGE_ACCEPTANCE_KEY'] = test_key
        endpoint = str(Path(os.environ['XDG_RUNTIME_DIR']) / 'mirage.sock')
        service_log = open(output / 'service.log', 'w')
        service_command = [str(args.build / 'apps/tray/mirage-tray'), '--socket', endpoint, '--config', str(config_path), '--state-dir', os.environ['XDG_STATE_HOME'], '--no-recovery', '--shell', str(args.build / 'apps/native/mirage-native')]
        if args.desktop_launch:
            from native_desktop_launch import launch
            env.pop('MIRAGE_NATIVE_SOCKET', None)
            env.pop('MIRAGE_TRAY_SOCKET', None)
            service, app, endpoint = launch(args.build, env, children, output)
        else:
            from native_desktop_launch import start_watcher
            start_watcher(env, children, output)
            service = subprocess.Popen(service_command, env=env, stdout=service_log, stderr=service_log)
            children.append(service)
        deadline = time.monotonic() + 10
        while not Path(endpoint).exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        wire = Wire(endpoint)
        wire.call('events.subscribe', chat_preview=True)
        initial_sessions = len(wire.call('session.list')['sessions'])
        env.pop('MIRAGE_ACCEPTANCE_KEY', None)
        ui_endpoint = endpoint + '.offline' if args.offline_start else endpoint
        env['MIRAGE_NATIVE_SOCKET'] = ui_endpoint
        app_log = open(output / 'native.log', 'w')
        if not args.desktop_launch:
            from native_desktop_launch import AdoptedProcess
            deadline = time.monotonic() + 10
            product = {}
            while time.monotonic() < deadline:
                product = wire.call('product.control', action='status', exit_epoch=0)
                if product.get('frontend_ready'): break
                time.sleep(.05)
            assert product.get('frontend_ready'), 'tray-owned frontend did not become ready'
            app = AdoptedProcess(product['frontend_pid'])
            children.append(app)
        deadline = time.monotonic() + 10
        w = None
        while time.monotonic() < deadline and app.poll() is None:
            matches = [w for w in root.query_tree().children
                       if w.get_wm_name() == 'Mirage · Agent'
                       and w.get_attributes().map_state == X.IsViewable]
            if matches:
                w = matches[0]
                break
            time.sleep(0.1)
        assert w is not None, 'native window startup'
        w.configure(x=10, y=10)
        w.set_input_focus(X.RevertToParent, X.CurrentTime)
        D.sync()
        time.sleep(1)

        def key(name, shift=False, ctrl=False):
            modifiers = [('Shift_L', shift), ('Control_L', ctrl)]
            for name_mod, on in modifiers:
                if on:
                    xtest.fake_input(D, X.KeyPress, D.keysym_to_keycode(XK.string_to_keysym(name_mod)))
            code = D.keysym_to_keycode(XK.string_to_keysym(name))
            xtest.fake_input(D, X.KeyPress, code)
            xtest.fake_input(D, X.KeyRelease, code)
            for name_mod, on in reversed(modifiers):
                if on:
                    xtest.fake_input(D, X.KeyRelease, D.keysym_to_keycode(XK.string_to_keysym(name_mod)))
            D.sync()
            time.sleep(0.09)

        def click(x, y):
            g = w.get_geometry()
            xtest.fake_input(D, X.MotionNotify, x=g.x + round(x), y=g.y + round(y))
            D.sync()
            time.sleep(0.05)
            xtest.fake_input(D, X.ButtonPress, 1)
            D.sync()
            time.sleep(0.06)
            xtest.fake_input(D, X.ButtonRelease, 1)
            D.sync()
            time.sleep(0.2)

        def capture(name, desktop=False):
            drawable = root if desktop else w
            g = drawable.get_geometry()
            raw = drawable.get_image(0, 0, g.width, g.height, X.ZPixmap, 4294967295)
            img = Image.frombytes('RGB', (g.width, g.height), raw.data, 'raw', 'BGRX')
            if not args.no_captures:
                img.save(output / (name + '.png'))
            return img

        def engine(name):
            subprocess.run(['/usr/bin/python3', '-c', "import gi;gi.require_version('IBus','1.0');from gi.repository import IBus;IBus.init();b=IBus.Bus();assert b.is_connected();assert b.set_global_engine(" + repr(name) + ')'], check=True, timeout=8)

        def paste(text):
            # Clipboard owner and all requests are on the private X server.
            owner = root.create_window(-10, -10, 1, 1, 0, D.screen().root_depth)
            clipboard = D.intern_atom('CLIPBOARD')
            utf8 = D.intern_atom('UTF8_STRING')
            targets = D.intern_atom('TARGETS')
            owner.set_selection_owner(clipboard, X.CurrentTime)
            D.sync()
            key('v', ctrl=True)
            deadline = time.monotonic() + 0.7
            while time.monotonic() < deadline:
                if not D.pending_events():
                    select.select([D], [], [], 0.02)
                    continue
                event = D.next_event()
                if event.type == X.SelectionRequest:
                    target = event.property or event.target
                    if event.target == targets:
                        event.requestor.change_property(target, Xatom.ATOM, 32, [utf8, targets])
                    else:
                        event.requestor.change_property(target, utf8, 8, text.encode())
                    event.requestor.send_event(protocol.event.SelectionNotify(time=event.time, requestor=event.requestor, selection=event.selection, target=event.target, property=target))
                    D.flush()
            owner.destroy()
            D.sync()

        def service_lifecycle(action):
            nonlocal service, wire, endpoint
            if action == 'stop':
                service.terminate()
                service.wait(timeout=8)
                wire.sock.close()
                time.sleep(.3)
            else:
                if action == 'restart_for_ui':
                    endpoint = ui_endpoint
                    service_command[2] = endpoint
                service = subprocess.Popen(service_command, env=env, stdout=service_log, stderr=service_log)
                children.append(service)
                deadline = time.monotonic() + 8
                while not Path(endpoint).exists() and time.monotonic() < deadline:
                    time.sleep(.1)
                wire = Wire(endpoint)
                return wire

        def clipboard_text():
            # Observe only the copied public Base URL, never the key control.
            key('c', ctrl=True)
            requestor = root.create_window(-10, -10, 1, 1, 0, D.screen().root_depth)
            prop = D.intern_atom('MIRAGE_PRESET_URL')
            requestor.convert_selection(D.intern_atom('CLIPBOARD'), D.intern_atom('UTF8_STRING'), prop, X.CurrentTime)
            D.flush()
            deadline = time.monotonic() + 2
            try:
                while time.monotonic() < deadline:
                    if not D.pending_events():
                        select.select([D], [], [], .02)
                        continue
                    event = D.next_event()
                    if event.type == X.SelectionNotify and event.requestor == requestor:
                        result = requestor.get_full_property(prop, X.AnyPropertyType)
                        return bytes(result.value).decode() if result else ''
                raise TimeoutError('public URL clipboard observation')
            finally:
                requestor.destroy()
                D.sync()

        if args.model_settings:
            engine('xkb:us::eng')
            click(226, 756)
            click(90, 226)
            capture('model-empty-live')
            if args.preset_selection_only:
                from native_preset_acceptance import run
                run(click, paste, key, capture, clipboard_text, wire, service_lifecycle,
                    output, args.offline_start, args.expect_fixed)
                click(1150, 30)
                app.wait(timeout=8)
                assert app.returncode in (0, -999)
                return
            if args.preset_minimax:
                click(420, 360)  # MiniMax built-in template in the service navigation.
                capture('model-preset-live')
                expected_name = 'MiniMax'
            else:
                expected_name = 'Acceptance service'
                click(750, 220)
                paste(expected_name)
                click(720, 284)
                paste(provider['base_url'].rstrip('/'))
                if provider['wire_api'] == 'responses':
                    click(780, 348)
                    click(780, 438)
            click(750, 412)
            paste(test_key)
            if not args.preset_minimax:
                click(1030, 456)
                click(750, 564)
                paste(cfg['model'])
                click(790, 604)
                click(1035, 220)  # Explicitly enable the initially disabled custom service.
            click(1060, 744)
            deadline = time.monotonic() + 8
            saved = None
            while time.monotonic() < deadline:
                result = wire.call('model.get')
                saved = json.loads(result['model_settings'])
                if saved.get('model', {}).get('provider_name') == expected_name:
                    break
                time.sleep(0.1)
            capture('model-after-save-live')
            assert saved and saved['model'].get('provider_name') == expected_name, 'model UI did not save'
            assert saved['model']['enabled'] and saved['model']['model'] == cfg['model']
            assert saved['model']['api_key_configured'] and len(saved['model']['credential_ref']) == 32
            assert len(saved['models']) == 1 and 'credential_env' not in saved['model']
            if args.preset_minimax:
                assert saved['model']['dialect'] == 'anthropic.messages.v1'
                assert saved['model']['api_prefix'] == '/anthropic/v1'
                assert saved['model']['provider_id'] == 'preset:minimax'
            else:
                assert saved['model']['dialect'] == ('openai.responses.v1' if provider['wire_api'] == 'responses' else 'openai.chat-completions.v1')
            assert test_key not in json.dumps(saved)
            stored = list(Path(os.environ['XDG_CONFIG_HOME']).rglob('service.json'))
            assert stored and any(saved['model']['credential_ref'] in path.read_text() for path in stored), 'saved reference missing from disk'
            assert all(test_key not in path.read_text() for path in stored), 'plaintext key persisted'
            capture('model-saved-live')
            # Refresh must restore the acknowledged name and configured-key state.
            click(977, 152)
            capture('model-refreshed-live')
            if args.settings_audit:
                from model_settings_audit import run
                run(click, paste, key, capture, wire, output, saved, args.expect_fixed, service_lifecycle)
                click(1150, 30)
                app.wait(timeout=8)
                assert app.returncode in (0, -999)
                return
            if args.settings_only:
                # Replace only the key of an acknowledged, otherwise clean
                # service. A new reference proves the UI submitted this edit.
                old_reference = saved['model']['credential_ref']
                click(750, 412)
                for char in 'replacementkey':
                    key(char)
                capture('key-typed-save-enabled')
                click(1060, 744)
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline:
                    updated = json.loads(wire.call('model.get')['model_settings'])
                    if updated['model']['credential_ref'] != old_reference:
                        break
                    time.sleep(0.1)
                assert updated['model']['credential_ref'] != old_reference, 'typed key did not enable save'
                assert updated['model']['api_key_configured'], 'typed key not configured'
                capture('key-typed-saved')
                results = dict(scope='real Release window, private keyring, synthetic key, no model request',
                               pasted_key_saved=True, typed_replacement_saved=True,
                               secret_not_in_settings=True, network_requests=0)
                (output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
                print(json.dumps(results), flush=True)
                if args.desktop_launch:
                    from native_desktop_launch import verify_reuse
                    verify_reuse(args.build, env, service, endpoint, output)
                click(1150, 30)
                app.wait(timeout=8)
                assert app.returncode in (0, -999)
                return
            click(86, 94)

        def candidate():
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                found = [v for v in root.query_tree().children if v.get_wm_name() == 'ibus-ui-gtk3' and v.get_attributes().map_state == X.IsViewable]
                if found:
                    g = found[0].get_geometry()
                    origin = w.get_geometry()
                    return [g.x - origin.x, g.y - origin.y]
                time.sleep(0.05)
            raise RuntimeError('candidate window absent')
        click(470, 361)
        engine('libpinyin')
        for char in 'nihao':
            key(char)
        first = candidate()
        assert 380 <= first[0] <= 550 and 330 <= first[1] <= 415, first
        capture('ime-first', True)
        key('space')
        key('Return', shift=True)
        for char in 'shijie':
            key(char)
        second = candidate()
        assert second[1] > first[1] + 10 and second[1] < 450, (first, second)
        capture('ime-multiline', True)
        key('space')
        assert len(wire.call('session.list')['sessions']) == initial_sessions, 'IME created session before sending'
        engine('xkb:us::eng')
        capture('committed-draft')
        key('a', ctrl=True)
        key('BackSpace')
        prompt = '请用中文分五条说明 agent harness 的职责，每条两句话，不调用工具。'
        owner = root.create_window(-10, -10, 1, 1, 0, D.screen().root_depth)
        clipboard = D.intern_atom('CLIPBOARD')
        utf8 = D.intern_atom('UTF8_STRING')
        targets = D.intern_atom('TARGETS')
        owner.set_selection_owner(clipboard, X.CurrentTime)
        D.sync()
        key('v', ctrl=True)
        deadline = time.monotonic() + 1
        while time.monotonic() < deadline:
            if not D.pending_events():
                select.select([D], [], [], 0.02)
                continue
            event = D.next_event()
            if event.type == X.SelectionRequest:
                prop = event.property or event.target
                if event.target == targets:
                    event.requestor.change_property(prop, D.intern_atom('ATOM'), 32, [targets, utf8])
                else:
                    event.requestor.change_property(prop, event.target, 8, prompt.encode())
                event.requestor.send_event(protocol.event.SelectionNotify(time=event.time, requestor=event.requestor, selection=event.selection, target=event.target, property=prop))
                D.flush()
        owner.destroy()
        time.sleep(0.2)
        key('Return')
        waiting = capture('waiting-first')
        time.sleep(0.7)
        waiting2 = capture('waiting-later')
        assert ImageChops.difference(waiting, waiting2).getbbox(), 'waiting frame did not update'
        previews = []
        terminal = None
        streaming_saved = False
        started = time.monotonic()
        sid = None
        while time.monotonic() - started < 65:
            try:
                obj = wire.events.pop(0) if wire.events else wire.receive(0.5)
            except (TimeoutError, socket.timeout):
                continue
            if obj.get('event') == 'session.chat_preview' and obj.get('text'):
                if len(previews) >= 4096:
                    raise RuntimeError('preview event budget exceeded')
                previews.append({'sequence': obj['sequence'], 'bytes': len(obj['text'].encode())})
                sid = obj['session_id']
                if not streaming_saved and len(obj['text'].encode()) > 60:
                    time.sleep(0.2)
                    capture('streaming-live')
                    streaming_saved = True
            if obj.get('event') == 'session.chat_updated' and obj.get('status') in ('ok', 'failed'):
                terminal = obj
                sid = obj['session_id']
                break
        assert terminal and terminal['status'] == 'ok', 'model did not complete successfully'
        assert previews and streaming_saved, 'no live stream before terminal'
        time.sleep(1)
        capture('completed-live')
        history = wire.call('session.chat.history', session_id=sid, limit=10)['turns']
        assert history[-1]['user_text'] == prompt, 'typed prompt mismatch'
        assert history[-1]['reply_text'] == terminal['reply_text'], 'canonical terminal differs'
        click(795, 744)
        capture('context-live')
        key('Escape')
        w.configure(width=860, height=620)
        D.sync()
        time.sleep(0.6)
        capture('minimum-light')
        w.configure(width=1180, height=800)
        D.sync()
        time.sleep(0.4)
        click(226, 756)
        click(80, 174)
        click(1000, 282)
        click(80, 100)
        capture('completed-dark')
        w.configure(width=860, height=620)
        D.sync()
        time.sleep(0.6)
        capture('minimum-dark')
        click(832, 30)
        app.wait(timeout=8)
        assert app.returncode in (0, -999), 'native window exit failed'
        results = {'scope': 'real Release window, private Xvfb/DBus/IBus, real provider', 'model': cfg['model'], 'candidate_positions': [first, second], 'ime_draft_did_not_create_session': True, 'waiting_frames_changed': True, 'preview_count': len(previews), 'preview_max_bytes': max((p['bytes'] for p in previews)), 'terminal_status': terminal['status'], 'final_bytes': len(terminal['reply_text'].encode()), 'history_verified': True, 'idle_native_window_closed': True}
        results['modelSettingsFromEmpty'] = args.model_settings
        results['presetMinimaxKeyOnly'] = args.preset_minimax
        (output / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps(results, ensure_ascii=False), flush=True)
    finally:
        from native_desktop_launch import adopt_remaining
        adopt_remaining(args.build, os.environ, children)
        if wire:
            wire.sock.close()
        stop(children)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--desktop-launch', action='store_true', help='Launch generated desktop entry; verify service bootstrap and key save')
    p.add_argument('--provider', type=Path, required=True)
    p.add_argument('--preset-selection-only', action='store_true', help='Exercise preset click/dirty navigation without saving or inference')
    p.add_argument('--offline-start', action='store_true', help='With preset-selection-only, launch UI before its Service socket exists')
    p.add_argument('--settings-audit', action='store_true', help='Audit real model setting controls without inference')
    p.add_argument('--expect-fixed', action='store_true', help='Drive repaired confirmation flows')
    p.add_argument('--settings-only', action='store_true', help='With --model-settings, stop after paste/save and typed key replacement; no inference request')
    p.add_argument('--no-captures', action='store_true')
    p.add_argument('--preset-minimax', action='store_true', help='With --model-settings, configure the MiniMax preset by entering only its key')
    p.add_argument('--model-settings', action='store_true', help='Start empty; configure the model through the UI and a private system keyring')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--xvfb', default='Xvfb')
    p.add_argument('--session', action='store_true', help=argparse.SUPPRESS)
    args = p.parse_args()
    if args.offline_start or args.settings_audit:
        p.error('DEC-045 requires a live tray-owned frontend; the former offline/headless-replacement audit is historical. Use tray_runtime_acceptance.py for host-loss/startup gates.')
    if args.desktop_launch and not (args.model_settings and args.settings_only and args.preset_minimax):
        p.error('--desktop-launch requires --model-settings --settings-only --preset-minimax')
    if args.desktop_launch and (args.offline_start or args.settings_audit or args.preset_selection_only):
        p.error('--desktop-launch is a separate settings-only run')
    if (args.settings_only or args.settings_audit or args.preset_selection_only) and not args.model_settings:
        p.error('settings-only/audit/preset-selection-only require --model-settings')
    if args.settings_audit and not args.preset_minimax:
        p.error('--settings-audit requires --preset-minimax for its synthetic catalog layout')
    if args.expect_fixed and not (args.settings_audit or args.preset_selection_only):
        p.error('--expect-fixed requires a settings audit or preset selection run')
    if args.offline_start and not args.preset_selection_only:
        p.error('--offline-start requires --preset-selection-only')
    if args.preset_selection_only and (args.settings_only or args.settings_audit):
        p.error('preset-selection-only is a separate settings run')
    if args.settings_only and args.settings_audit:
        p.error('choose either --settings-only or --settings-audit')
    args.build = args.build.resolve()
    args.provider = args.provider.resolve()
    args.output = args.output.resolve()
    if args.session:
        if os.environ.get('MIRAGE_ACCEPTANCE_PRIVATE_SESSION') != '1':
            raise RuntimeError('internal session must be launched by this test driver')
        session(args)
        return
    with tempfile.TemporaryDirectory(prefix='mirage-live-') as folder:
        env = os.environ.copy()
        for name, suffix in [('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache'), ('XDG_STATE_HOME', 'state'), ('XDG_RUNTIME_DIR', 'run')]:
            env[name] = str(Path(folder) / suffix)
            Path(env[name]).mkdir(mode=0o700)
        read_fd, write_fd = os.pipe()
        server = subprocess.Popen([args.xvfb, '-displayfd', str(write_fd), '-screen', '0', '1280x1000x24', '-nolisten', 'tcp', '-noreset'], pass_fds=(write_fd,), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_fd)
        driver = None
        try:
            if not select.select([read_fd], [], [], 5)[0]:
                raise RuntimeError('Xvfb startup timeout')
            env.update(DISPLAY=':' + os.read(read_fd, 32).decode().strip(),
                       XMODIFIERS='@im=ibus', LC_ALL='C.UTF-8',
                       MIRAGE_ACCEPTANCE_PRIVATE_SESSION='1')
            driver = subprocess.Popen(
                ['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                 '--build', str(args.build), '--provider', str(args.provider),
                 '--output', str(args.output), '--session',
                 *(['--no-captures'] if args.no_captures else []),
                 *(['--model-settings'] if args.model_settings else []),
                 *(['--desktop-launch'] if args.desktop_launch else []),
                 *(['--preset-minimax'] if args.preset_minimax else []),
                 *(['--settings-only'] if args.settings_only else []),
                 *(['--settings-audit'] if args.settings_audit else []),
                 *(['--preset-selection-only'] if args.preset_selection_only else []),
                 *(['--offline-start'] if args.offline_start else []),
                 *(['--expect-fixed'] if args.expect_fixed else [])],
                env=env, start_new_session=True)
            if driver.wait(timeout=240 if args.settings_audit else 110) != 0:
                raise RuntimeError('private acceptance session failed')
        finally:
            if driver is not None and driver.poll() is None:
                os.killpg(driver.pid, signal.SIGTERM)
                try:
                    driver.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(driver.pid, signal.SIGKILL)
                    driver.wait(timeout=3)
            os.close(read_fd)
            stop([server])
if __name__ == '__main__':
    main()
