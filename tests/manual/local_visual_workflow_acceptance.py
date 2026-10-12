"""M7-11 external owner: actual product workflow, bundled PP-OCR, X11 Canvas.

No model or credentials. Private XDG/DBus/Xvfb; no user desktop. Template patches
are generated from public fixture pixels before capture and supplied as assets.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
from native_conversation_acceptance import Wire, stop
from native_desktop_launch import start_watcher


def canvas_image(font, mode='normal', saved=False):
    from PIL import Image, ImageDraw, ImageFont
    image = Image.new('RGB', (600, 320), 'white')
    draw = ImageDraw.Draw(image)
    face = ImageFont.truetype(str(font), 32)
    draw.text((28, 10), 'Other page' if mode == 'page' else 'Document', font=face, fill='black')
    if saved:
        draw.text((28, 80), 'Saved', font=face, fill='black')
    else:
        draw.text((28, 80), 'Save', font=face, fill='black')
        if mode == 'duplicate':
            draw.text((310, 80), 'Save', font=face, fill='black')
    for x, cross in [(65, mode == 'swapped'), (325, mode != 'swapped')]:
        draw.rectangle((x, 180, x + 70, 250), fill='black')
        draw.line((x + 20, 200, x + 50, 230), fill='white', width=3)
        if cross:
            draw.line((x + 50, 200, x + 20, 230), fill='white', width=3)
    return image


def fixture(args):
    from Xlib import display, X
    D = display.Display()
    root = D.screen().root
    w = root.create_window(10, 20, 600, 320, 0, D.screen().root_depth,
        X.InputOutput, X.CopyFromParent, background_pixel=D.screen().white_pixel,
        event_mask=X.ExposureMask | X.ButtonPressMask | X.ButtonReleaseMask)
    w.set_wm_name('Knowledge Canvas')
    gc = w.create_gc()
    clicks = 0
    mode = 'normal'
    saved = False
    last_target = ''
    def state():
        args.output.write_text(json.dumps(dict(window=w.id, mode=mode, clicks=clicks,
            saved=saved, last_target=last_target)))
    def draw():
        image = canvas_image(args.font, mode, saved)
        # XPutImage request size stays below the server request budget.
        for y in range(0, 320, 40):
            band = image.crop((0, y, 600, min(320, y + 40)))
            w.put_image(gc, 0, y, band.width, band.height, X.ZPixmap,
                        D.screen().root_depth, 0, band.tobytes('raw', 'BGRX'))
        D.sync()
        state()
    w.map()
    w.set_input_focus(X.RevertToParent, X.CurrentTime)
    draw()
    while True:
        while D.pending_events():
            event = D.next_event()
            if event.type == X.Expose:
                draw()
            elif event.type == X.ButtonRelease:
                target = ''
                if 20 <= event.event_x <= 165 and 70 <= event.event_y <= 130:
                    target = 'Save'
                elif 60 <= event.event_x <= 140 and 175 <= event.event_y <= 255:
                    target = 'cross-icon' if mode == 'swapped' else 'line-icon'
                elif 320 <= event.event_x <= 400 and 175 <= event.event_y <= 255:
                    target = 'line-icon' if mode == 'swapped' else 'cross-icon'
                if target:
                    clicks += 1
                    last_target = target
                    saved = mode != 'no_post'
                    draw()
        ready, _, _ = select.select([D.fileno(), sys.stdin], [], [], 1)
        if sys.stdin in ready:
            line = sys.stdin.readline().strip()
            if not line:
                return
            mode, saved = line, False
            w.configure(x=70 if mode == 'moved' else 10, y=20)
            w.set_wm_name('Wrong title' if mode == 'title' else 'Knowledge Canvas')
            draw()


def allow_fixture_debug_attach():
    # Test-only private child, so its external owner can inspect a timeout.
    ctypes.CDLL(None).prctl(0x59616d61, ctypes.c_ulong(os.getppid()), 0, 0, 0)


def session(args):
    from Xlib import display, X
    children, wire, tray = [], None, None
    report = dict(scope='Linux private Xvfb; actual embedded product Runtime, PP-OCR/ncnn and public Canvas',
                  model_enabled=False, checks=[], runs=[])
    def check(name, passed):
        report['checks'].append(dict(name=name, passed=bool(passed)))
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(name + ': ' + ('PASS' if passed else 'FAIL'), flush=True)
        assert passed, name
    def wait(fn, predicate=bool, seconds=20):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            value = fn()
            if predicate(value):
                return value
            time.sleep(.05)
        raise TimeoutError('private visual workflow condition')
    try:
        env = os.environ.copy()
        start_watcher(env, children, args.output)
        settings = Path(env['XDG_CONFIG_HOME']) / 'mirage/service.json'
        settings.parent.mkdir(mode=0o700)
        settings.write_text(json.dumps(dict(schema=1, model=dict(enabled=False))))
        endpoint = str(Path(env['XDG_RUNTIME_DIR']) / 'mirage/mirage-service.sock')
        def launch(manifest=None):
            nonlocal tray, wire
            with (args.output / ('tray-template.log' if manifest else 'tray-default.log')).open('w') as log:
                cmd = [str(args.build / 'apps/tray/mirage-tray'), '--no-shell']
                if manifest:
                    cmd += ['--ocr-manifest', str(manifest)]
                tray = subprocess.Popen(cmd, env=env, stdout=log, stderr=log, preexec_fn=allow_fixture_debug_attach)
                children.append(tray)
            wait(lambda: Path(endpoint).exists())
            wire = Wire(endpoint)
            wire.call('hello')
            wire.call('events.subscribe')
            catalog = wire.call('workflow.atom.catalog')
            check('product registers visual knowledge atom', any(t['wire_name'] ==
                  'desktop.knowledge.visual_click' for t in catalog['tools']))
            rules = wire.call('policy.get')['rules']
            rules['input.inject'] = 'allow'
            rules['screen.capture'] = 'allow'
            wire.call('policy.set', rules=rules)
        def quit_owner():
            nonlocal wire
            closing = wire.call('product.control', action='quit', exit_epoch=0)
            if closing.get('exit_pending'):
                wire.call('product.control', action='confirm_quit', exit_epoch=closing['exit_epoch'])
            wire.sock.close()
            wire = None
            tray.wait(timeout=15)
            check('Runtime owner exits cleanly', tray.returncode == 0)
        launch()
        vision = wire.call('vision.get')
        (args.output / 'default-vision.json').write_text(json.dumps(vision, indent=2) + '\n')
        check('bundled vision runs without manual settings', vision['vision_running'])
        state_path = args.output / 'fixture-state.json'
        app = subprocess.Popen([sys.executable, __file__, '--fixture', '--font', str(args.font),
                                '--output', str(state_path)], env=env, stdin=subprocess.PIPE,
                                text=True, stdout=subprocess.DEVNULL)
        children.append(app)
        def state():
            try:
                return json.loads(state_path.read_text())
            except (FileNotFoundError, json.JSONDecodeError):
                return None
        initial = wait(state)
        D = display.Display()
        def focus():
            w = D.create_resource_object('window', initial['window'])
            w.set_input_focus(X.RevertToParent, X.CurrentTime)
            D.sync()
        def reset(mode):
            app.stdin.write(mode + '\n'); app.stdin.flush()
            wait(state, lambda s: s and s['mode'] == mode and not s['saved'])
            focus()
            time.sleep(.1)
        focus()
        definition = json.loads((Path(__file__).resolve().parents[2] /
                                 'docs/examples/local-visual-save.workflow.json').read_text())
        def publish(definition):
            published = wire.call('workflow.publish', definition=definition)
            check('DryRun publishes without Canvas input', state()['clicks'] == expected_clicks)
            return published
        expected_clicks = 0
        published = publish(definition)
        def run(policy, expected, clicks, target=None):
            nonlocal expected_clicks
            focus()
            start = wire.call('workflow.run', workflow_id=definition['workflow_id'],
                              digest=published['digest'], policy=policy)
            def view():
                return next((r for r in wire.call('workflow.runs')['runs'] if r['run_id'] == start['run_id']), None)
            final = wait(view, lambda r: r and r['state'] in ('completed', 'failed', 'waiting_agent', 'cancelled'))
            if final['state'] != expected:
                report['failure_observation'] = wire.call('desktop.observe', semantic=False, visual=True)
                time.sleep(.1)
                wire.call('workflow.runs')
            report['runs'].append(final)
            report.setdefault('events', []).extend(wire.events)
            wire.events.clear()
            expected_clicks = clicks
            check(policy + ' -> ' + expected, final['state'] == expected)
            wait(state, lambda s: s and s['clicks'] == clicks)
            check('actual Canvas click count ' + str(clicks), state()['clicks'] == clicks)
            if target:
                check('physical target is ' + target, state()['last_target'] == target)
            return final
        run('strict', 'completed', 1, 'Save')
        check('actual Canvas postcondition changed', state()['saved'])
        run('recoverable', 'waiting_agent', 1)
        reset('moved'); run('strict', 'completed', 2, 'Save')
        reset('duplicate'); run('recoverable', 'waiting_agent', 2)
        reset('page'); run('recoverable', 'waiting_agent', 2)
        reset('title'); run('recoverable', 'waiting_agent', 2)
        reset('no_post'); run('recoverable', 'waiting_agent', 3, 'Save')
        reset('normal')
        rules = wire.call('policy.get')['rules']; rules['input.inject'] = 'deny'
        wire.call('policy.set', rules=rules)
        run('strict', 'failed', 3)
        quit_owner()
        # Existing Mirador VisualIndex consumes these fixed public assets; no enrollment from captures.
        bundled = args.build / 'share/mirage/vision/ppocrv5-mobile-v1/manifest.json'
        manifest = json.loads(bundled.read_text())
        for item in manifest['artifacts'].values():
            item['path'] = str((bundled.parent / item['path']).resolve())
        manifest['geometry_marks'] = True
        manifest['templates'] = []
        image = canvas_image(args.font)
        for x, identity in [(65, 'line-icon'), (325, 'cross-icon')]:
            patch = image.crop((x - 1, 179, x + 71, 251)).convert('RGBA')
            pixels = patch.tobytes('raw', 'BGRA')
            asset = args.output / (identity + '.bgra'); asset.write_bytes(pixels)
            manifest['templates'].append(dict(id=identity, format='bgra8', path=str(asset),
                width=72, height=72, bytes=len(pixels), sha256=hashlib.sha256(pixels).hexdigest()))
        path = args.output / 'template-manifest.json'; path.write_text(json.dumps(manifest, indent=2))
        report['templates'] = [{k:v for k,v in item.items() if k != 'path'} for item in manifest['templates']]
        launch(path)
        reset('normal')
        definition['workflow_id'] = '75c0d069a3be49a99297e83c65ce7112'
        args_ = definition['steps'][0]['arguments']
        args_.update(target_kind='template', target='cross-icon')
        published = publish(definition)
        run('strict', 'completed', 4, 'cross-icon')
        reset('swapped'); run('strict', 'completed', 5, 'cross-icon')
        reset('no_post'); run('recoverable', 'waiting_agent', 6, 'cross-icon')
        model = wire.call('model.get')['model_settings']
        model = json.loads(model) if isinstance(model, str) else model
        check('model stays disabled through all workflows', not model['model'].get('enabled', False))
        quit_owner()
    except TimeoutError:
        if tray and tray.poll() is None:
            with (args.output / "timeout-stack.txt").open("w") as log:
                subprocess.run(["gdb", "-q", "-batch", "-ex", "set pagination off",
                                "-ex", "thread apply all bt", "-p", str(tray.pid)],
                               stdout=log, stderr=log, timeout=15)
        raise
    finally:
        if wire:
            wire.sock.close()
        stop(children)
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--font', type=Path)
    parser.add_argument('--fixture', action='store_true')
    parser.add_argument('--session', action='store_true')
    args = parser.parse_args()
    args.output = args.output.resolve()
    if args.fixture:
        return fixture(args)
    args.build = args.build.resolve()
    args.font = (args.font or args.build / 'apps/native/assets/NotoSansSC-Regular.otf').resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.session:
        assert os.environ.get('MIRAGE_VISUAL_KNOWLEDGE_PRIVATE_SESSION') == '1'
        return session(args)
    with tempfile.TemporaryDirectory(prefix='mirage-m711-visual-') as tmp:
        env = os.environ.copy()
        for key, folder in [('XDG_RUNTIME_DIR', 'run'), ('XDG_CONFIG_HOME', 'config'),
                            ('XDG_STATE_HOME', 'state'), ('XDG_DATA_HOME', 'data')]:
            path = Path(tmp) / folder; path.mkdir(mode=0o700); env[key] = str(path)
        env.pop('MIRAGE_NATIVE_SOCKET', None)
        env['MIRAGE_VISUAL_KNOWLEDGE_PRIVATE_SESSION'] = '1'
        env['LC_ALL'] = 'C.UTF-8'
        r, w = os.pipe()
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(w), '-screen', '0', '1000x700x24',
                                 '-nolisten', 'tcp'], pass_fds=(w,), stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        os.close(w)
        try:
            assert select.select([r], [], [], 5)[0]
            env['DISPLAY'] = ':' + os.read(r, 32).decode().strip()
            env['XDG_SESSION_TYPE'] = 'x11'
            subprocess.run(['dbus-run-session', '--', sys.executable, __file__, '--session',
                            '--build', str(args.build), '--font', str(args.font), '--output', str(args.output)],
                           env=env, check=True, timeout=180)
        finally:
            os.close(r); xvfb.terminate(); xvfb.wait(timeout=5)


if __name__ == '__main__':
    main()
