// M4-01 Windows backend skeleton tests against the session's real
// interactive desktop (DEC-017 decision 9): real Win32 window management,
// real GDI capture, real SendInput — no simulator. Run evidence can only
// come from a machine with an interactive desktop (the CI windows job or a
// maintainer Windows machine); the MinGW cross builds on Linux provide
// compile and link evidence only.
//
// Assertion philosophy: invariant-style, never environment-fragile. Every
// reported outcome must agree with independently observed desktop state
// (GetForegroundWindow, GetCursorPos, monitor geometry), while refusals
// that depend on the session's privileges (foreground lock, UIPI, session
// 0 without a desktop) are loud stderr notes instead of failures — the
// contract rejections themselves (invalid ids, budgets, bad UTF-8,
// cancellation) are strict and happen before any side effect.
//
// Keyboard delivery (type_text / inject_key) is only provable when the
// test window actually owns the foreground: SendInput routes keystrokes to
// the focus owner, so a session that refused activation cannot observe
// delivery. That scenario is gated on activate() succeeding AND
// GetForegroundWindow() matching; otherwise it prints a note and only the
// rejection paths are exercised. The message loop runs on the main thread
// during and after injection (MsgWaitForMultipleObjects pump, RULE-03: no
// threads anywhere).

#include "../support/test.hpp"

#include <mirage/platform/windows/windows_desktop_environment.hpp>

// Keep every entry point on the explicit W surface regardless of the
// toolchain's default (MinGW defaults to ANSI): resource macros such as
// IDC_ARROW must expand to the wide variants.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::CaptureLimits;
using mirage::desktop::DisplayListLimits;
using mirage::desktop::InputLimits;
using mirage::desktop::KeySym;
using mirage::desktop::WindowListLimits;
using mirage::platform::windows_backend::WindowsDesktopEnvironment;

constexpr wchar_t kWindowClassName[] = L"MirageWin32BackendTestClass";
constexpr const char *kMainTitle = "mirage-win32-backend-test";
constexpr const char *kHiddenTitle = "mirage-win32-test-hidden";

// File-scope state the window procedure records for the scenarios. The test
// is single-threaded (main-thread message pump only, RULE-03), so plain
// globals need no synchronization.
int g_paint_count = 0;
int g_last_char = 0;    // last WM_CHAR code, 0 after a reset
int g_last_keydown = 0; // last WM_KEYDOWN virtual key, 0 after a reset
HANDLE g_key_event = nullptr;

LRESULT CALLBACK test_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        // Deterministic two-tone client fill: capture tests rely on the
        // window contributing visible variation at its own rectangle.
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT full{};
        GetClientRect(window, &full);
        const RECT left{full.left, full.top, full.right / 2, full.bottom};
        const RECT right{full.right / 2, full.top, full.right, full.bottom};
        const HBRUSH blue = CreateSolidBrush(RGB(10, 60, 200));
        FillRect(dc, &left, blue);
        DeleteObject(blue);
        const HBRUSH ivory = CreateSolidBrush(RGB(235, 235, 220));
        FillRect(dc, &right, ivory);
        DeleteObject(ivory);
        EndPaint(window, &paint);
        ++g_paint_count;
        return 0;
    }
    case WM_CHAR:
        g_last_char = static_cast<int>(wparam);
        if (g_key_event != nullptr) {
            SetEvent(g_key_event);
        }
        return 0;
    case WM_KEYDOWN:
        g_last_keydown = static_cast<int>(wparam);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

/// Services the thread's message queue for `milliseconds` (PeekMessage
/// drain plus a MsgWaitForMultipleObjects wait so the loop yields).
void run_pump(DWORD milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    MSG message{};
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
    }
}

/// Drains the queue until `event` fires or the deadline passes; false on
/// timeout. Keystrokes injected by the providers reach the window procedure
/// only while this pump runs, which is why delivery checks never assert
/// before pumping.
bool pump_until_event(HANDLE event, DWORD timeout_milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + timeout_milliseconds;
    MSG message{};
    for (;;) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (WaitForSingleObject(event, 0) == WAIT_OBJECT_0) {
            return true;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        MsgWaitForMultipleObjects(1, &event, FALSE, 15, QS_ALLINPUT);
    }
}

/// Window ids are the decimal form of the HWND value (DEC-017 decision 7);
/// the test parses them back without exceptions.
std::optional<unsigned long long> parse_decimal_id(const std::string &id) {
    if (id.empty() || id.size() > 20) {
        return std::nullopt;
    }
    unsigned long long value = 0;
    for (const char ch : id) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        value = value * 10u + static_cast<unsigned long long>(ch - '0');
    }
    return value;
}

std::string window_id(HWND window) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(window));
}

/// Registers the test window class and creates the two fixture windows: a
/// visible TOPMOST main window (enumeration / activation / capture target)
/// and a never-shown window (the root-frame model's not_found case).
class TestGui {
  public:
    TestGui() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &test_wndproc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
        window_class.lpszClassName = kWindowClassName;
        MIRAGE_CHECK(RegisterClassExW(&window_class) != 0);

        // The title doubles as the enumeration marker; WS_EX_TOPMOST plus an
        // explicit HWND_TOPMOST raise keeps the painted surface on top of
        // whatever the desktop shows behind it.
        const HINSTANCE instance = GetModuleHandleW(nullptr);
        main_ = CreateWindowExW(WS_EX_TOPMOST, kWindowClassName, L"mirage-win32-backend-test",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 320, 240, nullptr,
                                nullptr, instance, nullptr);
        MIRAGE_CHECK(main_ != nullptr);
        hidden_ =
            CreateWindowExW(0, kWindowClassName, L"mirage-win32-test-hidden", WS_OVERLAPPEDWINDOW,
                            40, 40, 200, 150, nullptr, nullptr, instance, nullptr);
        MIRAGE_CHECK(hidden_ != nullptr);
        if (main_ != nullptr) {
            SetWindowPos(main_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        run_pump(300); // let the first WM_PAINT and DWM composition settle
        MIRAGE_CHECK(g_paint_count >= 1);
    }

    ~TestGui() {
        if (main_ != nullptr) {
            DestroyWindow(main_);
        }
        if (hidden_ != nullptr) {
            DestroyWindow(hidden_);
        }
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }

    bool ok() const { return main_ != nullptr && hidden_ != nullptr; }
    HWND main_window() const { return main_; }
    HWND hidden_window() const { return hidden_; }

  private:
    HWND main_ = nullptr;
    HWND hidden_ = nullptr;
};

/// Disabled (default) construction exposes nothing; enabled construction
/// probes the interactive desktop and exposes exactly the M4-01 surface.
/// Both identities report the M4-01 product strings.
void environment_probe_and_identity() {
    WindowsDesktopEnvironment disabled;
    MIRAGE_CHECK(disabled.window() == nullptr);
    MIRAGE_CHECK(disabled.screen() == nullptr);
    MIRAGE_CHECK(disabled.input() == nullptr);
    MIRAGE_CHECK(disabled.filesystem() == nullptr); // M4-03 surface, not landed yet
    MIRAGE_CHECK(disabled.info().name == "mirage-windows");
    MIRAGE_CHECK(disabled.info().platform == "windows");

    WindowsDesktopEnvironment env(mirage::platform::windows_backend::Win32Options{true});
    std::fprintf(stderr, "virtual screen: %dx%d at (%d,%d)\n", GetSystemMetrics(SM_CXVIRTUALSCREEN),
                 GetSystemMetrics(SM_CYVIRTUALSCREEN), GetSystemMetrics(SM_XVIRTUALSCREEN),
                 GetSystemMetrics(SM_YVIRTUALSCREEN));
    // Probe honesty: on an interactive session these hold; a probe failure
    // leaves the accessors null (fail closed) and the checks above-inverted
    // record the gap loudly instead of skipping.
    MIRAGE_CHECK(env.window() != nullptr);
    MIRAGE_CHECK(env.screen() != nullptr);
    MIRAGE_CHECK(env.input() != nullptr);
    MIRAGE_CHECK(env.info().name == "mirage-windows");
    MIRAGE_CHECK(env.info().platform == "windows");
    MIRAGE_CHECK(env.accessibility() == nullptr); // UiaOptions defaults to off (M4-02)
    MIRAGE_CHECK(env.clipboard() == nullptr);     // M4-04 surface, not landed yet
}

/// Enumeration honesty: the visible titled fixture is listed with a
/// round-trippable id, the never-shown fixture is not, budgets refuse
/// without truncation, and cancellation is observed at the entry check.
void window_enumeration_and_budget(WindowsDesktopEnvironment &env, HWND main_window,
                                   HWND hidden_window) {
    mirage::desktop::WindowProvider &windows = *env.window();

    const auto listed = windows.list_windows();
    MIRAGE_CHECK(listed.ok);
    const auto self_entry = std::find_if(
        listed.windows.begin(), listed.windows.end(),
        [](const mirage::desktop::WindowInfo &info) { return info.title == kMainTitle; });
    MIRAGE_CHECK(self_entry != listed.windows.end());
    if (self_entry != listed.windows.end()) {
        MIRAGE_CHECK(self_entry->geometry.width > 0);
        MIRAGE_CHECK(self_entry->geometry.height > 0);
        const auto parsed = parse_decimal_id(self_entry->id);
        MIRAGE_CHECK(parsed.has_value());
        if (parsed.has_value()) {
            // id round-trip: the decimal string is the HWND value itself.
            MIRAGE_CHECK(*parsed == reinterpret_cast<std::uintptr_t>(main_window));
        }
    }
    MIRAGE_CHECK(std::find_if(listed.windows.begin(), listed.windows.end(),
                              [](const mirage::desktop::WindowInfo &info) {
                                  return info.title == kHiddenTitle;
                              }) == listed.windows.end());

    // Budget refusal: a limit of zero rejects the enumeration instead of
    // returning a truncated list (the fixture window alone is over budget).
    WindowListLimits zero;
    zero.max_windows = 0;
    const auto refused = windows.list_windows(zero, CancelToken{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "result_too_large");
    MIRAGE_CHECK(refused.windows.empty());
    MIRAGE_CHECK(windows.list_windows(WindowListLimits{}, CancelToken{}).ok);

    // Entry pre-check cancellation: reported before any enumeration.
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled = windows.list_windows(WindowListLimits{}, cancel);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.error.code == "cancelled");
    (void)hidden_window;
}

/// Activation is validated before effects (malformed and unknown ids),
/// reports cancellation at the entry check, and when the request is
/// accepted the reported result must agree with the observed foreground
/// state; a foreground-lock refusal is an honest io_error, not a failure.
void activation_and_focus_consistency(WindowsDesktopEnvironment &env, HWND main_window) {
    mirage::desktop::WindowProvider &windows = *env.window();

    MIRAGE_CHECK(!windows.activate("", CancelToken{}).ok);
    MIRAGE_CHECK(windows.activate("", CancelToken{}).error.code == "invalid_argument");
    MIRAGE_CHECK(!windows.activate("12abc", CancelToken{}).ok);
    MIRAGE_CHECK(windows.activate("12abc", CancelToken{}).error.code == "invalid_argument");
    MIRAGE_CHECK(!windows.activate("0", CancelToken{}).ok); // 0 is never an HWND
    MIRAGE_CHECK(windows.activate("0", CancelToken{}).error.code == "invalid_argument");

    MIRAGE_CHECK(!windows.activate("4294967295", CancelToken{}).ok);
    MIRAGE_CHECK(windows.activate("4294967295", CancelToken{}).error.code == "not_found");

    const std::string self_id = window_id(main_window);
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled = windows.activate(self_id, cancel);
    MIRAGE_CHECK(cancelled.cancelled);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.error.code == "cancelled");

    const auto activated = windows.activate(self_id, CancelToken{});
    if (activated.ok) {
        // Reported acceptance must match the observed foreground owner.
        MIRAGE_CHECK(GetForegroundWindow() == main_window);
        const auto front = windows.front_window(CancelToken{});
        MIRAGE_CHECK(front.ok);
        MIRAGE_CHECK(front.found);
        if (front.found) {
            MIRAGE_CHECK(front.window.id == self_id);
            MIRAGE_CHECK(front.window.focused);
        }
        // The enumeration's focused flag agrees with GetForegroundWindow.
        const auto relisted = windows.list_windows();
        MIRAGE_CHECK(relisted.ok);
        const auto entry = std::find_if(
            relisted.windows.begin(), relisted.windows.end(),
            [&self_id](const mirage::desktop::WindowInfo &info) { return info.id == self_id; });
        MIRAGE_CHECK(entry != relisted.windows.end());
        if (entry != relisted.windows.end()) {
            MIRAGE_CHECK(entry->focused);
        }
    } else {
        // The foreground lock may refuse activation from a background
        // process; the refusal must surface as io_error, never as a silent
        // no-op (DEC-017 risk note).
        MIRAGE_CHECK(activated.error.code == "io_error");
        std::fprintf(stderr, "note: this session refuses SetForegroundWindow (foreground lock); "
                             "focus-dependent scenarios are skipped\n");
    }

    // front_window consistency, independent of activation success.
    const auto front = windows.front_window(CancelToken{});
    MIRAGE_CHECK(front.ok);
    if (front.found) {
        MIRAGE_CHECK(front.window.focused);
        MIRAGE_CHECK(front.window.geometry.width >= 0);
        MIRAGE_CHECK(front.window.geometry.height >= 0);
    }

    CancelToken query_cancel;
    query_cancel.request_cancel();
    const auto query_cancelled = windows.front_window(query_cancel);
    MIRAGE_CHECK(!query_cancelled.ok);
    MIRAGE_CHECK(query_cancelled.error.code == "cancelled");
}

/// Display enumeration and the capture paths: budget refusals, ROI
/// validation, the root-frame visibility gate, unknown ids, and frame
/// invariants (Bgra8, stride and byte-count consistency, painted content).
void displays_and_capture(WindowsDesktopEnvironment &env, HWND main_window, HWND hidden_window) {
    mirage::desktop::ScreenProvider &screen = *env.screen();

    const auto displays = screen.list_displays(DisplayListLimits{}, CancelToken{});
    MIRAGE_CHECK(displays.ok);
    MIRAGE_CHECK(displays.displays.size() >= 1);
    std::size_t primary_count = 0;
    const mirage::desktop::DisplayInfo *primary = nullptr;
    for (const mirage::desktop::DisplayInfo &display : displays.displays) {
        MIRAGE_CHECK(display.geometry.width > 0);
        MIRAGE_CHECK(display.geometry.height > 0);
        // display ids are the verbatim MONITORINFOW::szDevice UTF-8 form.
        MIRAGE_CHECK(display.id.rfind("\\\\.\\", 0) == 0);
        if (display.primary) {
            ++primary_count;
            primary = &display;
        }
    }
    MIRAGE_CHECK(primary_count == 1);
    if (primary == nullptr && !displays.displays.empty()) {
        primary = &displays.displays[0];
    }

    // Budget refusal on displays, and entry pre-check cancellation.
    DisplayListLimits zero_displays;
    zero_displays.max_displays = 0;
    const auto refused = screen.list_displays(zero_displays, CancelToken{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "result_too_large");
    MIRAGE_CHECK(refused.displays.empty());
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled_list = screen.list_displays(DisplayListLimits{}, cancel);
    MIRAGE_CHECK(!cancelled_list.ok);
    MIRAGE_CHECK(cancelled_list.error.code == "cancelled");

    // ROI validation happens before any capture.
    const mirage::desktop::WindowGeometry zero_width{0, 0, 0, 10};
    MIRAGE_CHECK(!screen.capture_roi(zero_width, CaptureLimits{}, CancelToken{}).ok);
    MIRAGE_CHECK(screen.capture_roi(zero_width, CaptureLimits{}, CancelToken{}).error.code ==
                 "invalid_argument");
    const mirage::desktop::WindowGeometry negative_height{0, 0, 10, -5};
    MIRAGE_CHECK(screen.capture_roi(negative_height, CaptureLimits{}, CancelToken{}).error.code ==
                 "invalid_argument");

    // Zero byte budget refuses before any capture work.
    CaptureLimits zero_bytes;
    zero_bytes.max_bytes = 0;
    const auto too_large = screen.capture_roi(mirage::desktop::WindowGeometry{0, 0, 16, 16},
                                              zero_bytes, CancelToken{});
    MIRAGE_CHECK(!too_large.ok);
    MIRAGE_CHECK(too_large.error.code == "capture_too_large");
    MIRAGE_CHECK(too_large.frame.pixels.empty());

    // Capture cancellation is observed (under a non-zero budget).
    CancelToken capture_cancel;
    capture_cancel.request_cancel();
    const auto cancelled_capture = screen.capture_roi(mirage::desktop::WindowGeometry{0, 0, 16, 16},
                                                      CaptureLimits{}, capture_cancel);
    MIRAGE_CHECK(cancelled_capture.cancelled);
    MIRAGE_CHECK(cancelled_capture.error.code == "cancelled");

    // Unknown display ids fail closed; ids are opaque strings here, so the
    // malformed-shaped ones are simply not found, never partial captures.
    MIRAGE_CHECK(
        screen.capture_display("\\\\.\\DISPLAY99", CaptureLimits{}, CancelToken{}).error.code ==
        "not_found");
    MIRAGE_CHECK(
        screen.capture_display("no-such-monitor", CaptureLimits{}, CancelToken{}).error.code ==
        "not_found");

    if (primary != nullptr) {
        // Whole-display capture sized to the display's own geometry (the
        // default budget may legitimately refuse huge desktops).
        CaptureLimits display_budget;
        display_budget.max_bytes = static_cast<std::size_t>(primary->geometry.width) *
                                   static_cast<std::size_t>(primary->geometry.height) * 4u;
        const auto shot = screen.capture_display(primary->id, display_budget, CancelToken{});
        MIRAGE_CHECK(shot.ok);
        if (shot.ok) {
            MIRAGE_CHECK(shot.frame.format == mirage::desktop::ImageFormat::Bgra8);
            MIRAGE_CHECK(shot.frame.width == primary->geometry.width);
            MIRAGE_CHECK(shot.frame.height == primary->geometry.height);
            MIRAGE_CHECK(shot.frame.stride == static_cast<std::size_t>(shot.frame.width) * 4u);
            MIRAGE_CHECK(shot.frame.pixels.size() ==
                         shot.frame.stride * static_cast<std::size_t>(shot.frame.height));
        }
    }

    // ROI over the painted TOPMOST fixture window: frame invariants plus
    // content variation (the two-tone fill must be visible — per-byte
    // colors are not asserted so compositor jitter cannot flake the test).
    RECT bounds{};
    MIRAGE_CHECK(GetWindowRect(main_window, &bounds) != 0);
    const int roi_width = bounds.right - bounds.left;
    const int roi_height = bounds.bottom - bounds.top;
    MIRAGE_CHECK(roi_width > 0 && roi_height > 0);
    CaptureLimits roi_budget;
    roi_budget.max_bytes =
        static_cast<std::size_t>(roi_width) * static_cast<std::size_t>(roi_height) * 4u;
    const auto roi = screen.capture_roi(
        mirage::desktop::WindowGeometry{bounds.left, bounds.top, roi_width, roi_height}, roi_budget,
        CancelToken{});
    MIRAGE_CHECK(roi.ok);
    if (roi.ok) {
        MIRAGE_CHECK(roi.frame.format == mirage::desktop::ImageFormat::Bgra8);
        MIRAGE_CHECK(roi.frame.width == roi_width);
        MIRAGE_CHECK(roi.frame.height == roi_height);
        MIRAGE_CHECK(roi.frame.stride == static_cast<std::size_t>(roi_width) * 4u);
        MIRAGE_CHECK(roi.frame.pixels.size() ==
                     roi.frame.stride * static_cast<std::size_t>(roi_height));
        bool all_bytes_identical = true;
        for (const std::uint8_t byte : roi.frame.pixels) {
            if (byte != roi.frame.pixels.front()) {
                all_bytes_identical = false;
                break;
            }
        }
        MIRAGE_CHECK(!all_bytes_identical);
    }

    // Window capture of the visible fixture matches the window bounds.
    const std::string self_id = window_id(main_window);
    const auto window_shot = screen.capture_window(self_id, roi_budget, CancelToken{});
    MIRAGE_CHECK(window_shot.ok);
    if (window_shot.ok) {
        MIRAGE_CHECK(window_shot.frame.width == roi_width);
        MIRAGE_CHECK(window_shot.frame.height == roi_height);
    }

    // Root-frame model: the never-shown window has nothing on screen, so
    // capture fails closed with not_found.
    const std::string hidden_id = window_id(hidden_window);
    const auto hidden_shot = screen.capture_window(hidden_id, CaptureLimits{}, CancelToken{});
    MIRAGE_CHECK(!hidden_shot.ok);
    MIRAGE_CHECK(hidden_shot.error.code == "not_found");

    // Fabricated ids: non-numeric garbage is malformed, well-formed decimal
    // that resolves to no window is not_found.
    MIRAGE_CHECK(screen.capture_window("12abc", CaptureLimits{}, CancelToken{}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(screen.capture_window("4294967295", CaptureLimits{}, CancelToken{}).error.code ==
                 "not_found");

    CancelToken window_cancel;
    window_cancel.request_cancel();
    MIRAGE_CHECK(screen.capture_window(self_id, CaptureLimits{}, window_cancel).cancelled);
}

/// Contract rejections happen before any injection, so these checks have
/// zero side effects on the session's keyboard, pointer or focus.
void input_refusals_before_effects(WindowsDesktopEnvironment &env) {
    mirage::desktop::InputProvider &input = *env.input();

    // A non-positive timeout is invalid for every injection entry point.
    InputLimits dead;
    dead.timeout = std::chrono::milliseconds{0};
    MIRAGE_CHECK(input.inject_key(KeySym{"enter"}, true, dead, CancelToken{}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(input.type_text("x", dead, CancelToken{}).error.code == "invalid_argument");
    MIRAGE_CHECK(input.pointer_move(1, 1, dead, CancelToken{}).error.code == "invalid_argument");
    MIRAGE_CHECK(input.pointer_button("left", true, dead, CancelToken{}).error.code ==
                 "invalid_argument");

    // Unknown key names are rejected before injection: never-reserved name,
    // out-of-vocabulary function key, and a chord with a repeated modifier.
    MIRAGE_CHECK(
        input.inject_key(KeySym{"no_such_key"}, true, InputLimits{}, CancelToken{}).error.code ==
        "invalid_argument");
    MIRAGE_CHECK(input.inject_key(KeySym{"f13"}, true, InputLimits{}, CancelToken{}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(
        input.inject_key(KeySym{"ctrl+alt+meta+ctrl+x"}, true, InputLimits{}, CancelToken{})
            .error.code == "invalid_argument");

    // type_text rejects over-budget and non-UTF-8 payloads before injection.
    InputLimits tiny_text;
    tiny_text.max_text_bytes = 2;
    MIRAGE_CHECK(input.type_text("abc", tiny_text, CancelToken{}).error.code == "invalid_argument");
    MIRAGE_CHECK(input.type_text("\xff\xfe", InputLimits{}, CancelToken{}).error.code ==
                 "invalid_argument");

    // Unknown mouse button.
    MIRAGE_CHECK(input.pointer_button("up", true, InputLimits{}, CancelToken{}).error.code ==
                 "invalid_argument");

    // Entry pre-check cancellation on every injection and the read path.
    CancelToken cancel;
    cancel.request_cancel();
    MIRAGE_CHECK(input.inject_key(KeySym{"a"}, true, InputLimits{}, cancel).cancelled);
    MIRAGE_CHECK(input.type_text("x", InputLimits{}, cancel).cancelled);
    MIRAGE_CHECK(input.pointer_move(5, 5, InputLimits{}, cancel).cancelled);
    MIRAGE_CHECK(input.pointer_button("left", true, InputLimits{}, cancel).cancelled);
    MIRAGE_CHECK(input.pointer_position(cancel).cancelled);
}

/// Result-state correspondence for the pointer: a successful move is read
/// back through the independent GetCursorPos path (the virtual-desktop
/// quantization may shift the applied point by at most one pixel); an
/// io_error refusal (session 0) is an honest outcome — either way the
/// reported result must agree with the observed state.
void pointer_move_position_consistency(WindowsDesktopEnvironment &env) {
    mirage::desktop::ScreenProvider &screen = *env.screen();
    mirage::desktop::InputProvider &input = *env.input();

    const auto displays = screen.list_displays(DisplayListLimits{}, CancelToken{});
    MIRAGE_CHECK(displays.ok);
    if (!displays.ok || displays.displays.empty()) {
        return;
    }
    const mirage::desktop::DisplayInfo *primary = &displays.displays[0];
    for (const mirage::desktop::DisplayInfo &display : displays.displays) {
        if (display.primary) {
            primary = &display;
        }
    }
    const int center_x = primary->geometry.x + primary->geometry.width / 2;
    const int center_y = primary->geometry.y + primary->geometry.height / 2;

    const auto moved = input.pointer_move(center_x, center_y, InputLimits{}, CancelToken{});
    if (moved.ok) {
        const auto position = input.pointer_position(CancelToken{});
        MIRAGE_CHECK(position.ok);
        if (position.ok) {
            MIRAGE_CHECK(position.position.x >= center_x - 1);
            MIRAGE_CHECK(position.position.x <= center_x + 1);
            MIRAGE_CHECK(position.position.y >= center_y - 1);
            MIRAGE_CHECK(position.position.y <= center_y + 1);
        }
    } else {
        // SendInput can be refused in sessions without an interactive
        // desktop (services); the refusal is honest and the read path must
        // still answer consistently (either the real position or an
        // io_error of its own, never a fabricated position).
        MIRAGE_CHECK(moved.error.code == "io_error");
        std::fprintf(stderr,
                     "note: this session refuses SendInput (%s); pointer delivery is "
                     "environment-limited\n",
                     moved.error.message.c_str());
        const auto position = input.pointer_position(CancelToken{});
        MIRAGE_CHECK(position.ok || position.error.code == "io_error");
    }
}

/// Real keystroke delivery, only provable when the fixture window owns the
/// foreground (SendInput routes to the focus owner; a session that refused
/// activation cannot observe delivery, so the scenario prints a note and
/// the rejection paths already covered above stay the contract). The
/// injection itself returns once the events are queued; the main-thread
/// pump below delivers them to the window procedure, where WM_CHAR
/// (KEYEVENTF_UNICODE payload) and WM_KEYDOWN (VK_RETURN) are recorded.
void keyboard_delivery_when_focused(WindowsDesktopEnvironment &env, HWND main_window) {
    mirage::desktop::InputProvider &input = *env.input();

    const std::string self_id = window_id(main_window);
    const auto activated = env.window()->activate(self_id, CancelToken{});
    if (!activated.ok || GetForegroundWindow() != main_window) {
        std::fprintf(stderr,
                     "note: the fixture window does not own the foreground (activation %s); "
                     "keystroke delivery cannot be observed here and is skipped\n",
                     activated.ok ? "succeeded but focus moved" : "was refused");
        return;
    }

    HANDLE delivered = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    MIRAGE_CHECK(delivered != nullptr);
    if (delivered == nullptr) {
        return;
    }
    g_key_event = delivered;

    // Drain any pending input first so the scenario only sees fresh strokes.
    run_pump(50);
    g_last_char = 0;
    g_last_keydown = 0;
    ResetEvent(delivered);

    const auto typed = input.type_text("a", InputLimits{}, CancelToken{});
    MIRAGE_CHECK(typed.ok);
    if (typed.ok) {
        // KEYEVENTF_UNICODE events arrive as WM_KEYDOWN(VK_PACKET) followed
        // by WM_CHAR with the injected code unit; the event fires on WM_CHAR.
        const bool saw_char = pump_until_event(delivered, 5000);
        MIRAGE_CHECK(saw_char);
        MIRAGE_CHECK(g_last_char == 'a');
    }

    // Named-key path: the fixed VK table resolves "enter" to VK_RETURN.
    g_last_char = 0;
    g_last_keydown = 0;
    ResetEvent(delivered);
    const auto pressed = input.inject_key(KeySym{"enter"}, true, InputLimits{}, CancelToken{});
    MIRAGE_CHECK(pressed.ok);
    if (pressed.ok) {
        const bool saw_key = pump_until_event(delivered, 5000);
        MIRAGE_CHECK(saw_key);
        MIRAGE_CHECK(g_last_keydown == VK_RETURN);
    }
    MIRAGE_CHECK(input.inject_key(KeySym{"enter"}, false, InputLimits{}, CancelToken{}).ok);
    run_pump(150); // absorb the keyup before the fixture goes away

    g_key_event = nullptr;
    CloseHandle(delivered);
}

} // namespace

int main() {
    TestGui gui;
    if (!gui.ok()) {
        std::fprintf(stderr, "fixture windows could not be created; interactive desktop "
                             "required for win32_backend_test\n");
        return 1;
    }

    environment_probe_and_identity();

    WindowsDesktopEnvironment env(mirage::platform::windows_backend::Win32Options{true});
    if (env.window() == nullptr || env.screen() == nullptr || env.input() == nullptr) {
        // Probe failure: already recorded by environment_probe_and_identity.
        // Loud exit with the recorded failures — never a silent pass — and
        // no dereference of the null accessors.
        std::fprintf(stderr,
                     "interactive-desktop probe failed (SM_CXVIRTUALSCREEN=%d "
                     "SM_CYVIRTUALSCREEN=%d); run evidence requires an interactive "
                     "session (CI windows job or maintainer machine)\n",
                     GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
        return mirage::testing::finish("win32_backend_test");
    }

    window_enumeration_and_budget(env, gui.main_window(), gui.hidden_window());
    activation_and_focus_consistency(env, gui.main_window());
    displays_and_capture(env, gui.main_window(), gui.hidden_window());
    input_refusals_before_effects(env);
    pointer_move_position_consistency(env);
    keyboard_delivery_when_focused(env, gui.main_window());

    return mirage::testing::finish("win32_backend_test");
}
