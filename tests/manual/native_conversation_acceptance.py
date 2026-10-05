"""Opt-in Linux acceptance test for the real Mirage Release window.

Requires python-xlib, Pillow, Xvfb, DBus, IBus/libpinyin and system PyGObject.
--provider selects an already authorized local Mira model_provider directory;
it makes one paid model request. Credentials are read only into the service
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
    from Xlib import display, X, XK, protocol
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
        config['model'] = model
        config_path.write_text(json.dumps(config))
        config_path.chmod(384)
        env = os.environ.copy()
        env['MIRAGE_ACCEPTANCE_KEY'] = json.loads((args.provider / 'auth.json').read_text())['OPENAI_API_KEY']
        endpoint = str(Path(os.environ['XDG_RUNTIME_DIR']) / 'mirage.sock')
        service_log = open(output / 'service.log', 'w')
        children.append(subprocess.Popen([str(args.build / 'apps/mirage-service'), '--socket', endpoint, '--config', str(config_path), '--state-dir', os.environ['XDG_STATE_HOME'], '--no-recovery'], env=env, stdout=service_log, stderr=service_log))
        deadline = time.monotonic() + 10
        while not Path(endpoint).exists() and time.monotonic() < deadline:
            time.sleep(0.1)
        wire = Wire(endpoint)
        wire.call('events.subscribe', chat_preview=True)
        initial_sessions = len(wire.call('session.list')['sessions'])
        env.pop('MIRAGE_ACCEPTANCE_KEY', None)
        env['MIRAGE_NATIVE_SOCKET'] = endpoint
        app_log = open(output / 'native.log', 'w')
        app = subprocess.Popen([str(args.build / 'apps/native/mirage-native')], env=env, stdout=app_log, stderr=app_log)
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
        assert app.returncode == 0, 'native window exit failed'
        results = {'scope': 'real Release window, private Xvfb/DBus/IBus, real provider', 'model': cfg['model'], 'candidate_positions': [first, second], 'ime_draft_did_not_create_session': True, 'waiting_frames_changed': True, 'preview_count': len(previews), 'preview_max_bytes': max((p['bytes'] for p in previews)), 'terminal_status': terminal['status'], 'final_bytes': len(terminal['reply_text'].encode()), 'history_verified': True, 'idle_native_window_closed': True}
        (output / 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps(results, ensure_ascii=False), flush=True)
    finally:
        if wire:
            wire.sock.close()
        stop(children)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--provider', type=Path, required=True)
    p.add_argument('--no-captures', action='store_true')
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--xvfb', default='Xvfb')
    p.add_argument('--session', action='store_true', help=argparse.SUPPRESS)
    args = p.parse_args()
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
                 *(['--no-captures'] if args.no_captures else [])],
                env=env, start_new_session=True)
            if driver.wait(timeout=110) != 0:
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
