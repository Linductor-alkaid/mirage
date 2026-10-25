"""Reproducible private GTK/Xvfb transient sampling; never uses the user's desktop.
Run with /usr/bin/python3 (PyGObject), a PP-OCR runtime manifest and the acceptance binary.
"""
import argparse
import json
import hashlib
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time


def session(args):
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import Gtk, GLib, Gdk
    assert Gdk.Display.get_default().get_name() == os.environ['DISPLAY'], 'GTK did not use the private X11 display'
    window = Gtk.Window(title='Private continuous perception fixture')
    window.set_default_size(800, 480)
    layout = Gtk.Fixed()
    window.add(layout)
    heading = Gtk.Label(label='Private perception test')
    layout.put(heading, 30, 30)
    label = Gtk.Label()
    label.set_markup('<span font="Sans 28">Ready</span>')
    layout.put(label, 100, 160)
    window.show_all()
    for _ in range(30):
        while Gtk.events_pending():
            Gtk.main_iteration_do(False)
        time.sleep(.01)
    schedule = []
    words = ['ALPHA', 'BRAVO', 'CHARLIE', 'DELTA', 'ECHO', 'FOXTROT',
             'GOLF', 'HOTEL', 'INDIA', 'JULIET', 'KILO', 'LIMA',
             'MIKE', 'NOVEMBER', 'OSCAR', 'PAPA', 'QUEBEC', 'ROMEO']
    for index, word in enumerate(words):
        duration = [100, 500, 1500][index % 3]
        schedule.append((word, duration))
    duration_ms = sum(duration + 700 for _, duration in schedule) + 3000
    report_path = args.output.with_suffix('.raw.json')
    child = subprocess.Popen([str(args.binary.resolve()), str(args.manifest.resolve()),
                              str(duration_ms), str(report_path.resolve())],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        if not select.select([child.stdout], [], [], 30)[0] or child.stdout.readline().strip() != 'READY':
            raise RuntimeError('sampler failed to start')
        truth = []
        index = 0

        def hide():
            label.set_text('')
            truth[-1]['hidden_s'] = time.monotonic()
            GLib.timeout_add(700, show)
            return False

        def show():
            nonlocal index
            if index == len(schedule):
                GLib.timeout_add(2000, lambda: (Gtk.main_quit(), False)[1])
                return False
            word, duration = schedule[index]
            index += 1
            layout.move(label, 100 if index % 2 else 270, 160 if index % 2 else 250)
            label.set_markup(f'<span font="Sans 28">Notice {word}</span>')
            truth.append({'text': word, 'duration_ms': duration, 'shown_s': time.monotonic()})
            GLib.timeout_add(duration, hide)
            return False
        GLib.timeout_add(700, show)
        Gtk.main()
        _, error = child.communicate(timeout=20)
        if child.returncode:
            raise RuntimeError(error[-2000:])
        report = json.loads(report_path.read_text())
        for trial in truth:
            matched = [event for event in report['events'] if trial['text'] in event['text']
                       and event['observed_s'] >= trial['shown_s'] - .1]
            trial['detected'] = bool(matched)
            trial['latency_ms'] = (min(event['delivered_s'] for event in matched) - trial['shown_s']) * 1000 if matched else None
            trial['confirmed'] = any(event['confirmed'] and event['kind'] == 'appeared' for event in matched)
        report['binary_sha256'] = hashlib.sha256(args.binary.read_bytes()).hexdigest()
        report['manifest_sha256'] = hashlib.sha256(args.manifest.read_bytes()).hexdigest()
        report['trials'] = truth
        report['by_duration'] = [{'duration_ms': duration,
                                 'total': sum(t['duration_ms'] == duration for t in truth),
                                 'detected': sum(t['detected'] for t in truth if t['duration_ms'] == duration),
                                 'confirmed': sum(t['confirmed'] for t in truth if t['duration_ms'] == duration)}
                                for duration in [100, 500, 1500]]
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
        print(json.dumps({key: report[key] for key in ['by_duration', 'frames', 'analyzed', 'skipped', 'analysis_failed', 'peak_journal_bytes', 'peak_preview_bytes', 'process_peak_rss_kib']}, ensure_ascii=False))
    finally:
        if child.poll() is None:
            child.terminate()
            child.communicate(timeout=10)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--session', action='store_true')
    args = parser.parse_args()
    if args.session:
        session(args)
        return
    with tempfile.TemporaryDirectory(prefix='mirage-perception-') as folder:
        env = os.environ.copy()
        for key in ['CONFIG', 'CACHE', 'DATA', 'STATE', 'RUNTIME']:
            path = Path(folder) / key.lower()
            path.mkdir(mode=0o700)
            env[f'XDG_{key}_HOME' if key != 'RUNTIME' else 'XDG_RUNTIME_DIR'] = str(path)
        env.pop('WAYLAND_DISPLAY', None)
        env['GDK_BACKEND'] = 'x11'
        env['XDG_SESSION_TYPE'] = 'x11'
        read_fd, write_fd = os.pipe()
        server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '800x480x24',
                                   '-nolisten', 'tcp', '-noreset'], pass_fds=(write_fd,), env=env,
                                  stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(write_fd)
        try:
            if not select.select([read_fd], [], [], 10)[0]:
                raise RuntimeError('Xvfb unavailable')
            env['DISPLAY'] = ':' + os.read(read_fd, 64).decode().strip()
            subprocess.run(['dbus-run-session', '--', sys.executable, __file__, '--session',
                            '--binary', str(args.binary.resolve()), '--manifest', str(args.manifest.resolve()),
                            '--output', str(args.output.resolve())], env=env, check=True, timeout=110)
        finally:
            os.close(read_fd)
            server.terminate()
            server.wait(timeout=10)


if __name__ == '__main__':
    main()
