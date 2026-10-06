"""Private minimal ICCCM/EWMH peer for minimize/reopen acceptance, not a desktop WM."""
import argparse
import json
from pathlib import Path
from Xlib import display, X, Xatom, Xutil

p = argparse.ArgumentParser()
p.add_argument('--record', type=Path, required=True)
args = p.parse_args()
d = display.Display()
root = d.screen().root
root.change_attributes(event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask)
atom = {name: d.intern_atom(name) for name in ['WM_STATE', 'WM_CHANGE_STATE', '_NET_ACTIVE_WINDOW',
        '_NET_CLIENT_LIST', '_NET_SUPPORTED', '_NET_WM_STATE', '_NET_WM_STATE_HIDDEN', '_NET_SUPPORTING_WM_CHECK']}
check = root.create_window(0, 0, 1, 1, 0, X.CopyFromParent)
root.change_property(atom['_NET_SUPPORTING_WM_CHECK'], Xatom.WINDOW, 32, [check.id])
check.change_property(atom['_NET_SUPPORTING_WM_CHECK'], Xatom.WINDOW, 32, [check.id])
root.change_property(atom['_NET_SUPPORTED'], Xatom.ATOM, 32,
                     [atom[name] for name in ['_NET_ACTIVE_WINDOW', '_NET_CLIENT_LIST', '_NET_WM_STATE', '_NET_WM_STATE_HIDDEN']])
clients = []
record = dict(ready=True, minimized=0, activated=0)

def save():
    args.record.write_text(json.dumps(record))

def publish():
    root.change_property(atom['_NET_CLIENT_LIST'], Xatom.WINDOW, 32, clients)

def restore(w):
    w.change_property(atom['WM_STATE'], atom['WM_STATE'], 32, [Xutil.NormalState, 0])
    w.change_property(atom['_NET_WM_STATE'], Xatom.ATOM, 32, [])
    w.map()
    w.configure(stack_mode=X.Above)
    w.set_input_focus(X.RevertToParent, X.CurrentTime)
    root.change_property(atom['_NET_ACTIVE_WINDOW'], Xatom.WINDOW, 32, [w.id])

publish(); d.sync(); save()
while True:
    e = d.next_event()
    if e.type == X.MapRequest:
        if e.window.id not in clients: clients.append(e.window.id)
        publish(); restore(e.window)
    elif e.type == X.ConfigureRequest:
        values = {}
        for bit, key in [(X.CWX, 'x'), (X.CWY, 'y'), (X.CWWidth, 'width'), (X.CWHeight, 'height'),
                         (X.CWBorderWidth, 'border_width'), (X.CWStackMode, 'stack_mode')]:
            if e.value_mask & bit: values[key] = getattr(e, key)
        if values: e.window.configure(**values)
    elif e.type == X.ClientMessage:
        if e.client_type == atom['WM_CHANGE_STATE'] and e.data[1][0] == Xutil.IconicState:
            e.window.change_property(atom['WM_STATE'], atom['WM_STATE'], 32, [Xutil.IconicState, 0])
            e.window.change_property(atom['_NET_WM_STATE'], Xatom.ATOM, 32, [atom['_NET_WM_STATE_HIDDEN']])
            e.window.unmap(); record['minimized'] += 1; save()
        elif e.client_type == atom['_NET_ACTIVE_WINDOW'] and e.window.id in clients:
            restore(e.window); record['activated'] += 1; save()
    elif e.type == X.DestroyNotify and e.window.id in clients:
        clients.remove(e.window.id); publish()
    d.flush()
