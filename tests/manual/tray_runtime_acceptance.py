"""Actual product lifecycle: private DBus/SNI/Xvfb, real IPC and native mouse actions."""
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
from native_desktop_launch import launch, AdoptedProcess, adopt_remaining


def session(args):
    from Xlib import display, X, Xatom
    from Xlib.ext import xtest
    from PIL import Image
    children, wire = [], None
    env = os.environ.copy()
    output = args.output
    results = dict(scope='real Linux product; private XDG/DBus/SNI/X11; no inference', checks=[])

    def check(name, passed):
        results['checks'].append(dict(name=name, passed=bool(passed)))
        (output / 'lifecycle-results.json').write_text(json.dumps(results, indent=2))
        print(name + ': ' + ('PASS' if passed else 'FAIL'), flush=True)
        assert passed, name

    def command(*arguments):
        return subprocess.run([str(args.build / 'apps/mirage'), *arguments], env=env,
                              capture_output=True, text=True, timeout=20)

    def control(action='status', epoch=0):
        return wire.call('product.control', action=action, exit_epoch=epoch)

    def menu(item=99):
        destination = json.loads((output / 'sni-watcher.json').read_text())['items'][-1]
        script = '''import sys
from gi.repository import Gio, GLib
bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
bus.call_sync(sys.argv[1],'/org/mirage/tray/menu','com.canonical.dbusmenu','Event',
 GLib.Variant('(isvu)',(int(sys.argv[2]),'clicked',GLib.Variant('i',0),0)),
 None,Gio.DBusCallFlags.NONE,3000,None)
'''
        subprocess.run(['/usr/bin/python3', '-c', script, destination, str(item)], env=env,
                       check=True, timeout=5, capture_output=True)

    def await_value(probe, predicate, budget=8):
        deadline = time.monotonic() + budget
        while time.monotonic() < deadline:
            value = probe()
            if predicate(value): return value
            time.sleep(.05)
        raise AssertionError('bounded lifecycle wait expired')

    try:
        # The gate runs before GLFW: it works even with no display available.
        absent = dict(env, MIRAGE_NATIVE_SOCKET=str(output / 'absent.sock'))
        absent.pop('DISPLAY', None)
        denied = subprocess.run([str(args.build / 'apps/native/mirage-native')], env=absent,
                                capture_output=True, text=True, timeout=5)
        check('native refuses missing tray before display initialization', denied.returncode == 1 and 'tray runtime' in denied.stderr)
        check('product refuses no-tray frontend option', command('start', '--no-tray').returncode == 2)
        # A headless developer service does not authorize a product window.
        headless_endpoint = str(Path(env['XDG_RUNTIME_DIR']) / 'headless.sock')
        headless = subprocess.Popen([str(args.build / 'apps/mirage-service'), '--socket', headless_endpoint,
                                     '--no-recovery'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        children.append(headless)
        await_value(lambda: Path(headless_endpoint).exists(), bool)
        denied = subprocess.run([str(args.build / 'apps/native/mirage-native')],
                                env=dict(env, MIRAGE_NATIVE_SOCKET=headless_endpoint),
                                capture_output=True, text=True, timeout=5)
        check('native refuses headless service', denied.returncode == 1)
        check('launcher refuses headless endpoint without killing it',
              command('start', '--socket', headless_endpoint).returncode == 1 and headless.poll() is None)
        headless.terminate(); headless.wait(timeout=8)
        # Without a watcher no frontend or persistent Runtime is allowed.
        check('missing notification host fails startup', command('start', '--wait', '4').returncode == 1)
        tray, ui, endpoint = launch(args.build, env, children, output)
        wire = Wire(endpoint)
        first = control()
        check('two processes with ready native child', first['frontend_pid'] == ui.pid and first['frontend_ready'])
        check('repeat application open reuses tray and window', command('start').returncode == 0 and control()['frontend_pid'] == ui.pid)
        D = display.Display()
        root = D.screen().root
        w = None

        def window(pid):
            deadline = time.monotonic() + 8
            while time.monotonic() < deadline:
                for resource in root.query_tree().children:
                    prop = resource.get_full_property(D.intern_atom('_NET_WM_PID'), Xatom.CARDINAL)
                    if prop and int(prop.value[0]) == pid and resource.get_attributes().map_state == X.IsViewable:
                        resource.configure(x=10, y=10)
                        resource.set_input_focus(X.RevertToParent, X.CurrentTime)
                        D.sync()
                        return resource
                time.sleep(.05)
            raise AssertionError('native window not found')

        def click(x, y):
            origin = root.translate_coords(w, 0, 0)
            xtest.fake_input(D, X.MotionNotify, x=origin.x+x, y=origin.y+y)
            D.sync(); time.sleep(.05)
            xtest.fake_input(D, X.ButtonPress, 1); D.sync(); time.sleep(.06)
            xtest.fake_input(D, X.ButtonRelease, 1); D.sync(); time.sleep(.2)

        def capture(name):
            size = w.get_geometry()
            raw = w.get_image(0, 0, size.width, size.height, X.ZPixmap, 4294967295)
            Image.frombytes('RGB', (size.width, size.height), raw.data, 'raw', 'BGRX').save(output / (name + '.png'))

        w = window(ui.pid)
        click(1150, 30)
        closed = await_value(control, lambda p: p['frontend_pid'] == 0)
        check('closing native leaves tray and Runtime running; child reaped', closed['frontend_pid'] == 0 and tray.poll() is None)
        menu(5)
        reopened = await_value(control, lambda p: p['frontend_ready'])
        check('tray Open Mirage reopens a new child', reopened['frontend_pid'] != ui.pid)
        w = window(reopened['frontend_pid'])
        task = wire.call('task.submit', goal='退出确认测试：私有 sleep 任务', steps=[dict(op='process.execute', arg='sleep 30')])['task_id']
        check('incomplete task is counted by Runtime', control()['active_work'] >= 1)
        # Close UI during a live task; quitting from tray must reopen confirmation.
        click(1150, 30)
        await_value(control, lambda p: p['frontend_pid'] == 0)
        menu()
        pending = await_value(control, lambda p: p['exit_pending'] and p['frontend_ready'])
        w = window(pending['frontend_pid'])
        time.sleep(.4)
        capture('exit-confirm-light')
        check('tray Quit reopens active exit confirmation', pending['active_work'] >= 1 and tray.poll() is None)
        click(100,100) # outside modal: cannot implicitly cancel or confirm
        check('confirmation scrim leaves task running', control()['exit_pending'])
        click(628, 478)
        cancelled = await_value(control, lambda p: not p['exit_pending'])
        check('actual Continue Running keeps task and tray alive', cancelled['active_work'] >= 1 and tray.poll() is None)
        inspect = wire.call('task.inspect', task_id=task)
        check('cancelled exit does not cancel task', inspect['task']['progress'] not in ('Completed','Failed','Cancelled'))
        menu()
        pending = await_value(control, lambda p: p['exit_pending'])
        check('new exit prompt has a new epoch', pending['exit_epoch'] > cancelled['exit_epoch'])
        click(740, 478)
        wire.sock.close(); wire = None
        tray.wait(timeout=10)
        check('actual Stop and Exit closes tray, native and IPC', tray.returncode == 0 and not Path(endpoint).exists())
        D.close()
        # Fresh product + unexpected host signal: frontend loses IPC and exits.
        started = command('start')
        check('product restarts after complete exit', started.returncode == 0)
        wire = Wire(endpoint)
        second = control()
        native_pid = second['frontend_pid']
        tray_pid = int(Path(f'/proc/{native_pid}/stat').read_text().split(') ',1)[1].split()[1])
        second_tray = AdoptedProcess(tray_pid); children.append(second_tray)
        second_tray.terminate(); second_tray.wait(timeout=10)
        wire.sock.close(); wire = None
        check('host SIGTERM reaps native and removes endpoint', not Path(f'/proc/{native_pid}').exists() and not Path(endpoint).exists())
        results['inference_requests'] = 0
        (output / 'lifecycle-results.json').write_text(json.dumps(results, indent=2))
    finally:
        if wire: wire.sock.close()
        adopt_remaining(args.build, env, children)
        stop(children)


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--xvfb', default='Xvfb')
    p.add_argument('--session', action='store_true')
    args = p.parse_args()
    args.build, args.output = args.build.resolve(), args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.session:
        assert os.environ.get('MIRAGE_ACCEPTANCE_PRIVATE_SESSION') == '1'
        session(args); return
    with tempfile.TemporaryDirectory(prefix='mirage-tray-') as temporary:
        root = Path(temporary)
        env = os.environ.copy()
        for name, folder in [('XDG_RUNTIME_DIR','run'),('XDG_CONFIG_HOME','config'),('XDG_STATE_HOME','state'),('XDG_DATA_HOME','data')]:
            path=root/folder;path.mkdir(mode=0o700);env[name]=str(path)
        env.pop('MIRAGE_NATIVE_SOCKET', None)
        env['MIRAGE_ACCEPTANCE_PRIVATE_SESSION']='1'
        env['LC_ALL']='C.UTF-8'
        read_fd, write_fd = os.pipe()
        xvfb = subprocess.Popen([args.xvfb,'-displayfd',str(write_fd),'-screen','0','1500x1000x24','-nolisten','tcp'],
                                env=env, pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_fd)
        try:
            assert select.select([read_fd],[],[],5)[0], 'Xvfb timeout'
            env['DISPLAY']=':'+os.read(read_fd,32).decode().strip()
            completed = subprocess.run(['dbus-run-session','--',sys.executable,str(Path(__file__).resolve()),
                '--build',str(args.build),'--output',str(args.output),'--session'],env=env,timeout=120)
            assert completed.returncode == 0, 'private lifecycle acceptance failed'
        finally:
            os.close(read_fd);xvfb.terminate();xvfb.wait(timeout=5)

if __name__ == '__main__': main()
