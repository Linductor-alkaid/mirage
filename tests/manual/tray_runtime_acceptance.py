"""Actual product lifecycle: private DBus/SNI/Xvfb, real IPC and native mouse actions."""
import argparse
import json
import os
import select
import subprocess
import sys
import shutil
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

    def desktop_open():
        entry = output / 'org.mirage.native.desktop' if args.legacy_desktop else args.build / 'apps/native/org.mirage.native.desktop'
        script = "from gi.repository import Gio; import sys; assert Gio.DesktopAppInfo.new_from_filename(sys.argv[1]).launch([],None)"
        return subprocess.run(['/usr/bin/python3', '-c', script, str(entry)], env=env,
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
        if args.legacy_desktop:
            unrelated = dict(absent, GIO_LAUNCHED_DESKTOP_FILE='/tmp/unrelated.desktop')
            unrelated.pop('MIRAGE_NATIVE_SOCKET', None)
            denied = subprocess.run([str(args.build / 'apps/native/mirage-native')], env=unrelated,
                                    capture_output=True, text=True, timeout=5)
            check('unrelated desktop marker cannot bypass frontend ownership', denied.returncode == 1 and 'tray runtime' in denied.stderr)
            explicit = dict(absent, GIO_LAUNCHED_DESKTOP_FILE='/tmp/org.mirage.native.desktop')
            denied = subprocess.run([str(args.build / 'apps/native/mirage-native')], env=explicit,
                                    capture_output=True, text=True, timeout=5)
            check('explicit endpoint blocks desktop rebootstrap', denied.returncode == 1 and 'tray runtime' in denied.stderr)
            isolated = output / 'isolated-entry'
            isolated.mkdir(exist_ok=True)
            binary = isolated / 'mirage-native'
            shutil.copy2(args.build / 'apps/native/mirage-native', binary)
            marker = dict(unrelated, GIO_LAUNCHED_DESKTOP_FILE='/tmp/org.mirage.native.desktop')
            denied = subprocess.run([str(binary)], env=marker, capture_output=True, text=True, timeout=5)
            check('missing launcher refuses before window initialization', denied.returncode == 1 and 'launcher unavailable' in denied.stderr)
            launcher = isolated / 'mirage'
            launcher.symlink_to(binary)
            denied = subprocess.run([str(binary)], env=marker, capture_output=True, text=True, timeout=5)
            check('same-image launcher refuses exec cycle', denied.returncode == 1 and 'launcher unavailable' in denied.stderr)
            launcher.unlink(); binary.unlink(); isolated.rmdir()
            if args.bootstrap_only:
                results['scope'] = 'native bootstrap rejection paths; private XDG; no window or inference'
                results['inference_requests'] = 0
                (output / 'lifecycle-results.json').write_text(json.dumps(results, indent=2))
                return

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
        if args.window_manager:
            ready = output / 'wm-fixture.json'
            children.append(subprocess.Popen([sys.executable, str(Path(__file__).with_name('x11_wm_fixture.py')),
                                              '--record', str(ready)], env=env,
                                             stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
            await_value(lambda: ready.exists(), bool)
        tray, ui, endpoint = launch(args.build, env, children, output, legacy=args.legacy_desktop)
        wire = Wire(endpoint)
        first = control()
        check('two processes with ready native child', first['frontend_pid'] == ui.pid and first['frontend_ready'])
        destination = json.loads((output / 'sni-watcher.json').read_text())['items'][-1]
        menu_probe = """import json,sys
from gi.repository import Gio, GLib
bus=Gio.bus_get_sync(Gio.BusType.SESSION,None)
def call(path,iface,method,args,reply):
 return bus.call_sync(sys.argv[1],path,iface,method,args,GLib.VariantType.new(reply),
                      Gio.DBusCallFlags.NONE,3000,None).unpack()
_,root=call('/org/mirage/tray/menu','com.canonical.dbusmenu','GetLayout',
            GLib.Variant('(iias)',(0,-1,[])),'(u(ia{sv}av))')
menu=call('/org/mirage/tray','org.freedesktop.DBus.Properties','Get',
          GLib.Variant('(ss)',('org.kde.StatusNotifierItem','ItemIsMenu')),'(v)')[0]
about=call('/org/mirage/tray/menu','com.canonical.dbusmenu','AboutToShow',
           GLib.Variant('(i)',(0,)),'(b)')[0]
properties=call('/org/mirage/tray/menu','org.freedesktop.DBus.Properties','GetAll',
                GLib.Variant('(s)',('com.canonical.dbusmenu',)),'(a{sv})')[0]
print(json.dumps(dict(root_id=root[0],children=root[2],menu=menu,about=about,
                     version=properties['Version'])))
"""
        probe = subprocess.run(['/usr/bin/python3', '-c', menu_probe, destination], env=env,
                               check=True, capture_output=True, text=True, timeout=8)
        exported = json.loads(probe.stdout)
        check('left-click menu advertised with standard root and two enabled actions',
              exported['menu'] is True and exported['root_id'] == 0 and
              [(child[0], child[1]['label'], child[1]['enabled'], child[2])
               for child in exported['children']] ==
              [(5, '打开应用', True, []), (99, '退出应用', True, [])])
        check('host menu preparation and properties use interoperable signatures',
              exported['about'] is False and exported['version'] == 3)

        check('repeat application open reuses tray and window', command('start').returncode == 0 and control()['frontend_pid'] == ui.pid)
        opened = desktop_open()
        check('desktop reopen reuses existing tray and owned frontend',
              opened.returncode == 0 and control()['frontend_pid'] == ui.pid and tray.poll() is None)

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
        if args.window_manager:
            hidden = D.intern_atom('_NET_WM_STATE_HIDDEN')
            state_atom = D.intern_atom('_NET_WM_STATE')
            def minimized():
                value = w.get_full_property(state_atom, X.AnyPropertyType)
                return value is not None and hidden in value.value
            click(1068, 30)
            await_value(minimized, bool)
            check('native minimize stops painting and stays owned', minimized() and ui.poll() is None)
            opened = command('start')
            await_value(minimized, lambda value: not value)
            check('launcher restores minimized child without a paint-loop ACK',
                  opened.returncode == 0 and control()['frontend_pid'] == ui.pid)
            click(1068, 30)
            await_value(minimized, bool)
            menu(5)
            await_value(minimized, lambda value: not value)
            check('tray Open restores the same minimized child', control()['frontend_pid'] == ui.pid)
        click(1150, 30)
        closed = await_value(control, lambda p: p['frontend_pid'] == 0)
        check('closing native leaves tray and Runtime running; child reaped', closed['frontend_pid'] == 0 and tray.poll() is None)
        opened = desktop_open()
        reopened = await_value(control, lambda p: p['frontend_ready'])
        check('desktop entry reopens closed frontend without replacing tray',
              opened.returncode == 0 and reopened['frontend_pid'] != ui.pid and tray.poll() is None)
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
    p.add_argument('--bootstrap-only', action='store_true', help='Only rejection/exec-cycle probes; requires --legacy-desktop')
    p.add_argument('--legacy-desktop', action='store_true', help='Reproduce cached desktop Exec of mirage-native')
    p.add_argument('--window-manager', action='store_true', help='Private ICCCM/EWMH peer validates minimized restore')
    args = p.parse_args()
    if args.bootstrap_only and not args.legacy_desktop:
        p.error('--bootstrap-only requires --legacy-desktop')
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
                '--build',str(args.build),'--output',str(args.output),'--session', *(['--window-manager'] if args.window_manager else []), *(['--legacy-desktop'] if args.legacy_desktop else []), *(['--bootstrap-only'] if args.bootstrap_only else [])],env=env,timeout=120)
            assert completed.returncode == 0, 'private lifecycle acceptance failed'
        finally:
            os.close(read_fd);xvfb.terminate();xvfb.wait(timeout=5)

if __name__ == '__main__': main()
