"""Linux external test owner for processes launched by the actual desktop entry."""
import ctypes
import json
import os
import re
import signal
import subprocess
import time
from pathlib import Path


class AdoptedProcess:
    def __init__(self, pid):
        self.pid = pid
        self.returncode = None

    def poll(self):
        if self.returncode is None:
            try:
                pid, status = os.waitpid(self.pid, os.WNOHANG)
            except ChildProcessError:
                # UI is the tray's child and is reaped by its product owner.
                # -999 denotes observed disappearance, not an OS exit status.
                try:
                    state = Path(f'/proc/{self.pid}/stat').read_text().split(') ', 1)[1].split()[0]
                    if state == 'Z': self.returncode = -999
                except FileNotFoundError: self.returncode = -999
                return self.returncode
            if pid:
                self.returncode = os.waitstatus_to_exitcode(status)
        return self.returncode

    def wait(self, timeout):
        deadline = time.monotonic() + timeout
        while self.poll() is None:
            if time.monotonic() >= deadline:
                raise subprocess.TimeoutExpired('desktop child', timeout)
            time.sleep(.05)
        return self.returncode

    def terminate(self):
        os.kill(self.pid, signal.SIGTERM)

    def kill(self):
        os.kill(self.pid, signal.SIGKILL)


def start_watcher(env, children, output):
    watcher = output / 'sni-watcher.json'
    children.append(subprocess.Popen(['/usr/bin/python3', str(Path(__file__).with_name('sni_fixture.py')),
                                     '--record', str(watcher)], env=env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
    deadline = time.monotonic() + 5
    while not watcher.exists() and time.monotonic() < deadline: time.sleep(.05)
    assert watcher.exists(), 'private watcher did not start'
    return watcher


def launch(build, env, children, output, legacy=False):
    # The launcher exits after detaching product faces. This external driver
    # adopts them so it can verify exit statuses and clean its private session.
    assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0, 'private test subreaper'
    watcher = start_watcher(env, children, output)
    entry = build / 'apps/native/org.mirage.native.desktop'
    expected_binary = build / 'apps/mirage'
    if legacy:
        # Reproduce GNOME's cached pre-tray Exec while keeping the real GIO
        # desktop-launch environment; the native bootstrap must forward.
        entry = output / 'org.mirage.native.desktop'
        expected_binary = build / 'apps/native/mirage-native'
        entry.write_text('[Desktop Entry]\nType=Application\nName=Mirage\nExec="' +
                         str(expected_binary) + '"\nTerminal=false\n')
    script = '''import sys
from gi.repository import Gio, GLib
info = Gio.DesktopAppInfo.new_from_filename(sys.argv[1])
assert info and GLib.shell_parse_argv(info.get_commandline())[1][0] == sys.argv[2], 'desktop entry bypasses product launcher'
assert info.launch([], None), 'desktop launch failed'
'''
    started = subprocess.run(['/usr/bin/python3', '-c', script, str(entry), str(expected_binary)],
                             env=env, capture_output=True, text=True, timeout=25)
    (output / 'desktop-launch.log').write_text(started.stdout + started.stderr)
    assert started.returncode == 0, 'desktop entry launch failed'
    tray_match = re.search(r'tray runtime ready at .+ \(pid (\d+)\)', started.stdout)
    ui_match = re.search(r'mirage-native running \(pid (\d+)\)', started.stdout)
    assert tray_match and ui_match, 'desktop startup did not report tray and UI'
    tray, ui = [AdoptedProcess(int(m[1])) for m in (tray_match, ui_match)]
    for child in (tray, ui):
        fields = Path(f'/proc/{child.pid}/environ').read_bytes().split(b'\0')
        assert ('XDG_RUNTIME_DIR=' + env['XDG_RUNTIME_DIR']).encode() in fields, 'child escaped private session'
        children.append(child)
        assert child.poll() is None, 'desktop process exited during startup'
    parent = int(Path(f'/proc/{ui.pid}/stat').read_text().split(') ', 1)[1].split()[1])
    assert parent == tray.pid, 'UI is not owned by tray'
    assert len(json.loads(watcher.read_text())['items']) == 1
    result = dict(scope='GIO launches generated desktop entry; private XDG/DBus/Xvfb; embedded Runtime',
                  legacy_desktop_forwarded=legacy, tray_started=True, native_started=True, service_embedded=True,
                  separate_processes=tray.pid != ui.pid, frontend_owned_by_tray=True)
    (output / 'desktop-results.json').write_text(json.dumps(result, indent=2) + '\n')
    endpoint = str(Path(env['XDG_RUNTIME_DIR']) / 'mirage/mirage-service.sock')
    return tray, ui, endpoint


def adopt_remaining(build, env, children):
    """Bound failure cleanup to exact binaries in this test's private runtime."""
    binaries = {str((build / p).resolve()) for p in ('apps/mirage', 'apps/mirage-service',
                'apps/native/mirage-native', 'apps/tray/mirage-tray')}
    owned = {c.pid for c in children}
    marker = ('XDG_RUNTIME_DIR=' + env['XDG_RUNTIME_DIR']).encode()
    for folder in Path('/proc').iterdir():
        if not folder.name.isdecimal() or int(folder.name) in owned:
            continue
        try:
            if os.readlink(folder / 'exe') in binaries and marker in (folder / 'environ').read_bytes().split(b'\0'):
                children.append(AdoptedProcess(int(folder.name)))
        except (FileNotFoundError, ProcessLookupError, PermissionError):
            pass


def verify_reuse(build, env, service, endpoint, output):
    started = subprocess.run([str(build / 'apps/mirage'), 'start', '--no-shell'],
                             env=env, capture_output=True, text=True, timeout=15)
    assert started.returncode == 0 and 'tray runtime reused' in started.stdout
    assert service.poll() is None and Path(endpoint).exists()
    path = output / 'desktop-results.json'
    result = json.loads(path.read_text())
    result['existing_tray_reused'] = True
    result['inference_requests'] = 0
    path.write_text(json.dumps(result, indent=2) + '\n')
