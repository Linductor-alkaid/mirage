#include "overlay_backend.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/shape.h>

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::OverlayCarrierContext;
using mirage::desktop::OverlayClick;
using mirage::desktop::OverlaySurfaceFrame;

/// Bounded wait slice of one pump iteration (DEC-029 decision 6): stop and
/// wakeup latency never exceed this; frame changes and the confirmation
/// countdown are picked up at most this late.
constexpr int kPollSliceMs = 200;

/// Border width of target highlight outlines.
constexpr int kHighlightBorder = 3;

/// Layout metrics (pixels) of the composed surface.
constexpr int kBannerHeight = 22;
constexpr int kLabelHeight = 16;
constexpr int kButtonWidth = 78;
constexpr int kButtonHeight = 20;
constexpr int kPadding = 8;

/// The core X text API draws 8-bit strings; overlay labels are best-effort
/// ASCII (DEC-029 records locale-dependent Xutf8 text as an evolution, not
/// a claim). Non-ASCII bytes render as '?' deterministically.
std::string ascii_only(const std::string &text) {
    std::string out;
    out.reserve(text.size());
    for (const char character : text) {
        out.push_back(static_cast<unsigned char>(character) < 0x80 ? character : '?');
    }
    return out;
}

} // namespace

struct X11OverlayCarrier::Surface {
    /// One drawn rectangle (also entered into the bounding shape).
    struct Rect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        unsigned long pixel = 0;
    };
    /// One drawn text job (also inside its banner's shape rectangle).
    struct Text {
        int x = 0;
        int baseline = 0;
        std::string body;
        unsigned long pixel = 0;
    };
    /// Confirm-entry button: input-shape rect plus its click payload.
    struct Button {
        int x = 0;
        int y = 0;
        bool approved = false;
        std::string request_id;
    };

    Display *display = nullptr;
    Window window = None;
    GC gc = nullptr;
    XFontStruct *font = nullptr;
    int width = 0;
    int height = 0;
    int wake_read = -1;
    int wake_write = -1;
    bool mapped = false;
    bool presented = false;

    /// Last accepted frame plus its presentation cache (pump-thread only).
    OverlaySurfaceFrame frame;
    bool has_frame = false;
    std::chrono::steady_clock::time_point confirm_deadline{};
    long long shown_countdown = -1;
    std::vector<Button> buttons;

    unsigned long px_banner_bg = 0;
    unsigned long px_banner_fg = 0;
    unsigned long px_highlight = 0;
    unsigned long px_debug = 0;
    unsigned long px_approve = 0;
    unsigned long px_deny = 0;
    unsigned long px_black = 0;

    ~Surface() {
        if (window != None) {
            XDestroyWindow(display, window);
        }
        if (font != nullptr) {
            XFreeFont(display, font);
        }
        if (gc != nullptr) {
            XFreeGC(display, gc);
        }
        if (display != nullptr) {
            XCloseDisplay(display);
        }
        if (wake_read >= 0) {
            ::close(wake_read);
        }
        if (wake_write >= 0) {
            ::close(wake_write);
        }
    }

    int text_width(const std::string &text) const {
        if (font == nullptr || text.empty()) {
            return 0;
        }
        return XTextWidth(font, text.c_str(), static_cast<int>(text.size()));
    }

    int text_baseline(int band_y, int band_h) const {
        if (font == nullptr) {
            return band_y + band_h;
        }
        return band_y + (band_h + font->ascent - font->descent) / 2;
    }

    /// The confirmation's remaining whole seconds (clamped at zero),
    /// derived from the deadline captured when its frame arrived. long
    /// long: std::chrono::seconds::count() is __int64 on MSVC — a long
    /// narrows there (C4244 under warnings-as-errors); neutral on GCC/Clang.
    long long countdown_seconds(std::chrono::steady_clock::time_point now) const {
        if (!frame.confirmation.has_value()) {
            return -1;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::seconds>(confirm_deadline - now).count();
        return remaining > 0 ? remaining : 0;
    }
};

namespace {

/// Computes the whole presentation of `surface.frame` into filled
/// rectangles, text jobs and confirm buttons. Everything is clamped to the
/// root window; over-budget input was already truncated by the frame
/// contract (RULE-07).
void compose(X11OverlayCarrier::Surface &surface,
             std::vector<X11OverlayCarrier::Surface::Rect> &fills,
             std::vector<X11OverlayCarrier::Surface::Text> &texts) {
    using SurfaceT = X11OverlayCarrier::Surface;
    const OverlaySurfaceFrame &frame = surface.frame;
    const int screen_w = surface.width;
    const int screen_h = surface.height;
    const auto clamp_rect = [screen_w, screen_h](int x, int y, int w, int h) {
        const int cx = std::max(0, x);
        const int cy = std::max(0, y);
        return SurfaceT::Rect{cx, cy, std::min(w, screen_w - cx), std::min(h, screen_h - cy)};
    };

    // Status banner (top-center): the hint / task line.
    if (!frame.hint.empty() && surface.font != nullptr) {
        const std::string text = ascii_only(frame.hint);
        const int w = std::min(screen_w, surface.text_width(text) + 2 * kPadding);
        const int x = (screen_w - w) / 2;
        fills.push_back({x, 0, w, kBannerHeight, surface.px_banner_bg});
        texts.push_back(
            {x + kPadding, surface.text_baseline(0, kBannerHeight), text, surface.px_banner_fg});
    }

    // Target highlights: colored outline strips + optional label chip.
    const std::size_t highlight_count =
        std::min(frame.highlights.size(), OverlaySurfaceFrame::kMaxHighlights);
    for (std::size_t i = 0; i < highlight_count; ++i) {
        const auto &highlight = frame.highlights[i];
        const SurfaceT::Rect box =
            clamp_rect(highlight.rect.x, highlight.rect.y, static_cast<int>(highlight.rect.width),
                       static_cast<int>(highlight.rect.height));
        if (box.w <= 2 * kHighlightBorder || box.h <= 2 * kHighlightBorder) {
            continue;
        }
        fills.push_back({box.x, box.y, box.w, kHighlightBorder, surface.px_highlight});
        fills.push_back({box.x, box.y + box.h - kHighlightBorder, box.w, kHighlightBorder,
                         surface.px_highlight});
        fills.push_back({box.x, box.y, kHighlightBorder, box.h, surface.px_highlight});
        fills.push_back({box.x + box.w - kHighlightBorder, box.y, kHighlightBorder, box.h,
                         surface.px_highlight});
        const std::string label = ascii_only(highlight.label);
        if (!label.empty() && surface.font != nullptr) {
            const int w = std::min(screen_w, surface.text_width(label) + 2 * kPadding);
            const int y = box.y >= kLabelHeight + 2 ? box.y - kLabelHeight - 2 : box.y + box.h + 2;
            const SurfaceT::Rect chip = clamp_rect(box.x, y, w, kLabelHeight);
            if (chip.w > 0 && chip.h > 0) {
                fills.push_back({chip.x, chip.y, chip.w, chip.h, surface.px_highlight});
                texts.push_back({chip.x + kPadding, surface.text_baseline(chip.y, chip.h), label,
                                 surface.px_black});
            }
        }
    }

    // Observation debug face (DEC-026 semantic snapshot nodes): thin cyan
    // boxes without labels — the workspace observer remains the labeled face.
    const std::size_t debug_count =
        std::min(frame.debug_boxes.size(), OverlaySurfaceFrame::kMaxDebugBoxes);
    for (std::size_t i = 0; i < debug_count; ++i) {
        const auto &box = frame.debug_boxes[i];
        const SurfaceT::Rect rect =
            clamp_rect(box.rect.x, box.rect.y, static_cast<int>(box.rect.width),
                       static_cast<int>(box.rect.height));
        if (rect.w <= 2 || rect.h <= 2) {
            continue;
        }
        fills.push_back({rect.x, rect.y, rect.w, 1, surface.px_debug});
        fills.push_back({rect.x, rect.y + rect.h - 1, rect.w, 1, surface.px_debug});
        fills.push_back({rect.x, rect.y, 1, rect.h, surface.px_debug});
        fills.push_back({rect.x + rect.w - 1, rect.y, 1, rect.h, surface.px_debug});
    }

    // Confirmation banner (bottom-center): question + countdown + buttons.
    if (frame.confirmation.has_value() && surface.font != nullptr) {
        const long long countdown =
            std::max(0LL, surface.countdown_seconds(std::chrono::steady_clock::now()));
        const std::string question =
            ascii_only("Allow " + frame.confirmation->capability + " " +
                       frame.confirmation->resource + "? [" + std::to_string(countdown) + "s]");
        const int text_w = surface.text_width(question);
        const int w = std::min(screen_w, text_w + 2 * kPadding + 2 * (kButtonWidth + kPadding));
        const int x = (screen_w - w) / 2;
        const int y = screen_h - kBannerHeight - 4;
        fills.push_back({x, y, w, kBannerHeight, surface.px_banner_bg});
        texts.push_back({x + kPadding, surface.text_baseline(y, kBannerHeight), question,
                         surface.px_banner_fg});
        const int button_y = y + (kBannerHeight - kButtonHeight) / 2;
        const int deny_x = x + w - kPadding - kButtonWidth;
        const int approve_x = deny_x - kPadding - kButtonWidth;
        fills.push_back({approve_x, button_y, kButtonWidth, kButtonHeight, surface.px_approve});
        fills.push_back({deny_x, button_y, kButtonWidth, kButtonHeight, surface.px_deny});
        texts.push_back(
            {approve_x + kPadding,
             button_y + (kButtonHeight + surface.font->ascent - surface.font->descent) / 2,
             std::string("Approve"), surface.px_banner_fg});
        texts.push_back(
            {deny_x + kPadding,
             button_y + (kButtonHeight + surface.font->ascent - surface.font->descent) / 2,
             std::string("Deny"), surface.px_banner_fg});
        surface.buttons.push_back({approve_x, button_y, true, frame.confirmation->request_id});
        surface.buttons.push_back({deny_x, button_y, false, frame.confirmation->request_id});
    }
}

} // namespace

X11OverlayCarrier::~X11OverlayCarrier() = default;

std::unique_ptr<X11OverlayCarrier> X11OverlayCarrier::open(const std::string &display_name) {
    auto carrier = std::unique_ptr<X11OverlayCarrier>(new X11OverlayCarrier());
    carrier->surface_ = std::make_unique<Surface>();
    Surface &surface = *carrier->surface_;
    surface.display = XOpenDisplay(display_name.empty() ? nullptr : display_name.c_str());
    if (surface.display == nullptr) {
        return nullptr; // capability honesty: no X/XWayland, no overlay carrier
    }
    int wake[2] = {-1, -1};
    if (::pipe2(wake, O_NONBLOCK | O_CLOEXEC) != 0) {
        return nullptr;
    }
    surface.wake_read = wake[0];
    surface.wake_write = wake[1];
    const int screen = DefaultScreen(surface.display);
    surface.width = DisplayWidth(surface.display, screen);
    surface.height = DisplayHeight(surface.display, screen);
    surface.gc = XCreateGC(surface.display, DefaultRootWindow(surface.display), 0, nullptr);
    // "fixed" is the one font every X server ships; without it the surface
    // degrades to shapes only (boxes still read as boxes).
    surface.font = XLoadQueryFont(surface.display, "fixed");
    Visual *visual = DefaultVisual(surface.display, screen);
    const int depth = DefaultDepth(surface.display, screen);
    // TrueColor pixel composition (the backend's root-window capture rides
    // the same assumption, DEC-015); other visual classes fall back to
    // black/white so the surface stays visible, just plainer. The member is
    // spelled c_class under C++ (Xlib's keyword workaround).
    const bool color =
        (visual->c_class == TrueColor || visual->c_class == DirectColor) && depth >= 24;
    const unsigned long black = BlackPixel(surface.display, screen);
    const unsigned long white = WhitePixel(surface.display, screen);
    const auto px = [color, depth](unsigned r, unsigned g, unsigned b, unsigned long fallback) {
        return color ? ((r << 16) | (g << 8) | b) : fallback;
    };
    surface.px_banner_bg = px(0x10, 0x18, 0x20, black);
    surface.px_banner_fg = px(0xE8, 0xED, 0xF2, white);
    surface.px_highlight = px(0x00, 0xC8, 0x53, white);
    surface.px_debug = px(0x00, 0xBC, 0xD4, white);
    surface.px_approve = px(0x2E, 0x7D, 0x32, white);
    surface.px_deny = px(0xC6, 0x28, 0x28, white);
    surface.px_black = black;
    return carrier;
}

mirage::desktop::OverlayCarrier::RunReport
X11OverlayCarrier::run(const OverlayCarrierContext &context,
                       const std::function<bool()> &stop_requested) {
    RunReport report;
    if (surface_ == nullptr || surface_->display == nullptr) {
        report.diagnostic = "overlay carrier is not open";
        return report;
    }
    Surface &surface = *surface_;
    Display *display = surface.display;

    XSetWindowAttributes attributes{};
    attributes.override_redirect = True; // above window management, DEC-029 decision 4
    attributes.event_mask = ExposureMask | ButtonPressMask;
    surface.window = XCreateWindow(
        display, DefaultRootWindow(display), 0, 0, static_cast<unsigned>(surface.width),
        static_cast<unsigned>(surface.height), 0, CopyFromParent, InputOutput, CopyFromParent,
        CWOverrideRedirect | CWEventMask, &attributes);
    if (surface.window == None) {
        report.diagnostic = "overlay window creation failed";
        return report;
    }
    // Fully click-through and content-less before mapping: empty bounding
    // and input shapes (DEC-029 decision 4).
    XShapeCombineRectangles(display, surface.window, ShapeBounding, 0, 0, nullptr, 0, ShapeSet,
                            Unsorted);
    XShapeCombineRectangles(display, surface.window, ShapeInput, 0, 0, nullptr, 0, ShapeSet,
                            Unsorted);

    report.clean = true;
    XEvent event;
    OverlaySurfaceFrame incoming;
    pollfd fds[2] = {{ConnectionNumber(display), POLLIN, 0}, {surface.wake_read, POLLIN, 0}};

    while (!stop_requested()) {
        // All pending X events first: expose repaints and confirm clicks.
        while (XPending(display) > 0) {
            XNextEvent(display, &event);
            if (event.type == Expose && event.xexpose.count == 0) {
                surface.presented = false; // full repaint on this pass
                continue;
            }
            if (event.type == ButtonPress && event.xbutton.button == Button1) {
                for (const Surface::Button &button : surface.buttons) {
                    if (event.xbutton.x >= button.x && event.xbutton.x < button.x + kButtonWidth &&
                        event.xbutton.y >= button.y && event.xbutton.y < button.y + kButtonHeight) {
                        if (context.on_click) {
                            context.on_click(OverlayClick{button.request_id, button.approved});
                        }
                        break;
                    }
                }
            }
        }

        if (context.on_tick) {
            context.on_tick();
        }

        if (context.load_frame && context.load_frame(incoming)) {
            surface.frame = std::move(incoming);
            surface.has_frame = true;
            surface.shown_countdown = -1;
            surface.confirm_deadline =
                std::chrono::steady_clock::now() +
                std::chrono::milliseconds(
                    surface.frame.confirmation.has_value()
                        ? std::max<std::int64_t>(0, surface.frame.confirmation->timeout_ms)
                        : 0);
            surface.presented = false;
        }
        incoming = OverlaySurfaceFrame{};

        if (surface.has_frame) {
            // Repaint on new frames and on confirmation countdown ticks.
            const long long countdown = surface.countdown_seconds(std::chrono::steady_clock::now());
            if (!surface.presented || countdown != surface.shown_countdown) {
                surface.shown_countdown = countdown;
                surface.buttons.clear();
                const bool want_mapped = surface.frame.visible;
                if (want_mapped != surface.mapped) {
                    if (want_mapped) {
                        XMapRaised(display, surface.window);
                    } else {
                        XUnmapWindow(display, surface.window);
                    }
                    surface.mapped = want_mapped;
                }
                if (surface.mapped) {
                    std::vector<Surface::Rect> fills;
                    std::vector<Surface::Text> texts;
                    compose(surface, fills, texts);
                    // Bounding shape = union of everything drawn; input
                    // shape = confirm buttons only (click-through elsewhere).
                    std::vector<XRectangle> bounds;
                    bounds.reserve(fills.size());
                    for (const Surface::Rect &rect : fills) {
                        bounds.push_back({static_cast<short>(rect.x), static_cast<short>(rect.y),
                                          static_cast<unsigned short>(std::max(0, rect.w)),
                                          static_cast<unsigned short>(std::max(0, rect.h))});
                    }
                    std::vector<XRectangle> inputs;
                    inputs.reserve(surface.buttons.size());
                    for (const Surface::Button &button : surface.buttons) {
                        inputs.push_back({static_cast<short>(button.x),
                                          static_cast<short>(button.y),
                                          static_cast<unsigned short>(kButtonWidth),
                                          static_cast<unsigned short>(kButtonHeight)});
                    }
                    XShapeCombineRectangles(display, surface.window, ShapeBounding, 0, 0,
                                            bounds.data(), static_cast<int>(bounds.size()),
                                            ShapeSet, Unsorted);
                    XShapeCombineRectangles(display, surface.window, ShapeInput, 0, 0,
                                            inputs.data(), static_cast<int>(inputs.size()),
                                            ShapeSet, Unsorted);
                    for (const Surface::Rect &rect : fills) {
                        XSetForeground(display, surface.gc, rect.pixel);
                        XFillRectangle(display, surface.window, surface.gc, rect.x, rect.y,
                                       static_cast<unsigned>(std::max(0, rect.w)),
                                       static_cast<unsigned>(std::max(0, rect.h)));
                    }
                    for (const Surface::Text &text : texts) {
                        XSetForeground(display, surface.gc, text.pixel);
                        XDrawString(display, surface.window, surface.gc, text.x, text.baseline,
                                    text.body.c_str(), static_cast<int>(text.body.size()));
                    }
                    XFlush(display);
                }
                surface.presented = true;
            }
        }

        if (stop_requested()) {
            break;
        }
        (void)::poll(fds, 2, kPollSliceMs);
        // Drain the self-pipe regardless of which descriptor woke the wait.
        char token = 0;
        while (::read(surface.wake_read, &token, 1) == 1) {
        }
    }

    XDestroyWindow(display, surface.window);
    surface.window = None;
    XFlush(display);
    return report;
}

void X11OverlayCarrier::wakeup() {
    if (surface_ == nullptr || surface_->wake_write < 0) {
        return;
    }
    const char token = 'w';
    const ssize_t written = ::write(surface_->wake_write, &token, 1);
    (void)written; // O_NONBLOCK: a full pipe means the wait is already open
}

std::unique_ptr<mirage::desktop::OverlayCarrier>
open_overlay_carrier(const std::string &display_name) {
    return X11OverlayCarrier::open(display_name);
}

} // namespace mirage::platform::linux_backend
