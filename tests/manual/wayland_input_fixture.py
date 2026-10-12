"""Public Wayland-only fixture. State contains fixture results, never user content."""
import json
import os
from pathlib import Path
import sys
os.environ['GDK_BACKEND'] = 'wayland'
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk
output = Path(sys.argv[1])
state = {'changed': '', 'submitted': '', 'clicks': 0, 'scrolls': 0, 'native_wayland': True,
         'keys': []}
def save():
    temporary = output.with_suffix('.tmp')
    temporary.write_text(json.dumps(state, ensure_ascii=False))
    temporary.replace(output)
css = Gtk.CssProvider()
css.load_from_data(b'window {background:#ffffff;} entry {background:#1450f0;color:white;font-size:28px;min-height:90px;} button {background:#14c850;color:black;min-height:100px;} label {color:#000000;font-size:28px;}')
Gtk.StyleContext.add_provider_for_screen(Gdk.Screen.get_default(), css, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
window = Gtk.Window(title='Mirage Wayland Input Acceptance')
window.set_default_size(1000, 700)
window.fullscreen()
box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=30)
box.set_border_width(80)
box.pack_start(Gtk.Label(label='MIRAGE WAYLAND INPUT TEST'), False, False, 0)
box.pack_start(Gtk.Label(label='Only this public test window is controlled.'), False, False, 0)
# GTK's own animation keeps this public fixture producing compositor damage.
# No application scheduler or background capture worker is introduced.
spinner = Gtk.Spinner()
spinner.set_size_request(24, 24)
spinner.start()
box.pack_start(spinner, False, False, 0)
entry = Gtk.Entry()
entry.set_property('im-module', 'simple')
entry.set_placeholder_text('')
entry.connect('changed', lambda widget: (state.update(changed=widget.get_text()), save()))
entry.connect('activate', lambda widget: (state.update(submitted=widget.get_text()), save()))
box.pack_start(entry, False, False, 0)
button = Gtk.Button(label='CLICK TEST')
button.connect('clicked', lambda _: (state.update(clicks=state['clicks'] + 1), save()))
button.add_events(Gdk.EventMask.SCROLL_MASK | Gdk.EventMask.SMOOTH_SCROLL_MASK)
button.connect('scroll-event', lambda *_: (state.update(scrolls=state['scrolls'] + 1), save(), True)[-1])
box.pack_start(button, False, False, 0)
events = Gtk.EventBox()
events.add(Gtk.Label(label='SCROLL TEST'))
events.add_events(Gdk.EventMask.SCROLL_MASK | Gdk.EventMask.SMOOTH_SCROLL_MASK)
events.connect('scroll-event', lambda *_: (state.update(scrolls=state['scrolls'] + 1), save(), True)[-1])
box.pack_start(events, True, True, 0)
window.add(box)
def key_event(widget, event):
    if len(state['keys']) < 256:
        state['keys'].append([Gdk.keyval_name(event.keyval), int(event.type)])
        save()
    return False
window.connect('key-press-event', key_event)
window.connect('key-release-event', key_event)
entry.connect('key-press-event', key_event)
entry.connect('key-release-event', key_event)
window.connect('destroy', Gtk.main_quit)
window.show_all()
save()
Gtk.main()
