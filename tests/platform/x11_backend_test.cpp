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

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

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

void open_fail_closed_on_bad_display() {
    // Capability honesty end-to-end: an environment whose X11 opt-in fails
    // exposes no desktop surface at all.
    LinuxDesktopEnvironment without_x11({}, {.enabled = true, .display = "missing-99"});
    MIRAGE_CHECK(without_x11.window() == nullptr);
    MIRAGE_CHECK(without_x11.screen() == nullptr);
    MIRAGE_CHECK(without_x11.input() == nullptr);
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
    // Providers the M2-03 milestone has not landed yet stay null (fail
    // closed), while the M1 surface remains available.
    MIRAGE_CHECK(env.accessibility() == nullptr);
    MIRAGE_CHECK(env.clipboard() == nullptr);
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
    MIRAGE_CHECK(silent.filesystem() != nullptr);
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
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Xvfb setup failed: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("x11_backend");
}
