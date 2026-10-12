"""DEC-071: private Secret Service; no connection to the user's keyring."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def serve():
    from gi.repository import Gio, GLib

    xml = """<node>
    <interface name="org.freedesktop.Secret.Service">
      <method name="OpenSession"><arg type="s" direction="in"/><arg type="v" direction="in"/>
        <arg type="v" direction="out"/><arg type="o" direction="out"/></method>
      <method name="SearchItems"><arg type="a{ss}" direction="in"/>
        <arg type="ao" direction="out"/><arg type="ao" direction="out"/></method>
      <method name="ReadAlias"><arg type="s" direction="in"/><arg type="o" direction="out"/></method>
    </interface>
    <interface name="org.freedesktop.Secret.Session"><method name="Close"/></interface>
    <interface name="org.freedesktop.Secret.Item">
      <method name="GetSecret"><arg type="o" direction="in"/>
        <arg type="(oayays)" direction="out"/></method>
      <method name="Delete"><arg type="o" direction="out"/></method>
    </interface>
    <interface name="org.freedesktop.Secret.Collection">
      <method name="CreateItem"><arg type="a{sv}" direction="in"/>
        <arg type="(oayays)" direction="in"/><arg type="b" direction="in"/>
        <arg type="o" direction="out"/><arg type="o" direction="out"/></method>
    </interface>
    <interface name="org.freedesktop.DBus.Properties">
      <method name="Get"><arg type="s" direction="in"/><arg type="s" direction="in"/>
        <arg type="v" direction="out"/></method>
    </interface>
    <interface name="org.mirage.CredentialFixture.Control">
      <method name="SetMode"><arg type="s" direction="in"/></method>
    </interface></node>"""
    node = Gio.DBusNodeInfo.new_for_xml(xml)
    connection = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    connection.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
                         "org.freedesktop.DBus", "RequestName",
                         GLib.Variant("(su)", ("org.freedesktop.secrets", 0)),
                         GLib.VariantType.new("(u)"), Gio.DBusCallFlags.NONE, 2000, None)
    session = "/org/freedesktop/secrets/session/fixture"
    collection = "/org/freedesktop/secrets/collection/login"
    volatile = "/org/freedesktop/secrets/collection/session"
    mode = "default_session"
    items = {}
    serial = 0

    def method(conn, sender, path, interface, name, parameters, invocation):
        nonlocal serial, mode
        args = parameters.unpack()
        if name == "OpenSession":
            result = GLib.Variant("(vo)", (GLib.Variant("s", ""), session))
        elif name == "Close":
            result = GLib.Variant("()", ())
        elif name == "SetMode":
            mode = args[0]
            if mode == "drop_session":
                for item in list(items):
                    if item.startswith(volatile + "/"):
                        del items[item]
            result = GLib.Variant("()", ())
        elif name == "Get" and args[1] == "Collections":
            result = GLib.Variant("(v)", (GLib.Variant("ao", [volatile] if mode == "no_login" else [collection, volatile]),))
        elif name == "Get" and args[1] == "Locked":
            result = GLib.Variant("(v)", (GLib.Variant("b", path == collection and mode == "locked_login"),))
        elif name == "ReadAlias":
            result = GLib.Variant("(o)", (collection if mode == "default_persistent" else volatile,))
        elif name == "SearchItems":
            ref = args[0].get("reference", "")
            matches = [p for p, (r, _) in items.items() if r == ref]
            if ref == "b" * 32:
                result = GLib.Variant("(aoao)", ([], [collection + "/locked"]))
            elif ref == "c" * 32:
                result = GLib.Variant("(aoao)", ([collection + "/duplicate1"],
                                               [collection + "/duplicate2"]))
            else:
                result = GLib.Variant("(aoao)", (matches, []))
        elif name == "CreateItem":
            props, secret, _ = args
            if path != collection or mode in ["no_login", "locked_login"]:
                invocation.return_dbus_error("org.freedesktop.Secret.Error.IsLocked", "fixture persistence guard")
                return
            ref = props["org.freedesktop.Secret.Item.Attributes"]["reference"]
            matches = [p for p, (r, _) in items.items() if r == ref]
            if matches:
                target = matches[0]
            else:
                serial += 1
                target = collection + "/item" + str(serial)
                connection.register_object(target, node.lookup_interface(
                    "org.freedesktop.Secret.Item"), method, None, None)
            items[target] = (ref, secret[2])
            result = GLib.Variant("(oo)", (target, "/"))
        elif name == "GetSecret" and path in items:
            result = GLib.Variant("((oayays))", ((session, [], items[path][1], "text/plain"),))
        elif name == "Delete" and path in items:
            del items[path]
            result = GLib.Variant("(o)", ("/",))
        else:
            invocation.return_dbus_error("org.freedesktop.Secret.Error.NoSuchObject", "fixture")
            return
        invocation.return_value(result)

    for path, interface in [("/org/freedesktop/secrets", "Service"),
                            (session, "Session"), (collection, "Collection")]:
        connection.register_object(path, node.lookup_interface(
            "org.freedesktop.Secret." + interface), method, None, None)
        connection.register_object(path, node.lookup_interface(
            "org.freedesktop.DBus.Properties"), method, None, None)
    connection.register_object("/org/mirage/CredentialFixture", node.lookup_interface(
        "org.mirage.CredentialFixture.Control"), method, None, None)
    print("READY", flush=True)
    GLib.MainLoop().run()


def run(binary):
    with tempfile.TemporaryDirectory(prefix="mirage-credential-fixture-") as temporary:
        env = dict(os.environ, XDG_DATA_HOME=temporary, XDG_CONFIG_HOME=temporary,
                   XDG_STATE_HOME=temporary)
        bus_config = Path(temporary) / "bus.conf"
        bus_config.write_text("<busconfig><type>session</type><listen>unix:tmpdir=" + temporary +
                              "</listen><auth>EXTERNAL</auth><policy context='default'>"
                              "<allow own='*'/><allow send_destination='*'/>"
                              "<allow receive_sender='*'/></policy></busconfig>")
        # No service directories: unavailable-service tests must not auto-start
        # the host's real GNOME Keyring daemon on this private bus.
        bus = subprocess.Popen(["dbus-daemon", "--config-file=" + str(bus_config),
                                "--nofork", "--print-address=1"],
                               stdout=subprocess.PIPE, text=True, env=env)
        service = None
        try:
            env["DBUS_SESSION_BUS_ADDRESS"] = bus.stdout.readline().strip()
            assert env["DBUS_SESSION_BUS_ADDRESS"].startswith("unix:"), "private bus failed"
            service = subprocess.Popen([sys.executable, str(Path(__file__).resolve()), "--serve"],
                                       stdout=subprocess.PIPE, text=True, env=env)
            assert service.stdout.readline().strip() == "READY", "private service failed"
            from gi.repository import Gio, GLib
            control = Gio.DBusConnection.new_for_address_sync(
                env["DBUS_SESSION_BUS_ADDRESS"],
                Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION,
                None, None)

            def set_mode(mode):
                control.call_sync("org.freedesktop.secrets", "/org/mirage/CredentialFixture",
                                  "org.mirage.CredentialFixture.Control", "SetMode",
                                  GLib.Variant("(s)", (mode,)), GLib.VariantType.new("()"),
                                  Gio.DBusCallFlags.NONE, 2000, None)

            def probe(mode):
                subprocess.run([binary, "--fixture-" + mode], env=env, check=True, timeout=20)

            probe("write")  # Default alias is volatile: must choose login.
            probe("read")
            set_mode("drop_session")
            probe("read")  # Separate process after volatile collection clearing.
            probe("guards")
            probe("delete")
            for mode in ["locked_login", "no_login"]:
                set_mode(mode)
                probe("write-refused")
            set_mode("default_persistent")
            probe("write")
            probe("read")
            probe("delete")
            service.terminate()
            service.wait(timeout=5)
            subprocess.run([binary, "--fixture-unavailable"], env=env, check=True, timeout=20)
            print("credential fixture: persistent/default login, volatile clearing, cross-process restart, locked/missing/duplicate/service guards and cleanup passed")
        finally:
            if service is not None and service.poll() is None:
                service.terminate()
                service.wait(timeout=5)
            bus.terminate()
            bus.wait(timeout=5)


if __name__ == "__main__":
    if sys.argv[1] == "--serve":
        serve()
    else:
        run(str(Path(sys.argv[1]).resolve()))
