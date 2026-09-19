// M2-02 Linux backend skeleton tests against a private headless Xvfb
// (DEC-015 test topology): real X server, real Xlib/XTest behavior, no
// running desktop required. Every contract the fake environment asserts is
// re-verified here on the real frontend path — enumeration honesty, budget
// refusals, cancellation before effects, EWMH-less activation fallback,
// capture bytes, and XTest event delivery.

#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <sys/select.h>
#include <sys/wait.h>

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

namespace {

using mirage::desktop::CancelToken;
using mirage::platform::linux_backend::LinuxDesktopEnvironment;
using mirage::testing::XvfbDisplay;

/// Test-side X connection helper: window creation, drawing, event waiting.
class TestClient {
  public:
    explicit TestClient(const std::string &display_name)
        : display_(XOpenDisplay(display_name.c_str())) {
        if (display_ == nullptr) {
            std::fprintf(stderr, "test client cannot open display %s\n", display_name.c_str());
        }
    }
    ~TestClient() {
        if (display_ != nullptr) {
            XCloseDisplay(display_);
        }
    }
    bool ok() const { return display_ != nullptr; }
    Display *get() { return display_; }
    Window root() { return DefaultRootWindow(display_); }

    /// Creates, sizes, titles and maps one test window with the given
    /// background; returns once the server reports it viewable.
    Window make_window(int x, int y, int width, int height, unsigned long background,
                       const std::string &title) {
        XSetWindowAttributes attributes{};
        attributes.background_pixel = background;
        attributes.override_redirect = False;
        const Window window =
            XCreateWindow(display_, root(), x, y, static_cast<unsigned int>(width),
                          static_cast<unsigned int>(height), 0, CopyFromParent, InputOutput,
                          CopyFromParent, CWBackPixel | CWOverrideRedirect, &attributes);
        XStoreName(display_, window, title.c_str());
        XMapWindow(display_, window);
        XSync(display_, False);
        return window;
    }

    /// Creates but deliberately does not map one window: it exists on the
    /// server yet is never viewable, for not_found / visibility-gate tests.
    Window make_unmapped_window(int x, int y, int width, int height, unsigned long background) {
        XSetWindowAttributes attributes{};
        attributes.background_pixel = background;
        attributes.override_redirect = False;
        const Window window =
            XCreateWindow(display_, root(), x, y, static_cast<unsigned int>(width),
                          static_cast<unsigned int>(height), 0, CopyFromParent, InputOutput,
                          CopyFromParent, CWBackPixel | CWOverrideRedirect, &attributes);
        XSync(display_, False);
        return window;
    }

    void fill(Window window, unsigned long pixel) {
        const GC gc = XCreateGC(display_, window, 0, nullptr);
        XSetForeground(display_, gc, pixel);
        XFillRectangle(display_, window, gc, 0, 0, 10000, 10000);
        XFreeGC(display_, gc);
        XSync(display_, False);
    }

    /// Waits up to `timeout_ms` for the next event; false on deadline.
    bool next_event(XEvent &event, int timeout_ms = 2000) {
        const int fd = ConnectionNumber(display_);
        deadline_ms_ = now_ms() + timeout_ms;
        for (;;) {
            if (XPending(display_) > 0) {
                XNextEvent(display_, &event);
                return true;
            }
            const long remaining = deadline_ms_ - now_ms();
            if (remaining <= 0) {
                return false;
            }
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(fd, &read_fds);
            timeval timeout{remaining / 1000, (remaining % 1000) * 1000};
            if (::select(fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0 && remaining > 0) {
                if (XPending(display_) == 0 && now_ms() >= deadline_ms_) {
                    return false;
                }
            }
        }
    }

  private:
    static long now_ms() {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return now.tv_sec * 1000 + now.tv_nsec / 1000000;
    }
    Display *display_ = nullptr;
    long deadline_ms_ = 0;
};

unsigned long rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (static_cast<unsigned long>(r) << 16) | (static_cast<unsigned long>(g) << 8) | b;
}

std::string hex_id(Window window) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%lu", static_cast<unsigned long>(window));
    return buffer;
}

long monotonic_ms() {
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void open_fail_closed_on_bad_display() {
    // Capability honesty end-to-end: an environment whose X11 opt-in fails
    // exposes no desktop surface at all.
    LinuxDesktopEnvironment without_x11({}, {.enabled = true, .display = "missing-99"});
    MIRAGE_CHECK(without_x11.window() == nullptr);
    MIRAGE_CHECK(without_x11.screen() == nullptr);
    MIRAGE_CHECK(without_x11.input() == nullptr);
    MIRAGE_CHECK(without_x11.clipboard() == nullptr); // M2-04: no X, no clipboard either
    MIRAGE_CHECK(without_x11.filesystem() != nullptr);
    MIRAGE_CHECK(without_x11.info().platform == "linux");
}

void window_enumeration_and_activation(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::WindowProvider &windows = *env.window();

    const Window first = client.make_window(10, 10, 200, 150, rgb(10, 20, 30), "first-window");
    const Window second = client.make_window(300, 40, 120, 90, rgb(200, 100, 0), "second-window");
    XSync(client.get(), False);

    const auto listed = windows.list_windows();
    MIRAGE_CHECK(listed.ok);
    const auto first_entry = std::find_if(listed.windows.begin(), listed.windows.end(),
                                          [&](const auto &w) { return w.title == "first-window"; });
    const auto second_entry =
        std::find_if(listed.windows.begin(), listed.windows.end(),
                     [&](const auto &w) { return w.title == "second-window"; });
    MIRAGE_CHECK(first_entry != listed.windows.end());
    MIRAGE_CHECK(second_entry != listed.windows.end());
    MIRAGE_CHECK(first_entry->geometry.width == 200);
    MIRAGE_CHECK(first_entry->geometry.height == 150);
    MIRAGE_CHECK(first_entry->geometry.x == 10);
    MIRAGE_CHECK(first_entry->geometry.y == 10);
    MIRAGE_CHECK(!first_entry->focused);
    MIRAGE_CHECK(!second_entry->focused);

    // Budget refusal: the table holds two windows, a limit of one is a
    // result_too_large rejection, never a truncated list.
    mirage::desktop::WindowListLimits tight;
    tight.max_windows = 1;
    const auto refused = windows.list_windows(tight, {});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "result_too_large");
    MIRAGE_CHECK(windows.list_windows(mirage::desktop::WindowListLimits{{64}}, {})
                     .ok); // generous budget passes

    // No WM on Xvfb: activation takes the input-focus fallback and is
    // observable through front_window.
    const auto none = windows.front_window();
    MIRAGE_CHECK(none.ok);
    // (Fresh Xvfb has no focus owner; found may be false here.)

    const auto activated = windows.activate(hex_id(first));
    MIRAGE_CHECK(activated.ok);
    const auto front = windows.front_window();
    MIRAGE_CHECK(front.ok);
    MIRAGE_CHECK(front.found);
    MIRAGE_CHECK(front.window.id == hex_id(first));
    // The focused flag must be consistent between front_window and the
    // enumeration (both use the input-focus fallback when no WM runs).
    const auto relisted = windows.list_windows();
    MIRAGE_CHECK(relisted.ok);
    for (const auto &window : relisted.windows) {
        MIRAGE_CHECK(window.focused == (window.id == hex_id(first)));
    }

    MIRAGE_CHECK(windows.activate(hex_id(second)).ok);
    MIRAGE_CHECK(windows.front_window().window.id == hex_id(second));
    const auto relisted_again = windows.list_windows();
    for (const auto &window : relisted_again.windows) {
        MIRAGE_CHECK(window.focused == (window.id == hex_id(second)));
    }

    const auto unknown = windows.activate("999999");
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "not_found");
    MIRAGE_CHECK(windows.front_window().window.id == hex_id(second)); // unchanged

    MIRAGE_CHECK(!windows.activate("").ok);
    MIRAGE_CHECK(windows.activate("").error.code == "invalid_argument");
    MIRAGE_CHECK(!windows.activate("not-a-number").ok);

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(windows.activate(hex_id(first), cancel).cancelled);
    MIRAGE_CHECK(windows.front_window().window.id == hex_id(second));
}

void capture_paths(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::ScreenProvider &screen = *env.screen();

    const auto displays = screen.list_displays();
    MIRAGE_CHECK(displays.ok);
    MIRAGE_CHECK(displays.displays.size() >= 1);
    MIRAGE_CHECK(displays.displays[0].geometry.width == 640); // Xvfb -screen geometry
    MIRAGE_CHECK(displays.displays[0].geometry.height == 480);

    const Window painted = client.make_window(20, 30, 60, 40, rgb(255, 0, 255), "painted");
    client.fill(painted, rgb(255, 0, 255));
    XSync(client.get(), False);

    // ROI capture over the painted window reads its pixels off the root
    // framebuffer (Bgra8, red channel 255, alpha untouched by X).
    const auto roi = screen.capture_roi({20, 30, 60, 40});
    MIRAGE_CHECK(roi.ok);
    MIRAGE_CHECK(roi.frame.width == 60);
    MIRAGE_CHECK(roi.frame.height == 40);
    MIRAGE_CHECK(roi.frame.stride >= static_cast<std::size_t>(60) * 4u);
    MIRAGE_CHECK(roi.frame.format == mirage::desktop::ImageFormat::Bgra8);
    const std::size_t center =
        (static_cast<std::size_t>(20) * roi.frame.stride) + static_cast<std::size_t>(30) * 4u;
    MIRAGE_CHECK(roi.frame.pixels[center + 2] == 255); // red channel

    const auto whole = screen.capture_window(hex_id(painted));
    MIRAGE_CHECK(whole.ok);
    MIRAGE_CHECK(whole.frame.width == 60);
    MIRAGE_CHECK(whole.frame.height == 40);

    const auto display_capture = screen.capture_display(displays.displays[0].id);
    MIRAGE_CHECK(display_capture.ok);
    MIRAGE_CHECK(display_capture.frame.width == 640);
    MIRAGE_CHECK(display_capture.frame.height == 480);

    const auto unknown_display = screen.capture_display("no-such-monitor");
    MIRAGE_CHECK(!unknown_display.ok);
    MIRAGE_CHECK(unknown_display.error.code == "not_found");

    // Unmapped windows have nothing on screen: capture fails closed.
    XSetWindowAttributes hidden_attributes{};
    hidden_attributes.background_pixel = rgb(1, 2, 3);
    const Window hidden =
        XCreateWindow(client.get(), client.root(), 5, 5, 40, 30, 0, CopyFromParent, InputOutput,
                      CopyFromParent, CWBackPixel, &hidden_attributes);
    XSync(client.get(), False);
    const auto hidden_capture = screen.capture_window(hex_id(hidden));
    MIRAGE_CHECK(!hidden_capture.ok);
    MIRAGE_CHECK(hidden_capture.error.code == "not_found");
    XDestroyWindow(client.get(), hidden);

    mirage::desktop::CaptureLimits tiny;
    tiny.max_bytes = 1024;
    const auto too_large = screen.capture_roi({0, 0, 100, 100}, tiny, {});
    MIRAGE_CHECK(!too_large.ok);
    MIRAGE_CHECK(too_large.error.code == "capture_too_large");
    MIRAGE_CHECK(too_large.frame.pixels.empty());

    const auto bad_roi = screen.capture_roi({0, 0, -5, 10});
    MIRAGE_CHECK(!bad_roi.ok);
    MIRAGE_CHECK(bad_roi.error.code == "invalid_argument");

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(screen.capture_roi({0, 0, 10, 10}, {}, cancel).cancelled);
}

void input_injection_events(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::InputProvider &input = *env.input();
    Display *display = client.get();

    const Window listener = client.make_window(0, 0, 100, 100, rgb(0, 0, 0), "listener");
    XSelectInput(display, listener,
                 KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask);
    XSetInputFocus(display, listener, RevertToParent, CurrentTime);
    XSync(display, False);

    // Pointer move + button: the XTest events surface as real events on the
    // listener window.
    MIRAGE_CHECK(input.pointer_move(50, 40).ok);
    XEvent event{};
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == MotionNotify);
    MIRAGE_CHECK(event.xmotion.x == 50 && event.xmotion.y == 40);

    MIRAGE_CHECK(input.pointer_button("left", true).ok);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == ButtonPress && event.xbutton.button == Button1);
    MIRAGE_CHECK(input.pointer_button("left", false).ok);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == ButtonRelease && event.xbutton.button == Button1);

    // Plain key: down then up, keycode matches the server mapping.
    const KeyCode a_code = XKeysymToKeycode(display, XK_a);
    MIRAGE_CHECK(input.inject_key({"a"}, true).ok);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == KeyPress && event.xkey.keycode == a_code);
    MIRAGE_CHECK(input.inject_key({"a"}, false).ok);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == KeyRelease && event.xkey.keycode == a_code);

    // Shifted text: type_text resolves shift chords; the listener sees
    // Shift_L down, 'H' keycode, Shift_L up, then plain 'i'.
    const KeyCode h_code = XKeysymToKeycode(display, XK_H);
    const KeyCode i_code = XKeysymToKeycode(display, XK_i);
    MIRAGE_CHECK(h_code != 0 && i_code != 0);
    MIRAGE_CHECK(input.type_text("Hi").ok);
    MIRAGE_CHECK(client.next_event(event)); // Shift press
    const bool shift_press =
        event.type == KeyPress && event.xkey.keycode == XKeysymToKeycode(display, XK_Shift_L);
    MIRAGE_CHECK(shift_press);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == KeyPress && event.xkey.keycode == h_code);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == KeyRelease && event.xkey.keycode == h_code);
    MIRAGE_CHECK(client.next_event(event)); // Shift release
    MIRAGE_CHECK(event.type == KeyRelease);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == KeyPress && event.xkey.keycode == i_code);
    MIRAGE_CHECK(client.next_event(event));
    MIRAGE_CHECK(event.type == KeyRelease && event.xkey.keycode == i_code);

    // Contract rejections happen before any injection: nothing new arrives.
    MIRAGE_CHECK(!input.inject_key({"ctrl+alt+meta+ctrl+x"}, true).ok);
    MIRAGE_CHECK(!input.type_text("\xff\xfe").ok);      // invalid UTF-8
    MIRAGE_CHECK(!input.type_text("no\x01pe").ok);      // non-printable control char
    MIRAGE_CHECK(!input.pointer_button("up", true).ok); // not a button
    mirage::desktop::InputLimits dead;
    dead.timeout = std::chrono::milliseconds{0};
    MIRAGE_CHECK(input.pointer_move(1, 1, dead, {}).error.code == "invalid_argument");
    while (XPending(display) > 0) {
        XNextEvent(display, &event);
    }
    MIRAGE_CHECK(!client.next_event(event, 150)); // deadline: no leaked strokes

    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(input.pointer_move(5, 5, {}, cancel).cancelled);
    MIRAGE_CHECK(input.type_text("x", {}, cancel).cancelled);
    MIRAGE_CHECK(input.inject_key({"a"}, true, {}, cancel).cancelled);
}

/// Independent-verification additions: negative and boundary edges the base
/// scenarios leave open — malformed/unknown ids on every id-taking path,
/// display budget refusal, off-screen ROI, the visibility gate on activate,
/// the type_text byte budget and keymap-gap rejections happening before any
/// stroke reaches the server, and cancellation on pointer_button. None
/// changes contract or implementation semantics; each pins observed,
/// deterministic behavior of the real X server.
void negative_and_boundary_edges(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::ScreenProvider &screen = *env.screen();
    mirage::desktop::WindowProvider &windows = *env.window();
    mirage::desktop::InputProvider &input = *env.input();
    Display *display = client.get();

    // Malformed and unknown window ids get distinct, pre-side-effect codes.
    MIRAGE_CHECK(screen.capture_window("999999").error.code == "not_found");
    MIRAGE_CHECK(screen.capture_window("12abc").error.code == "invalid_argument");
    MIRAGE_CHECK(screen.capture_window("").error.code == "invalid_argument");
    MIRAGE_CHECK(windows.activate("12abc").error.code == "invalid_argument");
    MIRAGE_CHECK(input.inject_key({"12abc"}, true).error.code == "invalid_argument");
    MIRAGE_CHECK(input.inject_key({"enter"}, true).ok); // named key still injects

    // Display budget: over-budget enumeration is refused, never truncated.
    mirage::desktop::DisplayListLimits zero_displays;
    zero_displays.max_displays = 0;
    const auto displays_refused = screen.list_displays(zero_displays, {});
    MIRAGE_CHECK(!displays_refused.ok);
    MIRAGE_CHECK(displays_refused.error.code == "result_too_large");
    MIRAGE_CHECK(displays_refused.displays.empty());
    MIRAGE_CHECK(screen.list_displays({}, {}).ok); // default budget passes

    // A ROI extending past the root drawable is refused by the server
    // (BadMatch surfaces as io_error through the quiet error handler, never
    // a process kill or partial pixels).
    const auto offscreen = screen.capture_roi({-10, -10, 30, 30});
    MIRAGE_CHECK(!offscreen.ok);
    MIRAGE_CHECK(offscreen.error.code == "io_error");
    MIRAGE_CHECK(offscreen.frame.pixels.empty());

    // The visibility gate: a window that exists but is not mapped is "no
    // such viewable window" for activate, and focus stays where it was.
    const Window listener = client.make_window(20, 340, 80, 60, rgb(9, 9, 9), "edge-listener");
    XSetInputFocus(display, listener, RevertToParent, CurrentTime);
    XSync(display, False);
    const auto focused_before = windows.front_window();
    MIRAGE_CHECK(focused_before.found && focused_before.window.id == hex_id(listener));

    const Window hidden = client.make_unmapped_window(120, 340, 50, 40, rgb(8, 8, 8));
    const auto hidden_activate = windows.activate(hex_id(hidden));
    MIRAGE_CHECK(!hidden_activate.ok);
    MIRAGE_CHECK(hidden_activate.error.code == "not_found");
    const auto focused_after = windows.front_window();
    MIRAGE_CHECK(focused_after.found && focused_after.window.id == hex_id(listener));
    // The unmapped window never shows up in the enumeration either.
    const auto listed = windows.list_windows();
    MIRAGE_CHECK(listed.ok);
    MIRAGE_CHECK(std::find_if(listed.windows.begin(), listed.windows.end(), [&](const auto &w) {
                     return w.id == hex_id(hidden);
                 }) == listed.windows.end());

    // type_text rejections happen before any stroke: budget first...
    mirage::desktop::InputLimits tiny_text;
    tiny_text.max_text_bytes = 2;
    MIRAGE_CHECK(input.type_text("abc", tiny_text, {}).error.code == "invalid_argument");
    // ...then characters outside the server keymap (e-acute is printable
    // UTF-8 but absent from the default Xvfb map).
    const auto unmapped_char = input.type_text("\xc3\xa9");
    MIRAGE_CHECK(!unmapped_char.ok);
    MIRAGE_CHECK(unmapped_char.error.code == "unsupported_key");
    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(input.pointer_button("left", true, {}, cancel).cancelled);
    MIRAGE_CHECK(screen.capture_window(hex_id(listener), {}, cancel).cancelled);

    while (XPending(display) > 0) {
        XEvent drained{};
        XNextEvent(display, &drained);
    }
    XEvent event{};
    MIRAGE_CHECK(!client.next_event(event, 150)); // deadline: no leaked strokes
}

void environment_identity_and_defaults(XvfbDisplay &server) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    MIRAGE_CHECK(env.info().name == "mirage-linux");
    MIRAGE_CHECK(env.info().platform == "linux");
    // Providers later milestones have not landed stay null (fail closed),
    // while the M1 surface and the M2 X11/AT-SPI2 providers are available.
    // M2-04 moved clipboard() from the null group to the live group.
    MIRAGE_CHECK(env.accessibility() == nullptr);
    MIRAGE_CHECK(env.clipboard() != nullptr);
    MIRAGE_CHECK(env.application() == nullptr);
    MIRAGE_CHECK(env.notification() == nullptr);
    MIRAGE_CHECK(env.window() != nullptr);
    MIRAGE_CHECK(env.screen() != nullptr);
    MIRAGE_CHECK(env.input() != nullptr);

    // Default constructor: no X11 opt-in, M1-only topology unchanged.
    LinuxDesktopEnvironment silent;
    MIRAGE_CHECK(silent.window() == nullptr);
    MIRAGE_CHECK(silent.screen() == nullptr);
    MIRAGE_CHECK(silent.input() == nullptr);
    MIRAGE_CHECK(silent.clipboard() == nullptr);
    MIRAGE_CHECK(silent.filesystem() != nullptr);
}

// ---- M2-04 clipboard (X11 selection protocol) ------------------------------
//
// The clipboard is shared machine state negotiated through the ICCCM
// selection protocol, so the tests drive it from a real second client. The
// single-threaded test process cannot service a peer selection owner while
// the backend's bounded read waits for the SelectionNotify, so peer owners
// run in a forked child with its own display connection — the same process
// isolation the XvfbDisplay fixture uses (no threads anywhere, RULE-03).

/// Upper bound on how long a forked peer owner keeps serving requests.
constexpr int kPeerServeMs = 8000;

/// Forks an independent X client that takes the CLIPBOARD selection on its
/// own window and serves peer requests until kPeerServeMs elapses. With
/// utf8_capable the owner answers UTF8_STRING (and ICCCM metadata) targets;
/// without it, UTF8_STRING gets the explicit refusal (SelectionNotify with
/// property=None) that pins the unsupported_content contract. Never returns
/// in the child.
pid_t fork_clipboard_owner(const std::string &display_name, const std::string &payload,
                           bool utf8_capable) {
    const pid_t pid = ::fork();
    if (pid != 0) {
        return pid;
    }
    // Child: never touch the parent's Xlib state; open a fresh connection.
    Display *display = XOpenDisplay(display_name.c_str());
    if (display == nullptr) {
        _exit(1);
    }
    const Atom clipboard = XInternAtom(display, "CLIPBOARD", False);
    const Atom utf8 = XInternAtom(display, "UTF8_STRING", False);
    const Atom targets = XInternAtom(display, "TARGETS", False);
    const Atom timestamp = XInternAtom(display, "TIMESTAMP", False);
    const Window window =
        XCreateSimpleWindow(display, DefaultRootWindow(display), 0, 0, 1, 1, 0, 0, 0);
    // ICCCM ownership timestamp via the property round trip (the server
    // time of the PropertyNotify is our TIMESTAMP answer).
    const Atom marker = XInternAtom(display, "MIRAGE_TEST_STAMP", False);
    XSelectInput(display, window, PropertyChangeMask);
    XChangeProperty(display, window, marker, XA_INTEGER, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(""), 0);
    XSetSelectionOwner(display, window, clipboard, CurrentTime);
    XSync(display, False);
    unsigned long stamp = 1;
    XEvent event;
    while (XCheckTypedWindowEvent(display, window, PropertyNotify, &event)) {
        if (event.xproperty.atom == marker) {
            stamp = event.xproperty.time;
        }
    }
    const long deadline = monotonic_ms() + kPeerServeMs;
    bool running = true;
    while (running && monotonic_ms() < deadline) {
        while (XPending(display) > 0) {
            XNextEvent(display, &event);
            if (event.type != SelectionRequest) {
                continue;
            }
            const XSelectionRequestEvent &request = event.xselectionrequest;
            const Atom property = request.property != None ? request.property : request.target;
            Atom reply_property = property;
            if (request.target == targets) {
                const Atom supported[] = {targets, utf8_capable ? utf8 : XA_STRING, timestamp};
                XChangeProperty(display, request.requestor, property, XA_ATOM, 32, PropModeReplace,
                                reinterpret_cast<const unsigned char *>(supported), 3);
            } else if (request.target == timestamp) {
                const long value = static_cast<long>(stamp);
                XChangeProperty(display, request.requestor, property, XA_INTEGER, 32,
                                PropModeReplace, reinterpret_cast<const unsigned char *>(&value),
                                1);
            } else if (utf8_capable && request.target == utf8) {
                XChangeProperty(display, request.requestor, property, utf8, 8, PropModeReplace,
                                reinterpret_cast<const unsigned char *>(payload.data()),
                                static_cast<int>(payload.size()));
            } else if (!utf8_capable && request.target == XA_STRING) {
                XChangeProperty(display, request.requestor, property, XA_STRING, 8, PropModeReplace,
                                reinterpret_cast<const unsigned char *>(payload.data()),
                                static_cast<int>(payload.size()));
            } else {
                reply_property = None; // ICCCM refusal for unsupported targets
            }
            XSelectionEvent reply = {};
            reply.type = SelectionNotify;
            reply.display = display;
            reply.requestor = request.requestor;
            reply.selection = request.selection;
            reply.target = request.target;
            reply.property = reply_property;
            reply.time = request.time;
            XSendEvent(display, request.requestor, False, NoEventMask,
                       reinterpret_cast<XEvent *>(&reply));
            XFlush(display);
        }
        ::usleep(20 * 1000);
    }
    XCloseDisplay(display);
    _exit(0);
}

/// Waits until a client other than `previous_owner` owns the CLIPBOARD
/// selection (used after forking a peer owner); false on timeout.
bool wait_for_clipboard_owner(TestClient &client, Atom clipboard, Window previous_owner,
                              int timeout_ms) {
    const long deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < deadline) {
        const Window owner = XGetSelectionOwner(client.get(), clipboard);
        if (owner != None && owner != previous_owner) {
            return true;
        }
        ::usleep(10 * 1000);
    }
    return false;
}

/// Reaps a forked peer owner: SIGKILL closes its display connection, which
/// releases the selection server-side.
void reap_peer_owner(pid_t pid) {
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
}

/// Deterministic patterned payload, valid UTF-8 end to end (the read path
/// re-validates the encoding), cut on an ASCII boundary so chunk limits can
/// land next to multi-byte sequences.
std::string make_utf8_payload(std::size_t target_bytes) {
    const std::string base = "m2-04 \xc3\xa9\xe4\xb8\x96 \xf0\x9f\x96\x96 |";
    std::string payload;
    payload.reserve(target_bytes + base.size() + 16);
    for (std::size_t line = 0; payload.size() < target_bytes; ++line) {
        payload += base;
        payload += std::to_string(line);
        payload += '\n';
    }
    const std::size_t cut = payload.rfind('\n', target_bytes);
    payload.resize(cut + 1);
    return payload;
}

/// The backend's serving chunk (x11_backend.cpp clipboard_chunk_bytes): the
/// largest single-property write the server accepts, derived from the
/// extended BIG-REQUESTS limit when present (Xvfb advertises ~16 MiB, NOT
/// the classic 256 KiB XMaxRequestSize). INCR only engages beyond this, so
/// the incremental tests size their payloads from it.
std::size_t backend_chunk_bytes(Display *display) {
    long units = XExtendedMaxRequestSize(display);
    if (units <= 0) {
        units = XMaxRequestSize(display);
    }
    if (units <= 0) {
        return 16384;
    }
    const std::size_t bytes = static_cast<std::size_t>(units) * 4u;
    return bytes > 2048u ? bytes - 1024u : bytes / 2u;
}

/// Single-property peer read of a selection: converts, waits for the
/// SelectionNotify (draining PropertyNotify noise; `pump` is invoked while
/// idle so the backend serves the request during a provider call), then
/// fetches the property with delete=True. `type`/`data` receive the answer
/// (data sized by format); false on timeout or protocol failure.
bool peer_read_selection(TestClient &client, Window requestor, Atom selection, Atom target,
                         Atom property, Atom &type, int &format, std::string &data,
                         std::function<void()> pump, int timeout_ms) {
    Display *display = client.get();
    XDeleteProperty(display, requestor, property);
    XConvertSelection(display, selection, target, property, requestor, CurrentTime);
    XSync(display, False);
    XEvent event{};
    const long deadline = monotonic_ms() + timeout_ms;
    bool notified = false;
    while (monotonic_ms() < deadline) {
        if (client.next_event(event, 100)) {
            if (event.type == SelectionNotify && event.xselection.selection == selection &&
                event.xselection.requestor == requestor) {
                notified = true;
                break;
            }
            continue; // PropertyNotify and unrelated traffic
        }
        if (pump) {
            pump();
        }
    }
    if (!notified) {
        return false;
    }
    Atom actual_type = None;
    int actual_format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *raw = nullptr;
    if (XGetWindowProperty(display, requestor, property, 0, 1 << 20, True, AnyPropertyType,
                           &actual_type, &actual_format, &count, &remaining, &raw) != Success) {
        return false;
    }
    type = actual_type;
    format = actual_format;
    if (raw != nullptr) {
        const std::size_t unit =
            actual_format == 32 ? sizeof(long) : (actual_format == 16 ? sizeof(short) : 1);
        data.assign(reinterpret_cast<const char *>(raw), count * unit);
        XFree(raw);
    }
    return true;
}

/// Reads a full incremental (INCR) transfer as a peer requestor: expects the
/// INCR header after the convert, then accumulates delete-acked chunks.
/// `pump` drives the owner (each chunk is released by the owner's next
/// provider call, per the DEC-015 serving-latency design). Returns false on
/// any timeout.
bool peer_read_incremental(TestClient &client, Window requestor, Atom selection, Atom target,
                           Atom property, Atom incr_atom, std::string &data,
                           std::function<void()> pump, int timeout_ms, unsigned long chunk_longs) {
    Display *display = client.get();
    XDeleteProperty(display, requestor, property);
    XConvertSelection(display, selection, target, property, requestor, CurrentTime);
    XSync(display, False);
    XEvent event{};
    long deadline = monotonic_ms() + timeout_ms;
    bool notified = false;
    while (!notified && monotonic_ms() < deadline) {
        if (client.next_event(event, 100)) {
            if (event.type == SelectionNotify && event.xselection.selection == selection &&
                event.xselection.requestor == requestor) {
                notified = true;
            }
        } else {
            pump();
        }
    }
    if (!notified) {
        return false;
    }
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *raw = nullptr;
    // Header: delete=False first (ICCCM), then an explicit delete starts the
    // chunk flow.
    if (XGetWindowProperty(display, requestor, property, 0, 1, False, AnyPropertyType, &type,
                           &format, &count, &remaining, &raw) != Success) {
        return false;
    }
    const bool is_incr = type == incr_atom;
    if (raw != nullptr) {
        XFree(raw);
    }
    if (!is_incr) {
        return false;
    }
    XDeleteProperty(display, requestor, property);
    deadline = monotonic_ms() + timeout_ms;
    while (monotonic_ms() < deadline) {
        pump(); // each delete-ack is observed by the owner on a provider call
        if (!client.next_event(event, 200)) {
            continue;
        }
        if (event.type != PropertyNotify || event.xproperty.atom != property ||
            event.xproperty.state != PropertyNewValue) {
            continue;
        }
        if (XGetWindowProperty(display, requestor, property, 0, static_cast<long>(chunk_longs),
                               True, AnyPropertyType, &type, &format, &count, &remaining,
                               &raw) != Success) {
            return false;
        }
        const std::size_t bytes = raw != nullptr ? count : 0;
        if (raw != nullptr) {
            data.append(reinterpret_cast<const char *>(raw), bytes);
            XFree(raw);
        }
        if (bytes == 0) {
            return true; // zero-length terminator chunk ends the transfer
        }
    }
    return false;
}

/// Same-backend roundtrips through the self-request path (owner and
/// requestor are the backend window): ASCII plus multi-byte UTF-8, the
/// write-overwrite rule, and the empty-payload boundary.
void clipboard_same_backend_roundtrips(XvfbDisplay &server) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();

    const std::string text = "h\xc3\xa9llo \xe4\xb8\x96\xe7\x95\x8c \xf0\x9f\x96\x96";
    const auto written = clipboard.write_text(text);
    MIRAGE_CHECK(written.ok);
    MIRAGE_CHECK(!written.cancelled);
    const auto read_back = clipboard.read_text();
    MIRAGE_CHECK(read_back.ok);
    MIRAGE_CHECK(read_back.content == text);

    // Overwrite: a second write replaces the payload entirely.
    MIRAGE_CHECK(clipboard.write_text("second-write").ok);
    const auto second = clipboard.read_text();
    MIRAGE_CHECK(second.ok);
    MIRAGE_CHECK(second.content == "second-write");

    // The empty string is valid UTF-8: the selection stays owned with a
    // zero-length payload and reads back as empty content, not not_found.
    MIRAGE_CHECK(clipboard.write_text("").ok);
    const auto empty = clipboard.read_text();
    MIRAGE_CHECK(empty.ok);
    MIRAGE_CHECK(empty.content.empty());
}

/// Empty-clipboard reads, budget refusals, encoding refusals and
/// pre-side-effect cancellation, each with the X-server ownership observed
/// through the test client's own connection.
void clipboard_refusals_keep_state(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();
    Display *display = client.get();
    const Atom clipboard_atom = XInternAtom(display, "CLIPBOARD", False);

    // Fresh / abandoned clipboard: no owner at all is not_found.
    MIRAGE_CHECK(XGetSelectionOwner(display, clipboard_atom) == None);
    const auto empty = clipboard.read_text();
    MIRAGE_CHECK(!empty.ok);
    MIRAGE_CHECK(empty.error.code == "not_found");

    // A peer that takes and then abandons ownership leaves the clipboard
    // empty again; the backend observes both hops through its pump.
    const Window transient = client.make_window(0, 470, 1, 1, 0, "transient-owner");
    XSetSelectionOwner(display, transient, clipboard_atom, CurrentTime);
    XSync(display, False);
    MIRAGE_CHECK(XGetSelectionOwner(display, clipboard_atom) == transient);
    XDestroyWindow(display, transient);
    XSync(display, False);
    MIRAGE_CHECK(env.window()->front_window().ok); // any provider call pumps
    MIRAGE_CHECK(XGetSelectionOwner(display, clipboard_atom) == None);
    const auto abandoned = clipboard.read_text();
    MIRAGE_CHECK(!abandoned.ok);
    MIRAGE_CHECK(abandoned.error.code == "not_found");

    // Read budget below the content size refuses without truncation...
    MIRAGE_CHECK(clipboard.write_text("0123456789").ok);
    mirage::desktop::ClipboardReadLimits tight_read;
    tight_read.max_bytes = 4;
    const auto too_large = clipboard.read_text(tight_read, {});
    MIRAGE_CHECK(!too_large.ok);
    MIRAGE_CHECK(too_large.error.code == "clipboard_too_large");
    MIRAGE_CHECK(too_large.content.empty());

    // ...and the refused write keeps both the server-side ownership and the
    // previously written payload.
    const Window owned = XGetSelectionOwner(display, clipboard_atom);
    MIRAGE_CHECK(owned != None);
    mirage::desktop::ClipboardWriteLimits tight_write;
    tight_write.max_bytes = 4;
    const auto refused = clipboard.write_text("0123456789", tight_write, {});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "invalid_argument");
    MIRAGE_CHECK(XGetSelectionOwner(display, clipboard_atom) == owned);
    MIRAGE_CHECK(clipboard.read_text().content == "0123456789");

    // Zero budgets are invalid arguments in both directions, ownership
    // untouched.
    mirage::desktop::ClipboardReadLimits zero_read;
    zero_read.max_bytes = 0;
    MIRAGE_CHECK(clipboard.read_text(zero_read, {}).error.code == "invalid_argument");
    mirage::desktop::ClipboardWriteLimits zero_write;
    zero_write.max_bytes = 0;
    MIRAGE_CHECK(clipboard.write_text("x", zero_write, {}).error.code == "invalid_argument");
    MIRAGE_CHECK(XGetSelectionOwner(display, clipboard_atom) == owned);
    MIRAGE_CHECK(clipboard.read_text().content == "0123456789");

    // Malformed UTF-8 payloads are refused before any side effect: bad
    // continuation byte, truncated 2-byte sequence, lone continuation.
    for (const char *bad : {"\xff\xfe", "ok\xc3", "a\x80"}) {
        const auto outcome = clipboard.write_text(bad);
        MIRAGE_CHECK(!outcome.ok);
        MIRAGE_CHECK(outcome.error.code == "invalid_argument");
    }
    MIRAGE_CHECK(XGetSelectionOwner(display, clipboard_atom) == owned);
    MIRAGE_CHECK(clipboard.read_text().content == "0123456789");

    // Cancellation is observed before the write: reported as cancelled, and
    // neither ownership nor payload move.
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled = clipboard.write_text("nope", {}, cancel);
    MIRAGE_CHECK(cancelled.cancelled);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(XGetSelectionOwner(display, clipboard_atom) == owned);
    MIRAGE_CHECK(clipboard.read_text().content == "0123456789");
    const auto cancelled_read = clipboard.read_text({}, cancel);
    MIRAGE_CHECK(!cancelled_read.ok);
    MIRAGE_CHECK(cancelled_read.error.code == "cancelled");
}

/// Cross-client transfers with the backend owning the selection and the
/// test client as an independent requestor: single-property UTF-8 read,
/// the ICCCM TARGETS/TIMESTAMP metadata, and a full incremental (INCR)
/// transfer above the server's max request size.
void clipboard_peer_requests_from_backend(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();
    Display *display = client.get();

    const Atom clipboard_atom = XInternAtom(display, "CLIPBOARD", False);
    const Atom utf8_atom = XInternAtom(display, "UTF8_STRING", False);
    const Atom incr_atom = XInternAtom(display, "INCR", False);
    const Atom read_prop = XInternAtom(display, "MIRAGE_TEST_READ", False);
    const Window requestor = client.make_window(0, 0, 1, 1, 0, "peer-requestor");
    XSelectInput(display, requestor, PropertyChangeMask);
    const auto pump = [&env]() {
        const auto quiet = env.window()->front_window(); // any provider call pumps
        (void)quiet;
    };

    MIRAGE_CHECK(clipboard.write_text("from-mirage \xe4\xb8\x96\xe7\x95\x8c").ok);
    const Window backend_window = XGetSelectionOwner(display, clipboard_atom);
    MIRAGE_CHECK(backend_window != None);

    // Plain transfer: the peer gets the exact bytes the backend holds.
    Atom type = None;
    int format = 0;
    std::string data;
    MIRAGE_CHECK(peer_read_selection(client, requestor, clipboard_atom, utf8_atom, read_prop, type,
                                     format, data, pump, 4000));
    MIRAGE_CHECK(type == utf8_atom);
    MIRAGE_CHECK(format == 8);
    MIRAGE_CHECK(data == "from-mirage \xe4\xb8\x96\xe7\x95\x8c");

    // ICCCM metadata: TIMESTAMP is a real (nonzero) server timestamp...
    unsigned long stamp = 0;
    MIRAGE_CHECK(peer_read_selection(client, requestor, clipboard_atom,
                                     XInternAtom(display, "TIMESTAMP", False), read_prop, type,
                                     format, data, pump, 4000));
    MIRAGE_CHECK(type == XA_INTEGER && format == 32 && data.size() == sizeof(long));
    std::memcpy(&stamp, data.data(), sizeof(stamp));
    MIRAGE_CHECK(stamp != 0);

    // ...and TARGETS advertises UTF8_STRING.
    MIRAGE_CHECK(peer_read_selection(client, requestor, clipboard_atom,
                                     XInternAtom(display, "TARGETS", False), read_prop, type,
                                     format, data, pump, 4000));
    MIRAGE_CHECK(type == XA_ATOM && format == 32);
    bool advertises_utf8 = false;
    for (std::size_t offset = 0; offset + sizeof(Atom) <= data.size(); offset += sizeof(Atom)) {
        Atom atom = None;
        std::memcpy(&atom, data.data() + offset, sizeof(atom));
        advertises_utf8 = advertises_utf8 || atom == utf8_atom;
    }
    MIRAGE_CHECK(advertises_utf8);

    // Incremental transfer: payload above the backend's serving chunk (the
    // server's extended BIG-REQUESTS ceiling — Xvfb advertises ~16 MiB, so
    // sized dynamically because the ceiling is a server configuration, not
    // a protocol constant) delivered to a real second client in chunks. The
    // write carries an explicit budget because the payload exceeds the
    // default 1 MiB write cap.
    const std::size_t chunk = backend_chunk_bytes(display);
    const std::string big = make_utf8_payload(chunk + 64u * 1024u);
    MIRAGE_CHECK(big.size() > chunk);
    mirage::desktop::ClipboardWriteLimits big_write;
    big_write.max_bytes = big.size();
    MIRAGE_CHECK(clipboard.write_text(big, big_write, {}).ok);
    std::string received;
    MIRAGE_CHECK(peer_read_incremental(client, requestor, clipboard_atom, utf8_atom, read_prop,
                                       incr_atom, received, pump, 8000, big.size() / 4u + 8192u));
    MIRAGE_CHECK(received == big);
}

/// Paths where a peer owns the selection: the backend reads the peer's
/// content through its pump-driven service, observes a takeover of its own
/// ownership (SelectionClear), and reports unsupported_content for an owner
/// that cannot serve UTF-8.
void clipboard_peer_owned_selection(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();
    Display *display = client.get();
    const Atom clipboard_atom = XInternAtom(display, "CLIPBOARD", False);

    // The peer takes over content the backend wrote; the backend observes
    // the SelectionClear on its next provider call and afterwards serves
    // the peer's payload to a read.
    MIRAGE_CHECK(clipboard.write_text("mirage-first").ok);
    const Window backend_window = XGetSelectionOwner(display, clipboard_atom);
    MIRAGE_CHECK(backend_window != None);
    const pid_t owner = fork_clipboard_owner(server.display_name(), "peer-content", true);
    MIRAGE_CHECK(owner > 0);
    MIRAGE_CHECK(wait_for_clipboard_owner(client, clipboard_atom, backend_window, 4000));
    MIRAGE_CHECK(env.window()->front_window().ok); // pump processes the SelectionClear
    const auto read_back = clipboard.read_text();
    MIRAGE_CHECK(read_back.ok);
    MIRAGE_CHECK(read_back.content == "peer-content");
    reap_peer_owner(owner);
    MIRAGE_CHECK(env.window()->front_window().ok); // pump absorbs the released ownership

    // An owner that holds CLIPBOARD but only serves XA_STRING refuses the
    // UTF8_STRING conversion: unsupported_content, not_found is reserved
    // for "no owner at all".
    const pid_t string_owner = fork_clipboard_owner(server.display_name(), "plain", false);
    MIRAGE_CHECK(string_owner > 0);
    MIRAGE_CHECK(wait_for_clipboard_owner(client, clipboard_atom, None, 4000));
    const auto unsupported = clipboard.read_text();
    MIRAGE_CHECK(!unsupported.ok);
    MIRAGE_CHECK(unsupported.error.code == "unsupported_content");
    reap_peer_owner(string_owner);
}

/// Large-payload roundtrip where the backend is both owner and requestor:
/// one pass exercises the outgoing INCR streaming and the incoming INCR
/// reception, including a budget refusal announced by the INCR header and a
/// clean full read afterwards.
void clipboard_large_self_incremental(XvfbDisplay &server, TestClient &client) {
    LinuxDesktopEnvironment env({}, {.enabled = true, .display = server.display_name()});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();

    // Payload above the backend's serving chunk (the extended BIG-REQUESTS
    // ceiling — Xvfb advertises ~16 MiB, not the classic 256 KiB) so both
    // INCR directions engage; sized dynamically because the ceiling is a
    // server configuration. The explicit budgets cover the payload, which
    // exceeds the 1 MiB defaults.
    const std::size_t chunk = backend_chunk_bytes(client.get());
    const std::string big = make_utf8_payload(chunk + 64u * 1024u);
    MIRAGE_CHECK(big.size() > chunk);
    mirage::desktop::ClipboardWriteLimits big_write;
    big_write.max_bytes = big.size();
    mirage::desktop::ClipboardReadLimits big_read;
    big_read.max_bytes = big.size();

    MIRAGE_CHECK(clipboard.write_text(big, big_write, {}).ok);
    const auto read_back = clipboard.read_text(big_read, {});
    MIRAGE_CHECK(read_back.ok);
    MIRAGE_CHECK(read_back.content.size() == big.size());
    MIRAGE_CHECK(read_back.content == big);

    // The INCR header announces the total size, so a budget one byte under
    // the payload refuses before any chunk flows.
    mirage::desktop::ClipboardReadLimits tight;
    tight.max_bytes = big.size() - 1;
    const auto refused = clipboard.read_text(tight, {});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "clipboard_too_large");
    MIRAGE_CHECK(refused.content.empty());

    // A read following the refused one must still deliver the full payload:
    // no aborted transfer may linger on the transfer property.
    const auto after = clipboard.read_text(big_read, {});
    MIRAGE_CHECK(after.ok);
    MIRAGE_CHECK(after.content == big);
    MIRAGE_CHECK(clipboard.write_text("reset", big_write, {}).ok);
    MIRAGE_CHECK(clipboard.read_text(big_read, {}).content == "reset");
}

} // namespace

int main() {
    const std::string xvfb = mirage::testing::find_xvfb();
    if (xvfb.empty()) {
        // Loud failure, never a skip (DOD-03): the M2-02 exit conditions
        // require the X11 skeleton to be exercised against a real X server.
        std::fprintf(
            stderr,
            "Xvfb not found: install xvfb or set MIRAGE_XVFB "
            "(user-prefix extraction documented in docs/plans/m2-desktop-environment.md)\n");
        return 1;
    }
    try {
        mirage::testing::XvfbDisplay server(xvfb);
        TestClient client(server.display_name());
        if (!client.ok()) {
            std::fprintf(stderr, "cannot open test display %s\n", server.display_name().c_str());
            return 1;
        }
        open_fail_closed_on_bad_display();
        window_enumeration_and_activation(server, client);
        capture_paths(server, client);
        input_injection_events(server, client);
        negative_and_boundary_edges(server, client);
        environment_identity_and_defaults(server);
        clipboard_same_backend_roundtrips(server);
        clipboard_refusals_keep_state(server, client);
        clipboard_peer_requests_from_backend(server, client);
        clipboard_peer_owned_selection(server, client);
        clipboard_large_self_incremental(server, client);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Xvfb setup failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("x11_backend");
}
