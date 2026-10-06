#include "../../platform/common/src/frontend_activation.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <string>
#include <unistd.h>
#include <vector>

int main() {
    mirage::testing::XvfbDisplay server(mirage::testing::find_xvfb(), "800x600x24");
    ::setenv("DISPLAY", server.display_name().c_str(), 1);
    Display *display = XOpenDisplay(nullptr);
    MIRAGE_CHECK(display);
    if (!display)
        return mirage::testing::finish("frontend_activation_test");
    const auto root = DefaultRootWindow(display);
    const auto owned = XCreateSimpleWindow(display, root, 0, 0, 200, 100, 0, 0, 0);
    const auto other = XCreateSimpleWindow(display, root, 200, 0, 200, 100, 0, 0, 0);
    const auto owner = XInternAtom(display, "_NET_WM_PID", False);
    unsigned long pid = static_cast<unsigned long>(::getpid()), unrelated = pid + 1;
    XChangeProperty(display, owned, owner, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&pid), 1);
    XChangeProperty(display, other, owner, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&unrelated), 1);
    XSync(display, False);
    std::string diagnostic;
    MIRAGE_CHECK(mirage::platform::detail::activate_frontend(pid, diagnostic));
    XSync(display, False);
    XWindowAttributes attributes{};
    MIRAGE_CHECK(XGetWindowAttributes(display, owned, &attributes) &&
                 attributes.map_state == IsViewable);
    MIRAGE_CHECK(XGetWindowAttributes(display, other, &attributes) &&
                 attributes.map_state == IsUnmapped);

    // A private EWMH peer observes the restore request without requiring any
    // frontend rendering/event consumption. Only the exact owner PID matches.
    const auto clients = XInternAtom(display, "_NET_CLIENT_LIST", False);
    const auto active = XInternAtom(display, "_NET_ACTIVE_WINDOW", False);
    const Window ids[] = {other, owned};
    XChangeProperty(display, root, clients, XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(ids), 2);
    XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(display, False);
    MIRAGE_CHECK(mirage::platform::detail::activate_frontend(pid, diagnostic));
    XSync(display, False);
    XEvent request{};
    MIRAGE_CHECK(XCheckTypedEvent(display, ClientMessage, &request));
    MIRAGE_CHECK(request.xclient.window == owned && request.xclient.message_type == active &&
                 request.xclient.data.l[0] == 2);
    MIRAGE_CHECK(mirage::platform::detail::activate_frontend(pid + 2, diagnostic));
    XSync(display, False);
    MIRAGE_CHECK(!XCheckTypedEvent(display, ClientMessage, &request));

    std::vector<Window> oversized(257, owned);
    XChangeProperty(display, root, clients, XA_WINDOW, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(oversized.data()),
                    static_cast<int>(oversized.size()));
    XSync(display, False);
    MIRAGE_CHECK(!mirage::platform::detail::activate_frontend(pid, diagnostic));
    MIRAGE_CHECK(diagnostic.find("256") != std::string::npos);
    XSync(display, False);
    MIRAGE_CHECK(!XCheckTypedEvent(display, ClientMessage, &request));
    XDestroyWindow(display, owned);
    XDestroyWindow(display, other);
    XCloseDisplay(display);
    return mirage::testing::finish("frontend_activation_test");
}
