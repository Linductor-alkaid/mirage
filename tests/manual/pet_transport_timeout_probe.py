"""Measure pet lifetime with live events and delayed status replies, without a model.

Runs on a private Xvfb/DBus session with temporary XDG directories. The bounded
mock speaks only the pet bootstrap/status protocol; this is a transport failure
probe, not a Runtime/real compositor acceptance. No user's tray is contacted.
"""
import argparse
import json
import os
from pathlib import Path
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time


def session(args):
    assert os.environ.get('MIRAGE_PET_PROBE_PRIVATE') == '1'
    with tempfile.TemporaryDirectory(prefix='mirage-pet-transport-') as temporary:
        root = Path(temporary)
        endpoint = str(root / 'mock.sock')
        env = os.environ.copy()
        for name in ('XDG_RUNTIME_DIR', 'XDG_STATE_HOME', 'XDG_CONFIG_HOME', 'XDG_DATA_HOME'):
            folder = root / name
            folder.mkdir(mode=0o700)
            env[name] = str(folder)
        env.update(MIRAGE_PET_SOCKET=endpoint, XDG_SESSION_TYPE='x11', LIBGL_ALWAYS_SOFTWARE='1')
        env.pop('WAYLAND_DISPLAY', None)
        args.output.mkdir(parents=True, exist_ok=True)
        children, clients, subscribers, sequences, delayed = [], {}, set(), {}, []
        server = socket.socket(socket.AF_UNIX)
        server.bind(endpoint)
        server.listen(8)
        server.setblocking(False)
        started = time.monotonic()

        def send(client, value):
            data = json.dumps(value).encode()
            client.sendall(struct.pack('<I', len(data)) + data)

        def status():
            return dict(schema_version=1, owner_epoch='a' * 32,
                        revision=int((time.monotonic() - started) * 20) + 1,
                        host_state='running', display_state='thinking', source_kind='turn',
                        source_id='b' * 32, session_id='c' * 32, active_count=1,
                        summary='synthetic transport probe', reason_code='', elapsed_ms=10)

        with (args.output / 'pet.log').open('w') as log:
            try:
                record = root / 'wm.json'
                wm = subprocess.Popen([sys.executable, str(Path(__file__).with_name('x11_wm_fixture.py')),
                    '--record', str(record), '--advertise-compositor'], env=env, stdout=log, stderr=log)
                children.append(wm)
                while not record.exists():
                    assert wm.poll() is None and time.monotonic() - started < 5, 'WM bootstrap'
                    time.sleep(.01)
                pet = subprocess.Popen([str(args.build / 'apps/pet/mira_pet')], env=env,
                                       stdout=log, stderr=log)
                children.append(pet)
                ready, last_event, replies, deferred = None, 0, 0, 0
                started = time.monotonic()
                while time.monotonic() - started < 22 and pet.poll() is None:
                    readable, _, _ = select.select([server, *clients], [], [], .01)
                    for client in readable:
                        if client is server:
                            client, _ = server.accept()
                            client.settimeout(1)
                            assert len(clients) < 8, 'connection capacity'
                            clients[client], sequences[client] = b'', 0
                            continue
                        data = client.recv(65536)
                        if not data:
                            clients.pop(client)
                            subscribers.discard(client)
                            client.close()
                            continue
                        clients[client] += data
                        assert len(clients[client]) <= 1048580, 'frame capacity'
                        while len(clients[client]) >= 4:
                            size = struct.unpack('<I', clients[client][:4])[0]
                            assert 0 < size <= 1048576
                            if len(clients[client]) < size + 4:
                                break
                            request = json.loads(clients[client][4:4 + size])
                            clients[client] = clients[client][4 + size:]
                            reply = dict(v=1, id=request['id'], ok=True)
                            if request['op'] == 'hello':
                                reply.update(service='mirage-runtime', mirage_version='0.1.0',
                                    mira_core_version='0.1.0', host_status='running', protocol=1,
                                    tray=True, pet_status=True)
                            elif request['op'] == 'product.control':
                                reply.update(product=True, pet_pid=pet.pid, frontend_pid=0,
                                    window_epoch=1, exit_epoch=0, active_work=1, exit_pending=False,
                                    frontend_ready=False, pet_state='running', pet_diagnostic='')
                                if request.get('action') == 'pet_ready':
                                    ready = time.monotonic()
                            elif request['op'] == 'pet.status':
                                reply.update(pet=True, **status())
                            elif request['op'] == 'events.subscribe':
                                subscribers.add(client)
                            else:
                                raise AssertionError('unexpected mock request: ' + request['op'])
                            if ready and args.delay_ms and request['op'] == 'pet.status':
                                assert len(delayed) < 8
                                delayed.append((time.monotonic() + args.delay_ms / 1000, client, reply))
                                deferred += 1
                            else:
                                send(client, reply)
                                replies += 1
                    for pending in delayed[:]:
                        when, client, reply = pending
                        if time.monotonic() >= when:
                            try:
                                send(client, reply)
                                replies += 1
                            except OSError:
                                pass
                            delayed.remove(pending)
                    if time.monotonic() - last_event >= .05:
                        for client in list(subscribers):
                            sequences[client] += 1
                            try:
                                send(client, dict(v=1, seq=sequences[client], event='pet.updated', **status()))
                            except OSError:
                                subscribers.discard(client)
                        last_event = time.monotonic()
                report = dict(scope='private Xvfb/DBus and bounded mock; no model or user tray',
                    delay_ms=args.delay_ms, ready=ready is not None, alive_after_window=pet.poll() is None,
                    exit_code=pet.poll(), duration_seconds=round(time.monotonic() - started, 3),
                    immediate_replies=replies, deferred_replies=deferred,
                    max_event_sequence=max(sequences.values(), default=0))
                (args.output / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
                print(json.dumps(report), flush=True)
                assert report['ready'], 'probe did not reach pet_ready'
            finally:
                for child in reversed(children):
                    if child.poll() is None:
                        child.terminate()
                        try:
                            child.wait(timeout=8)
                        except subprocess.TimeoutExpired:
                            child.kill()
                            child.wait(timeout=3)
                for client in clients:
                    client.close()
                server.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--delay-ms', type=int, default=2000)
    parser.add_argument('--session', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    assert 0 <= args.delay_ms <= 5000
    args.build, args.output = args.build.resolve(), args.output.resolve()
    if args.session:
        session(args)
    else:
        env = os.environ.copy()
        env['MIRAGE_PET_PROBE_PRIVATE'] = '1'
        subprocess.run(['xvfb-run', '-a', 'dbus-run-session', '--', sys.executable,
            str(Path(__file__).resolve()), '--build', str(args.build), '--output', str(args.output),
            '--delay-ms', str(args.delay_ms), '--session'], env=env, check=True, timeout=50)


if __name__ == '__main__':
    main()
