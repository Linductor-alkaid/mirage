"""Operator-assisted M7-09 GTK acceptance on the current Xorg desktop.

External test orchestration owns the GTK process and waits for its native release
packet before starting the finite Executor-managed C++ driver. Only the fixture
window ROI is captured by the product; no live model, weights or credentials.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import select
import subprocess
import time


def packet(stream, seconds):
    deadline = time.monotonic() + seconds
    data = bytearray()
    while len(data) < 4096:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError('fixture/operator packet deadline')
        ready, _, _ = select.select([stream], [], [], min(1, remaining))
        if not ready:
            continue
        char = os.read(stream.fileno(), 1)
        if not char:
            raise RuntimeError('fixture exited before packet')
        if char == b'\n':
            value = json.loads(data)
            if not isinstance(value, dict):
                raise ValueError('invalid fixture packet')
            return value
        data += char
    raise ValueError('fixture packet exceeded 4 KiB')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, default=Path(__file__).with_name('gtk_pointer_fixture.py'))
    parser.add_argument('--sample', choices=['scroll', 'native-dnd', 'native-blocked', 'native-cancel'])
    parser.add_argument('--preflight-only', action='store_true')
    parser.add_argument('--diagnostics', action='store_true')
    parser.add_argument('--late-cancel', action='store_true')
    args = parser.parse_args()
    if args.late_cancel and args.sample != 'native-cancel':
        parser.error('--late-cancel requires --sample native-cancel')
    args.output.mkdir(parents=True, exist_ok=True)
    report = {'schema': 1, 'work_item': 'M7-09', 'decision': 'DEC-065',
              'recorded_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'session_type': os.environ.get('XDG_SESSION_TYPE', ''),
              'display': os.environ.get('DISPLAY', ''), 'operator_required': True,
              'source': 'own GTK3 fixture, real root ScreenProvider window ROI',
              'model': 'canonical tool registry; no inference; native allocation candidates',
              'limits': {'packet_bytes': 4096, 'operator_seconds': 120,
                         'driver_seconds': 45, 'owner_seconds': 30, 'reference_seconds': 5},
              'samples': [], 'passed': 0, 'attempted': 0,
              'total': 1 if args.sample else 4}
    report['pointer_diagnostics'] = args.diagnostics
    report['cancel_distance_logical'] = 50 if args.late_cancel else 8

    def save():
        (args.output / 'report.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')

    if report['session_type'] != 'x11' or not re.fullmatch(r':\d+(?:\.\d+)?', report['display']):
        report['preflight_error'] = 'current local Xorg session required'
    elif not args.binary.is_file() or not os.access(args.binary, os.X_OK) or not args.fixture.is_file():
        report['preflight_error'] = 'built driver and fixture required'
    if 'preflight_error' in report:
        save()
        print(report['preflight_error'], flush=True)
        return 2
    subprocess.run(['/usr/bin/python3', '-c',
                    "import gi; gi.require_version('Gtk', '3.0'); from gi.repository import Gtk"], check=True)
    report['binary_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
    report['fixture_sha256'] = hashlib.sha256(args.fixture.read_bytes()).hexdigest()
    report['preflight'] = 'ready for operator'
    save()
    if args.preflight_only:
        print('HOST_PREFLIGHT ready; no window or input started', flush=True)
        return 0
    modes = [args.sample] if args.sample else ['scroll', 'native-dnd', 'native-blocked', 'native-cancel']
    for mode in modes:
        result = {'sample': mode, 'passed': False, 'driver_started': False}
        app = None
        try:
            with (args.output / (mode + '-gtk.log')).open('wb') as gtk_log:
                app = subprocess.Popen(['/usr/bin/python3', str(args.fixture.resolve()), mode, '--operator'],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=gtk_log,
                                       bufsize=0)
                initial = packet(app.stdout, 10)
                result['initial_state'] = initial
                print(f'HOST_READY {mode}: click 点击这里开始 once, release, then leave input untouched.', flush=True)
                armed = packet(app.stdout, 120)
                if armed.get('kind') != 'operator_release' or not armed.get('operator_release') or armed['window'] != initial['window']:
                    raise ValueError('missing matching native operator release')
                result['operator_state'] = armed
                # External test orchestration allows the host compositor to
                # finish focus/map transitions before the first observation.
                # This never refreshes/retries an admitted product action.
                time.sleep(0.5)
                result['compositor_settle_seconds'] = 0.5
                env = os.environ.copy()
                env.update(MIRAGE_GTK_HOST_INPUT_FD=str(app.stdin.fileno()),
                           MIRAGE_GTK_HOST_OUTPUT_FD=str(app.stdout.fileno()),
                           MIRAGE_GTK_HOST_WINDOW=str(armed['window']))
                # Do not inherit diagnostic/test controls from another run.
                env.pop('MIRAGE_GTK_HOST_DIAGNOSTICS', None)
                env['MIRAGE_GTK_HOST_CANCEL_DISTANCE'] = str(report['cancel_distance_logical'])
                if args.diagnostics:
                    env['MIRAGE_GTK_HOST_DIAGNOSTICS'] = '1'
                result['driver_started'] = True
                with (args.output / (mode + '-driver.log')).open('wb') as driver_log:
                    process = subprocess.run([str(args.binary.resolve()), '/usr/bin/python3',
                                              str(args.fixture.resolve()), '--host=' + mode],
                                             env=env, pass_fds=(app.stdin.fileno(), app.stdout.fileno()),
                                             stdout=driver_log, stderr=subprocess.STDOUT, timeout=45)
                log = (args.output / (mode + '-driver.log')).read_bytes()
                if len(log) > 65536:
                    raise ValueError('driver log exceeded 64 KiB')
                result['exit_code'] = process.returncode
                result['log_sha256'] = hashlib.sha256(log).hexdigest()
                for line in log.decode(errors='replace').splitlines():
                    if line.startswith('GTK_EFFECT '):
                        result['final_state'] = json.loads(line.split(' ', 2)[2])
                    if line.startswith('GTK_DISPATCH '):
                        _, _, failed, payload = line.split(' ', 3)
                        result['dispatch_failed'] = failed == 'failed=1'
                        result['dispatch'] = json.loads(payload)
                    if line.startswith('GTK_ACTION '):
                        result['verified'] = line.split(' ', 3)[2] == 'verified=1'
                    if line.startswith('GTK_RELEASE '):
                        result['physical_released'] = 'physical=1' in line.split()
                        result['owner_released'] = 'owner=1' in line.split()
                    if line.startswith('GTK_POINTER_DIAGNOSTIC '):
                        result['pointer_diagnostic'] = json.loads(line.split(' ', 2)[2])
                    if line.startswith('GTK_PIXEL_CHANGE '):
                        result['pixel_diagnostic'] = line
                expected = mode in ('scroll', 'native-dnd')
                result['passed'] = (process.returncode == 0 and result.get('verified') is expected
                                    and result.get('physical_released') and result.get('owner_released')
                                    and 'final_state' in result and b'checks, 0 failures' in log)
                if not result['passed']:
                    result['diagnostic'] = log.decode(errors='replace')[-4096:]
        except Exception as error:
            result['error'] = str(error)
        finally:
            if app is not None:
                if app.poll() is None:
                    app.terminate()
                    try:
                        app.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        app.kill()
                        app.wait(timeout=5)
                app.stdin.close()
                app.stdout.close()
            report['samples'].append(result)
            report['attempted'] += 1
            report['passed'] += int(result['passed'])
            save()
        print(f'HOST_RESULT {mode} passed={int(result["passed"])}', flush=True)
        if not result['passed']:
            print(json.dumps(result, ensure_ascii=False), flush=True)
            return 1
    print(f'HOST_FINISHED {report["passed"]}/{report["total"]}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
