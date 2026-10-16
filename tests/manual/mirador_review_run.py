"""M7-20: display images on private X11, capture through Mirage, export review.

Requires /usr/bin/python3 with GTK3/Pillow and Xvfb/ffmpeg. All child processes
belong to this fixture and are reaped. Exit 0 proves acquisition/execution only;
manual visual judgments live in the companion review report.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import time

from PIL import Image, ImageDraw


def viewer(args):
    import gi
    gi.require_version('Gtk', '3.0')
    from gi.repository import GdkPixbuf, GLib, Gtk
    pix = GdkPixbuf.Pixbuf.new_from_file(str(args.viewer))
    win = Gtk.Window()
    win.set_decorated(False)
    win.set_resizable(False)
    win.move(37, 53)
    win.add(Gtk.Image.new_from_pixbuf(pix))
    win.connect('destroy', Gtk.main_quit)
    win.show_all()
    def ready():
        print('READY', flush=True)
        return False
    GLib.timeout_add(250, ready)
    Gtk.main()


def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


def main(args):
    root = args.output.resolve()
    corpus = root / 'corpus'
    entries = json.loads((corpus / 'corpus.json').read_text())
    # M7-22: derive known transforms/replacement from the independent browser
    # source. These are diagnostic image transformations, not detector output.
    cap = root / 'capabilities'
    cap.mkdir(exist_ok=True)
    form = Image.open(corpus / 'form-light.png').convert('RGB')
    moved = Image.new('RGB', form.size, 'white')
    moved.paste(form, (12, -8))
    moved.save(cap / 'translated.ppm')
    edited = form.copy()
    ImageDraw.Draw(edited).rectangle((650, 320, 665, 335), fill='red')
    edited.save(cap / 'small-edit.ppm')
    imposter = form.copy()
    imposter.paste(form.crop((248, 387, 367, 418)).resize((98, 27)), (55, 530))
    imposter.save(cap / 'imposter.ppm')
    # Actual H.264 encoding/decoding; review decoded pixels, not the source PNG.
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-framerate', '1', '-i',
                    str(corpus / 'video-%d.png'), '-c:v', 'libx264', '-crf', '18',
                    '-pix_fmt', 'yuv420p', str(corpus / 'sequence.mp4')], check=True, timeout=30)
    subprocess.run(['ffmpeg', '-y', '-v', 'error', '-i', str(corpus / 'sequence.mp4'),
                    '-start_number', '0', str(corpus / 'decoded-%d.png')], check=True, timeout=30)
    host = root / 'host' / 'raw.ppm'
    if host.exists():
        # Task conversation only: exclude desktop/sidebar and other chat titles.
        im = Image.open(host)
        if im.size == (3200, 2000):
            im.crop((1300, 390, 2700, 1070)).save(corpus / 'host-crop.png')
            entries.append({'name': 'host-crop', 'source': 'real Wayland Portal image, crop [1300,390,1400,680]; pixel-local X11 replay'})
        else:
            print('Host crop not generated: select a safe crop for this resolution', file=sys.stderr)
    if args.samples:
        entries = [e for e in entries if e['name'] in args.samples.split(',')]
        if not entries:
            raise RuntimeError('No selected samples')
    read_fd, write_fd = os.pipe()
    server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                               '1800x1100x24', '-nolisten', 'tcp'], pass_fds=(write_fd,),
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.close(write_fd)
    samples = []
    try:
        if not select.select([read_fd], [], [], 10)[0]:
            raise RuntimeError('Xvfb startup timeout')
        display = ':' + os.read(read_fd, 40).decode().strip()
        os.close(read_fd)
        env = dict(os.environ, DISPLAY=display, GDK_BACKEND='x11', GDK_SCALE='1', GDK_DPI_SCALE='1')
        for entry in entries:
            name = entry['name']
            source = corpus / (f'decoded-{entry["frame"]}.png' if 'frame' in entry else name + '.png')
            image = Image.open(source).convert('RGB')
            out = root / args.results_dir / name
            out.mkdir(parents=True, exist_ok=True)
            child = subprocess.Popen(['/usr/bin/python3', str(Path(__file__).resolve()),
                                      '--viewer', str(source)], env=env, stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE, text=True)
            try:
                if not select.select([child.stdout], [], [], 10)[0] or child.stdout.readline().strip() != 'READY':
                    raise RuntimeError('GTK viewer startup failed')
                started = time.monotonic()
                proc = subprocess.run(['/usr/bin/time', '-f', '%M', '-o', str(out / 'max-rss-kib.txt'),
                                       str(args.binary.resolve()), str(args.manifest.resolve()),
                                       str(out), display, '37', '53', str(image.width), str(image.height)],
                                      env=env, text=True, capture_output=True, timeout=60)
                (out / 'execution.log').write_text(proc.stdout + proc.stderr)
                result = dict(entry, exit_code=proc.returncode, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
                result['process_wall_seconds'] = time.monotonic() - started
                result['process_max_rss_kib'] = int((out / 'max-rss-kib.txt').read_text().strip()) if proc.returncode == 0 else None
                if proc.returncode == 0:
                    raw = Image.open(out / 'raw.ppm').convert('RGB')
                    result['capture_pixels_exact'] = raw.size == image.size and raw.tobytes() == image.tobytes()
                    result['raw_pixels_sha256'] = hashlib.sha256(raw.tobytes()).hexdigest()
                    raw.save(out / 'raw.png')
                    Image.open(out / 'marked.ppm').save(out / 'marked.png')
                    report = json.loads((out / 'report.json').read_text())
                    result['region_count'] = len(report['regions'])
                    result['ocr_count'] = sum(r['source'] == 0 for r in report['regions'])
                    result['geometry_count'] = sum(r['source'] == 3 for r in report['regions'])
                    result['backend_calls'] = report['backend_calls']
                samples.append(result)
                print(name, json.dumps({k: v for k, v in result.items() if k in
                      ['exit_code', 'capture_pixels_exact', 'region_count', 'ocr_count', 'geometry_count']}), flush=True)
            finally:
                stop(child)
    finally:
        stop(server)
    (root / (args.results_dir + '-execution.json')).write_text(json.dumps({'samples': samples, 'quality_review': 'pending',
        'source': 'Private Xvfb; GTK pixel display; actual Mirage ROI ScreenProvider; production PP-OCR + geometry + SoM'}, ensure_ascii=False, indent=2) + '\n')
    return int(any(s['exit_code'] or not s.get('capture_pixels_exact') for s in samples))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--viewer', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--manifest', type=Path)
    parser.add_argument('--samples', help='Optional comma-separated subset')
    parser.add_argument('--results-dir', default='samples')
    args = parser.parse_args()
    if args.viewer:
        viewer(args)
    elif args.output and args.binary and args.manifest:
        raise SystemExit(main(args))
    else:
        parser.error('--output, --binary, --manifest are required')
