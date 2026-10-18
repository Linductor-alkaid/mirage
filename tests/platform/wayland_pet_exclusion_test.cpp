#include "../../platform/common/src/frontend_activation.hpp"
#include "../../platform/common/src/presentation_context.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <algorithm>
#include <atomic>
#include <kairo/executor.hpp>
#include <poll.h>
#include <thread>
#include <vector>

// Xlib's None macro must not reach the kairo public headers above.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace {
// AGENTS fixture exemption: this controlled WM models platform affinity only;
// its RAII thread is joined before the displays and private X server are closed.
struct Wm {
    Display *display = XOpenDisplay(nullptr);
    Window root = DefaultRootWindow(display), identity = 0;
    std::atomic_bool stop{false}, ignore{false};
    std::thread thread;
    std::vector<Window> clients;
    Atom atom(const char *name) { return XInternAtom(display, name, False); }
    void state(Window window, long value) {
        const long values[] = {value, None};
        XChangeProperty(display, window, atom("WM_STATE"), atom("WM_STATE"), 32, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(values), 2);
        std::erase(clients, window);
        if (value != WithdrawnState)
            clients.push_back(window);
        XChangeProperty(display, root, atom("_NET_CLIENT_LIST"), XA_WINDOW, 32, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(clients.data()),
                        static_cast<int>(clients.size()));
    }
    Wm() {
        identity = XCreateSimpleWindow(display, root, 0, 0, 1, 1, 0, 0, 0);
        for (auto window : {root, identity})
            XChangeProperty(display, window, atom("_NET_SUPPORTING_WM_CHECK"), XA_WINDOW, 32,
                            PropModeReplace, reinterpret_cast<const unsigned char *>(&identity), 1);
        constexpr char name[] = "GNOME Shell";
        XChangeProperty(display, identity, atom("_NET_WM_NAME"), atom("UTF8_STRING"), 8,
                        PropModeReplace, reinterpret_cast<const unsigned char *>(name), 11);
        XSelectInput(display, root, SubstructureRedirectMask | SubstructureNotifyMask);
        XSync(display, False);
        thread = std::thread([this] {
            while (!stop.load()) {
                for (unsigned i = 0; i < 128 && XPending(display); ++i) {
                    XEvent event{};
                    XNextEvent(display, &event);
                    if (event.type == MapRequest) {
                        XMapWindow(display, event.xmaprequest.window);
                        state(event.xmaprequest.window, NormalState);
                    } else if (event.type == UnmapNotify && !ignore.load()) {
                        state(event.xunmap.window, WithdrawnState);
                        XDeleteProperty(display, event.xunmap.window, atom("_NET_WM_STATE"));
                    }
                }
                XFlush(display);
                struct pollfd fd {
                    ConnectionNumber(display), POLLIN, 0
                };
                (void)::poll(&fd, 1, 10);
            }
        });
    }
    ~Wm() {
        stop.store(true);
        thread.join();
        XCloseDisplay(display);
    }
};
bool mapped(Display *display, Window window, bool expected) {
    for (unsigned i = 0; i < 200; ++i) {
        XWindowAttributes attributes{};
        if (XGetWindowAttributes(display, window, &attributes) &&
            attributes.map_state == (expected ? IsViewable : IsUnmapped))
            return true;
        (void)::poll(nullptr, 0, 10);
    }
    return false;
}
void cardinal(Display *display, Window window, const char *name, unsigned long value) {
    XChangeProperty(display, window, XInternAtom(display, name, False), XA_CARDINAL, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(&value), 1);
    XSync(display, False);
}
} // namespace
int main() {
    using namespace mirage;
    XInitThreads();
    testing::XvfbDisplay server(testing::find_xvfb(), "800x600x24");
    ::setenv("DISPLAY", server.display_name().c_str(), 1);
    ::setenv("XDG_SESSION_TYPE", "wayland", 1);
    ::setenv("WAYLAND_DISPLAY", "private-protocol-fixture", 1);
    Wm wm;
    Display *display = XOpenDisplay(nullptr);
    const auto pet =
        XCreateSimpleWindow(display, DefaultRootWindow(display), 40, 40, 264, 322, 0, 0, 0xaa3355);
    const auto pid = static_cast<unsigned long>(::getpid());
    cardinal(display, pet, "_NET_WM_PID", pid);
    cardinal(display, pet, "_MIRAGE_PET_SURFACE", 1);
    const auto utility = XInternAtom(display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    XChangeProperty(display, pet, XInternAtom(display, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(&utility), 1);
    const auto above = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    const auto state_atom = XInternAtom(display, "_NET_WM_STATE", False);
    XChangeProperty(display, pet, state_atom, XA_ATOM, 32, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&above), 1);
    XMapWindow(display, pet);
    XFlush(display);
    MIRAGE_CHECK(mapped(display, pet, true));
    auto context = std::make_shared<platform::PresentationContext>();
    auto control = platform::detail::make_pet_excluder(pid, context);
    // A same-session peer can forge root properties. Validate format before
    // interpreting the data as machine-sized Xlib window IDs.
    const auto clients_atom = XInternAtom(display, "_NET_CLIENT_LIST", False);
    const unsigned char malformed[] = {1};
    XChangeProperty(display, DefaultRootWindow(display), clients_atom, XA_WINDOW, 8,
                    PropModeReplace, malformed, 1);
    XSync(display, False);
    auto malformed_list = control->acquire({});
    MIRAGE_CHECK(!malformed_list.lease && !malformed_list.error.code.empty() &&
                 context->not_before_ns.load() == 0);
    XChangeProperty(display, DefaultRootWindow(display), clients_atom, XA_WINDOW, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(&pet), 1);
    XSync(display, False);
    const auto name_atom = XInternAtom(display, "_NET_WM_NAME", False);
    const auto utf8 = XInternAtom(display, "UTF8_STRING", False);
    constexpr char unsupported[] = "Unverified WM";
    XChangeProperty(display, wm.identity, name_atom, utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(unsupported), 13);
    XSync(display, False);
    context->not_before_ns.store(123); // a previous compositor's stale fence
    auto unverified_wm = control->acquire({});
    MIRAGE_CHECK(unverified_wm.lease && unverified_wm.error.code.empty() &&
                 context->not_before_ns.load() == 0);
    MIRAGE_CHECK(mapped(display, pet, true));
    if (unverified_wm.lease) {
        MIRAGE_CHECK(!unverified_wm.lease->excluded() &&
                     unverified_wm.lease->verify({}).code.empty());
        desktop::CancelToken passive_cancel;
        passive_cancel.request_cancel();
        MIRAGE_CHECK(unverified_wm.lease->verify(passive_cancel).code == "cancelled");
        MIRAGE_CHECK(unverified_wm.lease->release().code.empty());
        MIRAGE_CHECK(unverified_wm.lease->release().code.empty());
        MIRAGE_CHECK(!unverified_wm.lease->verify({}).code.empty());
        MIRAGE_CHECK(mapped(display, pet, true));
    }
    constexpr char gnome[] = "GNOME Shell";
    XChangeProperty(display, wm.identity, name_atom, utf8, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(gnome), 11);
    XDeleteProperty(display, pet, XInternAtom(display, "_NET_WM_WINDOW_TYPE", False));
    XSync(display, False);
    auto ordinary_window = control->acquire({});
    MIRAGE_CHECK(!ordinary_window.lease && !ordinary_window.error.code.empty());
    XChangeProperty(display, pet, XInternAtom(display, "_NET_WM_WINDOW_TYPE", False), XA_ATOM, 32,
                    PropModeReplace, reinterpret_cast<const unsigned char *>(&utility), 1);
    XSync(display, False);
    auto absent_fence = platform::detail::make_pet_excluder(pid)->acquire({});
    MIRAGE_CHECK(!absent_fence.lease && absent_fence.error.code == "pet_exclusion_unavailable");
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 2;
    config.queue_capacity = config.max_in_flight_tasks = 8;
    MIRAGE_CHECK(executor.initialize(config));
    auto acquired = executor.submit_auto([&] { return control->acquire({}); });
    auto first = acquired.get();
    MIRAGE_CHECK(first.lease && first.error.code.empty());
    if (first.lease) {
        MIRAGE_CHECK(mapped(display, pet, false));
        MIRAGE_CHECK(first.lease->verify({}).code.empty() && context->not_before_ns.load() > 0);
        const auto fence = context->not_before_ns.load();
        auto second = control->acquire({});
        MIRAGE_CHECK(second.lease && context->not_before_ns.load() == fence);
        MIRAGE_CHECK(first.lease->release().code.empty());
        MIRAGE_CHECK(mapped(display, pet, false));
        if (second.lease)
            MIRAGE_CHECK(second.lease->release().code.empty());
        MIRAGE_CHECK(mapped(display, pet, true));
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, rest = 0;
        unsigned char *data = nullptr;
        MIRAGE_CHECK(XGetWindowProperty(display, pet, state_atom, 0, 64, False, XA_ATOM, &actual,
                                        &format, &count, &rest, &data) == Success);
        MIRAGE_CHECK(actual == XA_ATOM && format == 32 && count == 1 && data &&
                     *reinterpret_cast<unsigned long *>(data) == above);
        if (data)
            XFree(data);
        XWindowAttributes bounds{};
        MIRAGE_CHECK(XGetWindowAttributes(display, pet, &bounds) && bounds.x == 40 &&
                     bounds.y == 40 && bounds.width == 264 && bounds.height == 322);
    }
    // Preserve an already withdrawn window, including across RAII cleanup.
    XWithdrawWindow(display, pet, DefaultScreen(display));
    XFlush(display);
    MIRAGE_CHECK(mapped(display, pet, false));
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    first.lease.reset();
    MIRAGE_CHECK(mapped(display, pet, false));
    XMapWindow(display, pet);
    XFlush(display);
    MIRAGE_CHECK(mapped(display, pet, true));
    wm.ignore.store(true);
    unsigned polls = 0;
    desktop::CancelToken cancel([&] { return ++polls > 4; });
    auto refused = control->acquire(cancel);
    MIRAGE_CHECK(!refused.lease && refused.error.code == "cancelled");
    MIRAGE_CHECK(mapped(display, pet, true));
    refused = control->acquire({});
    MIRAGE_CHECK(!refused.lease && refused.error.code == "pet_exclusion_timeout");
    MIRAGE_CHECK(mapped(display, pet, true));
    wm.ignore.store(false);
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    XMapWindow(display, pet);
    XFlush(display);
    MIRAGE_CHECK(mapped(display, pet, true));
    if (first.lease) {
        MIRAGE_CHECK(first.lease->verify({}).code == "pet_exclusion_failed");
        MIRAGE_CHECK(first.lease->release().code.empty());
    }
    // Identity loss cannot let cleanup act on a replacement surface.
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    cardinal(display, pet, "_MIRAGE_PET_SURFACE", 2);
    if (first.lease) {
        MIRAGE_CHECK(!first.lease->verify({}).code.empty());
        MIRAGE_CHECK(first.lease->release().code == "pet_restore_failed");
    }
    cardinal(display, pet, "_MIRAGE_PET_SURFACE", 1);
    XMapWindow(display, pet);
    XFlush(display);
    MIRAGE_CHECK(mapped(display, pet, true));
    first = control->acquire({});
    MIRAGE_CHECK(first.lease);
    XDestroyWindow(display, pet);
    XSync(display, False);
    if (first.lease) {
        MIRAGE_CHECK(!first.lease->verify({}).code.empty());
        MIRAGE_CHECK(first.lease->release().code.empty());
    }
    executor.shutdown(true);
    XCloseDisplay(display);
    ::setenv("DISPLAY", "private-unavailable-display", 1);
    context->not_before_ns.store(123);
    auto no_xwayland = platform::detail::make_pet_excluder(pid, context)->acquire({});
    MIRAGE_CHECK(no_xwayland.lease && no_xwayland.error.code.empty() &&
                 !no_xwayland.lease->excluded() && context->not_before_ns.load() == 0);
    return testing::finish("wayland_pet_exclusion_test");
}
