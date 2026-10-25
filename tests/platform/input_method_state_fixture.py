"""DEC-110/M7-28: private protocol services, never the user's input method."""
import json
import os
import socket
from pathlib import Path
import subprocess
import sys
import tempfile


def serve(mode):
    from gi.repository import Gio, GLib
    bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
    xml = """<node><interface name="org.fcitx.Fcitx.Controller1">
      <method name="CurrentInputMethod"><arg type="s" direction="out"/></method>
      <method name="State"><arg type="i" direction="out"/></method>
    </interface><interface name="org.freedesktop.DBus.Properties">
      <method name="Get"><arg type="s" direction="in"/><arg type="s" direction="in"/>
        <arg type="v" direction="out"/></method>
    </interface></node>"""
    node = Gio.DBusNodeInfo.new_for_xml(xml)
    calls = 0

    def method(conn, sender, path, iface, name, args, invocation):
        nonlocal calls
        calls += 1
        if mode == "hang":
            # Keep the invocation alive until the client cancels or times out.
            pending.append(invocation)
            return
        if mode == "error":
            invocation.return_dbus_error("org.mirage.Fixture.Error", "private-address-must-not-leak")
            return
        if name == "CurrentInputMethod":
            engine = "keyboard-us" if mode == "english" else "pinyin"
            if mode == "large":
                engine = "x" * 257
            result = GLib.Variant("(s)", (engine,))
        elif name == "State":
            state = {"inactive": 1, "no_context": 0, "invalid": 9}.get(mode, 2)
            result = GLib.Variant("(i)", (state,))
            if mode == "vanish":
                conn.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "ReleaseName",
                               GLib.Variant("(s)", ("org.fcitx.Fcitx5",)), None,
                               Gio.DBusCallFlags.NONE, 1000, None)
        elif name == "Get":
            interface, prop = args.unpack()
            assert interface == "org.freedesktop.IBus"
            if prop == "GlobalEngine":
                if mode == "invalid":
                    value = GLib.Variant("s", "invalid-engine")
                else:
                    # GlobalEngine has D-Bus property type v; Properties.Get
                    # adds another v. This matches the real IBus wire shape.
                    desc = GLib.Variant("(sa{sv}ssssssssussssssss)", (
                        "IBusEngineDesc", {}, "libpinyin", "pinyin", "desc", "zh", "lgpl",
                        "author", "", "us", 80, "", "", "", "", "", "", "", ""))
                    value = GLib.Variant("v", desc)
            elif prop == "CurrentInputContext":
                value = GLib.Variant("o", "/org/freedesktop/IBus/InputContext_42")
                if mode == "ibus_vanish":
                    conn.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                   "org.freedesktop.DBus", "ReleaseName",
                                   GLib.Variant("(s)", ("org.freedesktop.IBus",)), None,
                                   Gio.DBusCallFlags.NONE, 1000, None)
            else:
                raise AssertionError(prop)
            result = GLib.Variant("(v)", (value,))
        else:
            raise AssertionError("Unexpected mutating method: " + name)
        invocation.return_value(result)

    pending = []
    bus.register_object("/controller", node.lookup_interface("org.fcitx.Fcitx.Controller1"), method, None, None)
    bus.register_object("/org/freedesktop/IBus", node.lookup_interface("org.freedesktop.DBus.Properties"), method, None, None)
    for name in ["org.fcitx.Fcitx5", "org.freedesktop.IBus"]:
        bus.call_sync("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                      "RequestName", GLib.Variant("(su)", (name, 0)), None,
                      Gio.DBusCallFlags.NONE, 1000, None)
    print("ready", flush=True)
    GLib.MainLoop().run()


def run(binary):
    with tempfile.TemporaryDirectory(prefix="mirage-ime-") as temp:
        env = os.environ.copy()
        env["XDG_CONFIG_HOME"] = temp
        env["IBUS_ADDRESS_FILE"] = str(Path(temp) / "ibus-address")
        bus = subprocess.Popen(["dbus-daemon", "--session", "--nofork", "--print-address=1"],
                               stdout=subprocess.PIPE, text=True)
        address = bus.stdout.readline().strip()
        env["DBUS_SESSION_BUS_ADDRESS"] = address
        env["IBUS_ADDRESS"] = address
        service = None

        def probe(option=None, overrides=None):
            result = subprocess.run([binary] + ([option] if option else []),
                                    env=env | (overrides or {}), capture_output=True, text=True, timeout=8)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            if option in ("--cancel", "--timeout"):
                return None
            values = json.loads(result.stdout)
            assert "private-address-must-not-leak" not in result.stdout
            assert values["schema"] == 1 and "observation_scope" not in values
            assert "environment_epoch" not in values
            return {v["backend"]: v for v in values["input_methods"]}

        def unknown(field, reason=None):
            assert field["status"] == "unknown" and field["value"] is None, field
            assert field["reason"] and (reason is None or field["reason"] == reason), field

        def known(field, value):
            assert field["status"] == "known" and field["value"] == value, field
            assert field["source"] and field["captured_at_ms"] > 0 and not field["reason"], field

        try:
            absent = probe()
            unknown(absent["fcitx5"]["engine"], "service_unavailable")
            unknown(absent["ibus"]["engine"], "service_unavailable")
            for mode in ["normal", "english", "inactive", "no_context", "invalid", "large", "error", "vanish", "ibus_vanish", "hang"]:
                service = subprocess.Popen([sys.executable, __file__, "--serve", mode], env=env,
                                           stdout=subprocess.PIPE, text=True)
                assert service.stdout.readline().strip() == "ready"
                try:
                    if mode == "hang":
                        probe("--timeout")
                        probe("--cancel")
                        continue
                    states = probe()
                    fcitx, ibus = states["fcitx5"], states["ibus"]
                    for item in states.values():
                        for name in ["conversion_mode", "layout", "focused_window", "preedit_active", "candidates_visible"]:
                            unknown(item[name])
                    if mode == "invalid":
                        unknown(fcitx["activation"], "invalid_reply")
                        unknown(ibus["engine"], "invalid_reply")
                    elif mode == "large":
                        unknown(fcitx["engine"], "invalid_reply")
                    elif mode == "error":
                        unknown(fcitx["engine"], "query_failed")
                        unknown(ibus["engine"], "query_failed")
                    elif mode == "vanish":
                        unknown(fcitx["engine"], "service_changed")
                    elif mode == "ibus_vanish":
                        unknown(ibus["engine"], "service_changed")
                        unknown(ibus["focus_context"], "service_changed")
                    else:
                        known(fcitx["engine"], "keyboard-us" if mode == "english" else "pinyin")
                        known(fcitx["activation"], {"inactive": "inactive", "no_context": "no_context"}.get(mode, "active"))
                        known(ibus["engine"], "libpinyin")
                        known(ibus["focus_context"], "/org/freedesktop/IBus/InputContext_42")
                    if mode == "normal":
                        # SDK-selected file, not directory scanning or address caching.
                        Path(env["IBUS_ADDRESS_FILE"]).write_text("IBUS_ADDRESS=" + address + "\n")
                        without_env = env.copy()
                        without_env.pop("IBUS_ADDRESS")
                        result = subprocess.run([binary], env=without_env, check=True, capture_output=True, text=True, timeout=8)
                        state = json.loads(result.stdout)["input_methods"][1]
                        if os.environ.get("MIRAGE_TEST_IBUS_DISCOVERY") == "1":
                            known(state["engine"], "libpinyin")
                        # Network addresses are never contacted.
                        unsupported = probe(overrides={"IBUS_ADDRESS": "tcp:host=127.0.0.1,port=1"})
                        unknown(unsupported["ibus"]["engine"], "service_unavailable")
                finally:
                    service.terminate()
                    service.wait(timeout=5)
                    service = None
            # Previously working state must not survive service termination.
            unknown(probe()["ibus"]["engine"], "service_unavailable")
            # A connected unix socket that never completes D-Bus authentication
            # must also converge via the same cancellation/deadline boundary.
            with socket.socket(socket.AF_UNIX) as stalled:
                path = str(Path(temp) / "stalled-auth")
                stalled.bind(path)
                stalled.listen(4)
                probe("--timeout", {"DBUS_SESSION_BUS_ADDRESS": "unix:path=" + path})
                probe("--cancel", {"DBUS_SESSION_BUS_ADDRESS": "unix:path=" + path})
            print("input method fixture: state/unknown/budget/cancel/service-generation/transport guards passed")
        finally:
            if service and service.poll() is None:
                service.terminate()
                service.wait(timeout=5)
            bus.terminate()
            bus.wait(timeout=5)


if __name__ == "__main__":
    if sys.argv[1] == "--serve":
        serve(sys.argv[2])
    else:
        run(str(Path(sys.argv[1]).resolve()))
