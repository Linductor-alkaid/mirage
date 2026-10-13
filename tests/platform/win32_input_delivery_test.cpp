// M7-10 Windows input delivery evidence against the session's real
// interactive desktop (DEC-017 decision 9): the Unicode text transport
// (KEYEVENTF_UNICODE, DEC-017 decision 5), pointer landing on every
// attached display, and real button delivery to the fixture window — in an
// AWARE process. Unlike win32_backend_test (whose provider regressions are
// self-consistent in whatever mode the process runs), this test declares
// the runtime host's per-monitor DPI awareness through the same public
// entry the product hosts use (M7-10, DEC-086), so its geometry
// assertions are witnesses for the physical-pixel coordinate space that
// enumeration, capture and injected input must share on mixed-DPI
// desktops.
//
// Delivery can only be observed when the session actually injects: the
// keyboard scenarios gate on the fixture owning the foreground, and the
// pointer scenarios on SendInput being accepted; a session that refuses
// prints a loud note and only the independent read paths are asserted.
// Contract rejections (bad UTF-8, budgets, cancellation) stay with
// win32_backend_test; this binary pins delivery, not refusal.

#include "../support/test.hpp"

#include <mirage/platform/host_environment.hpp>
#include <mirage/platform/windows/windows_desktop_environment.hpp>

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
#include <string>
#include <vector>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::DisplayListLimits;
using mirage::desktop::InputLimits;
using mirage::desktop::WindowGeometry;
using mirage::platform::windows_backend::WindowsDesktopEnvironment;

constexpr wchar_t kWindowClassName[] = L"MirageWin32InputDeliveryTestClass";

// File-scope state the window procedure records; the test is single-threaded
// (main-thread pump only, RULE-03), so plain globals need no synchronization.
std::vector<int> g_char_stream; // every WM_CHAR unit, in arrival order
int g_last_keydown = 0;         // last WM_KEYDOWN virtual key
int g_click_count = 0;          // WM_LBUTTONDOWN hits on the fixture
int g_click_client_x = -1;      // client-space click point
int g_click_client_y = -1;

LRESULT CALLBACK test_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CHAR:
        g_char_stream.push_back(static_cast<int>(wparam));
        return 0;
    case WM_KEYDOWN:
        g_last_keydown = static_cast<int>(wparam);
        return 0;
    case WM_LBUTTONDOWN:
        ++g_click_count;
        g_click_client_x = static_cast<short>(LOWORD(lparam));
        g_click_client_y = static_cast<short>(HIWORD(lparam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

/// The product runtime hosts declare their awareness through the public
/// platform entry (M7-10); this test does the same so its geometry
/// assertions run under exactly the mode the product runs under. False only
/// when the process awareness is fixed to a non-physical mode — the caller
/// turns that into a loud failure.
bool declare_dpi_awareness() {
    const auto mode = mirage::platform::declare_host_dpi_awareness();
    std::fprintf(stderr, "[host] dpi awareness mode %d\n", static_cast<int>(mode));
    return mode == mirage::platform::DpiAwareness::per_monitor_v2 ||
           mode == mirage::platform::DpiAwareness::prior_per_monitor ||
           mode == mirage::platform::DpiAwareness::system;
}

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

/// Drains the queue until the recorded char stream reaches `count` units or
/// the deadline passes. KEYEVENTF_UNICODE events reach the window procedure
/// only while this pump runs.
bool pump_until_chars(std::size_t count, DWORD timeout_milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + timeout_milliseconds;
    MSG message{};
    for (;;) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (g_char_stream.size() >= count) {
            return true;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
    }
}

/// The TOPMOST fixture window the scenarios type into and click on.
class FixtureWindow {
  public:
    FixtureWindow() {
        WNDCLASSEXW window_class{};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &test_wndproc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.lpszClassName = kWindowClassName;
        MIRAGE_CHECK(RegisterClassExW(&window_class) != 0);
        window_ =
            CreateWindowExW(WS_EX_TOPMOST, kWindowClassName, L"mirage-win32-input-delivery-test",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 320, 240, nullptr, nullptr,
                            GetModuleHandleW(nullptr), nullptr);
        MIRAGE_CHECK(window_ != nullptr);
        if (window_ != nullptr) {
            SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        run_pump(300);
    }

    ~FixtureWindow() {
        if (window_ != nullptr) {
            DestroyWindow(window_);
        }
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }

    FixtureWindow(const FixtureWindow &) = delete;
    FixtureWindow &operator=(const FixtureWindow &) = delete;

    HWND handle() const { return window_; }

    WindowGeometry rect() const {
        RECT bounds{};
        MIRAGE_CHECK(GetWindowRect(window_, &bounds) != 0);
        return WindowGeometry{bounds.left, bounds.top, bounds.right - bounds.left,
                              bounds.bottom - bounds.top};
    }

  private:
    HWND window_ = nullptr;
};

std::string window_id(HWND window) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(window));
}

/// The OS foreground lock refuses SetForegroundWindow to a process the
/// session did not hand focus (a shell-started test). Attaching to the
/// foreground thread before the raise — the same exemption automation
/// shells use — restores the fixture's eligibility. Touches only this
/// test's own window; against an ELEVATED foreground window the attach
/// fails (UIPI) and the caller skips loudly, which is that boundary
/// surfacing exactly as the product would see it.
bool force_own_foreground(HWND main_window) {
    const HWND foreground = GetForegroundWindow();
    const DWORD foreground_thread =
        foreground == nullptr ? 0 : GetWindowThreadProcessId(foreground, nullptr);
    const DWORD this_thread = GetCurrentThreadId();
    const bool attached = foreground_thread != 0 && foreground_thread != this_thread &&
                          AttachThreadInput(this_thread, foreground_thread, TRUE) != 0;
    BringWindowToTop(main_window);
    const BOOL raised = SetForegroundWindow(main_window);
    SetFocus(main_window);
    if (attached) {
        AttachThreadInput(this_thread, foreground_thread, FALSE);
    }
    run_pump(120);
    const bool owned = (raised != 0 || attached) && GetForegroundWindow() == main_window;
    if (!owned) {
        std::fprintf(stderr, "[focus] forced raise failed (raised=%ld attached=%d)\n",
                     static_cast<long>(raised), attached ? 1 : 0);
    }
    return owned;
}

/// Unicode transport witness: the typed UTF-8 text must arrive at the
/// focused fixture as the exact UTF-16 unit sequence — CJK text (multi-unit
/// in UTF-8), BMP punctuation and an emoji (surrogate pair injecting as two
/// WM_CHAR units the system combines visually). Two rounds also exercise
/// per-call independence of the injection path.
void unicode_delivery(WindowsDesktopEnvironment &env, HWND main_window) {
    mirage::desktop::InputProvider &input = *env.input();

    const std::string self_id = window_id(main_window);
    const auto activated = env.window()->activate(self_id, CancelToken{});
    bool focused = activated.ok && GetForegroundWindow() == main_window;
    if (!focused) {
        // Provider activation can be refused by the foreground lock even
        // though delivery itself would work; the fixture's own raise is the
        // test-side fallback for observing the transport.
        focused = force_own_foreground(main_window);
    }
    if (!focused) {
        std::fprintf(stderr,
                     "note: the fixture window does not own the foreground (activation %s); "
                     "unicode delivery cannot be observed here and is skipped\n",
                     activated.ok ? "succeeded but focus moved" : "was refused");
        return;
    }

    struct Round {
        const char *utf8;
        std::vector<int> expected_units;
    };
    // "a中🎮文" and "中文abc🎮！": ASCII, CJK, fullwidth punctuation and a
    // surrogate pair, interleaved so order within one batch matters.
    const Round rounds[] = {
        {"a\xE4\xB8\xAD\xF0\x9F\x8E\xAE\xE6\x96\x87", {'a', 0x4E2D, 0xD83C, 0xDFAE, 0x6587}},
        {"\xE4\xB8\xAD\xE6\x96\x87"
         "abc\xF0\x9F\x8E\xAE\xEF\xBC\x81",
         {0x4E2D, 0x6587, 'a', 'b', 'c', 0xD83C, 0xDFAE, 0xFF01}},
    };

    for (const Round &round : rounds) {
        run_pump(50);
        g_char_stream.clear();
        const auto typed = input.type_text(round.utf8, InputLimits{}, CancelToken{});
        MIRAGE_CHECK(typed.ok);
        if (!typed.ok) {
            std::fprintf(stderr, "[unicode] type_text refused: %s / %s\n", typed.error.code.c_str(),
                         typed.error.message.c_str());
            continue;
        }
        const bool complete = pump_until_chars(round.expected_units.size(), 5000);
        MIRAGE_CHECK(complete);
        MIRAGE_CHECK(g_char_stream == round.expected_units);
        if (g_char_stream != round.expected_units) {
            std::fprintf(stderr,
                         "[unicode] received %zu units, expected %zu:", g_char_stream.size(),
                         round.expected_units.size());
            for (const int unit : g_char_stream) {
                std::fprintf(stderr, " %04X", unit);
            }
            std::fprintf(stderr, "\n");
        }
    }
    run_pump(100);
}

/// Pointer landing on EVERY attached display: the provider's move must land
/// the cursor at the requested physical point on each monitor, read back
/// through the independent pointer_position path. This is the mixed-DPI
/// witness for the injected-input coordinate space (a virtualized host
/// would systematically miss on scaled outputs). The cursor returns to the
/// primary center afterwards — the test moves the user's real cursor only
/// within the established input-test discipline.
void pointer_landing_all_displays(WindowsDesktopEnvironment &env) {
    mirage::desktop::ScreenProvider &screen = *env.screen();
    mirage::desktop::InputProvider &input = *env.input();

    const auto displays = screen.list_displays(DisplayListLimits{}, CancelToken{});
    MIRAGE_CHECK(displays.ok);
    if (!displays.ok || displays.displays.empty()) {
        return;
    }
    int restored_x = 0;
    int restored_y = 0;
    bool have_restore = false;
    for (const auto &display : displays.displays) {
        const WindowGeometry &geometry = display.geometry;
        const int center_x = geometry.x + geometry.width / 2;
        const int center_y = geometry.y + geometry.height / 2;
        if (display.primary) {
            restored_x = center_x;
            restored_y = center_y;
            have_restore = true;
        }
        const auto moved = input.pointer_move(center_x, center_y, InputLimits{}, CancelToken{});
        if (!moved.ok) {
            // SendInput can be refused in sessions without an interactive
            // desktop; the refusal is honest and lands the same way as in
            // win32_backend_test — a loud note, not a fake pass.
            MIRAGE_CHECK(moved.error.code == "io_error");
            std::fprintf(stderr,
                         "note: this session refuses SendInput (%s); pointer landing is "
                         "environment-limited\n",
                         moved.error.message.c_str());
            return;
        }
        const auto position = input.pointer_position(CancelToken{});
        MIRAGE_CHECK(position.ok);
        if (position.ok) {
            std::fprintf(stderr, "[display %s] target (%d,%d) landed (%d,%d)\n", display.id.c_str(),
                         center_x, center_y, position.position.x, position.position.y);
            MIRAGE_CHECK(position.position.x >= center_x - 2 &&
                         position.position.x <= center_x + 2);
            MIRAGE_CHECK(position.position.y >= center_y - 2 &&
                         position.position.y <= center_y + 2);
        }
    }
    if (have_restore) {
        MIRAGE_CHECK(input.pointer_move(restored_x, restored_y, InputLimits{}, CancelToken{}).ok);
    }
}

/// Button delivery witness: a left click on the fixture's center must
/// arrive as WM_LBUTTONDOWN at the matching client point — the end-to-end
/// coordinate chain (provider normalization -> system injection -> window
/// hit-testing) on the aware process.
void pointer_click_delivery(WindowsDesktopEnvironment &env, HWND main_window) {
    mirage::desktop::InputProvider &input = *env.input();

    const WindowGeometry window_rect = [&] {
        RECT bounds{};
        MIRAGE_CHECK(GetWindowRect(main_window, &bounds) != 0);
        return WindowGeometry{bounds.left, bounds.top, bounds.right - bounds.left,
                              bounds.bottom - bounds.top};
    }();
    POINT center{window_rect.x + window_rect.width / 2, window_rect.y + window_rect.height / 2};
    POINT expected_client = center;
    MIRAGE_CHECK(ScreenToClient(main_window, &expected_client) != 0);

    run_pump(50);
    g_click_count = 0;
    const auto moved = input.pointer_move(center.x, center.y, InputLimits{}, CancelToken{});
    if (!moved.ok) {
        MIRAGE_CHECK(moved.error.code == "io_error");
        return; // the landing scenario already reported the environment note
    }
    const auto pressed = input.pointer_button("left", true, InputLimits{}, CancelToken{});
    MIRAGE_CHECK(pressed.ok);
    if (pressed.ok) {
        const auto released = input.pointer_button("left", false, InputLimits{}, CancelToken{});
        MIRAGE_CHECK(released.ok);
    }
    run_pump(150); // let the click reach the window procedure

    MIRAGE_CHECK(g_click_count == 1);
    if (g_click_count == 1) {
        std::fprintf(stderr, "[click] client (%d,%d), expected (%d,%d)\n", g_click_client_x,
                     g_click_client_y, expected_client.x, expected_client.y);
        MIRAGE_CHECK(g_click_client_x >= expected_client.x - 2 &&
                     g_click_client_x <= expected_client.x + 2);
        MIRAGE_CHECK(g_click_client_y >= expected_client.y - 2 &&
                     g_click_client_y <= expected_client.y + 2);
    }
}

/// Release discipline: after every scenario the injected stream must leave
/// no modifier stuck down (the M7 contract's cleanup guarantee, observed
/// through the async keyboard state).
void check_no_stuck_modifiers() {
    const UINT modifiers[] = {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN};
    for (const UINT modifier : modifiers) {
        const auto state = static_cast<unsigned short>(GetAsyncKeyState(modifier));
        MIRAGE_CHECK((state & 0x8000u) == 0);
    }
}

} // namespace

int main() {
    const bool aware = declare_dpi_awareness();
    MIRAGE_CHECK(aware);
    if (!aware) {
        return mirage::testing::finish("win32_input_delivery_test");
    }

    FixtureWindow fixture;
    if (fixture.handle() == nullptr) {
        return mirage::testing::finish("win32_input_delivery_test");
    }

    WindowsDesktopEnvironment env(mirage::platform::windows_backend::Win32Options{true});
    if (env.window() == nullptr || env.screen() == nullptr || env.input() == nullptr) {
        std::fprintf(stderr, "interactive-desktop probe failed; input delivery requires an "
                             "interactive session\n");
        return mirage::testing::finish("win32_input_delivery_test");
    }

    unicode_delivery(env, fixture.handle());
    pointer_landing_all_displays(env);
    pointer_click_delivery(env, fixture.handle());
    check_no_stuck_modifiers();

    return mirage::testing::finish("win32_input_delivery_test");
}
