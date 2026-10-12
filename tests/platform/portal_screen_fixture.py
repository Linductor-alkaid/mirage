"""DEC-073: isolated Portal bus and synthetic pixels; never the user desktop."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def png(width=4, height=2):
    def chunk(kind, data):
        return struct.pack('!I', len(data)) + kind + data + struct.pack('!I', zlib.crc32(kind + data))
    raw = (b'\0' + bytes([10, 20, 30, 255]) * width) * height
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('!2I5B', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def serve(directory):
    from gi.repository import Gio, GLib
    xml = '''<node><interface name="org.freedesktop.portal.Screenshot">
      <method name="Screenshot"><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/>
      <arg type="o" direction="out"/></method></interface>
      <interface name="org.freedesktop.portal.Request"><method name="Close"/>
      <signal name="Response"><arg type="u"/><arg type="a{sv}"/></signal></interface></node>'''
    node = Gio.DBusNodeInfo.new_for_xml(xml)
    conn = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    conn.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus',
                   'RequestName', GLib.Variant('(su)', ('org.freedesktop.portal.Desktop', 0)),
                   GLib.VariantType('(u)'), Gio.DBusCallFlags.NONE, 1000, None)
    cast = Gio.DBusNodeInfo.new_for_xml('''<node><interface name="org.freedesktop.portal.ScreenCast">
      <property name="version" type="u" access="read"/>
      <method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
      <method name="SelectSources"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
      <method name="Start"><arg type="o" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
      <method name="OpenPipeWireRemote"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="h" direction="out"/></method>
      </interface><interface name="org.freedesktop.portal.Session"><method name="Close"/>
      <signal name="Closed"><arg type="a{sv}"/></signal></interface></node>''')
    remote = Gio.DBusNodeInfo.new_for_xml("""<node><interface name="org.freedesktop.portal.RemoteDesktop">
      <property name="version" type="u" access="read"/>
      <method name="CreateSession"><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
      <method name="SelectDevices"><arg type="o" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
      <method name="Start"><arg type="o" direction="in"/><arg type="s" direction="in"/><arg type="a{sv}" direction="in"/><arg type="o" direction="out"/></method>
      </interface></node>""")
    state = {'cast_object': 0, 'calls': 0, 'closes': 0, 'mode': 'success'}
    control = Gio.DBusNodeInfo.new_for_xml('''<node><interface name="org.mirage.PortalFixture">
      <method name="Mode"><arg type="s" direction="in"/></method></interface></node>''')
    def method(connection, sender, path, interface, name, params, invocation):
        if name == 'Mode':
            state['mode'] = params.unpack()[0]
            if state['mode'].startswith(('cast_', 'rd_')) and not state['cast_object']:
                state['cast_object'] = conn.register_object('/org/freedesktop/portal/desktop',
                    cast.interfaces[0], method, lambda *args: GLib.Variant('u', 5), None)
            if state['mode'].startswith('rd_') and not state.get('remote_object'):
                state['remote_object'] = conn.register_object('/org/freedesktop/portal/desktop',
                    remote.interfaces[0], method, lambda *args: GLib.Variant('u', 1 if state['mode'] == 'rd_persist_v1' else 2), None)
            invocation.return_value(GLib.Variant('()', ()))
            return
        if name == 'Close':
            state['closes'] += 1
            (directory / 'closed').write_text(str(state['closes']))
            invocation.return_value(GLib.Variant('()', ()))
            return
        if interface in ('org.freedesktop.portal.ScreenCast', 'org.freedesktop.portal.RemoteDesktop'):
            state['calls'] += 1
            (directory / 'counts').write_text(str(state['calls']))
            mode = state['mode']
            if mode.startswith('rd_'):
                expected_interface = 'org.freedesktop.portal.ScreenCast' if name in ('SelectSources', 'OpenPipeWireRemote') else 'org.freedesktop.portal.RemoteDesktop'
                assert interface == expected_interface
                if name == 'SelectDevices':
                    assert params.unpack()[-1]['types'] == 3
                    options = params.unpack()[-1]
                    if state['mode'].startswith('rd_persist_'):
                        assert options['persist_mode'] == 2
                        if state['mode'] == 'rd_persist_restore':
                            assert options['restore_token'].startswith('fixture-token-')
                        elif state['mode'] != 'rd_persist_readonly':
                            assert 'restore_token' not in options
                mode = {'rd_user_cancel':'cast_user_cancel', 'rd_wrong_path':'cast_wrong_path',
                        'rd_cancel':'cast_cancel', 'rd_persist_cancel':'cast_cancel',
                        'rd_persist_timeout':'cast_timeout'}.get(mode, mode)
            if name == 'OpenPipeWireRemote':
                invocation.return_value(GLib.Variant('(h)', (0,)))
                return
            options = params.unpack()[-1]
            handle = '/org/freedesktop/portal/desktop/request/' + sender[1:].replace('.', '_') + '/' + options['handle_token']
            conn.register_object(handle, node.interfaces[1], method, None, None)
            session = ('/org/freedesktop/portal/desktop/session/' + sender[1:].replace('.', '_') + '/' + options['session_handle_token']) if name == 'CreateSession' else params.unpack()[0]
            if name == 'CreateSession':
                conn.register_object(session, cast.interfaces[1], method, None, None)
            if name == 'SelectSources':
                assert options['types'] == 1 and options['multiple'] is False
                assert 'restore_token' not in options and 'persist_mode' not in options
            if mode == 'cast_denied' and name == 'Start':
                invocation.return_dbus_error('org.freedesktop.DBus.Error.AccessDenied', 'synthetic denial')
                return
            invocation.return_value(GLib.Variant('(o)', (handle + '_wrong' if mode == 'cast_wrong_path' and name == 'Start' else handle,)))
            def cast_response():
                if mode == 'cast_closed' and name == 'Start':
                    conn.emit_signal(sender, session, 'org.freedesktop.portal.Session', 'Closed', GLib.Variant('(a{sv})', ({},)))
                    return False
                if mode == 'cast_malformed' and name == 'Start':
                    conn.emit_signal(sender, handle, 'org.freedesktop.portal.Request', 'Response', GLib.Variant('(s)', ('invalid',)))
                    return False
                values = {}
                if name == 'CreateSession':
                    if mode != 'cast_missing_session':
                        values['session_handle'] = GLib.Variant('s', session + '_wrong' if mode == 'cast_bad_session' else session)
                elif name == 'Start':
                    properties = {}
                    if state['mode'].startswith('rd_'):
                        values['devices'] = GLib.Variant('u', 1 if mode in ('rd_input_denied', 'rd_persist_denied') else 3)
                        if mode.startswith('rd_persist_') and mode != 'rd_persist_missing':
                            values['restore_token'] = GLib.Variant('s', 'fixture-token-' + str(state['calls']))
                        if mode != 'rd_mapping':
                            properties['size'] = GLib.Variant('(ii)', (1600, 1000))
                    streams = [(0 if mode == 'cast_node' else 17, properties)]
                    if mode == 'cast_streams':
                        streams = []
                    if mode == 'cast_multiple':
                        streams.append((18, {}))
                    if mode != 'cast_missing_streams':
                        values['streams'] = GLib.Variant('a(ua{sv})', streams)
                code = (1 if mode == 'cast_user_cancel' else 2 if mode == 'cast_failed' else 0) if name == 'Start' else 0
                conn.emit_signal(sender, handle, 'org.freedesktop.portal.Request', 'Response', GLib.Variant('(ua{sv})', (code, values)))
                if mode == 'cast_closed_before_select' and name == 'CreateSession':
                    conn.emit_signal(sender, session, 'org.freedesktop.portal.Session', 'Closed', GLib.Variant('(a{sv})', ({},)))
                return False
            if name != 'Start' or mode not in ('cast_cancel', 'cast_timeout', 'cast_wrong_path'):
                GLib.timeout_add(500 if mode == 'cast_late' and name == 'Start' else 20, cast_response)
            return
        state['calls'] += 1
        mode = state['mode']
        if mode == 'method_denied':
            invocation.return_dbus_error('org.freedesktop.DBus.Error.AccessDenied', 'synthetic denial')
            return
        _, options = params.unpack()
        handle = '/org/freedesktop/portal/desktop/request/' + sender[1:].replace('.', '_') + '/' + options['handle_token']
        conn.register_object(handle, node.interfaces[1], method, None, None)
        (directory / 'counts').write_text(str(state['calls']))
        def respond():
            if mode == 'malformed':
                conn.emit_signal(sender, handle, 'org.freedesktop.portal.Request', 'Response',
                                 GLib.Variant('(s)', ('not a response',)))
                return False
            uri = (directory / ('hires.png' if mode == 'hires' else 'image.png')).as_uri()
            if mode == 'remote_uri':
                uri = 'https://example.invalid/private.png'
            if mode in ('bad_png', 'symlink', 'fifo', 'large_file', 'truncated'):
                uri = (directory / mode).as_uri()
            values = {} if mode == 'missing_uri' else {'uri': GLib.Variant('s', uri)}
            code = 2 if mode == 'deny' else 1 if mode == 'user_cancel' else 0
            conn.emit_signal(sender, handle, 'org.freedesktop.portal.Request', 'Response',
                             GLib.Variant('(ua{sv})', (code, values)))
            return False
        if mode == 'early':
            respond()
        invocation.return_value(GLib.Variant('(o)', (handle + '_wrong' if mode == 'wrong_path' else handle,)))
        if mode not in ('early', 'cancel', 'timeout', 'wrong_path'):
            GLib.timeout_add(6100 if mode == 'delayed_tool' else 500 if mode == 'late' else 20, respond)
    conn.register_object('/org/freedesktop/portal/desktop', node.interfaces[0], method, None, None)
    conn.register_object('/org/mirage/PortalFixture', control.interfaces[0], method, None, None)
    (directory / 'ready').write_text('ready')
    GLib.MainLoop().run()


def run(binary, with_cast=False):
    from gi.repository import Gio, GLib
    with tempfile.TemporaryDirectory(prefix='mirage-portal-fixture-') as tmp:
        directory = Path(tmp)
        (directory / 'image.png').write_bytes(png())
        (directory / 'hires.png').write_bytes(png(3200, 2000))
        (directory / 'bad_png').write_bytes(b'not PNG')
        (directory / 'symlink').symlink_to(directory / 'image.png')
        os.mkfifo(directory / 'fifo')
        (directory / 'truncated').write_bytes(png()[:30])
        with (directory / 'large_file').open('wb') as out:
            out.truncate((16 << 20) + 1)
        bus = subprocess.Popen(['dbus-daemon', '--session', '--nofork', '--print-address=1'], stdout=subprocess.PIPE, text=True)
        env = os.environ.copy()
        env['DBUS_SESSION_BUS_ADDRESS'] = bus.stdout.readline().strip()
        env['XDG_SESSION_TYPE'] = 'wayland'
        for name in ('STATE', 'CONFIG', 'DATA'):
            env['XDG_' + name + '_HOME'] = str(directory / name.lower())
        service = None
        try:
            service = subprocess.Popen(['/usr/bin/python3', __file__, '--serve', str(directory)], env=env)
            import time
            deadline = time.monotonic() + 5
            while not (directory / 'ready').exists() and time.monotonic() < deadline:
                time.sleep(.025)
            assert (directory / 'ready').exists()
            conn = Gio.DBusConnection.new_for_address_sync(env['DBUS_SESSION_BUS_ADDRESS'],
                Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
            modes = ['success', 'early', 'deny', 'user_cancel', 'wrong_path', 'missing_uri',
                     'remote_uri', 'bad_png', 'symlink', 'fifo', 'large_file', 'truncated',
                     'budget', 'pre_cancel', 'cancel', 'timeout', 'tool', 'tool_denied', 'delayed_tool',
                     'hires', 'malformed', 'late', 'method_denied']
            if with_cast:
                subprocess.run([binary, 'pixels'], env=env, check=True, timeout=5)
                modes.extend(['cast_bad_session', 'cast_missing_session', 'cast_wrong_path',
                    'cast_user_cancel', 'cast_failed', 'cast_missing_streams', 'cast_streams',
                    'cast_multiple', 'cast_node', 'cast_remote', 'cast_denied', 'cast_malformed',
                    'cast_closed', 'cast_closed_before_select', 'cast_cancel', 'cast_late', 'cast_timeout',
                    'cast_retry', 'cast_pre_cancel_session', 'rd_input_denied', 'rd_mapping',
                    'rd_remote', 'rd_user_cancel', 'rd_wrong_path', 'rd_cancel', 'rd_pre_cancel', 'rd_persist_success', 'rd_persist_missing', 'rd_persist_restore', 'rd_persist_denied', 'rd_persist_store', 'rd_persist_readonly', 'rd_persist_v1', 'rd_persist_cancel', 'rd_persist_timeout', 'rd_persist_pre_cancel'])
            for mode in modes:
                print(f'Portal fixture mode: {mode}', flush=True)
                conn.call_sync('org.freedesktop.portal.Desktop', '/org/mirage/PortalFixture',
                    'org.mirage.PortalFixture', 'Mode', GLib.Variant('(s)', (mode,)),
                    GLib.VariantType('()'), Gio.DBusCallFlags.NONE, 1000, None)
                counts = (directory / 'counts').read_text() if (directory / 'counts').exists() else None
                closes = int((directory / 'closed').read_text()) if (directory / 'closed').exists() else 0
                subprocess.run([binary, mode], env=env, check=True, timeout=40)
                if mode == 'cast_retry':
                    assert int((directory / 'counts').read_text()) - int(counts or 0) == 4
                if mode in ('tool_denied', 'pre_cancel', 'cast_pre_cancel_session', 'rd_pre_cancel', 'rd_persist_v1', 'rd_persist_pre_cancel'):
                    assert ((directory / 'counts').read_text() if (directory / 'counts').exists() else None) == counts
                if mode in ('cancel', 'timeout', 'wrong_path', 'late') or (mode.startswith(('cast_', 'rd_')) and mode not in ('cast_pre_cancel_session', 'rd_pre_cancel', 'rd_persist_v1', 'rd_persist_pre_cancel')):
                    assert int((directory / 'closed').read_text()) > closes
            service.terminate()
            service.wait(timeout=5)
            subprocess.run([binary, 'absent'], env=env, check=True, timeout=5)
            print(f'Portal fixture: {len(modes) + 1} scenarios passed; isolated bus, no user pixels/input')
        finally:
            if service and service.poll() is None:
                service.terminate()
                service.wait(timeout=5)
            bus.terminate()
            bus.wait(timeout=5)


if __name__ == '__main__':
    if sys.argv[1] == '--serve':
        serve(Path(sys.argv[2]))
    else:
        run(str(Path(sys.argv[1]).resolve()), '--screencast' in sys.argv)
