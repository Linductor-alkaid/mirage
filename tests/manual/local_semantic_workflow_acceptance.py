"""M7-11 external test owner: real product workflow IPC, GTK3 and AT-SPI2.

No model, credential, OCR weights or user desktop. All processes and settings
are contained in a private XDG/DBus/Xvfb session. Python owns test lifecycle;
product work remains on Runtime Service's existing Executor.
"""
import argparse
import json
import os
import select
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from native_conversation_acceptance import Wire, stop
from native_desktop_launch import start_watcher


def fixture(args):
    os.environ.pop('NO_AT_BRIDGE', None)
    os.environ['GTK_MODULES'] = 'gail:atk-bridge'
    import gi
    gi.require_version('Gtk', '3.0')
    gi.require_version('GdkX11', '3.0')
    from gi.repository import GLib, Gtk, GdkX11
    GLib.set_prgname('mirage-knowledge-fixture')
    GLib.set_application_name('Mirage Knowledge Fixture')
    window = Gtk.Window(title='Knowledge Editor')
    window.set_default_size(480, 240)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=20)
    box.set_border_width(24)
    window.add(box)
    label = Gtk.Label(label='Pending')
    button = Gtk.Button(label='Save')
    box.pack_start(button, False, False, 0)
    box.pack_start(label, False, False, 0)
    clicks = 0
    mode = 'normal'

    def state():
        args.output.write_text(json.dumps(dict(clicks=clicks, mode=mode,
            label=label.get_text(), window=window.get_window().get_xid())))

    def activated(_):
        nonlocal clicks
        clicks += 1
        if mode != 'no_post':
            label.set_text('Saved')
        state()
    button.connect('clicked', activated)
    duplicate = None

    def command(_channel, _condition):
        nonlocal mode, duplicate
        line = sys.stdin.readline().strip()
        if not line:
            Gtk.main_quit()
            return False
        mode = line
        label.set_text('Pending')
        if duplicate is not None:
            duplicate.destroy()
            duplicate = None
        if mode == 'duplicate':
            duplicate = Gtk.Button(label='Save')
            box.pack_start(duplicate, False, False, 0)
            duplicate.connect('clicked', activated)
            duplicate.show()
        window.move(80 if mode == 'moved' else 10, 30)
        # Insert a visible node before the button to change issued reference numbering.
        if mode == 'moved':
            extra = Gtk.Label(label='New toolbar')
            box.pack_start(extra, False, False, 0)
            box.reorder_child(extra, 0)
            extra.show()
        window.set_title('Wrong page' if mode == 'page' else 'Knowledge Editor')
        window.present()
        GLib.idle_add(lambda: (state(), False)[1])
        return True

    window.show_all()
    window.present()
    GLib.io_add_watch(sys.stdin, GLib.IO_IN | GLib.IO_HUP, command)
    GLib.timeout_add(100, lambda: (state(), False)[1])
    Gtk.main()


def session(args):
    from Xlib import display, X
    children = []
    wire = None
    report = dict(scope='Linux private Xvfb; actual embedded product Runtime, GTK3, AT-SPI2',
                  model_enabled=False, checks=[], runs=[])
    class KnowledgeWire(Wire):
        def receive(self, timeout=5):
            value = super().receive(timeout)
            if value.get('id') and value.get('ok') is False:
                report['last_ipc_error'] = value.get('error')
            return value
    def check(name, value):
        report['checks'].append(dict(name=name, passed=bool(value)))
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(name + ': ' + ('PASS' if value else 'FAIL'), flush=True)
        assert value, name
    def wait(fn, predicate=bool, seconds=10):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            value = fn()
            if predicate(value):
                return value
            time.sleep(.05)
        raise TimeoutError('private knowledge acceptance condition')
    try:
        env = os.environ.copy()
        env.pop('NO_AT_BRIDGE', None)
        env['GTK_MODULES'] = 'gail:atk-bridge'
        start_watcher(env, children, args.output)
        settings = Path(env['XDG_CONFIG_HOME']) / 'mirage/service.json'
        settings.parent.mkdir(mode=0o700)
        settings.write_text(json.dumps(dict(schema=1, model=dict(enabled=False))))
        with (args.output / 'tray.log').open('w') as log:
            tray = subprocess.Popen([str(args.build / 'apps/tray/mirage-tray'),
                                     '--no-shell', '--ocr-manifest', ''], env=env, stdout=log, stderr=log)
            children.append(tray)
        endpoint = str(Path(env['XDG_RUNTIME_DIR']) / 'mirage/mirage-service.sock')
        wait(lambda: Path(endpoint).exists())
        wire = KnowledgeWire(endpoint)
        wire.call('hello')
        wire.call('events.subscribe')
        catalog = wire.call('workflow.atom.catalog')
        check('product registers local knowledge atom', any(t['wire_name'] ==
              'desktop.knowledge.activate' for t in catalog['tools']))
        rules = wire.call('policy.get')['rules']
        rules['input.inject'] = 'allow'
        wire.call('policy.set', rules=rules)
        state_file = args.output / 'fixture-state.json'
        app = subprocess.Popen(['/usr/bin/python3', __file__, '--fixture', '--output', str(state_file)],
                               env=env, stdin=subprocess.PIPE, text=True, stdout=subprocess.DEVNULL)
        children.append(app)
        def state():
            try:
                return json.loads(state_file.read_text())
            except (FileNotFoundError, json.JSONDecodeError):
                return None
        initial = wait(state)
        D = display.Display()
        def focus():
            w = D.create_resource_object('window', initial['window'])
            w.set_input_focus(X.RevertToParent, X.CurrentTime)
            D.sync()
        focus()
        time.sleep(.3)  # External fixture readiness; no product timer.
        report['initial_observation'] = wire.call('desktop.observe', semantic=True, visual=False)
        example = Path(__file__).resolve().parents[2] / 'docs/examples/local-semantic-save.workflow.json'
        definition = json.loads(example.read_text())
        workflow_id = definition['workflow_id']
        (args.output / 'workflow.json').write_text(json.dumps(definition, indent=2) + '\n')
        published = wire.call('workflow.publish', definition=definition)
        check('DryRun publishes without any GTK activation', state()['clicks'] == 0)
        def run(policy, expected, clicks):
            focus()
            start = wire.call('workflow.run', workflow_id=workflow_id,
                              digest=published['digest'], policy=policy)
            def view():
                return next((r for r in wire.call('workflow.runs')['runs']
                             if r['run_id'] == start['run_id']), None)
            final = wait(view, lambda r: r and r['state'] in
                         ('completed', 'failed', 'waiting_agent', 'cancelled'))
            report['runs'].append(final)
            report.setdefault('events', []).extend(wire.events)
            wire.events.clear()
            check(policy + ' -> ' + expected, final['state'] == expected)
            check('GTK activation count ' + str(clicks), state()['clicks'] == clicks)
            return final
        run('strict', 'completed', 1)
        check('real GTK postcondition changed', state()['label'] == 'Saved')
        run('recoverable', 'waiting_agent', 1)
        def reset(mode):
            app.stdin.write(mode + '\n')
            app.stdin.flush()
            wait(state, lambda s: s and s['mode'] == mode and s['label'] == 'Pending')
            focus()
            time.sleep(.15)
        reset('moved')
        run('strict', 'completed', 2)
        reset('duplicate')
        run('recoverable', 'waiting_agent', 2)
        reset('page')
        run('recoverable', 'waiting_agent', 2)
        reset('no_post')
        run('recoverable', 'waiting_agent', 3)
        reset('normal')
        rules['input.inject'] = 'deny'
        wire.call('policy.set', rules=rules)
        run('strict', 'failed', 3)
        model = wire.call('model.get')['model_settings']
        model = json.loads(model) if isinstance(model, str) else model
        check('model remains disabled', not model['model'].get('enabled', False))
        closing = wire.call('product.control', action='quit', exit_epoch=0)
        check('waiting workflows require product exit confirmation', closing['exit_pending'])
        wire.call('product.control', action='confirm_quit', exit_epoch=closing['exit_epoch'])
        wire.sock.close()
        wire = None
        tray.wait(timeout=10)
        check('Runtime owner exits cleanly', tray.returncode == 0)
    finally:
        if wire:
            wire.sock.close()
        stop(children)
        (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--session', action='store_true')
    p.add_argument('--fixture', action='store_true')
    args = p.parse_args()
    args.output = args.output.resolve()
    if args.fixture:
        return fixture(args)
    args.build = args.build.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.session:
        assert os.environ.get('MIRAGE_KNOWLEDGE_PRIVATE_SESSION') == '1'
        return session(args)
    with tempfile.TemporaryDirectory(prefix='mirage-m711-') as tmp:
        env = os.environ.copy()
        for key, folder in [('XDG_RUNTIME_DIR', 'run'), ('XDG_CONFIG_HOME', 'config'),
                            ('XDG_STATE_HOME', 'state'), ('XDG_DATA_HOME', 'data')]:
            path = Path(tmp) / folder
            path.mkdir(mode=0o700)
            env[key] = str(path)
        env.pop('MIRAGE_NATIVE_SOCKET', None)
        env['MIRAGE_KNOWLEDGE_PRIVATE_SESSION'] = '1'
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
                            '--build', str(args.build), '--output', str(args.output)],
                           env=env, check=True, timeout=120)
        finally:
            os.close(r)
            xvfb.terminate()
            xvfb.wait(timeout=5)


if __name__ == '__main__':
    main()
