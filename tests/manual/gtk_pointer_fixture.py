"""Private GTK3 test application for M7-09; no model or Mirage input code.

Real GtkScrolledWindow adjustments and GtkTreeView's built-in row drag own the
effects. Stdout is a bounded test-only state oracle, separate from screenshots.
"""
import json
import os
import sys

os.environ['GDK_BACKEND'] = 'x11'
os.environ['NO_AT_BRIDGE'] = '1'
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('GdkX11', '3.0')
from gi.repository import Gtk, GLib, GdkX11, Gdk

mode = sys.argv[1]
operator_mode = len(sys.argv) == 3 and sys.argv[2] == "--operator"
operator_release = False
operator_press = False
Gtk.Settings.get_default().set_property('gtk-enable-animations', False)
window = Gtk.Window(title='Ready')
window.set_default_size(620, 360)
window.move(10, 10)
window.connect('destroy', Gtk.main_quit)
box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL, spacing=20)
box.set_border_width(12)
window.add(box)
store = Gtk.ListStore(str)
initial = ['Alpha', 'Bravo', 'Charlie', 'Delta', 'Echo']
for name in initial:
    store.append([name])
tree = Gtk.TreeView(model=store)
tree.set_headers_visible(False)
tree.set_reorderable(mode.startswith('native-') and mode != 'native-blocked')
renderer = Gtk.CellRendererText()
renderer.set_property('height', 48)
column = Gtk.TreeViewColumn('Item', renderer, text=0)
tree.append_column(column)
tree.set_size_request(80, 280)
paned = Gtk.Paned(orientation=Gtk.Orientation.HORIZONTAL)
paned.set_wide_handle(True)
paned.set_size_request(280, 280)
paned.pack1(tree, True, False)
second = Gtk.EventBox() if operator_mode else Gtk.Label(label='Resize panel')
if operator_mode:
    start_label = Gtk.Label(label='Mirage M7-09\n点击这里开始\n松开后请勿操作键鼠')
    start_label.set_line_wrap(True)
    second.add(start_label)
second.set_size_request(80, 280)
paned.pack2(second, True, False)
paned.set_position(130)
paned.set_sensitive(mode != 'splitter-blocked')
box.pack_start(paned, True, True, 0)
scroll = Gtk.ScrolledWindow()
scroll.set_policy(Gtk.PolicyType.ALWAYS, Gtk.PolicyType.ALWAYS)
scroll.set_size_request(300, 300)
text = Gtk.TextView()
text.set_editable(False)
text.set_cursor_visible(False)
text.set_wrap_mode(Gtk.WrapMode.NONE)
text.get_buffer().set_text('\n'.join(
    f'Line {i:03d}: ' + 'public scroll content ' * 12 for i in range(100)))
scroll.add(text)
box.pack_start(scroll, True, True, 0)
window.show_all()
window.present()
trace = []
row_activations = 0
click_events = {'presses': 0, 'releases': 0, 'doubles': 0, 'triples': 0}


def activated(*_):
    global row_activations
    row_activations += 1


tree.connect('row-activated', activated)


def record_event(_widget, event):
    for key, kind in [('presses', Gdk.EventType.BUTTON_PRESS),
                      ('releases', Gdk.EventType.BUTTON_RELEASE),
                      ('doubles', Gdk.EventType.DOUBLE_BUTTON_PRESS),
                      ('triples', Gdk.EventType.TRIPLE_BUTTON_PRESS)]:
        if event.type == kind:
            click_events[key] += 1
    if len(trace) < (12 if mode == 'click-sequence' else 24):
        trace.append({'type': str(event.type), 'x': round(event.x),
                      'y': round(event.y), 'time': event.time})
    return False


for event_name in ('button-press-event', 'button-release-event', 'motion-notify-event'):
    tree.connect(event_name, record_event)
drag_signals = {'begin': 0, 'drop': 0, 'end': 0, 'failed': 0, 'data': 0}


def record_drag(key):
    def handler(*_):
        drag_signals[key] += 1
        return False
    return handler


for signal, key in [('drag-begin', 'begin'), ('drag-drop', 'drop'), ('drag-end', 'end'),
                    ('drag-failed', 'failed'), ('drag-data-get', 'data')]:
    tree.connect(signal, record_drag(key))


def order():
    return [row[0] for row in store]


def update_title(*_):
    changed = order() != initial
    x = scroll.get_hadjustment().get_value()
    y = scroll.get_vadjustment().get_value()
    title = ('Dropped' if changed else 'Resized' if paned.get_position() >= 170
             else 'Scrolled' if x > 0 and y > 0
             and mode != 'scroll-blocked' else 'Ready')
    window.set_title(('Mirage M7-09 | ' if operator_mode else '') + title)


for signal in ('rows-reordered', 'row-inserted', 'row-deleted'):
    store.connect(signal, update_title)
paned.connect('notify::position', update_title)
for adjustment in (scroll.get_hadjustment(), scroll.get_vadjustment()):
    adjustment.connect('value-changed', update_title)


def state():
    # Coordinates come from native widget allocation, not a model answer.
    scale = window.get_window().get_scale_factor()
    rows = []
    for index in range(len(initial)):
        area = tree.get_cell_area(Gtk.TreePath.new_from_indices([index]), column)
        x, y = tree.convert_bin_window_to_widget_coords(area.x, area.y)
        x, y = tree.translate_coordinates(window, x, y)
        rows.append({'x': (x + 8) * scale, 'y': (y + 8) * scale,
                     'width': 140 * scale, 'height': 28 * scale})
    x, y = scroll.translate_coordinates(window, 0, 0)
    _, window_x, window_y = window.get_window().get_origin()
    handle = paned.get_handle_window()
    _, handle_x, handle_y = handle.get_origin()
    return {'window': window.get_window().get_xid(), 'rows': rows,
            'row_activations': row_activations, 'click_events': click_events,
            'handle': {'x': (handle_x - window_x) * scale,
                       'y': (handle_y - window_y) * scale,
                       'width': handle.get_width() * scale,
                       'height': handle.get_height() * scale},
            'position': paned.get_position(),
            'scroll_target': {'x': (x + 30) * scale, 'y': (y + 30) * scale,
                              'width': 80 * scale, 'height': 40 * scale},
            'scale': scale, 'pixel_width': window.get_window().get_width() * scale,
            'pixel_height': window.get_window().get_height() * scale,
            'operator_release': operator_release, 'operator_mode': operator_mode,
            'operator_target': {'x': second.translate_coordinates(window, 0, 0)[0] * scale,
                                'y': second.translate_coordinates(window, 0, 0)[1] * scale,
                                'width': second.get_allocated_width() * scale,
                                'height': second.get_allocated_height() * scale},
            'horizontal': scroll.get_hadjustment().get_value(),
            'vertical': scroll.get_vadjustment().get_value(), 'order': order(),
            'title': window.get_title(), 'events': trace, 'drag_signals': drag_signals,
            'gtk': [Gtk.MAJOR_VERSION, Gtk.MINOR_VERSION,
                                                Gtk.MICRO_VERSION]}


def operator_pressed(_widget, event):
    global operator_press
    if event.button == 1 and not operator_release:
        operator_press = True
    return False


def operator_released(_widget, event):
    global operator_release
    if (event.button != 1 or not operator_press or operator_release
            or not (0 <= event.x < second.get_allocated_width()
                    and 0 <= event.y < second.get_allocated_height())):
        return False
    operator_release = True
    start_label.set_text('正在验收\n请勿操作键鼠')
    clock = window.get_frame_clock()
    def painted(_clock):
        clock.disconnect(handler)
        window.get_display().sync()
        payload = state()
        payload["kind"] = "operator_release"
        print(json.dumps(payload), flush=True)
    handler = clock.connect('after-paint', painted)
    window.queue_draw()
    return False


if operator_mode:
    second.connect('button-press-event', operator_pressed)
    second.connect('button-release-event', operator_released)


def command(source, condition):
    line = source.readline(4097)
    if not line or len(line) > 4096 or not line.endswith('\n'):
        Gtk.main_quit()
        return False
    request = json.loads(line)
    if request['op'] == 'quit':
        Gtk.main_quit()
        return False
    if request['op'] == 'repaint':
        renderer.set_property('foreground', '#f00000')
        clock = window.get_frame_clock()
        def painted(_clock):
            clock.disconnect(handler)
            window.get_display().sync()
            payload = state()
            payload['kind'] = 'state'
            print(json.dumps(payload), flush=True)
        handler = clock.connect('after-paint', painted)
        tree.queue_draw()
        return True
    if request['op'] != 'state':
        raise ValueError('unknown test command')
    payload = state()
    payload["kind"] = "state"
    print(json.dumps(payload), flush=True)
    return True


def ready():
    if mode == 'scroll-blocked':
        for adjustment in (scroll.get_hadjustment(), scroll.get_vadjustment()):
            adjustment.set_value(adjustment.get_upper() - adjustment.get_page_size())
    update_title()
    clock = window.get_frame_clock()
    def painted(_clock):
        clock.disconnect(paint_handler)
        window.get_display().sync()
        payload = state()
        payload["kind"] = "ready"
        print(json.dumps(payload), flush=True)
        GLib.io_add_watch(sys.stdin, GLib.PRIORITY_LOW, GLib.IO_IN | GLib.IO_HUP, command)
    paint_handler = clock.connect('after-paint', painted)
    window.queue_draw()
    return False


# GTK's own external GUI loop, not a Mirage worker/timer implementation.
GLib.idle_add(ready, priority=GLib.PRIORITY_LOW)
Gtk.main()
