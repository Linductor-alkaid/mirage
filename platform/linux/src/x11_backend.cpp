#include "x11_backend.hpp"
#include <mirage/desktop/visual_snapshot.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <poll.h>

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XTest.h>
#ifdef MIRAGE_LINUX_XRES
#include <X11/extensions/XRes.h>
#endif
#include <X11/extensions/Xrandr.h>
#include <X11/keysym.h>

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::CaptureLimits;
using mirage::desktop::CaptureOutcome;
using mirage::desktop::ClipboardReadLimits;
using mirage::desktop::ClipboardReadOutcome;
using mirage::desktop::ClipboardWriteLimits;
using mirage::desktop::ClipboardWriteOutcome;
using mirage::desktop::DisplayListLimits;
using mirage::desktop::DisplayListOutcome;
using mirage::desktop::InputLimits;
using mirage::desktop::InputOutcome;
using mirage::desktop::KeyChord;
using mirage::desktop::KeySym;
using mirage::desktop::MouseButton;
using mirage::desktop::PointerQueryOutcome;
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

// DEC-065: server resource ownership, never PID/title/property whitelists.
// Only one newly visible same-connection helper may hide the target branch.
struct DragHelpers {
    std::array<Window, 128> visible{};
    std::size_t visible_count = 0;
    XID base = 0, resource_mask = 0;
    bool available = false;

    void prepare(Display *display, Window target, Window root) {
#ifdef MIRAGE_LINUX_XRES
        int event = 0, error_base = 0, count = 0;
        XResClient *clients = nullptr;
        if (!XResQueryExtension(display, &event, &error_base))
            return;
        const bool queried = XResQueryClients(display, &count, &clients);
        if (queried && clients && count > 0 && count <= 256)
            for (int n = 0; n < count; ++n)
                if ((target & ~clients[n].resource_mask) == clients[n].resource_base) {
                    base = clients[n].resource_base;
                    resource_mask = clients[n].resource_mask;
                    break;
                }
        if (clients)
            XFree(clients);
        if (!base)
            return;
        Window returned_root = None, parent = None, *children = nullptr;
        unsigned int children_count = 0;
        const bool tree =
            XQueryTree(display, root, &returned_root, &parent, &children, &children_count);
        bool valid = tree && children_count <= visible.size();
        for (unsigned int n = 0; valid && n < children_count; ++n) {
            XWindowAttributes attributes{};
            valid = XGetWindowAttributes(display, children[n], &attributes) != 0;
            if (valid && attributes.map_state == IsViewable)
                visible[visible_count++] = children[n];
        }
        if (children)
            XFree(children);
        available = valid;
#else
        (void)display;
        (void)target;
        (void)root;
#endif
    }

    template <class Walk>
    bool target_below(Display *display, Window root, int x, int y, Walk &&walk) const {
        if (!available)
            return false;
        Window returned_root = None, parent = None, *children = nullptr;
        unsigned int count = 0;
        const bool queried = XQueryTree(display, root, &returned_root, &parent, &children, &count);
        bool result = false;
        if (queried && count <= visible.size()) {
            bool skipped = false;
            // XQueryTree returns bottom-to-top stacking order.
            for (unsigned int n = count; n > 0; --n) {
                const auto child = children[n - 1];
                XWindowAttributes a{};
                if (!XGetWindowAttributes(display, child, &a))
                    break;
                if (a.map_state != IsViewable || x < std::int64_t{a.x} - a.border_width ||
                    y < std::int64_t{a.y} - a.border_width ||
                    x >= std::int64_t{a.x} + a.width + a.border_width ||
                    y >= std::int64_t{a.y} + a.height + a.border_width)
                    continue;
                if (!skipped && a.override_redirect && a.c_class == InputOutput &&
                    (child & ~resource_mask) == base &&
                    std::find(visible.begin(), visible.begin() + visible_count, child) ==
                        visible.begin() + visible_count) {
                    skipped = true;
                    continue;
                }
                result = skipped && walk(child);
                break; // Never look through an unrelated receiver/occluder.
            }
        }
        if (children)
            XFree(children);
        return result;
    }
};

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

    // Clipboard surface (M2-04): one hidden, never-mapped window carries the
    // CLIPBOARD selection ownership, the transfer property for reads and the
    // timestamps ICCCM requires. SelectionRequest / SelectionClear /
    // SelectionNotify arrive without an event mask; PropertyChangeMask is
    // selected for the incremental transfers in both directions.
    Window clipboard_window = None;
    Atom clipboard = None;
    Atom targets = None;
    Atom incr = None;
    Atom timestamp_target = None;
    Atom transfer_property = None;
    Atom timestamp_marker = None;
    bool clipboard_ready = false;

    /// Payload served while we own the CLIPBOARD selection.
    std::string clipboard_payload;
    bool clipboard_owned = false;
    unsigned long clipboard_timestamp = 0;

    /// One in-progress outgoing incremental (INCR) transfer (ICCCM): the
    /// INCR header is on the requestor's property and each PropertyDelete
    /// releases the next chunk.
    struct OutgoingTransfer {
        Window requestor = None;
        Atom property = None;
        std::size_t offset = 0;
        std::size_t chunk = 0;
        bool started = false; ///< header acked, chunks may flow
    };
    std::vector<OutgoingTransfer> transfers;

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
    backend->connection_->net_wm_name = XInternAtom(display, "_NET_WM_NAME", False);
    // UTF8_STRING must be created when missing (only_if_exists = False): on
    // a fresh server no client has interned it yet, so `True` returns None
    // and every clipboard read would convert with an invalid None target
    // (found by the M2-04 verification pass against a clean Xvfb).
    backend->connection_->utf8_string = XInternAtom(display, "UTF8_STRING", False);
    XConnection &connection = *backend->connection_;
    connection.clipboard = XInternAtom(display, "CLIPBOARD", False);
    connection.targets = XInternAtom(display, "TARGETS", False);
    connection.incr = XInternAtom(display, "INCR", False);
    connection.timestamp_target = XInternAtom(display, "TIMESTAMP", False);
    connection.transfer_property = XInternAtom(display, "MIRAGE_CLIPBOARD_DATA", False);
    connection.timestamp_marker = XInternAtom(display, "MIRAGE_CLIPBOARD_TIMESTAMP", False);
    connection.clipboard_window =
        XCreateSimpleWindow(display, backend->connection_->root(), 0, 0, 1, 1, 0, 0, 0);
    if (connection.clipboard_window != None) {
        XSelectInput(display, connection.clipboard_window, PropertyChangeMask);
        connection.clipboard_ready = XSync(display, False) != 0;
    }
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
            if (type == connection.utf8_string && format == 8 && remaining == 0 && !title.empty() &&
                mirage::desktop::is_valid_utf8(title)) {
                return title;
            }
        }
    }
    // ICCCM WM_NAME/XA_STRING is Latin-1, not UTF-8. Bound the read before allocation.
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    const auto status =
        XGetWindowProperty(connection.display, window, XA_WM_NAME, 0, 4096, False, AnyPropertyType,
                           &type, &format, &count, &remaining, &data);
    std::string title;
    if (status == Success && data && format == 8 && remaining == 0) {
        if (type == XA_STRING) {
            title.reserve(count * 2);
            for (unsigned long n = 0; n < count; ++n) {
                const auto byte = data[n];
                if (byte < 128)
                    title.push_back(static_cast<char>(byte));
                else {
                    title.push_back(static_cast<char>(0xc0 | (byte >> 6)));
                    title.push_back(static_cast<char>(0x80 | (byte & 0x3f)));
                }
            }
        } else if (type == connection.utf8_string) {
            title.assign(reinterpret_cast<const char *>(data), count);
            if (!mirage::desktop::is_valid_utf8(title))
                title.clear();
        }
    }
    if (data)
        XFree(data);
    return title;
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
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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

// Complete actions own only keys/buttons observed released during preflight.
InputOutcome X11Backend::key_chord(const KeySym &key, const InputLimits &limits,
                                   const CancelToken &cancel) {
    std::lock_guard guard(mutex_);
    InputOutcome out;
    const auto chord = mirage::desktop::parse_key_chord(key.name);
    if (!chord || limits.timeout.count() <= 0) {
        out.error = error("invalid_argument", "invalid key or input budget");
        return out;
    }
    Display *display = connection_->display;
    std::vector<KeyCode> codes;
    const std::pair<const char *, bool> flags[4] = {
        {"ctrl", chord->ctrl}, {"alt", chord->alt}, {"shift", chord->shift}, {"meta", chord->meta}};
    for (const auto &[name, active] : flags)
        if (active)
            codes.push_back(XKeysymToKeycode(display, modifier_keysym(name)));
    const auto symbol = chord->base.size() == 1
                            ? codepoint_keysym(static_cast<unsigned char>(chord->base[0]))
                            : XStringToKeysym(named_key_keysym_name(chord->base).c_str());
    const auto base = XKeysymToKeycode(display, symbol);
    if (chord->base.size() == 1 && base != 0 && XkbKeycodeToKeysym(display, base, 0, 0) != symbol) {
        if (XkbKeycodeToKeysym(display, base, 0, 1) != symbol) {
            out.error = error("unsupported_key", "key needs an unsupported keyboard level");
            return out;
        }
        const auto shift = XKeysymToKeycode(display, XK_Shift_L);
        if (std::find(codes.begin(), codes.end(), shift) == codes.end())
            codes.push_back(shift);
    }
    codes.push_back(base);
    char physical[32]{};
    XQueryKeymap(display, physical);
    for (const auto modifier_symbol : {XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R, XK_Shift_L,
                                       XK_Shift_R, XK_Super_L, XK_Super_R}) {
        const auto modifier = XKeysymToKeycode(display, modifier_symbol);
        if (modifier && (physical[modifier / 8] & (1 << (modifier % 8)))) {
            out.error = error("input_busy", "a modifier is already physically held");
            return out;
        }
    }
    for (const auto code : codes) {
        if (!code) {
            out.error = error("unsupported_key", "key is absent from the keymap");
            return out;
        }
        if (physical[code / 8] & (1 << (code % 8))) {
            out.error = error("input_busy", "requested key is already physically held");
            return out;
        }
    }
    if (cancel.cancelled()) {
        out.cancelled = true;
        out.error = error("cancelled", "key cancelled");
        return out;
    }
    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    bool dispatched = true;
    std::size_t owned = 0;
    for (const auto code : codes) {
        if (cancel.cancelled() || std::chrono::steady_clock::now() >= deadline) {
            dispatched = false;
            break;
        }
        ++owned; // An attempted down can have reached the server even on failure.
        out.side_effect_may_have_occurred = true;
        if (!XTestFakeKeyEvent(display, code, True, CurrentTime)) {
            dispatched = false;
            break;
        }
    }
    bool released = true;
    while (owned > 0)
        released =
            (XTestFakeKeyEvent(display, codes[--owned], False, CurrentTime) != 0) && released;
    released = (XSync(display, False) != 0) && released;
    out.input_released = released;
    out.cancelled = cancel.cancelled();
    out.ok =
        dispatched && released && !out.cancelled && std::chrono::steady_clock::now() < deadline;
    if (!out.ok)
        out.error = error(!released                                      ? "input_release_failed"
                          : out.cancelled                                ? "cancelled"
                          : std::chrono::steady_clock::now() >= deadline ? "timeout"
                                                                         : "io_error",
                          "key action did not complete; do not retry uncertain input");
    return out;
}

InputOutcome X11Backend::click_at(std::int32_t x, std::int32_t y, const MouseButton &button,
                                  const InputLimits &limits, const CancelToken &cancel) {
    std::lock_guard guard(mutex_);
    InputOutcome out;
    const auto code = mouse_button_code(button);
    if (!code || limits.timeout.count() <= 0) {
        out.error = error("invalid_argument", "invalid click");
        return out;
    }
    Display *display = connection_->display;
    Window root{}, child{};
    int rx{}, ry{}, wx{}, wy{};
    unsigned int mask{};
    if (!XQueryPointer(display, DefaultRootWindow(display), &root, &child, &rx, &ry, &wx, &wy,
                       &mask)) {
        out.error = error("io_error", "pointer state unavailable");
        return out;
    }
    if (code > 5) {
        out.error = error("unsupported_key", "complete click supports primary buttons only");
        return out;
    }
    if (mask & (Button1Mask << (code - 1))) {
        out.error = error("input_busy", "button already physically held");
        return out;
    }
    if (cancel.cancelled()) {
        out.cancelled = true;
        out.error = error("cancelled", "click cancelled");
        return out;
    }
    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    out.side_effect_may_have_occurred = true;
    bool sent = XTestFakeMotionEvent(display, -1, x, y, CurrentTime) != 0;
    bool pressed = false;
    if (sent && !cancel.cancelled() && std::chrono::steady_clock::now() < deadline) {
        pressed = true;
        sent = XTestFakeButtonEvent(display, code, True, CurrentTime) != 0;
    } else
        sent = false;
    bool released = !pressed || XTestFakeButtonEvent(display, code, False, CurrentTime) != 0;
    released = (XSync(display, False) != 0) && released;
    out.input_released = released;
    out.cancelled = cancel.cancelled();
    out.ok = sent && released && !out.cancelled && std::chrono::steady_clock::now() < deadline;
    if (!out.ok)
        out.error = error(!released                                      ? "input_release_failed"
                          : out.cancelled                                ? "cancelled"
                          : std::chrono::steady_clock::now() >= deadline ? "timeout"
                                                                         : "io_error",
                          "click action did not complete; do not retry uncertain input");
    return out;
}

InputOutcome X11Backend::scroll_at(const mirage::desktop::PointerActionScope &scope,
                                   mirage::desktop::PointerPoint at, std::int32_t horizontal,
                                   std::int32_t vertical, const InputLimits &limits,
                                   const CancelToken &cancel) {
    std::lock_guard guard(mutex_);
    return pointer_path_locked(scope, at, at, horizontal, vertical, false, limits, cancel);
}

InputOutcome X11Backend::drag_to(const mirage::desktop::PointerActionScope &scope,
                                 mirage::desktop::PointerPoint from,
                                 mirage::desktop::PointerPoint to, const InputLimits &limits,
                                 const CancelToken &cancel) {
    std::lock_guard guard(mutex_);
    return pointer_path_locked(scope, from, to, 0, 0, true, limits, cancel);
}

InputOutcome X11Backend::pointer_path_locked(const mirage::desktop::PointerActionScope &scope,
                                             mirage::desktop::PointerPoint from,
                                             mirage::desktop::PointerPoint to,
                                             std::int32_t horizontal, std::int32_t vertical,
                                             bool drag, const InputLimits &limits,
                                             const CancelToken &cancel) {
    InputOutcome out;
    if (!mirage::desktop::valid_pointer_scope(scope, from, limits) ||
        !mirage::desktop::valid_pointer_scope(scope, to, limits) ||
        (drag ? !mirage::desktop::valid_drag_path(from, to)
              : !mirage::desktop::valid_scroll_steps(horizontal, vertical))) {
        out.error = error("invalid_argument", "invalid or over-budget pointer action");
        return out;
    }
    const auto window = parse_window_id(scope.window_id);
    if (!window) {
        out.error = error("invalid_argument", "invalid pointer window");
        return out;
    }
    Display *display = connection_->display;
    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    // Walk actual focus ancestry without reentering a Provider (mutex held).
    const auto focused = [&] {
        Window current = None;
        int revert = 0;
        XGetInputFocus(display, &current, &revert);
        for (int depth = 0; depth < 32 && current != None && current != PointerRoot; ++depth) {
            if (current == *window)
                return true;
            Window root = None, parent = None, *children = nullptr;
            unsigned int count = 0;
            const bool queried = XQueryTree(display, current, &root, &parent, &children, &count);
            if (children)
                XFree(children);
            if (!queried || current == parent)
                break;
            current = parent;
        }
        return false;
    };
    const auto stable = [&] {
        const auto geometry = window_geometry(xconn(), *window);
        const auto &expected = scope.geometry;
        const auto active = active_window_property(xconn());
        return geometry && is_viewable(xconn(), *window) && geometry->x == expected.x &&
               geometry->y == expected.y && geometry->width == expected.width &&
               geometry->height == expected.height && (active == None || active == *window) &&
               focused();
    };
    unsigned int owned = 0;
    DragHelpers helpers;
    const auto pointer = [&](unsigned int &mask, bool check_owner) {
        const auto walk = [&](Window current) {
            for (int depth = 0; depth < 32; ++depth) {
                Window root = None, child = None;
                int rx = 0, ry = 0, wx = 0, wy = 0;
                if (!XQueryPointer(display, current, &root, &child, &rx, &ry, &wx, &wy, &mask))
                    return false;
                if (current == *window)
                    return true;
                if (child == None)
                    return false;
                current = child;
            }
            return false;
        };
        const auto root = connection_->root();
        Window returned_root = None, child = None;
        int rx = 0, ry = 0, wx = 0, wy = 0;
        if (!XQueryPointer(display, root, &returned_root, &child, &rx, &ry, &wx, &wy, &mask))
            return false;
        if (!check_owner || walk(root))
            return true;
        // QueryPointer follows the logical pointer ancestry: an occluded WM
        // frame does not reveal its child. Resolve the first geometric hit
        // within that branch instead, with an independent descendant budget.
        unsigned int remaining = 128;
        const auto below = [&](auto &&self, Window current, int depth) -> bool {
            if (current == *window)
                return true;
            if (depth >= 32)
                return false;
            Window translated_child = None;
            int local_x = 0, local_y = 0;
            if (!XTranslateCoordinates(display, root, current, rx, ry, &local_x, &local_y,
                                       &translated_child))
                return false;
            Window branch_root = None, parent = None, *children = nullptr, hit = None;
            unsigned int count = 0;
            const bool queried =
                XQueryTree(display, current, &branch_root, &parent, &children, &count);
            if (queried && count <= remaining) {
                remaining -= count;
                for (unsigned int n = count; n > 0; --n) {
                    XWindowAttributes a{};
                    if (!XGetWindowAttributes(display, children[n - 1], &a))
                        break;
                    if (a.map_state == IsViewable &&
                        local_x >= std::int64_t{a.x} - a.border_width &&
                        local_y >= std::int64_t{a.y} - a.border_width &&
                        local_x < std::int64_t{a.x} + a.width + a.border_width &&
                        local_y < std::int64_t{a.y} + a.height + a.border_width) {
                        hit = children[n - 1];
                        break;
                    }
                }
            }
            if (children)
                XFree(children);
            return hit != None && self(self, hit, depth + 1);
        };
        return drag && owned == Button1 &&
               helpers.target_below(display, root, rx, ry,
                                    [&](Window branch) { return below(below, branch, 0); });
    };
    unsigned int mask = 0;
    constexpr unsigned int buttons =
        Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask;
    if (!pointer(mask, false)) {
        out.error = error("io_error", "pointer state unavailable");
        return out;
    }
    char physical[32]{};
    XQueryKeymap(display, physical);
    bool modifier_held = false;
    for (const auto symbol : {XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R, XK_Shift_L,
                              XK_Shift_R, XK_Super_L, XK_Super_R}) {
        const auto code = XKeysymToKeycode(display, symbol);
        modifier_held |= code && (physical[code / 8] & (1 << (code % 8)));
    }
    if ((mask & buttons) || modifier_held) {
        out.error = error("input_busy", "physical button or modifier already held");
        return out;
    }
    // No allocation/throwing application callbacks while a button is owned.
    // CancelToken probes are noexcept; cleanup ignores cancellation/deadline.
    if (drag)
        helpers.prepare(display, *window, connection_->root());
    const char *failure = nullptr;
    bool drag_press_attempted = false;
    bool released = true;
    const auto ready = [&](bool check_owner) {
        if (cancel.cancelled())
            failure = "cancelled";
        else if (std::chrono::steady_clock::now() >= deadline)
            failure = "timeout";
        else if (!stable())
            failure = "focus_changed";
        else if (!pointer(mask, check_owner))
            failure = "target_obscured";
        else if (mask & (buttons & ~(owned == Button1 ? Button1Mask : 0u)))
            failure = "input_busy";
        else if (mask & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask))
            failure = "input_busy";
        if (!failure && std::chrono::steady_clock::now() >= deadline)
            failure = "timeout";
        return failure == nullptr;
    };
    const auto move = [&](mirage::desktop::PointerPoint point) {
        if (!ready(owned == Button1))
            return false;
        out.side_effect_may_have_occurred = true;
        // DEC-064: XTEST's native delivery delay gives external toolkits time
        // to establish their drag session instead of coalescing the entire
        // gesture before release. This is one synchronous platform request,
        // not a private timer/task; cleanup release always has zero delay.
        const unsigned long delivery_delay = drag && owned == Button1 ? 8UL : CurrentTime;
        if (!XTestFakeMotionEvent(display, -1, point.x, point.y, delivery_delay) ||
            !XSync(display, False)) {
            failure = "io_error";
            return false;
        }
        return ready(true);
    };
    const auto release = [&] {
        if (owned) {
            const bool sent = XTestFakeButtonEvent(display, owned, False, CurrentTime) != 0;
            released = sent && released;
            owned = 0;
        }
        released = (XSync(display, False) != 0) && released;
    };
    if (move(from)) {
        if (drag) {
            owned = Button1; // attempted down may have reached the server
            drag_press_attempted = true;
            if (!XTestFakeButtonEvent(display, owned, True, CurrentTime) || !XSync(display, False))
                failure = "io_error";
            for (int step = 1; step <= 16 && !failure; ++step) {
                const mirage::desktop::PointerPoint point{
                    static_cast<std::int32_t>(std::int64_t{from.x} +
                                              (std::int64_t{to.x} - from.x) * step / 16),
                    static_cast<std::int32_t>(std::int64_t{from.y} +
                                              (std::int64_t{to.y} - from.y) * step / 16)};
                if (!move(point))
                    break;
            }
        } else {
            for (int axis = 0; axis < 2 && !failure; ++axis) {
                const auto count = axis == 0 ? vertical : horizontal;
                const auto code = axis == 0 ? (count < 0 ? 4u : 5u) : (count < 0 ? 6u : 7u);
                for (int step = 0; step < (count < 0 ? -count : count) && !failure; ++step) {
                    if (!ready(true))
                        break;
                    owned = code;
                    if (!XTestFakeButtonEvent(display, owned, True, CurrentTime))
                        failure = "io_error";
                    release();
                    if (!released)
                        failure = "input_release_failed";
                }
            }
        }
    }
    release();
    unsigned int final_mask = 0;
    if (drag_press_attempted && (!pointer(final_mask, false) || (final_mask & Button1Mask)))
        released = false;
    if (!failure)
        (void)ready(false);
    out.input_released = released;
    out.cancelled = failure && std::strcmp(failure, "cancelled") == 0;
    out.ok = !failure && released;
    if (!out.ok)
        out.error = error(released ? (failure ? failure : "io_error") : "input_release_failed",
                          "pointer action did not complete; do not retry uncertain input");
    return out;
}

InputOutcome X11Backend::type_text(const std::string &text, const InputLimits &limits,
                                   const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    pump_clipboard_locked();
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
        if (!codepoint.has_value() || (*codepoint < 0x20 && *codepoint != 10) ||
            (*codepoint >= 0x7F && *codepoint < 0xA0)) {
            outcome.error = error("invalid_argument", "text contains non-printable characters");
            return outcome;
        }
        const unsigned long keysym = *codepoint == 10 ? XK_Return : codepoint_keysym(*codepoint);
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
    char physical[32]{};
    XQueryKeymap(display, physical);
    for (const auto modifier_symbol : {XK_Control_L, XK_Control_R, XK_Alt_L, XK_Alt_R, XK_Shift_L,
                                       XK_Shift_R, XK_Super_L, XK_Super_R}) {
        const auto modifier = XKeysymToKeycode(display, modifier_symbol);
        if (modifier && (physical[modifier / 8] & (1 << (modifier % 8)))) {
            outcome.error = error("input_busy", "a modifier is already physically held");
            return outcome;
        }
    }
    for (const auto &stroke : strokes) {
        if ((physical[stroke.keycode / 8] & (1 << (stroke.keycode % 8))) ||
            (physical[shift_code / 8] & (1 << (shift_code % 8)))) {
            outcome.error = error("input_busy", "text keys or shift are already physically held");
            return outcome;
        }
    }
    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    for (const TextStroke &stroke : strokes) {
        if (cancel.cancelled() || std::chrono::steady_clock::now() >= deadline) {
            outcome.cancelled = cancel.cancelled();
            outcome.error = error(outcome.cancelled ? "cancelled" : "timeout",
                                  "text input stopped between complete strokes");
            XSync(display, False);
            return outcome;
        }
        outcome.side_effect_may_have_occurred = true;
        bool sent = true;
        if (stroke.shift)
            sent = XTestFakeKeyEvent(display, shift_code, True, CurrentTime) != 0;
        sent = (XTestFakeKeyEvent(display, stroke.keycode, True, CurrentTime) != 0) && sent;
        // Release even if down reported failure; cancellation cannot block cleanup.
        bool released = XTestFakeKeyEvent(display, stroke.keycode, False, CurrentTime) != 0;
        if (stroke.shift)
            released =
                (XTestFakeKeyEvent(display, shift_code, False, CurrentTime) != 0) && released;
        if (!sent || !released) {
            outcome.input_released = released;
            outcome.error = error(released ? "io_error" : "input_release_failed",
                                  "text stroke failed; input may have occurred");
            XSync(display, False);
            return outcome;
        }
    }
    outcome.ok = XSync(display, False) != 0;
    if (!outcome.ok) {
        outcome.input_released = false;
        outcome.error = error("io_error", "text injection failed on the X server");
    } else if (cancel.cancelled() || std::chrono::steady_clock::now() >= deadline) {
        outcome.ok = false;
        outcome.cancelled = cancel.cancelled();
        outcome.error = error(outcome.cancelled ? "cancelled" : "timeout",
                              "text input stopped after its last complete stroke");
    }
    return outcome;
}

InputOutcome X11Backend::pointer_move(std::int32_t x, std::int32_t y, const InputLimits &limits,
                                      const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    pump_clipboard_locked();
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
    pump_clipboard_locked();
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

PointerQueryOutcome X11Backend::pointer_position(const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    pump_clipboard_locked();
    PointerQueryOutcome outcome;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "pointer query cancelled");
        return outcome;
    }
    Window root_return = None;
    Window child_return = None;
    int root_x = 0;
    int root_y = 0;
    int win_x = 0;
    int win_y = 0;
    unsigned int mask = 0;
    Display *display = connection_->display;
    // Global desktop coordinates are root-window coordinates throughout the
    // backend (window geometry, ROI capture), so the pointer query reports
    // the same space. False means the pointer is on another screen (Zaphod
    // multi-screen); this backend operates on one root, so that is an I/O
    // condition, not a position.
    const Bool on_screen = XQueryPointer(display, XDefaultRootWindow(display), &root_return,
                                         &child_return, &root_x, &root_y, &win_x, &win_y, &mask);
    if (!on_screen) {
        outcome.error = error("io_error", "pointer is not on the queried screen");
        return outcome;
    }
    outcome.ok = true;
    outcome.position = {root_x, root_y};
    return outcome;
}

namespace {

// --- Clipboard selection service (M2-04) --------------------------------
// Every helper runs under `mutex_` on the calling provider thread. Serving
// pattern per the DEC-015 amendment (M2-04): peer requests are answered
// during provider activity — on entry of every provider method and inside
// the clipboard paths — instead of a dedicated serving thread, which RULE-03
// forbids outside the Executor lifecycle. The upgrade path to an
// Executor-hosted event loop is recorded in DEC-015 for M2-06 wiring.

constexpr std::size_t kMaxClipboardTransfers = 8;
constexpr std::chrono::milliseconds kClipboardReadDeadline{5000};
constexpr std::chrono::milliseconds kClipboardPollSlice{25};

/// Largest single-property write the server accepts; the incremental (INCR)
/// scheme takes over above this in both directions.
std::size_t clipboard_chunk_bytes(Display *display) {
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

void send_selection_notify(X11Backend::XConnection &connection,
                           const XSelectionRequestEvent &request, Atom property) {
    XSelectionEvent reply = {};
    reply.type = SelectionNotify;
    reply.display = connection.display;
    reply.requestor = request.requestor;
    reply.selection = request.selection;
    reply.target = request.target;
    reply.property = property;
    reply.time = request.time;
    XSendEvent(connection.display, request.requestor, False, NoEventMask,
               reinterpret_cast<XEvent *>(&reply));
    XFlush(connection.display);
}

void abort_transfer(X11Backend::XConnection &connection,
                    std::vector<X11Backend::XConnection::OutgoingTransfer>::iterator transfer) {
    XSelectInput(connection.display, transfer->requestor, 0);
    connection.transfers.erase(transfer);
}

void serve_clipboard_request_locked(X11Backend::XConnection &connection,
                                    const XSelectionRequestEvent &request) {
    const Atom property = request.property != None ? request.property : request.target;
    if (request.target == connection.targets) {
        const Atom supported[] = {connection.targets, connection.utf8_string,
                                  connection.timestamp_target};
        XChangeProperty(connection.display, request.requestor, property, XA_ATOM, 32,
                        PropModeReplace, reinterpret_cast<const unsigned char *>(supported), 3);
        send_selection_notify(connection, request, property);
        return;
    }
    if (request.target == connection.timestamp_target) {
        const long stamp = static_cast<long>(connection.clipboard_timestamp);
        XChangeProperty(connection.display, request.requestor, property, XA_INTEGER, 32,
                        PropModeReplace, reinterpret_cast<const unsigned char *>(&stamp), 1);
        send_selection_notify(connection, request, property);
        return;
    }
    if (request.target == connection.utf8_string) {
        if (!connection.clipboard_owned) {
            send_selection_notify(connection, request, None);
            return;
        }
        const std::string &payload = connection.clipboard_payload;
        const std::size_t chunk = clipboard_chunk_bytes(connection.display);
        if (payload.size() <= chunk) {
            XChangeProperty(connection.display, request.requestor, property, connection.utf8_string,
                            8, PropModeReplace,
                            reinterpret_cast<const unsigned char *>(payload.data()),
                            static_cast<int>(payload.size()));
            send_selection_notify(connection, request, property);
            return;
        }
        if (connection.transfers.size() >= kMaxClipboardTransfers) {
            // Budget refusal (RULE-07): a fresh transfer request beyond the
            // concurrent-transfer cap is refused, never queued without bound.
            send_selection_notify(connection, request, None);
            return;
        }
        XSelectInput(connection.display, request.requestor, PropertyChangeMask);
        const long total = static_cast<long>(payload.size());
        XChangeProperty(connection.display, request.requestor, property, connection.incr, 32,
                        PropModeReplace, reinterpret_cast<const unsigned char *>(&total), 1);
        connection.transfers.push_back({request.requestor, property, 0, chunk, false});
        send_selection_notify(connection, request, property);
        return;
    }
    send_selection_notify(connection, request, None); // unknown target: ICCCM refusal
}

void handle_selection_clear_locked(X11Backend::XConnection &connection,
                                   const XSelectionClearEvent &clear) {
    if (clear.window != connection.clipboard_window || clear.selection != connection.clipboard) {
        return;
    }
    connection.clipboard_owned = false;
    connection.clipboard_payload.clear();
    connection.clipboard_payload.shrink_to_fit();
    while (!connection.transfers.empty()) {
        abort_transfer(connection, std::prev(connection.transfers.end()));
    }
}

/// Advances the outgoing INCR transfers: each PropertyDelete on the
/// requestor's property releases the next chunk, and the zero-length
/// property terminates the transfer (ICCCM incremental scheme).
void advance_clipboard_transfers_locked(X11Backend::XConnection &connection,
                                        const XPropertyEvent &event) {
    if (event.state != PropertyDelete) {
        return;
    }
    for (auto it = connection.transfers.begin(); it != connection.transfers.end(); ++it) {
        if (it->requestor != event.window || it->property != event.atom) {
            continue;
        }
        if (!connection.clipboard_owned) {
            abort_transfer(connection, it); // ownership lost mid-transfer
            return;
        }
        const std::string &payload = connection.clipboard_payload;
        if (it->offset >= payload.size()) {
            static const char kEmpty = 0;
            XChangeProperty(connection.display, it->requestor, it->property, connection.incr, 8,
                            PropModeReplace, reinterpret_cast<const unsigned char *>(&kEmpty), 0);
            abort_transfer(connection, it);
            return;
        }
        if (!it->started) {
            it->started = true; // header acked; chunk streaming begins
        }
        const std::size_t remaining = payload.size() - it->offset;
        const std::size_t take = remaining < it->chunk ? remaining : it->chunk;
        XChangeProperty(connection.display, it->requestor, it->property, connection.incr, 8,
                        PropModeReplace,
                        reinterpret_cast<const unsigned char *>(payload.data() + it->offset),
                        static_cast<int>(take));
        it->offset += take;
        return;
    }
}

/// Server time for the ownership timestamp via the property round-trip: the
/// PropertyNotify timestamp of a change we just caused is the server's
/// current time.
unsigned long clipboard_server_timestamp_locked(X11Backend::XConnection &connection) {
    static const char kEmpty = 0;
    XChangeProperty(connection.display, connection.clipboard_window, connection.timestamp_marker,
                    XA_INTEGER, 8, PropModeReplace,
                    reinterpret_cast<const unsigned char *>(&kEmpty), 0);
    XSync(connection.display, False);
    XEvent event;
    while (XCheckTypedWindowEvent(connection.display, connection.clipboard_window, PropertyNotify,
                                  &event)) {
        if (event.xproperty.atom == connection.timestamp_marker) {
            return event.xproperty.time;
        }
    }
    return CurrentTime; // practically unreachable; ownership still works
}

} // namespace

void X11Backend::pump_clipboard_locked() {
    XConnection &connection = xconn();
    if (!connection.clipboard_ready) {
        return;
    }
    Display *display = connection.display;
    XEvent event;
    while (XCheckTypedEvent(display, SelectionRequest, &event)) {
        serve_clipboard_request_locked(connection, event.xselectionrequest);
    }
    while (XCheckTypedEvent(display, SelectionClear, &event)) {
        handle_selection_clear_locked(connection, event.xselectionclear);
    }
    while (XCheckTypedEvent(display, PropertyNotify, &event)) {
        advance_clipboard_transfers_locked(connection, event.xproperty);
    }
}

ClipboardReadOutcome X11Backend::read_text(const ClipboardReadLimits &limits,
                                           const CancelToken &cancel) {
    ClipboardReadOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.error = error("cancelled", "clipboard read cancelled");
        return outcome;
    }
    if (limits.max_bytes == 0) {
        outcome.error = error("invalid_argument", "read budget must be positive");
        return outcome;
    }
    XConnection &connection = xconn();
    if (!connection.clipboard_ready) {
        outcome.error = error("unsupported_platform", "clipboard surface is unavailable");
        return outcome;
    }
    pump_clipboard_locked();
    // A previous read that ended early (budget refusal, cancellation,
    // deadline) may leave a dangling self-transfer bound to the transfer
    // property; drop it before the fresh conversion — otherwise the stale
    // stream keeps advancing on the same property and interleaves corrupt
    // bytes into the new read. The event mask on our own window is left
    // untouched: incoming incremental reads still need PropertyChangeMask.
    std::erase_if(connection.transfers, [&](const XConnection::OutgoingTransfer &transfer) {
        return transfer.requestor == connection.clipboard_window &&
               transfer.property == connection.transfer_property;
    });
    Display *display = connection.display;
    if (XGetSelectionOwner(display, connection.clipboard) == None) {
        outcome.error = error("not_found", "clipboard is empty");
        return outcome;
    }
    XDeleteProperty(display, connection.clipboard_window, connection.transfer_property);
    XConvertSelection(display, connection.clipboard, connection.utf8_string,
                      connection.transfer_property, connection.clipboard_window, CurrentTime);

    const auto deadline = std::chrono::steady_clock::now() + kClipboardReadDeadline;
    bool incremental = false;
    std::string content;
    bool done = false;
    while (!done) {
        if (std::chrono::steady_clock::now() >= deadline) {
            outcome.error =
                error("deadline_exceeded", "clipboard owner did not deliver content in time");
            return outcome;
        }
        if (cancel.cancelled()) {
            XDeleteProperty(display, connection.clipboard_window, connection.transfer_property);
            outcome.error = error("cancelled", "clipboard read cancelled");
            return outcome;
        }
        pollfd fds = {ConnectionNumber(display), POLLIN, 0};
        const int ready = ::poll(&fds, 1, static_cast<int>(kClipboardPollSlice.count()));
        if (ready < 0 && errno != EINTR) {
            outcome.error = error("io_error", "poll on the X connection failed");
            return outcome;
        }
        while (XPending(display) > 0) {
            XEvent event;
            XNextEvent(display, &event);
            if (event.type == SelectionRequest) {
                // Requests from peers (and from our own read below) keep
                // being served while we wait.
                serve_clipboard_request_locked(connection, event.xselectionrequest);
                continue;
            }
            if (event.type == SelectionClear) {
                handle_selection_clear_locked(connection, event.xselectionclear);
                continue;
            }
            if (event.type == SelectionNotify &&
                event.xselection.requestor == connection.clipboard_window &&
                event.xselection.selection == connection.clipboard) {
                if (event.xselection.property == None) {
                    outcome.error = error(XGetSelectionOwner(display, connection.clipboard) == None
                                              ? "not_found"
                                              : "unsupported_content",
                                          "clipboard has no UTF-8 text");
                    return outcome;
                }
                Atom type = None;
                int format = 0;
                unsigned long count = 0;
                unsigned long remaining = 0;
                unsigned char *data = nullptr;
                const int status = XGetWindowProperty(
                    display, connection.clipboard_window, connection.transfer_property, 0,
                    static_cast<long>(limits.max_bytes / 4 + 1), True, AnyPropertyType, &type,
                    &format, &count, &remaining, &data);
                if (status != Success) {
                    outcome.error = error("io_error", "clipboard property read failed");
                    return outcome;
                }
                if (type == connection.incr) {
                    // INCR header: the property announces the total size.
                    unsigned long total = 0;
                    if (data != nullptr && count > 0) {
                        total = static_cast<unsigned long>(*reinterpret_cast<const long *>(data));
                    }
                    if (data != nullptr) {
                        XFree(data);
                    }
                    if (total > limits.max_bytes) {
                        outcome.error = error("clipboard_too_large",
                                              "clipboard exceeds the read budget of " +
                                                  std::to_string(limits.max_bytes) + " bytes");
                        return outcome;
                    }
                    incremental = true;
                    continue; // chunks arrive as PropertyNewValue events
                }
                if (type == connection.utf8_string) {
                    const std::size_t bytes = data != nullptr ? count : 0;
                    if (bytes > limits.max_bytes) {
                        if (data != nullptr) {
                            XFree(data);
                        }
                        outcome.error = error("clipboard_too_large",
                                              "clipboard exceeds the read budget of " +
                                                  std::to_string(limits.max_bytes) + " bytes");
                        return outcome;
                    }
                    if (remaining != 0) {
                        if (data != nullptr) {
                            XFree(data);
                        }
                        outcome.error =
                            error("io_error", "non-incremental clipboard transfer exceeded the "
                                              "server request limit");
                        return outcome;
                    }
                    if (data != nullptr) {
                        content.assign(reinterpret_cast<const char *>(data), bytes);
                        XFree(data);
                    }
                    done = true;
                    break;
                }
                if (data != nullptr) {
                    XFree(data);
                }
                outcome.error = error("unsupported_content", "clipboard holds non-UTF-8 content");
                return outcome;
            }
            if (event.type == PropertyNotify &&
                event.xproperty.window == connection.clipboard_window &&
                event.xproperty.state == PropertyNewValue && incremental) {
                Atom type = None;
                int format = 0;
                unsigned long count = 0;
                unsigned long remaining = 0;
                unsigned char *data = nullptr;
                const int status = XGetWindowProperty(
                    display, connection.clipboard_window, connection.transfer_property, 0,
                    static_cast<long>(limits.max_bytes / 4 + 1), True, AnyPropertyType, &type,
                    &format, &count, &remaining, &data);
                if (status != Success) {
                    outcome.error = error("io_error", "clipboard chunk read failed");
                    return outcome;
                }
                if (type != connection.incr) {
                    if (data != nullptr) {
                        XFree(data);
                    }
                    outcome.error = error("io_error", "unexpected clipboard transfer type");
                    return outcome;
                }
                const std::size_t bytes = data != nullptr ? count : 0;
                if (bytes == 0) {
                    if (data != nullptr) {
                        XFree(data);
                    }
                    done = true; // zero-length chunk terminates the transfer
                    break;
                }
                if (content.size() + bytes > limits.max_bytes) {
                    if (data != nullptr) {
                        XFree(data);
                    }
                    XDeleteProperty(display, connection.clipboard_window,
                                    connection.transfer_property);
                    outcome.error = error("clipboard_too_large",
                                          "clipboard exceeds the read budget of " +
                                              std::to_string(limits.max_bytes) + " bytes");
                    return outcome;
                }
                content.append(reinterpret_cast<const char *>(data), bytes);
                XFree(data);
                continue;
            }
            advance_clipboard_transfers_locked(connection, event.xproperty);
        }
    }
    if (!mirage::desktop::is_valid_utf8(content)) {
        outcome.error = error("unsupported_content", "clipboard content is not valid UTF-8");
        return outcome;
    }
    outcome.ok = true;
    outcome.content = std::move(content);
    return outcome;
}

ClipboardWriteOutcome X11Backend::write_text(const std::string &text,
                                             const ClipboardWriteLimits &limits,
                                             const CancelToken &cancel) {
    ClipboardWriteOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "clipboard write cancelled");
        return outcome;
    }
    if (limits.max_bytes == 0) {
        outcome.error = error("invalid_argument", "write budget must be positive");
        return outcome;
    }
    if (text.size() > limits.max_bytes) {
        outcome.error = error("invalid_argument", "payload exceeds the write budget of " +
                                                      std::to_string(limits.max_bytes) + " bytes");
        return outcome;
    }
    if (!mirage::desktop::is_valid_utf8(text)) {
        outcome.error = error("invalid_argument", "text must be UTF-8");
        return outcome;
    }
    XConnection &connection = xconn();
    if (!connection.clipboard_ready) {
        outcome.error = error("unsupported_platform", "clipboard surface is unavailable");
        return outcome;
    }
    pump_clipboard_locked();
    Display *display = connection.display;
    const unsigned long stamp = clipboard_server_timestamp_locked(connection);
    connection.clipboard_payload = text;
    connection.clipboard_timestamp = stamp;
    // Xlib's argument order is (display, selection, owner, time): the
    // selection atom comes first, the owning window third (Xlib.h) — the
    // opposite of what the man-page summary suggests.
    XSetSelectionOwner(display, connection.clipboard, connection.clipboard_window, stamp);
    const bool acquired =
        XSync(display, False) != 0 &&
        XGetSelectionOwner(display, connection.clipboard) == connection.clipboard_window;
    if (!acquired) {
        connection.clipboard_payload.clear();
        connection.clipboard_timestamp = 0;
        outcome.error = error("io_error", "selection ownership could not be acquired");
        return outcome;
    }
    connection.clipboard_owned = true;
    pump_clipboard_locked(); // serve requesters that asked while we set up
    outcome.ok = true;
    return outcome;
}

} // namespace mirage::platform::linux_backend
