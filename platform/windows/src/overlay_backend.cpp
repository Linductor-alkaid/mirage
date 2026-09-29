#include "overlay_backend.hpp"

#include "win32_util.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace mirage::platform::windows_backend {
namespace {

using mirage::desktop::OverlayCarrierContext;
using mirage::desktop::OverlayClick;
using mirage::desktop::OverlaySurfaceFrame;

using win32_util::last_error_suffix;

/// Bounded wait slice of one pump iteration (DEC-029 decision 6): stop,
/// wakeup and frame latency never exceed this.
constexpr int kPollSliceMs = 200;

/// Border width of target highlight outlines.
constexpr int kHighlightBorder = 3;

/// Layout metrics (pixels) — shared with the Linux carrier so both
/// platforms present the same composition (DEC-029).
constexpr int kBannerHeight = 22;
constexpr int kLabelHeight = 16;
constexpr int kButtonWidth = 78;
constexpr int kButtonHeight = 20;
constexpr int kPadding = 8;

/// ARGB colors (non-premultiplied in layout, premultiplied at write time).
constexpr uint32_t kBannerBg = 0x101820; ///< painted with alpha 216
constexpr uint32_t kBannerFg = 0xE8EDF2;
constexpr uint32_t kHighlight = 0x00C853;
constexpr uint32_t kDebug = 0x00BCD4;
constexpr uint32_t kApprove = 0x2E7D32;
constexpr uint32_t kDeny = 0xC62828;
constexpr uint32_t kLabelText = 0x08130C;

/// Premultiplied little-endian ARGB pixel of one (rgb, alpha) color.
constexpr uint32_t premultiply(uint32_t rgb, uint8_t alpha) {
    const uint32_t r = ((rgb >> 16) & 0xFF) * alpha / 255;
    const uint32_t g = ((rgb >> 8) & 0xFF) * alpha / 255;
    const uint32_t b = (rgb & 0xFF) * alpha / 255;
    return (static_cast<uint32_t>(alpha) << 24) | (r << 16) | (g << 8) | b;
}

/// Lenient UTF-8 to UTF-16 for display labels: malformed input renders as
/// replacement characters instead of failing the whole overlay frame (this
/// is presentation, not a provider payload boundary).
std::wstring wide_of(const std::string &utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size =
        ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    const int written = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(),
                                              static_cast<int>(utf8.size()), out.data(), size);
    if (written <= 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// The window class shared by both overlay windows (registered once per
/// process; the procedure routes confirm-entry clicks to the pump state).
constexpr wchar_t kClassName[] = L"MirageOverlaySurface";

/// One composed presentation of a frame: premultiplied fills, text jobs
/// (with the background they sit on, for the alpha fixup pass) and the
/// confirm-entry buttons.
struct Layout {
    struct Fill {
        RECT rect;
        uint32_t pixel; ///< premultiplied
    };
    struct Text {
        RECT rect;
        std::wstring body;
        uint32_t color;   ///< 0xRRGGBB
        uint32_t bg_rgb;  ///< 0xRRGGBB under the glyphs
        uint8_t bg_alpha; ///< alpha restored for background pixels
    };
    struct Button {
        RECT rect; ///< screen coordinates
        bool approved = false;
        std::string request_id;
    };

    std::vector<Fill> fills;
    std::vector<Text> texts;
    std::vector<Button> buttons;
    RECT banner{0, 0, 0, 0}; ///< confirm banner in screen coordinates
};

/// Per-run pump state; everything except the wakeup window handle lives on
/// the pump thread (the handle is published through the carrier's atomic).
struct OverlayState {
    HWND visual = nullptr;
    HWND interaction = nullptr;
    OverlayCarrierContext context;
    RECT screen{0, 0, 0, 0};

    OverlaySurfaceFrame frame;
    bool has_frame = false;
    bool presented = false;
    bool interaction_shown = false;
    std::chrono::steady_clock::time_point confirm_deadline{};
    long shown_countdown = -1;
    Layout layout; ///< last composed presentation
    bool layout_valid = false;

    long countdown_seconds(std::chrono::steady_clock::time_point now) const {
        if (!frame.confirmation.has_value()) {
            return -1;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::seconds>(confirm_deadline - now).count();
        return remaining > 0 ? remaining : 0;
    }
};

/// Routes confirm-entry clicks to the pump context. The interaction window
/// is positioned exactly over the confirm banner, so the window-relative
/// click plus the banner origin are the screen coordinates of the button
/// rects (DEC-029 decision 3).
LRESULT CALLBACK overlay_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_LBUTTONDOWN) {
        auto *state = reinterpret_cast<OverlayState *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
        if (state != nullptr && state->layout_valid) {
            const LONG hit_x = state->layout.banner.left + static_cast<short>(LOWORD(lparam));
            const LONG hit_y = state->layout.banner.top + static_cast<short>(HIWORD(lparam));
            for (const Layout::Button &button : state->layout.buttons) {
                if (hit_x >= button.rect.left && hit_x < button.rect.right &&
                    hit_y >= button.rect.top && hit_y < button.rect.bottom) {
                    if (state->context.on_click) {
                        state->context.on_click(OverlayClick{button.request_id, button.approved});
                    }
                    break;
                }
            }
        }
        return 0;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

bool register_window_class() {
    static const bool registered = [] {
        WNDCLASSEXW description{};
        description.cbSize = sizeof(description);
        description.lpfnWndProc = overlay_window_proc;
        description.hInstance = ::GetModuleHandleW(nullptr);
        description.lpszClassName = kClassName;
        return ::RegisterClassExW(&description) != 0;
    }();
    return registered;
}

/// One layered window's presentation surface: a 32bpp top-down DIB plus the
/// helper DC it is selected into, presented through UpdateLayeredWindow.
struct LayeredSurface {
    HBITMAP dib = nullptr;
    HDC memory_dc = nullptr;
    HDC window_dc = nullptr;
    uint32_t *pixels = nullptr;
    int width = 0;
    int height = 0;
    HWND window = nullptr;

    bool create(HWND owner, int w, int h, std::string &diagnostic) {
        window = owner;
        width = w;
        height = h;
        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = w;
        header.bV5Height = -h; // top-down rows
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00FF0000;
        header.bV5GreenMask = 0x0000FF00;
        header.bV5BlueMask = 0x000000FF;
        header.bV5AlphaMask = 0xFF000000;
        window_dc = ::GetDC(owner);
        memory_dc = ::CreateCompatibleDC(window_dc);
        if (memory_dc == nullptr) {
            diagnostic = "overlay compatible DC creation failed" + last_error_suffix();
            return false;
        }
        void *bits = nullptr;
        dib = ::CreateDIBSection(memory_dc, reinterpret_cast<const BITMAPINFO *>(&header),
                                 DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib == nullptr || bits == nullptr) {
            diagnostic = "overlay DIB creation failed" + last_error_suffix();
            return false;
        }
        pixels = static_cast<uint32_t *>(bits);
        ::SelectObject(memory_dc, dib);
        return true;
    }

    void destroy() {
        if (memory_dc != nullptr) {
            ::DeleteDC(memory_dc);
            memory_dc = nullptr;
        }
        if (dib != nullptr) {
            ::DeleteObject(dib);
            dib = nullptr;
        }
        if (window_dc != nullptr && window != nullptr) {
            ::ReleaseDC(window, window_dc);
            window_dc = nullptr;
        }
        pixels = nullptr;
    }

    void clear() {
        std::memset(pixels, 0,
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                        sizeof(uint32_t));
    }

    /// Fills `rect` in this surface's local coordinates.
    void fill(RECT rect, uint32_t pixel) {
        rect.left = std::max<LONG>(0L, rect.left);
        rect.top = std::max<LONG>(0L, rect.top);
        rect.right = std::min<LONG>(static_cast<LONG>(width), rect.right);
        rect.bottom = std::min<LONG>(static_cast<LONG>(height), rect.bottom);
        for (int y = rect.top; y < rect.bottom; ++y) {
            uint32_t *row = pixels + static_cast<std::size_t>(y) * width;
            std::fill(row + rect.left, row + rect.right, pixel);
        }
    }

    /// GDI wrote the glyphs (RGB only; the alpha byte is undefined on DIB
    /// sections): restore the background alpha for untouched pixels and
    /// force glyph pixels opaque (DEC-017 GDI discipline).
    void fixup_text(const Layout::Text &text) {
        for (int y = text.rect.top; y < text.rect.bottom; ++y) {
            uint32_t *row = pixels + static_cast<std::size_t>(y) * width;
            for (int x = text.rect.left; x < text.rect.right; ++x) {
                const uint32_t rgb = row[x] & 0x00FFFFFF;
                const uint8_t alpha = rgb == text.bg_rgb ? text.bg_alpha : 255;
                row[x] = (static_cast<uint32_t>(alpha) << 24) | rgb;
            }
        }
    }

    void draw_texts(HFONT font, const std::vector<Layout::Text> &texts, int origin_x,
                    int origin_y) {
        if (font != nullptr) {
            ::SelectObject(memory_dc, font);
        }
        ::SetBkMode(memory_dc, TRANSPARENT);
        for (const Layout::Text &text : texts) {
            ::SetTextColor(memory_dc, RGB((text.color >> 16) & 0xFF, (text.color >> 8) & 0xFF,
                                          text.color & 0xFF));
            const RECT local{text.rect.left - origin_x, text.rect.top - origin_y,
                             text.rect.right - origin_x, text.rect.bottom - origin_y};
            ::DrawTextW(memory_dc, text.body.c_str(), static_cast<int>(text.body.size()),
                        const_cast<LPRECT>(&local), DT_SINGLELINE | DT_NOPREFIX);
            fixup_text({local, text.body, text.color, text.bg_rgb, text.bg_alpha});
        }
    }

    bool present(const POINT &origin) {
        // Non-const: the MinGW-w64 winuser.h signature takes SIZE * (the
        // MSVC SDK widens it to CONST SIZE *), so a const local fails the
        // MinGW toolchain (DEC-017 dual-toolchain gate).
        SIZE size{width, height};
        POINT source{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        HDC screen_dc = ::GetDC(nullptr);
        const BOOL ok = ::UpdateLayeredWindow(window, screen_dc, const_cast<POINT *>(&origin),
                                              &size, memory_dc, &source, 0, &blend, ULW_ALPHA);
        ::ReleaseDC(nullptr, screen_dc);
        return ok != 0;
    }
};

/// Composes `state.frame` into `layout` over the virtual screen; returns
/// false when the surface state cannot support composition (no font, no
/// measurement DC).
bool compose_layout(OverlayState &state, HDC measure_dc, long countdown) {
    const OverlaySurfaceFrame &frame = state.frame;
    const int screen_w = state.screen.right - state.screen.left;
    const int screen_h = state.screen.bottom - state.screen.top;
    const auto rect_of = [screen_w, screen_h](LONG x, LONG y, LONG w, LONG h) {
        RECT rect{x, y, x + w, y + h};
        rect.left = std::max<LONG>(0L, rect.left);
        rect.top = std::max<LONG>(0L, rect.top);
        rect.right = std::min<LONG>(rect.right, static_cast<LONG>(screen_w));
        rect.bottom = std::min<LONG>(rect.bottom, static_cast<LONG>(screen_h));
        return rect;
    };
    const auto measure = [measure_dc](const std::wstring &body) -> int {
        if (body.empty() || measure_dc == nullptr) {
            return 0;
        }
        SIZE extent{};
        if (!::GetTextExtentPoint32W(measure_dc, body.c_str(), static_cast<int>(body.size()),
                                     &extent)) {
            return 0;
        }
        return extent.cx;
    };

    Layout layout;
    // Status banner (top-center): the hint / task line.
    if (!frame.hint.empty()) {
        const std::wstring body = wide_of(frame.hint);
        const int text_w = measure(body);
        const int w = std::min(screen_w, text_w + 2 * kPadding);
        const int x = (screen_w - w) / 2;
        const RECT rect = rect_of(x, 0, w, kBannerHeight);
        if (rect.right > rect.left && rect.bottom > rect.top) {
            layout.fills.push_back({rect, premultiply(kBannerBg, 216)});
            layout.texts.push_back(
                {{rect.left + kPadding, rect.top, rect.left + kPadding + text_w, rect.bottom},
                 body,
                 kBannerFg,
                 kBannerBg,
                 216});
        }
    }

    // Target highlights: colored outline strips + optional label chip.
    const std::size_t highlight_count =
        std::min(frame.highlights.size(), OverlaySurfaceFrame::kMaxHighlights);
    for (std::size_t i = 0; i < highlight_count; ++i) {
        const auto &highlight = frame.highlights[i];
        const RECT box =
            rect_of(highlight.rect.x, highlight.rect.y, static_cast<LONG>(highlight.rect.width),
                    static_cast<LONG>(highlight.rect.height));
        if (box.right - box.left <= 2 * kHighlightBorder ||
            box.bottom - box.top <= 2 * kHighlightBorder) {
            continue;
        }
        const uint32_t pixel = premultiply(kHighlight, 255);
        layout.fills.push_back({rect_of(highlight.rect.x, highlight.rect.y,
                                        static_cast<LONG>(highlight.rect.width), kHighlightBorder),
                                pixel});
        layout.fills.push_back({rect_of(highlight.rect.x, box.bottom - kHighlightBorder,
                                        static_cast<LONG>(highlight.rect.width), kHighlightBorder),
                                pixel});
        layout.fills.push_back({rect_of(highlight.rect.x, highlight.rect.y, kHighlightBorder,
                                        static_cast<LONG>(highlight.rect.height)),
                                pixel});
        layout.fills.push_back({rect_of(box.right - kHighlightBorder, highlight.rect.y,
                                        kHighlightBorder, static_cast<LONG>(highlight.rect.height)),
                                pixel});
        const std::wstring label = wide_of(highlight.label);
        if (!label.empty()) {
            const int w = std::min(screen_w, measure(label) + 2 * kPadding);
            const int y = box.top >= kLabelHeight + 2 ? box.top - kLabelHeight - 2 : box.bottom + 2;
            const RECT chip = rect_of(box.left, y, w, kLabelHeight);
            if (chip.right > chip.left && chip.bottom > chip.top) {
                layout.fills.push_back({chip, premultiply(kHighlight, 255)});
                layout.texts.push_back({{chip.left + kPadding, chip.top,
                                         chip.left + kPadding + measure(label), chip.bottom},
                                        label,
                                        kLabelText,
                                        kHighlight,
                                        255});
            }
        }
    }

    // Observation debug face: thin boxes without labels (the workspace
    // observer remains the labeled face, DEC-029).
    const std::size_t debug_count =
        std::min(frame.debug_boxes.size(), OverlaySurfaceFrame::kMaxDebugBoxes);
    for (std::size_t i = 0; i < debug_count; ++i) {
        const auto &box = frame.debug_boxes[i];
        const RECT rect = rect_of(box.rect.x, box.rect.y, static_cast<LONG>(box.rect.width),
                                  static_cast<LONG>(box.rect.height));
        if (rect.right - rect.left <= 2 || rect.bottom - rect.top <= 2) {
            continue;
        }
        const uint32_t pixel = premultiply(kDebug, 255);
        layout.fills.push_back(
            {rect_of(box.rect.x, box.rect.y, static_cast<LONG>(box.rect.width), 1), pixel});
        layout.fills.push_back(
            {rect_of(box.rect.x, rect.bottom - 1, static_cast<LONG>(box.rect.width), 1), pixel});
        layout.fills.push_back(
            {rect_of(box.rect.x, box.rect.y, 1, static_cast<LONG>(box.rect.height)), pixel});
        layout.fills.push_back(
            {rect_of(rect.right - 1, box.rect.y, 1, static_cast<LONG>(box.rect.height)), pixel});
    }

    // Confirmation banner (bottom-center): question + countdown + buttons.
    if (frame.confirmation.has_value() && measure_dc != nullptr) {
        const std::wstring question =
            wide_of("Allow " + frame.confirmation->capability + " " + frame.confirmation->resource +
                    "? [" + std::to_string(std::max(0L, countdown)) + "s]");
        const int text_w = measure(question);
        const int w = std::min(screen_w, text_w + 2 * kPadding + 2 * (kButtonWidth + kPadding));
        const int x = (screen_w - w) / 2;
        const int y = screen_h - kBannerHeight - 4;
        layout.banner = rect_of(x, y, w, kBannerHeight);
        if (layout.banner.right > layout.banner.left) {
            layout.fills.push_back({layout.banner, premultiply(kBannerBg, 216)});
            const int button_y = y + (kBannerHeight - kButtonHeight) / 2;
            const int deny_x = x + w - kPadding - kButtonWidth;
            const int approve_x = deny_x - kPadding - kButtonWidth;
            const RECT approve_rect = rect_of(approve_x, button_y, kButtonWidth, kButtonHeight);
            const RECT deny_rect = rect_of(deny_x, button_y, kButtonWidth, kButtonHeight);
            layout.fills.push_back({approve_rect, premultiply(kApprove, 255)});
            layout.fills.push_back({deny_rect, premultiply(kDeny, 255)});
            layout.texts.push_back({{approve_rect.left + kPadding, approve_rect.top,
                                     approve_rect.right, approve_rect.bottom},
                                    wide_of("Approve"),
                                    kBannerFg,
                                    kApprove,
                                    255});
            layout.texts.push_back(
                {{deny_rect.left + kPadding, deny_rect.top, deny_rect.right, deny_rect.bottom},
                 wide_of("Deny"),
                 kBannerFg,
                 kDeny,
                 255});
            layout.buttons.push_back({approve_rect, true, frame.confirmation->request_id});
            layout.buttons.push_back({deny_rect, false, frame.confirmation->request_id});
        }
    }

    state.layout = std::move(layout);
    state.layout_valid = true;
    return true;
}

/// Repaints and presents the full-screen visual window from `state.layout`.
bool present_visual(OverlayState &state, LayeredSurface &surface, HFONT font) {
    surface.clear();
    for (const Layout::Fill &fill : state.layout.fills) {
        surface.fill(fill.rect, fill.pixel);
    }
    surface.draw_texts(font, state.layout.texts, state.screen.left, state.screen.top);
    const POINT origin{state.screen.left, state.screen.top};
    return surface.present(origin);
}

/// Repaints, positions and shows (or hides) the interaction window, whose
/// content is exactly the confirm banner (transparent elsewhere, so the
/// layered hit test passes clicks through around the buttons).
bool present_interaction(OverlayState &state, LayeredSurface &surface, HFONT font) {
    const bool want = state.frame.visible && state.frame.confirmation.has_value() &&
                      state.layout.banner.right > state.layout.banner.left;
    if (!want) {
        if (state.interaction_shown) {
            ::ShowWindow(state.interaction, SW_HIDE);
            state.interaction_shown = false;
        }
        return true;
    }
    const RECT &banner = state.layout.banner;
    const int banner_w = static_cast<int>(banner.right - banner.left);
    if (surface.pixels == nullptr || surface.width != banner_w || surface.height != kBannerHeight) {
        surface.destroy();
        std::string diagnostic;
        if (!surface.create(state.interaction, banner_w, kBannerHeight, diagnostic)) {
            return false;
        }
    }
    surface.clear();
    for (const Layout::Fill &fill : state.layout.fills) {
        if (fill.rect.top >= banner.top && fill.rect.bottom <= banner.bottom &&
            fill.rect.left >= banner.left && fill.rect.right <= banner.right) {
            surface.fill({fill.rect.left - banner.left, fill.rect.top - banner.top,
                          fill.rect.right - banner.left, fill.rect.bottom - banner.top},
                         fill.pixel);
        }
    }
    surface.draw_texts(font, state.layout.texts, banner.left, banner.top);
    if (!surface.present({banner.left, banner.top})) {
        return false;
    }
    ::SetWindowPos(state.interaction, HWND_TOPMOST, banner.left, banner.top, banner_w,
                   kBannerHeight, SWP_NOACTIVATE | (state.interaction_shown ? 0 : SWP_SHOWWINDOW));
    state.interaction_shown = true;
    return true;
}

} // namespace

struct Win32OverlayCarrier::State {
    OverlayState pump;
    LayeredSurface visual_surface;
    LayeredSurface interaction_surface;
    HFONT font = nullptr;
    HDC measure_dc = nullptr;
};

Win32OverlayCarrier::~Win32OverlayCarrier() { delete static_cast<State *>(state_); }

std::unique_ptr<Win32OverlayCarrier> Win32OverlayCarrier::open() {
    // A session without an interactive display has no honest overlay
    // surface (same probe as the Win32 frontend, DEC-017 decision 3).
    if (::GetSystemMetrics(SM_CXVIRTUALSCREEN) <= 0 ||
        ::GetSystemMetrics(SM_CYVIRTUALSCREEN) <= 0) {
        return nullptr;
    }
    return std::unique_ptr<Win32OverlayCarrier>(new Win32OverlayCarrier());
}

mirage::desktop::OverlayCarrier::RunReport
Win32OverlayCarrier::run(const OverlayCarrierContext &context,
                         const std::function<bool()> &stop_requested) {
    RunReport report;
    if (!register_window_class()) {
        report.diagnostic = "overlay window class registration failed" + last_error_suffix();
        return report;
    }
    State *state = new State();
    state_ = state;
    OverlayState &pump = state->pump;
    pump.context = context;
    pump.screen = {::GetSystemMetrics(SM_XVIRTUALSCREEN), ::GetSystemMetrics(SM_YVIRTUALSCREEN),
                   ::GetSystemMetrics(SM_XVIRTUALSCREEN) + ::GetSystemMetrics(SM_CXVIRTUALSCREEN),
                   ::GetSystemMetrics(SM_YVIRTUALSCREEN) + ::GetSystemMetrics(SM_CYVIRTUALSCREEN)};

    const DWORD ex_visual =
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    const DWORD ex_interaction =
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    const int vw = pump.screen.right - pump.screen.left;
    const int vh = pump.screen.bottom - pump.screen.top;
    pump.visual = ::CreateWindowExW(ex_visual, kClassName, L"Mirage Overlay", WS_POPUP,
                                    pump.screen.left, pump.screen.top, vw, vh, nullptr, nullptr,
                                    ::GetModuleHandleW(nullptr), nullptr);
    pump.interaction =
        ::CreateWindowExW(ex_interaction, kClassName, L"Mirage Overlay Confirm", WS_POPUP, 0, 0, 10,
                          10, nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (pump.visual == nullptr || pump.interaction == nullptr) {
        report.diagnostic = "overlay window creation failed" + last_error_suffix();
        if (pump.visual != nullptr) {
            ::DestroyWindow(pump.visual);
        }
        if (pump.interaction != nullptr) {
            ::DestroyWindow(pump.interaction);
        }
        delete state;
        state_ = nullptr;
        return report;
    }
    ::SetWindowLongPtrW(pump.interaction, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&pump));

    std::string diagnostic;
    bool surfaces_ok =
        state->visual_surface.create(pump.visual, vw, vh, diagnostic) &&
        state->interaction_surface.create(pump.interaction, 10, kBannerHeight, diagnostic);
    state->measure_dc = ::CreateCompatibleDC(nullptr);
    state->font = ::CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    if (state->measure_dc != nullptr && state->font != nullptr) {
        ::SelectObject(state->measure_dc, state->font);
    } else {
        surfaces_ok = false;
        diagnostic = "overlay font/measurement setup failed" + last_error_suffix();
    }

    // Publish the wakeup target only once the surface set is complete, so a
    // wakeup racing creation hits a harmless null.
    if (surfaces_ok) {
        wakeup_target_.store(pump.visual, std::memory_order_release);
        ::SetWindowPos(pump.visual, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    } else {
        report.diagnostic = diagnostic;
    }

    if (surfaces_ok) {
        report.clean = true;
        MSG message;
        bool quit_posted = false;
        while (!stop_requested() && !quit_posted) {
            // Drain the message queue first: queued confirm clicks are the
            // user-facing surface of the carrier.
            while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    quit_posted = true; // owner thread is going away
                    break;
                }
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
            if (stop_requested() || quit_posted) {
                break;
            }

            if (context.on_tick) {
                context.on_tick();
            }

            OverlaySurfaceFrame incoming;
            if (context.load_frame && context.load_frame(incoming)) {
                pump.frame = std::move(incoming);
                pump.has_frame = true;
                pump.shown_countdown = -1;
                pump.confirm_deadline =
                    std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(
                        pump.frame.confirmation.has_value()
                            ? std::max<std::int64_t>(0, pump.frame.confirmation->timeout_ms)
                            : 0);
                pump.presented = false;
            }

            if (pump.has_frame) {
                // Repaint on new frames and on confirmation countdown ticks.
                const long countdown = pump.countdown_seconds(std::chrono::steady_clock::now());
                if (!pump.presented || countdown != pump.shown_countdown) {
                    pump.shown_countdown = countdown;
                    if (compose_layout(pump, state->measure_dc, countdown) &&
                        present_visual(pump, state->visual_surface, state->font) &&
                        present_interaction(pump, state->interaction_surface, state->font)) {
                        // presented below
                    } else {
                        report.clean = false;
                        if (report.diagnostic.empty()) {
                            report.diagnostic = "overlay presentation failed" + last_error_suffix();
                        }
                        break;
                    }
                    pump.presented = true;
                }
            }

            // Bounded wait; wakeup() posts WM_NULL so a concurrent stop is
            // observed within one slice.
            (void)::MsgWaitForMultipleObjectsEx(0, nullptr, kPollSliceMs, QS_ALLINPUT,
                                                MWMO_INPUTAVAILABLE);
        }
    }

    // Teardown on the pump thread (thread affinity, DEC-018 ownership
    // model): hide the surfaces, destroy the windows, release the GDI kit.
    wakeup_target_.store(nullptr, std::memory_order_release);
    state->visual_surface.destroy();
    state->interaction_surface.destroy();
    if (state->font != nullptr) {
        ::DeleteObject(state->font);
        state->font = nullptr;
    }
    if (state->measure_dc != nullptr) {
        ::DeleteDC(state->measure_dc);
        state->measure_dc = nullptr;
    }
    if (pump.visual != nullptr) {
        ::DestroyWindow(pump.visual);
    }
    if (pump.interaction != nullptr) {
        ::DestroyWindow(pump.interaction);
    }
    delete state;
    state_ = nullptr;
    return report;
}

Win32OverlayCarrier::Win32OverlayCarrier() = default;

void Win32OverlayCarrier::wakeup() {
    // Published only while a healthy presentation loop is running; the
    // posted WM_NULL releases the MsgWaitForMultipleObjectsEx wait.
    const HWND target = static_cast<HWND>(wakeup_target_.load(std::memory_order_acquire));
    if (target != nullptr) {
        ::PostMessageW(target, WM_NULL, 0, 0);
    }
}

std::unique_ptr<mirage::desktop::OverlayCarrier> open_overlay_carrier() {
    return Win32OverlayCarrier::open();
}

} // namespace mirage::platform::windows_backend
