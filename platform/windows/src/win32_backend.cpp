#include "win32_backend.hpp"

#include "win32_util.hpp"

// The Win32 surface lives in win32_util.hpp (macro-neutral include, RULE-01,
// DEC-017 decision 5); every Win32 type stays inside this translation unit.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace mirage::platform::windows_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::CaptureLimits;
using mirage::desktop::CaptureOutcome;
using mirage::desktop::DisplayInfo;
using mirage::desktop::DisplayListLimits;
using mirage::desktop::DisplayListOutcome;
using mirage::desktop::InputLimits;
using mirage::desktop::InputOutcome;
using mirage::desktop::KeyChord;
using mirage::desktop::MouseButton;
using mirage::desktop::PointerQueryOutcome;
using mirage::desktop::ProviderError;
using mirage::desktop::WindowActionOutcome;
using mirage::desktop::WindowGeometry;
using mirage::desktop::WindowInfo;
using mirage::desktop::WindowListLimits;
using mirage::desktop::WindowListOutcome;
using mirage::desktop::WindowQueryOutcome;

using win32_util::error;
using win32_util::format_window_id;
using win32_util::last_error_suffix;
using win32_util::parse_window_id;
using win32_util::utf16_to_utf8;
using win32_util::utf8_to_utf16;

bool describe_window(HWND window, bool focused, WindowInfo &info) {
    const int title_len = ::GetWindowTextLengthW(window);
    std::wstring title(static_cast<std::size_t>(title_len) + 1, L'\0');
    const int copied = ::GetWindowTextW(window, title.data(), title_len + 1);
    if (copied < 0) {
        return false;
    }
    title.resize(static_cast<std::size_t>(copied));
    RECT rect{};
    if (::GetWindowRect(window, &rect) == 0) {
        return false;
    }
    info.id = format_window_id(window);
    info.title = utf16_to_utf8(title);
    info.geometry =
        WindowGeometry{rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
    info.focused = focused;
    return true;
}

struct EnumWindowsContext {
    std::vector<WindowInfo> windows;
    std::size_t max_windows = 0;
    bool over_budget = false;
    bool cancelled = false;
};

BOOL CALLBACK enum_windows_callback(HWND window, LPARAM lparam) {
    auto *context = reinterpret_cast<EnumWindowsContext *>(lparam);
    if (context->cancelled) {
        return FALSE;
    }
    // Skeleton enumeration discipline (M4-01): interactive top-level windows
    // with a title. Untitled system surfaces and DWM-cloaked windows (other
    // virtual desktops, suspended UWP hosts) are skipped or included only
    // where IsWindowVisible says so; a DWM-aware cloaking filter is a later
    // refinement and is recorded in the M4 plan.
    if (::IsWindowVisible(window) == 0 || ::GetWindowTextLengthW(window) == 0) {
        return TRUE;
    }
    if (context->windows.size() >= context->max_windows) {
        context->over_budget = true;
        return FALSE;
    }
    WindowInfo info;
    if (!describe_window(window, false, info)) {
        return TRUE; // a window that vanished mid-enumeration is not an error
    }
    context->windows.push_back(std::move(info));
    return TRUE;
}

struct EnumMonitorsContext {
    std::vector<DisplayInfo> displays;
    std::size_t max_displays = 0;
    bool over_budget = false;
    bool cancelled = false;
};

BOOL CALLBACK enum_monitors_callback(HMONITOR monitor, HDC, LPRECT, LPARAM lparam) {
    auto *context = reinterpret_cast<EnumMonitorsContext *>(lparam);
    if (context->cancelled) {
        return FALSE;
    }
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info); // EX variant: only it carries szDevice
    if (::GetMonitorInfoW(monitor, &info) == 0) {
        return TRUE; // a monitor that vanished mid-enumeration is not an error
    }
    if (context->displays.size() >= context->max_displays) {
        context->over_budget = true;
        return FALSE;
    }
    DisplayInfo display;
    display.id = utf16_to_utf8(info.szDevice);
    display.geometry = WindowGeometry{info.rcMonitor.left, info.rcMonitor.top,
                                      info.rcMonitor.right - info.rcMonitor.left,
                                      info.rcMonitor.bottom - info.rcMonitor.top};
    display.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;
    context->displays.push_back(std::move(display));
    return TRUE;
}

/// Enumerates monitors with the shared budget discipline; reused by
/// list_displays and capture_display's id resolution. The caller seeds
/// `context.cancelled` from the token before the call.
bool collect_monitors(const DisplayListLimits &limits, EnumMonitorsContext &context) {
    context.max_displays = limits.max_displays;
    ::EnumDisplayMonitors(nullptr, nullptr, enum_monitors_callback,
                          reinterpret_cast<LPARAM>(&context));
    return !context.cancelled && !context.over_budget;
}

/// Grabs `rect` (virtual-screen coordinates) from the screen DC into a
/// top-down 32bpp DIB and copies it as Bgra8 rows. Root-frame semantics,
/// identical to the X11 backend: pixels are what the screen shows at those
/// coordinates, overlapping content included (M2-02 contract clarification).
CaptureOutcome capture_rect(const WindowGeometry &rect, const CaptureLimits &limits,
                            const CancelToken &cancel) {
    CaptureOutcome outcome;
    const std::size_t stride = static_cast<std::size_t>(rect.width) * 4u;
    const std::size_t bytes = stride * static_cast<std::size_t>(rect.height);
    if (bytes > limits.max_bytes) {
        outcome.error = error("capture_too_large", "capture exceeds the byte budget of " +
                                                       std::to_string(limits.max_bytes) + " bytes");
        return outcome;
    }
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "capture cancelled");
        return outcome;
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.width;
    info.bmiHeader.biHeight = -rect.height; // top-down rows
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = ::GetDC(nullptr);
    if (screen_dc == nullptr) {
        outcome.error = error("io_error", "GetDC for the screen failed" + last_error_suffix());
        return outcome;
    }
    HDC memory_dc = ::CreateCompatibleDC(screen_dc);
    if (memory_dc == nullptr) {
        ::ReleaseDC(nullptr, screen_dc);
        outcome.error = error("io_error", "CreateCompatibleDC failed" + last_error_suffix());
        return outcome;
    }
    void *bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(memory_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        ::DeleteDC(memory_dc);
        ::ReleaseDC(nullptr, screen_dc);
        outcome.error = error("io_error", "CreateDIBSection failed" + last_error_suffix());
        return outcome;
    }
    const HGDIOBJ previous = ::SelectObject(memory_dc, bitmap);
    const BOOL blitted =
        ::BitBlt(memory_dc, 0, 0, rect.width, rect.height, screen_dc, rect.x, rect.y, SRCCOPY);
    ::GdiFlush();

    if (blitted == 0) {
        outcome.error = error("io_error", "BitBlt failed" + last_error_suffix());
    } else {
        outcome.frame.format = mirage::desktop::ImageFormat::Bgra8;
        outcome.frame.width = rect.width;
        outcome.frame.height = rect.height;
        outcome.frame.stride = stride;
        outcome.frame.pixels.resize(bytes);
        std::memcpy(outcome.frame.pixels.data(), bits, bytes);
        outcome.ok = true;
    }

    ::SelectObject(memory_dc, previous);
    ::DeleteObject(bitmap);
    ::DeleteDC(memory_dc);
    ::ReleaseDC(nullptr, screen_dc);
    return outcome;
}

/// Finds the monitor registered under `display_id` (the verbatim szDevice
/// string reported by list_displays) and returns its virtual-screen rect.
std::optional<WindowGeometry> monitor_rect_by_id(const std::string &display_id) {
    EnumMonitorsContext context;
    context.max_displays = 64; // resolution pass: ids, not user budgets
    ::EnumDisplayMonitors(nullptr, nullptr, enum_monitors_callback,
                          reinterpret_cast<LPARAM>(&context));
    for (const auto &display : context.displays) {
        if (display.id == display_id) {
            return display.geometry;
        }
    }
    return std::nullopt;
}

/// Resolves the base key of an already-parsed chord to a virtual-key code.
/// Callers only get here with a name that passed parse_key_chord, so the
/// possibilities are the fixed named-key table, f1..f12, or a single
/// printable ASCII character. Characters resolve through VkKeyScanW so the
/// injected key matches the active layout (the chord's explicit modifiers
/// stay the source of truth for shift state).
std::optional<WORD> base_key_vk(const std::string &base) {
    // Plain aggregates instead of std::pair: VK_* macros are plain int, and
    // a pair's forwarding constructor would narrow int -> WORD inside a
    // template (MSVC C4242), while aggregate initialization performs the
    // conversion at the constant itself.
    struct NamedKey {
        std::string_view name;
        WORD vk;
    };
    static constexpr std::array<NamedKey, 15> kNamedKeys{{
        {"enter", VK_RETURN},
        {"tab", VK_TAB},
        {"escape", VK_ESCAPE},
        {"backspace", VK_BACK},
        {"delete", VK_DELETE},
        {"insert", VK_INSERT},
        {"home", VK_HOME},
        {"end", VK_END},
        {"pageup", VK_PRIOR},
        {"pagedown", VK_NEXT},
        {"left", VK_LEFT},
        {"right", VK_RIGHT},
        {"up", VK_UP},
        {"down", VK_DOWN},
        {"space", VK_SPACE},
    }};
    for (const auto &[name, vk] : kNamedKeys) {
        if (base == name) {
            return vk;
        }
    }
    if (base.size() >= 2) {
        // f1..f12 are the only remaining valid multi-character names.
        const std::string_view number(base.data() + 1, base.size() - 1);
        if (base.front() != 'f' || !std::all_of(number.begin(), number.end(),
                                                [](char ch) { return ch >= '0' && ch <= '9'; })) {
            return std::nullopt;
        }
        const unsigned long index = std::stoul(base.substr(1));
        if (index >= 1 && index <= 12) {
            return static_cast<WORD>(VK_F1 + (index - 1));
        }
        return std::nullopt;
    }
    const SHORT scan = ::VkKeyScanW(static_cast<WCHAR>(base.front()));
    if (scan != -1) {
        return static_cast<WORD>(LOBYTE(scan));
    }
    return std::nullopt;
}

INPUT key_input(WORD vk, DWORD flags) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = flags;
    return input;
}

/// Sends one batch of key events; on partial injection (UIPI refusal,
/// session 0) a best-effort release of the events that did land keeps the
/// keyboard state clean instead of leaking stuck keys. SendInput's LPINPUT
/// parameter is mutable, so the batch is passed as a non-const vector.
InputOutcome send_key_events(std::vector<INPUT> &events) {
    InputOutcome outcome;
    const UINT sent = ::SendInput(static_cast<UINT>(events.size()), events.data(),
                                  static_cast<int>(sizeof(INPUT)));
    if (sent == events.size()) {
        outcome.ok = true;
        return outcome;
    }
    std::vector<INPUT> cleanup;
    cleanup.reserve(sent);
    for (UINT index = 0; index < sent; ++index) {
        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = events[index].ki.wVk;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        cleanup.push_back(up);
    }
    if (!cleanup.empty()) {
        ::SendInput(static_cast<UINT>(cleanup.size()), cleanup.data(),
                    static_cast<int>(sizeof(INPUT)));
    }
    outcome.error =
        error("io_error", "SendInput was refused after " + std::to_string(sent) + " of " +
                              std::to_string(events.size()) + " events" + last_error_suffix());
    return outcome;
}

} // namespace

std::unique_ptr<Win32Backend> Win32Backend::open() {
    // A session without an interactive display has no honest window,
    // capture or input surface (DEC-017 decision 3).
    if (::GetSystemMetrics(SM_CXVIRTUALSCREEN) <= 0 ||
        ::GetSystemMetrics(SM_CYVIRTUALSCREEN) <= 0) {
        return nullptr;
    }
    return std::unique_ptr<Win32Backend>(new Win32Backend());
}

Win32Backend::~Win32Backend() = default;

WindowListOutcome Win32Backend::list_windows(const WindowListLimits &limits,
                                             const CancelToken &cancel) {
    WindowListOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    EnumWindowsContext context;
    context.max_windows = limits.max_windows;
    context.cancelled = cancel.cancelled();
    ::EnumWindows(enum_windows_callback, reinterpret_cast<LPARAM>(&context));
    if (context.cancelled) {
        outcome.error = error("cancelled", "window enumeration cancelled");
        return outcome;
    }
    if (context.over_budget) {
        outcome.error =
            error("result_too_large",
                  "more than " + std::to_string(limits.max_windows) + " windows on this desktop");
        return outcome;
    }
    const HWND foreground = ::GetForegroundWindow();
    for (auto &window : context.windows) {
        window.focused = foreground != nullptr && window.id == format_window_id(foreground);
    }
    outcome.windows = std::move(context.windows);
    outcome.ok = true;
    return outcome;
}

WindowQueryOutcome Win32Backend::front_window(const CancelToken &cancel) {
    WindowQueryOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.error = error("cancelled", "focus query cancelled");
        return outcome;
    }
    const HWND foreground = ::GetForegroundWindow();
    if (foreground == nullptr) {
        // No window has focus right now — reported as found=false per the
        // WindowProvider contract, not as an error.
        outcome.ok = true;
        return outcome;
    }
    outcome.ok = describe_window(foreground, true, outcome.window);
    outcome.found = outcome.ok;
    if (!outcome.ok) {
        outcome.error =
            error("io_error", "describing the foreground window failed" + last_error_suffix());
    }
    return outcome;
}

WindowActionOutcome Win32Backend::activate(const std::string &window_id,
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
    if (::IsWindow(*window) == 0) {
        outcome.error = error("not_found", "no such window");
        return outcome;
    }
    if (::IsIconic(*window) != 0) {
        // A minimized window cannot take the foreground without a restore.
        ::ShowWindow(*window, SW_RESTORE);
    }
    // Plain request semantics, mirroring the X11 EWMH path: the provider
    // reports whether the request was accepted. The system's foreground
    // lock and UIPI may still refuse — that surfaces as a failed request,
    // never as a silent no-op (DEC-017 risk note).
    outcome.ok = ::SetForegroundWindow(*window) != 0;
    if (!outcome.ok) {
        outcome.error = error("io_error", "SetForegroundWindow was refused" + last_error_suffix());
    }
    return outcome;
}

DisplayListOutcome Win32Backend::list_displays(const DisplayListLimits &limits,
                                               const CancelToken &cancel) {
    DisplayListOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    EnumMonitorsContext context;
    context.cancelled = cancel.cancelled();
    if (!collect_monitors(limits, context)) {
        if (context.cancelled) {
            outcome.error = error("cancelled", "display enumeration cancelled");
        } else {
            outcome.error =
                error("result_too_large", "more than " + std::to_string(limits.max_displays) +
                                              " displays on this desktop");
        }
        return outcome;
    }
    outcome.displays = std::move(context.displays);
    outcome.ok = true;
    return outcome;
}

CaptureOutcome Win32Backend::capture_display(const std::string &display_id,
                                             const CaptureLimits &limits,
                                             const CancelToken &cancel) {
    CaptureOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    const auto rect = monitor_rect_by_id(display_id);
    if (!rect.has_value()) {
        outcome.error = error("not_found", "no such display");
        return outcome;
    }
    return capture_rect(*rect, limits, cancel);
}

CaptureOutcome Win32Backend::capture_window(const std::string &window_id,
                                            const CaptureLimits &limits,
                                            const CancelToken &cancel) {
    CaptureOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    const auto window = parse_window_id(window_id);
    if (!window.has_value()) {
        outcome.error = error("invalid_argument", "malformed window id");
        return outcome;
    }
    // Root-frame model (M2-02 contract clarification): a hidden window has
    // nothing on screen to capture, so it fails closed.
    if (::IsWindow(*window) == 0 || ::IsWindowVisible(*window) == 0) {
        outcome.error = error("not_found", "no such viewable window");
        return outcome;
    }
    RECT rect{};
    if (::GetWindowRect(*window, &rect) == 0) {
        outcome.error = error("io_error", "GetWindowRect failed" + last_error_suffix());
        return outcome;
    }
    const WindowGeometry geometry{rect.left, rect.top, rect.right - rect.left,
                                  rect.bottom - rect.top};
    if (geometry.width <= 0 || geometry.height <= 0) {
        outcome.error = error("not_found", "window has no on-screen extent");
        return outcome;
    }
    return capture_rect(geometry, limits, cancel);
}

CaptureOutcome Win32Backend::capture_roi(const WindowGeometry &roi, const CaptureLimits &limits,
                                         const CancelToken &cancel) {
    CaptureOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (roi.width <= 0 || roi.height <= 0) {
        outcome.error = error("invalid_argument", "roi extents must be positive");
        return outcome;
    }
    return capture_rect(roi, limits, cancel);
}

InputOutcome Win32Backend::inject_key(const mirage::desktop::KeySym &key, bool pressed,
                                      const InputLimits &limits, const CancelToken &cancel) {
    InputOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "key injection cancelled");
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
    const auto base = base_key_vk(chord->base);
    if (!base.has_value()) {
        outcome.error = error("invalid_argument", "unknown key name");
        return outcome;
    }
    // Modifier order follows the documented vocabulary order (ctrl, alt,
    // shift, meta); releases run in reverse.
    std::array<WORD, 4> modifiers{};
    std::size_t modifier_count = 0;
    if (chord->ctrl) {
        modifiers[modifier_count++] = VK_CONTROL;
    }
    if (chord->alt) {
        modifiers[modifier_count++] = VK_MENU;
    }
    if (chord->shift) {
        modifiers[modifier_count++] = VK_SHIFT;
    }
    if (chord->meta) {
        modifiers[modifier_count++] = VK_LWIN;
    }
    std::vector<INPUT> events;
    events.reserve(modifier_count + 1);
    if (pressed) {
        for (std::size_t index = 0; index < modifier_count; ++index) {
            events.push_back(key_input(modifiers[index], 0));
        }
        events.push_back(key_input(*base, 0));
    } else {
        for (std::size_t index = modifier_count; index > 0; --index) {
            events.push_back(key_input(modifiers[index - 1], KEYEVENTF_KEYUP));
        }
        events.push_back(key_input(*base, KEYEVENTF_KEYUP));
    }
    return send_key_events(events);
}

InputOutcome Win32Backend::type_text(const std::string &text, const InputLimits &limits,
                                     const CancelToken &cancel) {
    InputOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "text injection cancelled");
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
    const auto units = utf8_to_utf16(text);
    if (!units.has_value()) {
        outcome.error = error("invalid_argument", "text must be UTF-8");
        return outcome;
    }
    // One UNICODE event pair per UTF-16 code unit (surrogate pairs inject as
    // two pairs and the system combines them). DEC-017 decision 5: Windows
    // carries arbitrary UTF-16 text here — no X11-style keymap boundary.
    constexpr std::size_t kUnitsPerBatch = 512;
    std::vector<INPUT> events;
    events.reserve(kUnitsPerBatch * 2);
    for (std::size_t offset = 0; offset < units->size(); offset += kUnitsPerBatch) {
        events.clear();
        const std::size_t count = std::min(kUnitsPerBatch, units->size() - offset);
        for (std::size_t index = 0; index < count; ++index) {
            const WORD unit = static_cast<WORD>((*units)[offset + index]);
            INPUT down{};
            down.type = INPUT_KEYBOARD;
            down.ki.wScan = unit;
            down.ki.dwFlags = KEYEVENTF_UNICODE;
            events.push_back(down);
            INPUT up = down;
            up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            events.push_back(up);
        }
        const UINT sent = ::SendInput(static_cast<UINT>(events.size()), events.data(),
                                      static_cast<int>(sizeof(INPUT)));
        if (sent != events.size()) {
            // Release the units whose down events landed in this batch.
            std::vector<INPUT> cleanup;
            cleanup.reserve(sent / 2);
            for (UINT index = 0; index + 1 < sent; index += 2) {
                INPUT up{};
                up.type = INPUT_KEYBOARD;
                up.ki.wScan = events[index].ki.wScan;
                up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                cleanup.push_back(up);
            }
            if (!cleanup.empty()) {
                ::SendInput(static_cast<UINT>(cleanup.size()), cleanup.data(),
                            static_cast<int>(sizeof(INPUT)));
            }
            outcome.error = error(
                "io_error", "SendInput was refused after " + std::to_string(sent) + " of " +
                                std::to_string(events.size()) + " events" + last_error_suffix());
            return outcome;
        }
    }
    outcome.ok = true;
    return outcome;
}

InputOutcome Win32Backend::pointer_move(std::int32_t x, std::int32_t y, const InputLimits &limits,
                                        const CancelToken &cancel) {
    InputOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "pointer move cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    const int origin_x = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const int origin_y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int height = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0) {
        outcome.error = error("unsupported_platform", "no interactive display attached");
        return outcome;
    }
    // Absolute virtual-desktop mapping; out-of-range requests clamp to the
    // nearest representable position (SendInput has no partial-failure path
    // for a single event).
    const auto normalize = [](std::int32_t value, std::int32_t origin,
                              std::int32_t extent) -> WORD {
        const long long scaled =
            (static_cast<long long>(value) - origin) * 65536 / static_cast<long long>(extent);
        return static_cast<WORD>(std::clamp(scaled, 0LL, 65535LL));
    };
    INPUT event{};
    event.type = INPUT_MOUSE;
    event.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    event.mi.dx = normalize(x, origin_x, width);
    event.mi.dy = normalize(y, origin_y, height);
    if (::SendInput(1, &event, sizeof(INPUT)) != 1) {
        outcome.error = error("io_error", "SendInput was refused" + last_error_suffix());
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

InputOutcome Win32Backend::pointer_button(const MouseButton &button, bool pressed,
                                          const InputLimits &limits, const CancelToken &cancel) {
    InputOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "pointer button cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    DWORD flags = 0;
    WORD data = 0;
    if (button == "left") {
        flags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else if (button == "middle") {
        flags = pressed ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    } else if (button == "right") {
        flags = pressed ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else if (button == "back" || button == "forward") {
        flags = pressed ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP;
        data = button == "back" ? XBUTTON1 : XBUTTON2;
    } else {
        outcome.error = error("invalid_argument", "unknown mouse button");
        return outcome;
    }
    INPUT event{};
    event.type = INPUT_MOUSE;
    event.mi.dwFlags = flags;
    event.mi.mouseData = data;
    if (::SendInput(1, &event, sizeof(INPUT)) != 1) {
        outcome.error = error("io_error", "SendInput was refused" + last_error_suffix());
        return outcome;
    }
    outcome.ok = true;
    return outcome;
}

PointerQueryOutcome Win32Backend::pointer_position(const CancelToken &cancel) {
    PointerQueryOutcome outcome;
    std::lock_guard<std::mutex> guard(mutex_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "pointer query cancelled");
        return outcome;
    }
    POINT position{};
    if (::GetCursorPos(&position) == 0) {
        outcome.error = error("io_error", "GetCursorPos failed" + last_error_suffix());
        return outcome;
    }
    // GetCursorPos already reports virtual-screen coordinates (negative on
    // monitors left of / above the primary), matching the global desktop
    // coordinate space of the contract.
    outcome.position.x = position.x;
    outcome.position.y = position.y;
    outcome.ok = true;
    return outcome;
}

} // namespace mirage::platform::windows_backend
