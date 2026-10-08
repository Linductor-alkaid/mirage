"""Opt-in window presentation diagnostic; private X11/DBus/XDG, no inference.

Private Xvfb uses programmatic configure to probe renderer invalidation. Physical
compositor resize is sampled with --host-display --manual-drag. Pixel counts are
diagnostics; process/geometry assertions do not prove absence of visual flicker.

Requires the existing manual-test dependencies python-xlib, Pillow, Xvfb and DBus.
This bounded external driver owns its test processes; it is not a product worker.
"""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time

from native_conversation_acceptance import Wire, stop
from native_desktop_launch import AdoptedProcess, start_watcher


def session(args):
    from Xlib import display, X
    from Xlib.ext import xtest
    from Xlib.error import XError
    from PIL import Image

    children, wire = [], None
    results = {'scope': 'Linux X11, private product window, no inference',
               'resize_input': 'host grip/physical drag' if args.host_display else 'programmatic configure',
               'startup': [], 'drags': []}
    args.output.mkdir(parents=True, exist_ok=True)
    d = display.Display()
    root = d.screen().root
    endpoint = str(Path(os.environ['XDG_RUNTIME_DIR']) / 'mirage.sock')
    config = Path(os.environ['XDG_CONFIG_HOME']) / 'service.json'
    config.write_text(json.dumps({'schema': 1, 'socket': endpoint}))

    def candidates():
        ids = root.get_full_property(d.intern_atom('_NET_CLIENT_LIST'), X.AnyPropertyType)
        return ([d.create_resource_object('window', int(wid)) for wid in ids.value]
                if ids is not None else root.query_tree().children)

    existing = {item.id for item in candidates()}

    def window():
        for item in candidates():
            if item.id in existing:
                continue
            try:
                if item.get_wm_name() == 'Mirage · Agent' and item.get_attributes().map_state == X.IsViewable:
                    return item
            except XError:
                pass
        return None

    def sample(w):
        g = w.get_geometry()
        points = [(g.width // 2, g.height // 2), (g.width // 2, 8),
                  (8, g.height // 2), (g.width // 2, g.height - 24)]
        pixels = [list(w.get_image(x, y, 1, 1, X.ZPixmap, 0xffffffff).data[:3])[::-1]
                  for x, y in points]
        return {'width': g.width, 'height': g.height, 'rgb': pixels}

    def capture(w, name):
        g = w.get_geometry()
        data = w.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff).data
        Image.frombytes('RGB', (g.width, g.height), data, 'raw', 'BGRX').save(args.output / (name + '.png'))

    try:
        start_watcher(os.environ.copy(), children, args.output)
        with (args.output / 'service.log').open('w') as log:
            children.append(subprocess.Popen([
                str(args.build / 'apps/tray/mirage-tray'), '--socket', endpoint,
                '--config', str(config), '--state-dir', os.environ['XDG_STATE_HOME'],
                '--no-recovery', '--shell', str(args.build / 'apps/native/mirage-native')],
                stdout=log, stderr=log))
            start = time.monotonic()
            first = None
            while time.monotonic() - start < 8:
                w = window()
                if w:
                    item = sample(w)
                    item['ms'] = round((time.monotonic() - start) * 1000, 2)
                    results['startup'].append(item)
                    if first is None:
                        first = item
                        capture(w, 'first-visible')
                    if time.monotonic() - start > 1.5:
                        break
                time.sleep(.002)
            assert first, 'product window did not appear'
            results['startup_black_centers'] = sum(max(item['rgb'][0]) <= 10 for item in results['startup'])
            wire = Wire(endpoint)
            status = wire.call('product.control', action='status', exit_epoch=0)
            assert status['frontend_ready'], 'frontend did not register'
            children.append(AdoptedProcess(status['frontend_pid']))
            w = window()
            if args.manual_drag:
                title = 'Mirage · 缩放诊断（独立测试窗口）'
                w.set_wm_name('Mirage Resize Diagnostic')
                w.change_property(d.intern_atom('_NET_WM_NAME'), d.intern_atom('UTF8_STRING'), 8,
                                  title.encode('utf-8'))
                d.sync()
                print('manual test window ready; sample duration=60s', flush=True)
                samples = []
                results['manual_samples'] = samples
                start = time.monotonic()
                while time.monotonic() - start < 60:
                    try:
                        samples.append(sample(w))
                    except XError:
                        results['capture_races'] = results.get('capture_races', 0) + 1
                    time.sleep(.003)
                results['manual_samples'] = samples
                results['manual_black_centers'] = sum(max(s['rgb'][0]) <= 10 for s in samples)
                results['manual_sizes'] = len({(s['width'], s['height']) for s in samples})
                print(json.dumps({'manual_black_centers': results['manual_black_centers'],
                                  'manual_sizes': results['manual_sizes']}), flush=True)
                wire.call('product.control', action='quit', exit_epoch=0)
                children[1].wait(timeout=8)
                return
            for name, dx, dy in [('grow', 90, 70), ('shrink', -140, -110)]:
                g = w.get_geometry()
                origin = root.translate_coords(w, 0, 0)
                x, y = origin.x + g.width - 7, origin.y + g.height - 7
                if args.host_display:
                    xtest.fake_input(d, X.MotionNotify, x=x, y=y)
                    d.sync()
                    time.sleep(.05)
                    xtest.fake_input(d, X.ButtonPress, 1)
                    d.sync()
                    time.sleep(.05)
                samples = []
                for step in range(1, 31):
                    if args.host_display:
                        xtest.fake_input(d, X.MotionNotify, x=x + round(dx * step / 30),
                                         y=y + round(dy * step / 30))
                    else:
                        w.configure(width=g.width + round(dx * step / 30),
                                    height=g.height + round(dy * step / 30))
                    d.sync()
                    for _ in range(5):
                        try:
                            samples.append(sample(w))
                        except XError:
                            results['capture_races'] = results.get('capture_races', 0) + 1
                        time.sleep(.002)
                if args.host_display:
                    xtest.fake_input(d, X.ButtonRelease, 1)
                d.sync()
                time.sleep(.15)
                final = sample(w)
                assert (final['width'], final['height']) != (g.width, g.height), 'window did not resize (native compositor grabs require physical input)'
                black_centers = sum(max(item['rgb'][0]) <= 10 for item in samples)
                assert all(max(pixel) > 10 for pixel in final['rgb']), 'unpainted region after drag'
                results['drags'].append({'direction': name, 'samples': samples, 'black_centers': black_centers, 'final': final})
                capture(w, name)
            # A later request still restores the same window; the initial-show
            # guard must not swallow normal product restoration.
            response = wire.call('product.control', action='open', exit_epoch=0)
            assert response.get('frontend_pid') == status['frontend_pid'], 'open replaced frontend'
            results['restore_same_frontend'] = True
            wire.call('product.control', action='quit', exit_epoch=0)
            children[1].wait(timeout=8)
            results['graceful_shutdown'] = True
            (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            print(json.dumps({'startup_samples': len(results['startup']), 'startup_black_centers': results['startup_black_centers'],
                              'drag_black_centers': sum(x['black_centers'] for x in results['drags']),
                              'drag_samples': sum(len(x['samples']) for x in results['drags']),
                              'graceful_shutdown': True}), flush=True)
    finally:
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        if wire:
            wire.sock.close()
        stop(children)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--session', action='store_true')
    parser.add_argument('--manual-drag', action='store_true', help='Sample physical drags in a distinct diagnostic window for 60 seconds')
    parser.add_argument('--host-display', action='store_true', help='Use the current compositor for a private test window')
    args = parser.parse_args()
    if args.manual_drag and not args.host_display:
        parser.error('--manual-drag requires --host-display')
    args.build = args.build.resolve()
    args.output = args.output.resolve()
    if args.session:
        assert os.environ.get('MIRAGE_FRAME_PRIVATE_SESSION') == '1'
        session(args)
        return
    with tempfile.TemporaryDirectory(prefix='mirage-frames-') as temporary:
        env = os.environ.copy()
        for key, folder in [('XDG_RUNTIME_DIR', 'run'), ('XDG_CONFIG_HOME', 'config'),
                            ('XDG_STATE_HOME', 'state'), ('XDG_DATA_HOME', 'data')]:
            path = Path(temporary) / folder
            path.mkdir(mode=0o700)
            env[key] = str(path)
        for key in ['WAYLAND_DISPLAY', 'MIRAGE_NATIVE_SOCKET', 'MIRAGE_TRAY_SOCKET']:
            env.pop(key, None)
        env.update(MIRAGE_FRAME_PRIVATE_SESSION='1', XDG_SESSION_TYPE='x11', LC_ALL='C.UTF-8')
        if args.host_display:
            subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                            '--build', str(args.build), '--output', str(args.output), '--session', '--host-display', *(['--manual-drag'] if args.manual_drag else [])],
                           env=env, check=True, timeout=75)
            return
        read_fd, write_fd = os.pipe()
        server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                   '1600x1100x24', '-nolisten', 'tcp'], env=env,
                                  pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 5)[0], 'Xvfb readiness timeout'
            env['DISPLAY'] = ':' + os.read(read_fd, 32).decode().strip()
            subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                            '--build', str(args.build), '--output', str(args.output), '--session', *(['--manual-drag'] if args.manual_drag else [])],
                           env=env, check=True, timeout=75)
        finally:
            os.close(read_fd)
            stop([server])


if __name__ == '__main__':
    main()
