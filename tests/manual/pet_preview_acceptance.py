"""Private X11 peer: real pet RGBA, relocation, suspension, and owner exit.

No model requests. The minimal ICCCM peer and Xvfb are test fixtures, not a
compositor/desktop or hardware GPU acceptance. Generated evidence stays outside
the repository. This external driver owns all of its processes and deadlines.
"""
import argparse
import ctypes
import json
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from native_conversation_acceptance import Wire, stop
from native_desktop_launch import AdoptedProcess, start_watcher


def await_value(probe, predicate, budget=10):
    deadline = time.monotonic() + budget
    while time.monotonic() < deadline:
        value = probe()
        if predicate(value):
            return value
        time.sleep(.05)
    raise AssertionError('private pet acceptance deadline')


def session(args):
    from Xlib import display, X, Xutil, protocol, error
    from Xlib.ext import shape, xtest
    from PIL import Image, ImageChops
    children, wire = [], None
    result = dict(scope='Xvfb + ICCCM/compositor-selection fixture; real RGBA framebuffer and relocated processes; root is not composited; no inference',
                  checks=[], metrics={}, limitations=['hardware GPU budget', 'desktop compositor alpha',
                    'fullscreen', 'multi-monitor/DPI', 'seven loop visual seam acceptance'])

    def check(name, ok):
        result['checks'].append(dict(name=name, passed=bool(ok)))
        (args.output / 'pet-results.json').write_text(json.dumps(result, indent=2) + '\n')
        print(name + ': ' + ('PASS' if ok else 'FAIL'), flush=True)
        assert ok, name

    def cpu(pid):
        fields = Path(f'/proc/{pid}/stat').read_text().split(') ', 1)[1].split()
        return (int(fields[11]) + int(fields[12])) / os.sysconf('SC_CLK_TCK')

    def minimize(d, window):
        d.screen().root.send_event(protocol.event.ClientMessage(window=window,
            client_type=d.intern_atom('WM_CHANGE_STATE'), data=(32, [Xutil.IconicState, 0, 0, 0, 0])),
            event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
        d.sync()
        await_value(lambda: window.get_attributes().map_state, lambda value: value == X.IsUnmapped)

    try:
        assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0
        env = os.environ.copy()
        start_watcher(env, children, args.output)
        wm_record = args.output / 'wm.json'
        wm_record.unlink(missing_ok=True)
        children.append(subprocess.Popen([sys.executable, str(Path(__file__).with_name('x11_wm_fixture.py')),
            '--record', str(wm_record), '--advertise-compositor'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        await_value(wm_record.exists, bool)
        with tempfile.TemporaryDirectory(prefix='mirage-pet-relocated-') as temporary:
            folder = Path(temporary)
            for source in [args.build / 'apps/tray/mirage-tray', args.build / 'apps/pet/mira_pet']:
                shutil.copy2(source, folder / source.name)
            shutil.copytree(args.build / 'apps/pet/assets', folder / 'assets')
            check('relocated payload has nine local assets', len(list((folder / 'assets/pet').glob('*.webp'))) == 9)
            d = display.Display()
            for abrupt in [False, True]:
                position_file = Path(env['XDG_STATE_HOME']) / 'mirage/pet-window-position.txt'
                if abrupt:
                    position_file.write_text('2147483647 -2147483648\n')
                endpoint = str(Path(env['XDG_RUNTIME_DIR']) / ('pet-lost.sock' if abrupt else 'pet-normal.sock'))
                with (args.output / ('lost.log' if abrupt else 'normal.log')).open('w') as log:
                    tray = subprocess.Popen([str(folder / 'mirage-tray'), '--socket', endpoint,
                        '--state-dir', str(folder / 'state'), '--no-recovery', '--no-shell',
                        *(['--ocr-manifest', str(args.build / 'share/mirage/vision/ppocrv5-mobile-v1/manifest.json')]
                          if (args.build / 'share/mirage/vision/ppocrv5-mobile-v1/manifest.json').exists() else [])],
                        cwd='/', env=env, stdout=log, stderr=log)
                    children.append(tray)
                    await_value(lambda: Path(endpoint).exists() or tray.poll() is not None, bool)
                    check('tray starts in private session', tray.poll() is None)
                    wire = Wire(endpoint)
                    await_value(lambda: wire.call('hello'), lambda value: value.get('tray', False))
                    state = await_value(lambda: wire.call('product.control', action='status', exit_epoch=0),
                                        lambda value: value.get('pet_pid', 0) > 0)
                    state = await_value(lambda: wire.call('product.control', action='status', exit_epoch=0),
                                        lambda value: value.get('pet_state') == 'running')
                    check('owned pet completes explicit readiness bootstrap', state.get('pet_pid', 0) > 0)
                    pet = AdoptedProcess(state['pet_pid'])
                    children.append(pet)
                    (args.output / 'pet-pid.txt').write_text(str(pet.pid))
                    check('separate pet is owned by tray', int(Path(f'/proc/{pet.pid}/stat').read_text().split(') ', 1)[1].split()[1]) == tray.pid)
                    def find_window():
                        for window in d.screen().root.query_tree().children:
                            try:
                                pid = window.get_full_property(d.intern_atom('_NET_WM_PID'), X.AnyPropertyType)
                                geometry = window.get_geometry()
                                if (pid is not None and int(pid.value[0]) == pet.pid and
                                    geometry.width >= 200 and geometry.height >= 200 and
                                    window.get_attributes().map_state == X.IsViewable):
                                    return window
                            except (error.BadWindow, error.BadDrawable):
                                pass
                    window = await_value(find_window, bool)
                    if abrupt:
                        restored = window.get_geometry()
                        check('offscreen saved position is clamped into the current workarea',
                              0 <= restored.x <= d.screen().width_in_pixels - restored.width and
                              0 <= restored.y <= d.screen().height_in_pixels - restored.height)
                    time.sleep(6)  # entrance once and first asynchronous idle decode
                    if not abrupt:
                        geometry = window.get_geometry()
                        raw = window.get_image(0, 0, geometry.width, geometry.height, X.ZPixmap, 0xffffffff)
                        image = Image.frombytes('RGBA', (geometry.width, geometry.height), raw.data, 'raw', 'BGRA')
                        image.save(args.output / 'pet-rgba.png')
                        alpha = image.getchannel('A')
                        check('actual window framebuffer contains transparent and visible pixels', alpha.getextrema() == (0, 255))
                        check('actual framebuffer preserves transparent corner', image.getpixel((0, 0))[3] == 0)
                        character = alpha.crop((12, 70, 252, 290))
                        check('character region renders visible pixels', sum(character.histogram()[1:]) > 1000)
                        feet = alpha.crop((12, 270, 252, 302))
                        check('full character reaches the lower image region', sum(feet.histogram()[1:]) > 200)
                        check('pet window reserves bubbles without reducing the 240px character canvas',
                              geometry.width == 264 and geometry.height == 322)
                        wm_state = window.get_full_property(d.intern_atom('_NET_WM_STATE'), X.AnyPropertyType)
                        atoms = set(wm_state.value) if wm_state is not None else set()
                        check('pet is excluded from the dock and pager',
                              d.intern_atom('_NET_WM_STATE_SKIP_TASKBAR') in atoms and
                              d.intern_atom('_NET_WM_STATE_SKIP_PAGER') in atoms)
                        input_rectangles = shape.get_rectangles(window, shape.SK.Input).rectangles
                        def hit(x, y):
                            return any(r.x <= x < r.x + r.width and r.y <= y < r.y + r.height
                                       for r in input_rectangles)
                        check('transparent corner remains input-through', not hit(0, 0))
                        check('entire bubble and tail reserve remains input-through',
                              not any(hit(x, y) for x in range(12, 252) for y in range(0, 70)))
                        body = next((r for r in input_rectangles if r.y < 215 and r.width >= 20), None)
                        check('visible character has a drag region', body is not None)
                        if body:
                            before = window.get_geometry()
                            px = before.x + body.x + body.width // 2
                            py = before.y + body.y + body.height // 2
                            xtest.fake_input(d, X.MotionNotify, x=px, y=py); d.sync()
                            time.sleep(.1)
                            xtest.fake_input(d, X.ButtonPress, detail=1); d.sync()
                            time.sleep(.1)
                            xtest.fake_input(d, X.MotionNotify, x=px - 36, y=py - 28); d.sync()
                            time.sleep(.1)
                            xtest.fake_input(d, X.ButtonRelease, detail=1); d.sync()
                            after = await_value(window.get_geometry,
                                                lambda value: value.x != before.x or value.y != before.y)
                            check('dragging visible character moves its window',
                                  after.x == before.x - 36 and after.y == before.y - 28)
                            stored = await_value(lambda: position_file.read_text() if position_file.exists() else '', bool)
                            check('dragged position is persisted', stored.strip() == f'{after.x + 12} {after.y + 70}')
                        time.sleep(.7)
                        following = window.get_image(0, 0, geometry.width, geometry.height, X.ZPixmap, 0xffffffff)
                        following = Image.frombytes('RGBA', image.size, following.data, 'raw', 'BGRA')
                        motion = ImageChops.difference(image.crop((0, 0, geometry.width, 220)).convert('RGB'),
                                                      following.crop((0, 0, geometry.width, 220)).convert('RGB'))
                        check('same idle state advances actual rendered frames', motion.getbbox() is not None)
                        started, before = time.monotonic(), cpu(pet.pid)
                        time.sleep(3)
                        result['metrics']['visible_cpu_percent_one_core'] = round(100 * (cpu(pet.pid) - before) / (time.monotonic() - started), 2)
                        result['metrics']['rss_status'] = [line for line in Path(f'/proc/{pet.pid}/status').read_text().splitlines() if line.startswith(('VmRSS:', 'VmHWM:'))]
                    minimize(d, window)
                    time.sleep(.5)
                    started, before = time.monotonic(), cpu(pet.pid)
                    time.sleep(3)
                    if not abrupt:
                        result['metrics']['hidden_cpu_percent_one_core'] = round(100 * (cpu(pet.pid) - before) / (time.monotonic() - started), 2)
                        window.map(); d.sync()
                        await_value(lambda: window.get_attributes().map_state, lambda value: value == X.IsViewable)
                        time.sleep(1)
                        check('restored pet stays owned and does not restart process', wire.call('product.control', action='status', exit_epoch=0)['pet_pid'] == pet.pid)
                        minimize(d, window)
                        duplicate = wire.call('product.control', action='pet_start', exit_epoch=0)
                        check('independent repeated start reuses owned process', duplicate.get('pet_pid') == pet.pid)
                        command_at = time.monotonic()
                        stopping = wire.call('product.control', action='pet_stop', exit_epoch=0)
                        check('independent stop admits promptly', time.monotonic() - command_at < 1 and
                              stopping.get('pet_state') in ('stopping', 'stopped'))
                        status = wire.call('pet.status')
                        check('Agent runtime remains available while pet stops', status.get('host_state') == 'running')
                        await_value(lambda: wire.call('product.control', action='status', exit_epoch=0),
                                    lambda value: value.get('pet_state') == 'stopped' and value.get('pet_pid') == 0)
                        check('independent stop reaps only pet', not Path(f'/proc/{pet.pid}').exists() and tray.poll() is None)
                        wire.call('product.control', action='pet_start', exit_epoch=0)
                        state = await_value(lambda: wire.call('product.control', action='status', exit_epoch=0),
                                            lambda value: value.get('pet_state') == 'running')
                        restarted = AdoptedProcess(state['pet_pid']); children.append(restarted)
                        check('independent restart creates a new owned process', restarted.pid != pet.pid)
                        pet = restarted
                        window = await_value(find_window, bool)
                        restored_position = await_value(window.get_geometry,
                            lambda value: value.x == after.x and value.y == after.y)
                        check('restarted pet restores last dragged position',
                              restored_position.x == after.x and restored_position.y == after.y)
                        os.kill(pet.pid, 9)
                        failed = await_value(lambda: wire.call('product.control', action='status', exit_epoch=0),
                                             lambda value: value.get('pet_state') == 'failed' and value.get('pet_pid') == 0)
                        check('pet crash exposes a diagnostic and preserves tray', bool(failed.get('pet_diagnostic')) and tray.poll() is None)
                        wire.call('product.control', action='pet_start', exit_epoch=0)
                        state = await_value(lambda: wire.call('product.control', action='status', exit_epoch=0),
                                            lambda value: value.get('pet_state') == 'running')
                        pet = AdoptedProcess(state['pet_pid']); children.append(pet)
                        window = await_value(find_window, bool)
                        check('explicit recovery after crash completes readiness', pet.pid > 0)
                        minimize(d, window)
                    started = time.monotonic()
                    if abrupt:
                        tray.kill(); tray.wait(timeout=5)
                        wire.sock.close(); wire = None
                        pet.wait(timeout=12)
                        check('hidden pet exits after unexpected owner loss', pet.returncode == 0)
                    else:
                        wire.call('service.shutdown')
                        wire.sock.close(); wire = None
                        tray.wait(timeout=10)
                        check('hidden graceful exit stays within owner budget', tray.returncode == 0 and time.monotonic() - started < 6)
                        check('hidden pet is reaped and socket removed', not Path(f'/proc/{pet.pid}').exists() and not Path(endpoint).exists())
                    result['metrics']['hidden_owner_loss_seconds' if abrupt else 'hidden_shutdown_seconds'] = round(time.monotonic() - started, 3)
            d.close()
        (args.output / 'pet-results.json').write_text(json.dumps(result, indent=2) + '\n')
    finally:
        if wire:
            wire.sock.close()
        stop(children)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--session', action='store_true')
    args = parser.parse_args()
    args.build, args.output = args.build.resolve(), args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.session:
        assert os.environ.get('MIRAGE_ACCEPTANCE_PRIVATE_SESSION') == '1'
        session(args)
        return
    with tempfile.TemporaryDirectory(prefix='mirage-pet-session-') as temporary:
        root, env = Path(temporary), os.environ.copy()
        for name, folder in [('XDG_RUNTIME_DIR', 'run'), ('XDG_CONFIG_HOME', 'config'), ('XDG_STATE_HOME', 'state'), ('XDG_DATA_HOME', 'data')]:
            path = root / folder; path.mkdir(mode=0o700); env[name] = str(path)
        env['MIRAGE_ACCEPTANCE_PRIVATE_SESSION'] = '1'
        env['LC_ALL'] = 'C.UTF-8'
        env['LIBGL_ALWAYS_SOFTWARE'] = '1'
        env.pop('WAYLAND_DISPLAY', None)
        env['XDG_SESSION_TYPE'] = 'x11'
        read_fd, write_fd = os.pipe()
        xvfb = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '1500x1000x24', '-nolisten', 'tcp'],
            env=env, pass_fds=[write_fd], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_fd)
        try:
            assert select.select([read_fd], [], [], 5)[0]
            env['DISPLAY'] = ':' + os.read(read_fd, 32).decode().strip()
            completed = subprocess.run(['dbus-run-session', '--', sys.executable, str(Path(__file__).resolve()),
                '--build', str(args.build), '--output', str(args.output), '--session'], env=env, timeout=150)
            assert completed.returncode == 0
        finally:
            os.close(read_fd); xvfb.terminate(); xvfb.wait(timeout=5)


if __name__ == '__main__':
    main()
