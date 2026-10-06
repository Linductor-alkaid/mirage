"""External private-session SNI watcher; validates actual tray registration/menu IPC."""
import argparse
import json
from pathlib import Path
from gi.repository import Gio, GLib

p = argparse.ArgumentParser()
p.add_argument('--record', type=Path, required=True)
p.add_argument('--refuse', action='store_true')
args = p.parse_args()
name = 'org.kde.StatusNotifierWatcher'
connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
xml = '''<node><interface name="org.kde.StatusNotifierWatcher">
<method name="RegisterStatusNotifierItem"><arg type="s" direction="in"/></method>
<property name="IsStatusNotifierHostRegistered" type="b" access="read"/>
<property name="RegisteredStatusNotifierItems" type="as" access="read"/>
<property name="ProtocolVersion" type="i" access="read"/>
</interface></node>'''
record = dict(ready=True, items=[])

def called(conn, sender, path, interface, method, parameters, invocation):
    if args.refuse:
        invocation.return_dbus_error('org.mirage.Test.RegistrationRefused', 'fixture rejects registration')
        return
    item = parameters.unpack()[0]
    assert item.startswith(':'), 'SNI must register its unique bus name'
    record['items'].append(item)
    args.record.write_text(json.dumps(record))
    invocation.return_value(GLib.Variant('()', ()))

def property_value(conn, sender, path, interface, prop):
    return {'IsStatusNotifierHostRegistered': GLib.Variant('b', True),
            'RegisteredStatusNotifierItems': GLib.Variant('as', record['items']),
            'ProtocolVersion': GLib.Variant('i', 0)}[prop]

connection.register_object('/StatusNotifierWatcher', Gio.DBusNodeInfo.new_for_xml(xml).interfaces[0],
                           called, property_value, None)
reply = connection.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus',
                             'org.freedesktop.DBus', 'RequestName', GLib.Variant('(su)', (name, 4)),
                             None, Gio.DBusCallFlags.NONE, 3000, None)
assert reply.unpack()[0] == 1, 'private watcher name already owned'
args.record.write_text(json.dumps(record))
GLib.MainLoop().run()
