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
            pid, status = os.waitpid(self.pid, os.WNOHANG)
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


def launch(build, env, children, output):
    # The launcher exits after detaching product faces. This external driver
    # adopts them so it can verify exit statuses and clean its private session.
    assert ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0, 'private test subreaper'
    entry = build / 'apps/native/org.mirage.native.desktop'
    script = '''import sys
from gi.repository import Gio, GLib
info = Gio.DesktopAppInfo.new_from_filename(sys.argv[1])
assert info and GLib.shell_parse_argv(info.get_commandline())[1][0] == sys.argv[2], 'desktop entry bypasses product launcher'
assert info.launch([], None), 'desktop launch failed'
'''
    started = subprocess.run(['/usr/bin/python3', '-c', script, str(entry), str(build / 'apps/mirage')],
                             env=env, capture_output=True, text=True, timeout=25)
    (output / 'desktop-launch.log').write_text(started.stdout + started.stderr)
    assert started.returncode == 0, 'desktop entry launch failed'
    service_match = re.search(r'service ready at .+ \(pid (\d+)\)', started.stdout)
    ui_match = re.search(r'mirage-native running \(pid (\d+)\)', started.stdout)
    tray_match = re.search(r'mirage-tray running \(pid (\d+)\)', started.stdout)
    assert service_match and ui_match and tray_match, 'desktop startup did not report three processes'
    service, ui, tray = [AdoptedProcess(int(m[1])) for m in (service_match, ui_match, tray_match)]
    for child in (service, ui, tray):
        fields = Path(f'/proc/{child.pid}/environ').read_bytes().split(b'\0')
        assert ('XDG_RUNTIME_DIR=' + env['XDG_RUNTIME_DIR']).encode() in fields, 'child escaped private session'
        children.append(child)
        assert child.poll() is None, 'desktop process exited during startup'
    result = dict(scope='GIO launches generated desktop entry; no prestarted service; private XDG/DBus/Xvfb',
                  service_started=True, native_started=True, tray_started=True,
                  separate_processes=len({service.pid, ui.pid, tray.pid}) == 3)
    (output / 'desktop-results.json').write_text(json.dumps(result, indent=2) + '\n')
    endpoint = str(Path(env['XDG_RUNTIME_DIR']) / 'mirage/mirage-service.sock')
    return service, ui, endpoint


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
    started = subprocess.run([str(build / 'apps/mirage'), 'start', '--no-tray', '--no-shell'],
                             env=env, capture_output=True, text=True, timeout=15)
    assert started.returncode == 0 and 'service already running' in started.stdout
    assert service.poll() is None and Path(endpoint).exists()
    path = output / 'desktop-results.json'
    result = json.loads(path.read_text())
    result['existing_service_reused'] = True
    result['inference_requests'] = 0
    path.write_text(json.dumps(result, indent=2) + '\n')
