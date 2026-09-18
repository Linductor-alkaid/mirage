#include "x11_backend.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::CaptureLimits;
using mirage::desktop::CaptureOutcome;
using mirage::desktop::DisplayListLimits;
using mirage::desktop::DisplayListOutcome;
using mirage::desktop::InputLimits;
using mirage::desktop::InputOutcome;
using mirage::desktop::KeyChord;
using mirage::desktop::KeySym;
using mirage::desktop::MouseButton;
using mirage::desktop::ProviderError;
using mirage::desktop::WindowActionOutcome;
using mirage::desktop::WindowGeometry;
using mirage::desktop::WindowInfo;
using mirage::desktop::WindowListLimits;
using mirage::desktop::WindowListOutcome;
using mirage::desktop::WindowQueryOutcome;

/// Async X errors are normal in desktop land (windows vanish under us), so
/// the default Xlib handler's stderr noise is replaced with silence: call
/// results already surface failures through X return codes / XSync.
int quiet_x_error_handler(Display *, XErrorEvent *) { return 0; }

ProviderError error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Modifier name -> X base keysym for the chord modifier keys.
unsigned long modifier_keysym(const std::string &modifier) {
    if (modifier == "ctrl") {
        return XK_Control_L;
    }
    if (modifier == "alt") {
        return XK_Alt_L;
    }
    if (modifier == "shift") {
        return XK_Shift_L;
    }
    return XK_Super_L; // "meta"
}

/// X keysym name for the plain-key vocabulary of the desktop contract.
std::string named_key_keysym_name(std::string_view base) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 15> kNames = {{
        {"enter", "Return"},
        {"tab", "Tab"},
        {"escape", "Escape"},
        {"backspace", "BackSpace"},
        {"delete", "Delete"},
        {"insert", "Insert"},
        {"home", "Home"},
        {"end", "End"},
        {"pageup", "Prior"},
        {"pagedown", "Next"},
        {"left", "Left"},
        {"right", "Right"},
        {"up", "Up"},
        {"down", "Down"},
        {"space", "space"},
    }};
    for (const auto &[mirage_name, x_name] : kNames) {
        if (base == mirage_name) {
            return std::string(x_name);
        }
    }
    if (base.size() >= 2 && base[0] == 'f') { // f1..f12 -> F1..F12
        std::string upper(base);
        upper[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(upper[0])));
        return upper;
    }
    return std::string(base);
}

/// Keysym for one codepoint per the X keysym encoding: Latin-1 maps
/// directly, everything above U+00FF uses the keysym-unicode offset.
unsigned long codepoint_keysym(std::uint32_t codepoint) {
    if (codepoint < 0x100) {
        return codepoint;
    }
    return 0x01000000u + codepoint;
}

unsigned int mouse_button_code(const MouseButton &button) {
    if (button == "left") {
        return Button1;
    }
    if (button == "middle") {
        return Button2;
    }
    if (button == "right") {
        return Button3;
    }
    if (button == "back") {
        return 8;
    }
    if (button == "forward") {
        return 9;
    }
    return 0;
}

/// Decodes the next UTF-8 codepoint; nullopt on malformed input. Callers
/// validate with mirage::desktop::is_valid_utf8 first, so this mirrors the
/// same strictness for the value extraction.
std::optional<std::uint32_t> decode_codepoint(const std::string &text, std::size_t &i) {
    const auto byte_at = [&](std::size_t k) { return static_cast<unsigned char>(text[i + k]); };
    const unsigned char lead = byte_at(0);
    if (lead <= 0x7F) {
        ++i;
        return lead;
    }
    std::uint32_t codepoint = 0;
    std::size_t length = 0;
    if ((lead & 0xE0) == 0xC0) {
        codepoint = lead & 0x1F;
        length = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        codepoint = lead & 0x0F;
        length = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        codepoint = lead & 0x07;
        length = 4;
    } else {
        return std::nullopt;
    }
    if (i + length > text.size()) {
        return std::nullopt;
    }
    for (std::size_t k = 1; k < length; ++k) {
        const unsigned char continuation = byte_at(k);
        if ((continuation & 0xC0) != 0x80) {
            return std::nullopt;
        }
        codepoint = (codepoint << 6) | (continuation & 0x3F);
    }
    i += length;
    return codepoint;
}

/// One resolved text codepoint ready for injection.
struct TextStroke {
    KeyCode keycode;
    bool shift;
};

} // namespace

struct X11Backend::XConnection {
    Display *display = nullptr;
    Atom net_active_window = None;
    Atom net_supported = None;
    Atom net_wm_name = None;
    Atom utf8_string = None;

    ~XConnection() {
        if (display != nullptr) {
            XCloseDisplay(display);
        }
    }

    Window root() const { return DefaultRootWindow(display); }
};

X11Backend::~X11Backend() = default;

std::unique_ptr<X11Backend> X11Backend::open(const std::string &display_name) {
    XInitThreads();
    XSetErrorHandler(quiet_x_error_handler);
    auto backend = std::unique_ptr<X11Backend>(new X11Backend());
    backend->connection_ = std::make_unique<XConnection>();
    backend->connection_->display =
        XOpenDisplay(display_name.empty() ? nullptr : display_name.c_str());
    if (backend->connection_->display == nullptr) {
        return nullptr; // capability honesty: no X display, no X11 providers
    }
    Display *display = backend->connection_->display;
    backend->connection_->net_active_window = XInternAtom(display, "_NET_ACTIVE_WINDOW", True);
    backend->connection_->net_supported = XInternAtom(display, "_NET_SUPPORTED", True);
    backend->connection_->net_wm_name = XInternAtom(display, "_NET_WM_NAME", True);
    backend->connection_->utf8_string = XInternAtom(display, "UTF8_STRING", True);
    return backend;
}

namespace {

/// Reads the EWMH UTF-8 window title, falling back to the classic WM_NAME.
std::string window_title(X11Backend::XConnection &connection, Window window) {
    if (connection.net_wm_name != None) {
        Atom type = None;
        int format = 0;
        unsigned long count = 0;
        unsigned long remaining = 0;
        unsigned char *data = nullptr;
        const int status =
            XGetWindowProperty(connection.display, window, connection.net_wm_name, 0, 4096, False,
                               connection.utf8_string, &type, &format, &count, &remaining, &data);
        if (status == Success && data != nullptr) {
            const char *text = reinterpret_cast<const char *>(data);
            std::string title(text, text + static_cast<std::size_t>(count));
            XFree(data);
            if (!title.empty()) {
                return title;
            }
        }
    }
    char *classic = nullptr;
    if (XFetchName(connection.display, window, &classic) && classic != nullptr) {
        std::string title(classic);
        XFree(classic);
        return title;
    }
    return {};
}

/// Global-coordinate geometry of one window.
std::optional<WindowGeometry> window_geometry(X11Backend::XConnection &connection, Window window) {
    Window root = None;
    int x = 0;
    int y = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    unsigned int border = 0;
    unsigned int depth = 0;
    if (!XGetGeometry(connection.display, window, &root, &x, &y, &width, &height, &border,
                      &depth)) {
        return std::nullopt;
    }
    Window child = None;
    int global_x = x;
    int global_y = y;
    if (XTranslateCoordinates(connection.display, window, connection.root(), 0, 0, &global_x,
                              &global_y, &child)) {
        return WindowGeometry{global_x, global_y, static_cast<std::int32_t>(width),
                              static_cast<std::int32_t>(height)};
    }
    return WindowGeometry{x, y, static_cast<std::int32_t>(width),
                          static_cast<std::int32_t>(height)};
}

bool is_viewable(X11Backend::XConnection &connection, Window window) {
    XWindowAttributes attributes;
    if (!XGetWindowAttributes(connection.display, window, &attributes)) {
        return false;
    }
    return attributes.map_state == IsViewable && attributes.override_redirect == False;
}

Window active_window_property(X11Backend::XConnection &connection) {
    if (connection.net_active_window == None) {
        return None;
    }
    Atom type = None;
    int format = 0;
    unsigned long count = 0;
    unsigned long remaining = 0;
    unsigned char *data = nullptr;
    const int status =
        XGetWindowProperty(connection.display, connection.root(), connection.net_active_window, 0,
                           1, False, XA_WINDOW, &type, &format, &count, &remaining, &data);
    if (status != Success || data == nullptr || count == 0 ||
        (type != XA_WINDOW && type != XA_ATOM)) {
        if (data != nullptr) {
            XFree(data);
        }
        return None;
    }
    const Window active = *reinterpret_cast<const Window *>(data);
    XFree(data);
    return active;
}

/// One viewable managed window in enumeration order.
std::optional<WindowInfo> describe_window(X11Backend::XConnection &connection, Window window,
                                          bool focused) {
    if (!is_viewable(connection, window)) {
        return std::nullopt;
    }
    WindowInfo info;
    info.id = std::to_string(static_cast<unsigned long>(window));
    info.title = window_title(connection, window);
    const auto geometry = window_geometry(connection, window);
    if (geometry.has_value()) {
        info.geometry = *geometry;
    }
    info.focused = focused;
    return info;
}

/// Captures `width` x `height` at global `x`,`y` from the root window (the
/// composited screen content: under XWayland the compositor paints there,
/// under plain X the framebuffer holds the stacked windows as-is).
CaptureOutcome capture_region(X11Backend::XConnection &connection, const WindowGeometry &roi,
                              const CaptureLimits &limits) {
    CaptureOutcome outcome;
    Display *display = connection.display;
    const std::size_t stride = static_cast<std::size_t>(roi.width) * 4u;
    const std::size_t bytes = stride * static_cast<std::size_t>(roi.height);
    if (bytes > limits.max_bytes) {
        outcome.error = error("capture_too_large", "capture exceeds the byte budget of " +
                                                       std::to_string(limits.max_bytes) + " bytes");
        return outcome;
    }
    if (DefaultDepthOfScreen(DefaultScreenOfDisplay(display)) < 24) {
        outcome.error = error("unsupported_platform", "capture needs a 24-bit or deeper visual");
        return outcome;
    }
    XImage *image =
        XGetImage(display, connection.root(), roi.x, roi.y, static_cast<unsigned int>(roi.width),
                  static_cast<unsigned int>(roi.height), AllPlanes, ZPixmap);
    if (image == nullptr) {
        outcome.error = error("io_error", "XGetImage failed for the requested region");
        return outcome;
    }
    if (image->bits_per_pixel != 32) {
        XDestroyImage(image);
        outcome.error =
            error("unsupported_platform", "capture expects 32-bit ZPixmap, got " +
                                              std::to_string(image->bits_per_pixel) + " bpp");
        return outcome;
    }
    outcome.ok = true;
    outcome.frame.format = mirage::desktop::ImageFormat::Bgra8;
    outcome.frame.width = roi.width;
    outcome.frame.height = roi.height;
    outcome.frame.stride = stride;
    outcome.frame.pixels.resize(bytes);
    for (std::int32_t row = 0; row < roi.height; ++row) {
        std::memcpy(outcome.frame.pixels.data() + static_cast<std::size_t>(row) * stride,
                    image->data + static_cast<std::size_t>(row) * image->bytes_per_line, stride);
    }
    XDestroyImage(image);
    return outcome;
}

/// Parses an opaque window id (decimal string of the X window number).
std::optional<Window> parse_window_id(const std::string &id) {
    if (id.empty() || !std::isdigit(static_cast<unsigned char>(id.front()))) {
        return std::nullopt; // rejects signs, whitespace and other junk up front
    }
    unsigned long value = 0;
    try {
        std::size_t consumed = 0;
        value = std::stoul(id, &consumed);
        if (consumed != id.size()) {
            return std::nullopt;
        }
    } catch (const std::exception &) {
        return std::nullopt;
    }
    if (value == 0) {
        return std::nullopt;
    }
    return static_cast<Window>(value);
}

} // namespace

mirage::desktop::WindowListOutcome X11Backend::list_windows(const WindowListLimits &limits,
                                                            const CancelToken &cancel) {
    WindowListOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    Display *display = connection_->display;
    Window root_return = None;
    Window parent_return = None;
    Window *children = nullptr;
    unsigned int child_count = 0;
    if (!XQueryTree(display, connection_->root(), &root_return, &parent_return, &children,
                    &child_count)) {
        outcome.error = error("io_error", "XQueryTree failed on the root window");
        return outcome;
    }
    std::vector<Window> ids(children, children + child_count);
    if (children != nullptr) {
        XFree(children);
    }
    // Count the viewable set before building any result: over-budget is a
    // refusal, never a silent truncation (RULE-07).
    std::size_t viewable = 0;
    for (const Window id : ids) {
        if (is_viewable(xconn(), id)) {
            ++viewable;
        }
        if (viewable > limits.max_windows) {
            outcome.error =
                error("result_too_large", "more than " + std::to_string(limits.max_windows) +
                                              " viewable windows on this display");
            return outcome;
        }
    }
    const Window active = [&]() -> Window {
        Window window = active_window_property(xconn());
        if (window == None) {
            // No EWMH window manager (bare Xvfb, lightweight setups): the
            // X input focus is the observable truth, matching front_window.
            int revert = RevertToNone;
            XGetInputFocus(display, &window, &revert);
            if (window == PointerRoot || !is_viewable(xconn(), window)) {
                return None;
            }
        }
        return window;
    }();
    outcome.ok = true;
    outcome.windows.reserve(viewable);
    for (const Window id : ids) {
        if (cancel.cancelled()) {
            outcome.ok = false;
            outcome.windows.clear();
            outcome.error = error("cancelled", "enumeration cancelled");
            return outcome;
        }
        auto info = describe_window(xconn(), id, id == active);
        if (info.has_value()) {
            outcome.windows.push_back(std::move(*info));
        }
    }
    return outcome;
}

mirage::desktop::WindowQueryOutcome X11Backend::front_window(const CancelToken &cancel) {
    WindowQueryOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.error = error("cancelled", "query cancelled");
        return outcome;
    }
    Window active = active_window_property(xconn());
    if (active == None) {
        // No EWMH window manager (bare Xvfb, some lightweight setups): fall
        // back to the X input focus, which reflects keyboard focus directly.
        Window focused = None;
        int revert = RevertToNone;
        XGetInputFocus(connection_->display, &focused, &revert);
        if (focused != None && focused != PointerRoot && is_viewable(xconn(), focused)) {
            active = focused;
        }
    }
    if (active == None) {
        outcome.ok = true; // no focused window is a valid answer, not an error
        return outcome;
    }
    auto info = describe_window(xconn(), active, true);
    if (!info.has_value()) {
        outcome.ok = true;
        return outcome;
    }
    outcome.ok = true;
    outcome.found = true;
    outcome.window = std::move(*info);
    return outcome;
}

mirage::desktop::WindowActionOutcome X11Backend::activate(const std::string &window_id,
                                                          const CancelToken &cancel) {
    WindowActionOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "activation cancelled");
        return outcome;
    }
    const auto window = parse_window_id(window_id);
    if (!window.has_value()) {
        outcome.error = error("invalid_argument", "malformed window id");
        return outcome;
    }
    if (!is_viewable(xconn(), *window)) {
        outcome.error = error("not_found", "no such viewable window");
        return outcome;
    }
    Display *display = connection_->display;
    bool sent_ewmh = false;
    if (connection_->net_active_window != None && connection_->net_supported != None) {
        // EWMH path when a window manager advertises _NET_ACTIVE_WINDOW.
        Atom type = None;
        int format = 0;
        unsigned long count = 0;
        unsigned long remaining = 0;
        unsigned char *supported = nullptr;
        const int status =
            XGetWindowProperty(display, connection_->root(), connection_->net_supported, 0, 256,
                               False, XA_ATOM, &type, &format, &count, &remaining, &supported);
        const auto *atoms = reinterpret_cast<const Atom *>(supported);
        const bool ewmh_active =
            status == Success && supported != nullptr &&
            std::find(atoms, atoms + count, connection_->net_active_window) != atoms + count;
        if (supported != nullptr) {
            XFree(supported);
        }
        if (ewmh_active) {
            XEvent event = {};
            event.xclient.type = ClientMessage;
            event.xclient.window = *window;
            event.xclient.message_type = connection_->net_active_window;
            event.xclient.format = 32;
            event.xclient.data.l[0] = 1; // source indication: application
            event.xclient.data.l[1] = CurrentTime;
            event.xclient.data.l[2] = None; // no requesting window
            XSendEvent(display, connection_->root(), False,
                       SubstructureRedirectMask | SubstructureNotifyMask, &event);
            sent_ewmh = true;
        }
    }
    if (!sent_ewmh) {
        // Headless / WM-less fallback: direct input focus and raise, which
        // Xvfb and plain window managers honor without EWMH support.
        XSetInputFocus(display, *window, RevertToParent, CurrentTime);
        XRaiseWindow(display, *window);
    }
    outcome.ok = XSync(display, False) != 0;
    if (!outcome.ok) {
        outcome.error = error("io_error", "activation request failed on the X server");
    }
    return outcome;
}

std::vector<mirage::desktop::DisplayInfo> X11Backend::query_displays_locked() {
    std::vector<mirage::desktop::DisplayInfo> displays;
    Display *display = connection_->display;
    int monitor_count = 0;
    XRRMonitorInfo *monitors = XRRGetMonitors(display, connection_->root(), True, &monitor_count);
    if (monitors == nullptr || monitor_count <= 0) {
        if (monitors != nullptr) {
            XRRFreeMonitors(monitors);
        }
        // RandR-less server: expose the root geometry as one stable display.
        XWindowAttributes attributes;
        if (!XGetWindowAttributes(display, connection_->root(), &attributes)) {
            return displays;
        }
        displays.push_back(
            {"screen",
             {attributes.x, attributes.y, static_cast<std::int32_t>(attributes.width),
              static_cast<std::int32_t>(attributes.height)},
             true});
        return displays;
    }
    for (int i = 0; i < monitor_count; ++i) {
        const XRRMonitorInfo &monitor = monitors[i];
        char *name = XGetAtomName(display, monitor.name);
        std::string id = name != nullptr ? name : "screen";
        if (name != nullptr) {
            XFree(name);
        }
        displays.push_back({std::move(id),
                            {monitor.x, monitor.y, static_cast<std::int32_t>(monitor.width),
                             static_cast<std::int32_t>(monitor.height)},
                            monitor.primary != 0});
    }
    XRRFreeMonitors(monitors);
    return displays;
}

mirage::desktop::DisplayListOutcome X11Backend::list_displays(const DisplayListLimits &limits,
                                                              const CancelToken &cancel) {
    DisplayListOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.error = error("cancelled", "enumeration cancelled");
        return outcome;
    }
    std::vector<mirage::desktop::DisplayInfo> displays = query_displays_locked();
    if (displays.empty()) {
        outcome.error = error("io_error", "root window geometry unavailable");
        return outcome;
    }
    if (displays.size() > limits.max_displays) {
        outcome.error =
            error("result_too_large",
                  "more than " + std::to_string(limits.max_displays) + " monitors on this display");
        return outcome;
    }
    outcome.ok = true;
    outcome.displays = std::move(displays);
    return outcome;
}

CaptureOutcome X11Backend::capture_display(const std::string &display_id,
                                           const CaptureLimits &limits, const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        CaptureOutcome outcome;
        outcome.cancelled = true;
        outcome.error = error("cancelled", "capture cancelled");
        return outcome;
    }
    for (const auto &display : query_displays_locked()) {
        if (display.id == display_id) {
            return capture_region(xconn(), display.geometry, limits);
        }
    }
    CaptureOutcome outcome;
    outcome.error = error("not_found", "unknown display id");
    return outcome;
}

CaptureOutcome X11Backend::capture_window(const std::string &window_id, const CaptureLimits &limits,
                                          const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        CaptureOutcome outcome;
        outcome.cancelled = true;
        outcome.error = error("cancelled", "capture cancelled");
        return outcome;
    }
    const auto window = parse_window_id(window_id);
    if (!window.has_value()) {
        CaptureOutcome outcome;
        outcome.error = error("invalid_argument", "malformed window id");
        return outcome;
    }
    // Fail closed for windows that are not on screen (gone or unmapped):
    // there is no honest window content to return. Overlapping content is
    // included by design — the root-framebuffer model captures what a viewer
    // sees at the window's bounds (DEC-015).
    if (!is_viewable(xconn(), *window)) {
        CaptureOutcome outcome;
        outcome.error = error("not_found", "window is not viewable on this display");
        return outcome;
    }
    const auto geometry = window_geometry(xconn(), *window);
    if (!geometry.has_value() || geometry->width <= 0 || geometry->height <= 0) {
        CaptureOutcome outcome;
        outcome.error = error("not_found", "no such window");
        return outcome;
    }
    return capture_region(xconn(), *geometry, limits);
}

CaptureOutcome X11Backend::capture_roi(const WindowGeometry &roi, const CaptureLimits &limits,
                                       const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        CaptureOutcome outcome;
        outcome.cancelled = true;
        outcome.error = error("cancelled", "capture cancelled");
        return outcome;
    }
    if (roi.width <= 0 || roi.height <= 0) {
        CaptureOutcome outcome;
        outcome.error = error("invalid_argument", "roi extents must be positive");
        return outcome;
    }
    return capture_region(xconn(), roi, limits);
}

InputOutcome X11Backend::inject_key(const KeySym &key, bool pressed, const InputLimits &limits,
                                    const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    InputOutcome outcome;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "injection cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    const auto chord = mirage::desktop::parse_key_chord(key.name);
    if (!chord.has_value()) {
        outcome.error = error("invalid_argument", "unknown key name");
        return outcome;
    }
    Display *display = connection_->display;
    // Resolve every keycode before injecting anything: a chord fails as a
    // whole or not at all.
    const auto resolve = [&](const std::string &base) -> KeyCode {
        if (base.size() == 1) {
            const auto *single = reinterpret_cast<const unsigned char *>(base.data());
            return XKeysymToKeycode(display, codepoint_keysym(single[0]));
        }
        return XKeysymToKeycode(display, XStringToKeysym(named_key_keysym_name(base).c_str()));
    };
    const KeyCode base_code = resolve(chord->base);
    if (base_code == 0) {
        outcome.error = error("unsupported_key", "key is not in the current keyboard mapping");
        return outcome;
    }
    std::vector<KeyCode> modifiers;
    const std::pair<const char *, bool> modifier_flags[4] = {
        {"ctrl", chord->ctrl}, {"alt", chord->alt}, {"shift", chord->shift}, {"meta", chord->meta}};
    for (const auto &[modifier, active] : modifier_flags) {
        if (!active) {
            continue;
        }
        const KeyCode code = XKeysymToKeycode(display, modifier_keysym(modifier));
        if (code == 0) {
            outcome.error = error("unsupported_key", "modifier key is not in the keyboard mapping");
            return outcome;
        }
        modifiers.push_back(code);
    }
    if (pressed) {
        for (const KeyCode code : modifiers) {
            XTestFakeKeyEvent(display, code, True, CurrentTime);
        }
        XTestFakeKeyEvent(display, base_code, True, CurrentTime);
    } else {
        XTestFakeKeyEvent(display, base_code, False, CurrentTime);
        for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
            XTestFakeKeyEvent(display, *it, False, CurrentTime);
        }
    }
    outcome.ok = XSync(display, False) != 0;
    if (!outcome.ok) {
        outcome.error = error("io_error", "key injection failed on the X server");
    }
    return outcome;
}

InputOutcome X11Backend::type_text(const std::string &text, const InputLimits &limits,
                                   const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    InputOutcome outcome;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "injection cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    if (text.size() > limits.max_text_bytes) {
        outcome.error = error("invalid_argument", "text exceeds the length budget");
        return outcome;
    }
    if (!mirage::desktop::is_valid_utf8(text)) {
        outcome.error = error("invalid_argument", "text must be UTF-8");
        return outcome;
    }
    Display *display = connection_->display;
    // Resolve the whole stroke plan before injecting anything (contract:
    // rejections happen before any side effect).
    std::vector<TextStroke> strokes;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto codepoint = decode_codepoint(text, i);
        if (!codepoint.has_value() || *codepoint < 0x20 ||
            (*codepoint >= 0x7F && *codepoint < 0xA0)) {
            outcome.error = error("invalid_argument", "text contains non-printable characters");
            return outcome;
        }
        const unsigned long keysym = codepoint_keysym(*codepoint);
        const KeyCode keycode = XKeysymToKeycode(display, keysym);
        if (keycode == 0) {
            outcome.error =
                error("unsupported_key", "character is not in the current keyboard mapping");
            return outcome;
        }
        const unsigned long base = XkbKeycodeToKeysym(display, keycode, 0, 0);
        const unsigned long shifted = XkbKeycodeToKeysym(display, keycode, 0, 1);
        if (base == keysym) {
            strokes.push_back({keycode, false});
        } else if (shifted == keysym) {
            strokes.push_back({keycode, true});
        } else {
            outcome.error = error("unsupported_key", "character needs an unhandled level or group");
            return outcome;
        }
    }
    const KeyCode shift_code = XKeysymToKeycode(display, XK_Shift_L);
    for (const TextStroke &stroke : strokes) {
        if (stroke.shift) {
            XTestFakeKeyEvent(display, shift_code, True, CurrentTime);
        }
        XTestFakeKeyEvent(display, stroke.keycode, True, CurrentTime);
        XTestFakeKeyEvent(display, stroke.keycode, False, CurrentTime);
        if (stroke.shift) {
            XTestFakeKeyEvent(display, shift_code, False, CurrentTime);
        }
    }
    outcome.ok = XSync(display, False) != 0;
    if (!outcome.ok) {
        outcome.error = error("io_error", "text injection failed on the X server");
    }
    return outcome;
}

InputOutcome X11Backend::pointer_move(std::int32_t x, std::int32_t y, const InputLimits &limits,
                                      const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    InputOutcome outcome;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "injection cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    XTestFakeMotionEvent(connection_->display, -1, x, y, CurrentTime);
    outcome.ok = XSync(connection_->display, False) != 0;
    if (!outcome.ok) {
        outcome.error = error("io_error", "pointer move failed on the X server");
    }
    return outcome;
}

InputOutcome X11Backend::pointer_button(const MouseButton &button, bool pressed,
                                        const InputLimits &limits, const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    InputOutcome outcome;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "injection cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    const unsigned int code = mouse_button_code(button);
    if (code == 0) {
        outcome.error = error("invalid_argument", "unknown mouse button");
        return outcome;
    }
    XTestFakeButtonEvent(connection_->display, code, pressed ? True : False, CurrentTime);
    outcome.ok = XSync(connection_->display, False) != 0;
    if (!outcome.ok) {
        outcome.error = error("io_error", "button injection failed on the X server");
    }
    return outcome;
}

} // namespace mirage::platform::linux_backend
