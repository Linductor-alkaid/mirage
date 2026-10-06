#include "../../common/src/frontend_activation.hpp"
#if defined(MIRAGE_LINUX_X11)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <memory>
#endif

namespace mirage::platform::detail {
bool activate_frontend(std::int64_t pid, std::string &diagnostic) {
#if defined(MIRAGE_LINUX_X11)
    // Finite Runtime-owned work. A dedicated connection avoids calling GLFW
    // from its IPC worker or depending on a minimized UI's paint loop.
    const auto close = [](Display *display) { XCloseDisplay(display); };
    std::unique_ptr<Display, decltype(close)> connection(XOpenDisplay(nullptr), close);
    if (!connection) {
        diagnostic = "cannot activate frontend: X/XWayland connection unavailable";
        return false;
    }
    Display *display = connection.get();
    const Window root = DefaultRootWindow(display);
    const Atom clients = XInternAtom(display, "_NET_CLIENT_LIST", True);
    const Atom owner = XInternAtom(display, "_NET_WM_PID", True);
    if (owner == None)
        return true; // child is still initializing; its first map handles startup
    Window target = None;
    const auto belongs = [&](Window window) {
        Atom type = None;
        int format = 0;
        unsigned long count = 0, rest = 0;
        unsigned char *data = nullptr;
        const int code = XGetWindowProperty(display, window, owner, 0, 1, False, XA_CARDINAL, &type,
                                            &format, &count, &rest, &data);
        const bool matches =
            code == Success && type == XA_CARDINAL && format == 32 && count == 1 && data &&
            *reinterpret_cast<unsigned long *>(data) == static_cast<unsigned long>(pid);
        if (data)
            XFree(data);
        return matches;
    };
    // EWMH list includes iconified clients even if the WM reparents/unmaps them.
    Atom type = None;
    int format = 0;
    unsigned long count = 0, rest = 0;
    unsigned char *data = nullptr;
    if (clients != None &&
        XGetWindowProperty(display, root, clients, 0, 256, False, XA_WINDOW, &type, &format, &count,
                           &rest, &data) == Success &&
        type == XA_WINDOW && format == 32 && data) {
        if (rest != 0) {
            XFree(data);
            diagnostic = "frontend activation window list exceeds 256 entries";
            return false;
        }
        const auto *windows = reinterpret_cast<Window *>(data);
        for (unsigned long index = 0; index < count; ++index)
            if (belongs(windows[index])) {
                target = windows[index];
                break;
            }
    }
    if (data)
        XFree(data);
    if (target == None) {
        // WM-less Xvfb fallback; do not search unrelated descendants or PIDs.
        Window root_return = None, parent = None, *children = nullptr;
        unsigned int total = 0;
        if (XQueryTree(display, root, &root_return, &parent, &children, &total)) {
            if (total > 256) {
                if (children)
                    XFree(children);
                diagnostic = "frontend activation root exceeds 256 windows";
                return false;
            }
            for (unsigned int index = 0; index < total; ++index)
                if (belongs(children[index])) {
                    target = children[index];
                    break;
                }
            if (children)
                XFree(children);
        }
    }
    if (target == None)
        return true; // owned child has not mapped yet; no other window is activated
    const Atom active = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    if (active != None && clients != None) {
        XEvent request{};
        request.xclient.type = ClientMessage;
        request.xclient.window = target;
        request.xclient.message_type = active;
        request.xclient.format = 32;
        // Product entry/tray is an explicit user activation, not an Agent focus steal.
        request.xclient.data.l[0] = 2;
        request.xclient.data.l[1] = CurrentTime;
        if (!XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask,
                        &request)) {
            diagnostic = "frontend activation request was refused";
            return false;
        }
    } else {
        XMapRaised(display, target);
        XSetInputFocus(display, target, RevertToParent, CurrentTime);
    }
    XFlush(display);
    return true; // WM acknowledgement remains observable window state, not a paint ACK
#else
    (void)pid;
    diagnostic = "frontend activation requires the X/XWayland platform backend";
    return false;
#endif
}
} // namespace mirage::platform::detail
