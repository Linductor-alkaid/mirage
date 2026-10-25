"""Private real IBus+GTK+PP-OCR trials, through the production fill tool.
The mode indicator is a fixed fixture Chinese indicator; this does not validate
real desktop mode discovery or Portal authorization. Candidate UI and committed
GTK text are real. No VLM, network, clipboard, or user's display/configuration.
"""
import argparse
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time


def app(state):
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import Gtk, Gdk
    window = Gtk.Window(title='Mirage IME public test')
    window.set_decorated(False)
    window.set_default_size(1100, 720)
    window.move(0, 0)
    fixed = Gtk.Fixed()
    window.add(fixed)
    mode = Gtk.Label(label='中')
    fixed.put(mode, 25, 20)
    entry = Gtk.Entry()
    entry.set_size_request(950, 64)
    fixed.put(entry, 60, 130)
    css = Gtk.CssProvider()
    css.load_from_data(b'window { background: white; } entry { font: 28px "Noto Sans CJK SC"; padding: 4px; } label { font: 36px "Noto Sans CJK SC"; color: black; }')
    Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
    entry.connect('changed', lambda *_: Path(state).write_text(json.dumps({'text': entry.get_text()}, ensure_ascii=False)))
    window.connect('destroy', Gtk.main_quit)
    window.show_all()
    entry.grab_focus()
    Path(state).write_text('{"text":""}')
    print('ready', flush=True)
    Gtk.main()


def session(args):
    children = []
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['IBUS_ADDRESS'] = 'unix:path=' + env['XDG_RUNTIME_DIR'] + '/ibus-test'
    env['GTK_IM_MODULE'] = 'ibus'
    env['XMODIFIERS'] = '@im=ibus'
    env['GDK_BACKEND'] = 'x11'
    env['MIRAGE_ACCEPTANCE_PRIVATE_SESSION'] = '1'
    try:
        daemon = subprocess.Popen(['ibus-daemon', '--replace', '--xim', '--panel=/usr/libexec/ibus-ui-gtk3', '--config=/usr/libexec/ibus-memconf', '--emoji-extension=disable', '--address', env['IBUS_ADDRESS']], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        children.append(daemon)
        time.sleep(3)
        engine = subprocess.Popen(['/usr/libexec/ibus-engine-libpinyin', '--ibus'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        children.append(engine)
        subprocess.run(['gsettings', 'set', 'org.freedesktop.ibus.panel', 'lookup-table-orientation', '1'], env=env, check=True)
        subprocess.run(['gsettings', 'set', 'org.freedesktop.ibus.panel', 'custom-font', 'Noto Sans CJK SC 28'], env=env, check=True)
        subprocess.run(['gsettings', 'set', 'org.freedesktop.ibus.panel', 'use-custom-font', 'true'], env=env, check=True)
        trials = []
        corpus = [('你好', ['ni', 'hao']), ('中国', ['zhong', 'guo']), ('世界', ['shi', 'jie']),
                  ('北京', ['bei', 'jing']), ('上海', ['shang', 'hai']), ('学习', ['xue', 'xi']),
                  ('天气', ['tian', 'qi']), ('文字', ['wen', 'zi']), ('测试', ['ce', 'shi']), ('输入', ['shu', 'ru']),
                  ('电脑', ['dian', 'nao']), ('键盘', ['jian', 'pan']), ('鼠标', ['shu', 'biao']), ('桌面', ['zhuo', 'mian']),
                  ('窗口', ['chuang', 'kou']), ('文件', ['wen', 'jian']), ('系统', ['xi', 'tong']), ('开发', ['kai', 'fa']),
                  ('工具', ['gong', 'ju']), ('识别', ['shi', 'bie']), ('中文', ['zhong', 'wen']), ('拼音', ['pin', 'yin']),
                  ('候选', ['hou', 'xuan']), ('页面', ['ye', 'mian']), ('成功', ['cheng', 'gong']), ('失败', ['shi', 'bai']),
                  ('运行', ['yun', 'xing']), ('取消', ['qu', 'xiao']), ('输入法', ['shu', 'ru', 'fa']), ('人工智能', ['ren', 'gong', 'zhi', 'neng'])]
        for index in range(args.trials):
            expected, pinyin = corpus[index % len(corpus)]
            state = output / ('state-' + str(index) + '.json')
            gui = subprocess.Popen([sys.executable, __file__, '--app', str(state)], env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            children.append(gui)
            assert gui.stdout.readline().strip() == 'ready'
            time.sleep(.25)
            subprocess.run([sys.executable, '-c', "import gi;gi.require_version('IBus','1.0');from gi.repository import IBus;IBus.init();b=IBus.Bus();assert b.is_connected();assert b.set_global_engine('libpinyin')"], env=env, check=True, timeout=10)
            time.sleep(.4)
            units = ([{'text': c, 'pinyin': p} for c, p in zip(expected, pinyin)] if args.split_characters
                     else [{'text': expected, 'pinyin': ''.join(pinyin)}])
            request = {'units': units, 'arguments': {
                'text': expected, 'target': {'x': 66, 'y': 134, 'width': 940, 'height': 56},
                'mode_roi': {'x': 20, 'y': 15, 'width': 65, 'height': 60},
                'candidates_roi': {'x': 40, 'y': 198, 'width': 1040, 'height': 240}}}
            request_file = output / ('request-' + str(index) + '.json')
            request_file.write_text(json.dumps(request, ensure_ascii=False))
            report_file = output / ('trial-' + str(index) + '.json')
            proc = subprocess.run([str(args.binary.resolve()), str(args.manifest.resolve()), str(request_file), str(report_file)], env=env, capture_output=True, text=True, timeout=135)
            observed = json.loads(state.read_text())['text']
            report = json.loads(report_file.read_text()) if report_file.exists() else {'ok': False, 'error': proc.stderr[-1000:]}
            trial = {'trial': index, 'expected': expected, 'actual': observed, 'tool_ok': report.get('ok', False),
                     'success': report.get('ok', False) and observed == expected, 'false_accept': report.get('ok', False) and observed != expected,
                     'exit_code': proc.returncode, 'duration_ms': report.get('duration_ms'), 'report': report_file.name}
            trials.append(trial)
            print(json.dumps(trial, ensure_ascii=False), flush=True)
            gui.terminate()
            gui.wait(timeout=5)
            children.remove(gui)
        result = {'test': 'real IBus libpinyin + GTK + PP-OCR; test-only X11 shared-session shim',
                  'limitations': ['fixed Chinese mode indicator', 'prevalidated spelling plan', 'not Portal or online planning accuracy'],
                  'split_characters': args.split_characters, 'candidate_font': 'Noto Sans CJK SC 28',
                  'distinct_texts': len(set(t['expected'] for t in trials)),
                  'trials': trials, 'total': len(trials), 'successes': sum(v['success'] for v in trials),
                  'false_accepts': sum(v['false_accept'] for v in trials)}
        (output / 'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2))
        print(json.dumps({k: v for k, v in result.items() if k != 'trials'}, ensure_ascii=False), flush=True)
    finally:
        for child in reversed(children):
            if child.poll() is None:
                child.terminate()
                try: child.wait(timeout=5)
                except subprocess.TimeoutExpired: child.kill(); child.wait()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--trials', type=int, default=20)
    parser.add_argument('--session', action='store_true')
    parser.add_argument('--split-characters', action='store_true')
    parser.add_argument('--app')
    args = parser.parse_args()
    if args.app:
        app(args.app)
    elif args.session:
        session(args)
    else:
        with tempfile.TemporaryDirectory(prefix='mirage-real-ime-') as folder:
            env = os.environ.copy()
            for name, sub in [('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache'), ('XDG_DATA_HOME', 'data'), ('XDG_STATE_HOME', 'state'), ('XDG_RUNTIME_DIR', 'run')]:
                env[name] = str(Path(folder) / sub)
                Path(env[name]).mkdir(mode=0o700)
            env.pop('WAYLAND_DISPLAY', None)
            env['XDG_SESSION_TYPE'] = 'x11'
            read_fd, write_fd = os.pipe()
            server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0', '1100x720x24', '-nolisten', 'tcp', '-noreset'], pass_fds=(write_fd,), env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.close(write_fd)
            try:
                assert select.select([read_fd], [], [], 5)[0], 'Xvfb start timeout'
                env['DISPLAY'] = ':' + os.read(read_fd, 32).decode().strip()
                command = ['dbus-run-session', '--', sys.executable, __file__, '--session', '--binary', str(args.binary.resolve()), '--manifest', str(args.manifest.resolve()), '--output', str(args.output.resolve()), '--trials', str(args.trials)]
                if args.split_characters: command.append('--split-characters')
                subprocess.run(command, env=env, check=True, timeout=140 * args.trials + 30)
            finally:
                os.close(read_fd)
                server.terminate(); server.wait(timeout=5)

if __name__ == '__main__':
    main()
