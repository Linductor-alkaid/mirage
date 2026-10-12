"""DEC-078: private X11 product settings inspection; never opens a host Portal.

Reuses the bounded private Xvfb/DBus/XDG driver. Screenshots contain only the
fixture product configuration. Runtime authorization behavior is tested separately.
"""
import json
import os
from pathlib import Path
import subprocess
import time

import native_frame_presentation_acceptance as driver
from native_conversation_acceptance import Wire, stop
from native_desktop_launch import AdoptedProcess, start_watcher


def session(args):
    from Xlib import display, X
    from Xlib.ext import xtest
    from PIL import Image

    args.output.mkdir(parents=True, exist_ok=True)
    d = display.Display()
    root = d.screen().root
    children, wire = [], None
    endpoint = str(Path(os.environ['XDG_RUNTIME_DIR']) / 'mirage.sock')
    config = Path(os.environ['XDG_CONFIG_HOME']) / 'service.json'
    config.write_text(json.dumps({'schema': 1, 'socket': endpoint}))
    results = {'scope': 'private X11 settings; no host authorization or input', 'screens': []}
    try:
        start_watcher(os.environ.copy(), children, args.output)
        with (args.output / 'service.log').open('w') as log:
            children.append(subprocess.Popen([
                str(args.build / 'apps/tray/mirage-tray'), '--socket', endpoint,
                '--config', str(config), '--state-dir', os.environ['XDG_STATE_HOME'],
                '--no-recovery', '--shell', str(args.build / 'apps/native/mirage-native')],
                stdout=log, stderr=log))
        deadline = time.monotonic() + 10
        w = None
        while time.monotonic() < deadline:
            w = next((v for v in root.query_tree().children
                      if v.get_wm_name() == 'Mirage · Agent'), None)
            if w and w.get_attributes().map_state == X.IsViewable:
                break
            time.sleep(.05)
        assert w, 'product window missing'
        wire = Wire(endpoint)
        status = wire.call('product.control', action='status', exit_epoch=0)
        assert status['frontend_ready']
        children.append(AdoptedProcess(status['frontend_pid']))
        authorization = wire.call('desktop.authorization.get')
        assert not authorization['supported'] and authorization['desktop_authorization'] == 'unsupported'

        def click(x, y, button=1):
            origin = root.translate_coords(w, 0, 0)
            xtest.fake_input(d, X.MotionNotify, x=origin.x + x, y=origin.y + y)
            xtest.fake_input(d, X.ButtonPress, button)
            xtest.fake_input(d, X.ButtonRelease, button)
            d.sync()
            time.sleep(.25)

        def capture(name):
            g = w.get_geometry()
            raw = w.get_image(0, 0, g.width, g.height, X.ZPixmap, 0xffffffff).data
            img = Image.frombytes('RGB', (g.width, g.height), raw, 'raw', 'BGRX')
            img.save(args.output / (name + '.png'))
            if name == 'desktop-permissions-dark':
                assert max(img.getpixel((400, 40))) < 80, 'dark theme not applied'
            results['screens'].append({'file': name + '.png', 'width': g.width, 'height': g.height})

        w.configure(width=1120, height=980)
        d.sync()
        time.sleep(.5)
        click(230, 938)  # Existing sidebar settings gear.
        click(100, 280)  # Desktop tools navigation.
        click(605, 612)  # Refresh authorization status.
        capture('desktop-permissions-light')
        click(100, 170)  # Appearance.
        click(1000, 186)  # Dark theme, wide layout.
        click(100, 280)
        capture('desktop-permissions-dark')
        w.configure(width=800, height=650)
        d.sync()
        time.sleep(.3)
        click(600, 525, 5)
        click(600, 525, 5)
        click(600, 525, 5)
        capture('desktop-permissions-small')
        assert wire.call('hello')['ok']
        wire.call('product.control', action='quit', exit_epoch=0)
        children[1].wait(timeout=8)
        results['graceful_shutdown'] = True
        print(json.dumps(results), flush=True)
    finally:
        (args.output / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        if wire:
            wire.sock.close()
        stop(children)
        d.close()


if __name__ == '__main__':
    driver.session = session
    driver.__file__ = __file__
    driver.main()
