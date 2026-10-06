#include "tray_backend.hpp"

#include "win32_util.hpp"

// Shell_NotifyIconW / NOTIFYICONDATAW: WIN32_LEAN_AND_MEAN (win32_util.hpp)
// excludes shellapi.h, so the tray carrier includes it explicitly (the
// notification frontend's precedent, M4-05).
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <utility>

namespace mirage::platform::windows_backend {
namespace {

using mirage::desktop::TrayAction;
using mirage::desktop::TrayCarrierContext;
using mirage::desktop::TrayState;

using win32_util::last_error_suffix;

/// Bounded wait slice of one pump iteration (DEC-030): stop, wakeup and
/// state latency never exceed this.
constexpr int kPollSliceMs = 200;

/// The callback message the icon's notifications arrive with (uCallbackMessage).
constexpr UINT kIconCallbackMessage = WM_APP + 1;

/// The tray state changed; the pump re-reads it and refreshes the tooltip.
constexpr UINT kWakeupMessage = WM_APP + 2;

/// Menu command ids of the context menu (TrackPopupMenuEx returns the id).
enum : int {
    kMenuOpenShell = 3,
    kMenuQuit = 9,
};

/// The window class shared by the (single) callback window; registered once
/// per process.
constexpr wchar_t kClassName[] = L"MirageTrayWindow";

/// Per-run presentation state; everything except the published callback
/// window handle lives on the pump thread.
struct TrayPumpState {
    HWND window = nullptr;
    NOTIFYICONDATAW icon{}; ///< Shell_NotifyIcon state of the resident icon
    bool icon_added = false;
    bool icon_owned = false;
    TrayCarrierContext context;
    TrayState state;     ///< newest loaded presentation state
    std::wstring status; ///< UTF-16 of state.status (tooltip budget clamped)
};

/// Shell_NotifyIcon tooltip budget: szTip is 128 wchar_t including the
/// terminator (shellapi.h). Clamped in UTF-8 first, then UTF-16; the copy
/// is a bounded element loop — std::wcsncpy is C4996-deprecated on MSVC
/// (verification round 3 CI) and the SEC-API _s forms are not part of the
/// DEC-017 dual-toolchain baseline.
void set_tooltip(NOTIFYICONDATAW &icon, const std::wstring &status) {
    constexpr std::size_t kTipBudget = sizeof(icon.szTip) / sizeof(icon.szTip[0]);
    const std::size_t take = std::min(status.size(), kTipBudget - 1);
    std::copy(status.begin(), status.begin() + static_cast<std::ptrdiff_t>(take), icon.szTip);
    icon.szTip[take] = L'\0';
}

LRESULT CALLBACK tray_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *state = reinterpret_cast<TrayPumpState *>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    if (state == nullptr) {
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
    if (message == kIconCallbackMessage) {
        // A click on the resident icon: show the context menu at the cursor
        // (the TrackPopupMenu focus discipline: SetForegroundWindow first,
        // or the menu never closes — the classic tray-menu requirement).
        if (LOWORD(lparam) == WM_RBUTTONUP || LOWORD(lparam) == WM_LBUTTONUP) {
            HMENU menu = ::CreatePopupMenu();
            if (menu == nullptr) {
                return 0;
            }
            const auto append = [&](UINT flags, UINT_PTR id, const wchar_t *text) {
                ::AppendMenuW(menu, flags, id, text);
            };
            append(MF_STRING | (state->state.can_open_shell ? MF_ENABLED : MF_GRAYED),
                   kMenuOpenShell, L"打开应用");
            append(MF_STRING, kMenuQuit, L"退出应用");
            ::SetForegroundWindow(window);
            POINT cursor{};
            ::GetCursorPos(&cursor);
            // TrackPopupMenuEx returns BOOL (= int on both SDKs); with
            // TPM_RETURNCMD the value is the chosen command id.
            const int command =
                ::TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, cursor.x,
                                   cursor.y, window, nullptr);
            ::DestroyMenu(menu);
            ::PostMessageW(window, WM_NULL, 0, 0); // settle the foreground switch
            if (command == kMenuOpenShell || command == kMenuQuit) {
                const TrayAction action =
                    command == kMenuOpenShell ? TrayAction::OpenShell : TrayAction::Quit;
                if (state->context.on_action) {
                    state->context.on_action(action);
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
        description.lpfnWndProc = tray_window_proc;
        description.hInstance = ::GetModuleHandleW(nullptr);
        description.lpszClassName = kClassName;
        return ::RegisterClassExW(&description) != 0;
    }();
    return registered;
}

/// Refreshes the tooltip from the newest state. Cheap enough for every
/// wakeup: Shell_NotifyIcon(NIM_MODIFY) is a synchronous shell call.
void refresh_tooltip(TrayPumpState &state) {
    if (!state.icon_added) {
        return;
    }
    set_tooltip(state.icon, state.status);
    if (::Shell_NotifyIconW(NIM_MODIFY, &state.icon) == 0) {
        // The shell can transiently refuse (Explorer restart): the next
        // state change retries; a persistently gone area is the owner's
        // session reality, not a carrier bug (DEC-018 delivery semantics).
    }
}

} // namespace

Win32TrayCarrier::Win32TrayCarrier() = default;

Win32TrayCarrier::~Win32TrayCarrier() = default;

std::unique_ptr<Win32TrayCarrier> Win32TrayCarrier::open() {
    if (::GetSystemMetrics(SM_CXVIRTUALSCREEN) <= 0 ||
        ::GetSystemMetrics(SM_CYVIRTUALSCREEN) <= 0) {
        return nullptr;
    }
    return std::unique_ptr<Win32TrayCarrier>(new Win32TrayCarrier());
}

mirage::desktop::TrayCarrier::RunReport
Win32TrayCarrier::run(const TrayCarrierContext &context,
                      const std::function<bool()> &stop_requested) {
    RunReport report;
    if (!register_window_class()) {
        report.diagnostic = "tray window class registration failed" + last_error_suffix();
        return report;
    }
    TrayPumpState pump;
    pump.context = context;
    pump.window = ::CreateWindowExW(0, kClassName, L"MirageTrayWindow", 0, 0, 0, 0, 0, nullptr,
                                    nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (pump.window == nullptr) {
        report.diagnostic = "tray callback window creation failed" + last_error_suffix();
        return report;
    }
    ::SetWindowLongPtrW(pump.window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&pump));

    pump.icon.cbSize = sizeof(pump.icon);
    pump.icon.hWnd = pump.window;
    pump.icon.uID = 1;
    pump.icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    pump.icon.uCallbackMessage = kIconCallbackMessage;
    // MAKEINTRESOURCEW explicitly: without UNICODE the IDI_APPLICATION
    // macro maps to the ANSI resource cast (DEC-017 macro-neutral W discipline).
    if (!context.icon_path.empty()) {
        const auto path = win32_util::utf8_to_utf16(context.icon_path);
        if (path) {
            pump.icon.hIcon = static_cast<HICON>(::LoadImageW(nullptr, path->c_str(), IMAGE_ICON, 0,
                                                              0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
            pump.icon_owned = pump.icon.hIcon != nullptr;
        }
    }
    if (!pump.icon.hIcon)
        pump.icon.hIcon = ::LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    if (pump.icon.hIcon == nullptr) {
        report.diagnostic = "tray icon load failed" + last_error_suffix();
        ::DestroyWindow(pump.window);
        return report;
    }
    if (pump.context.load_state) {
        pump.state = pump.context.load_state();
    }
    pump.status = win32_util::utf8_to_utf16(pump.state.status).value_or(std::wstring{});
    set_tooltip(pump.icon, pump.status);
    if (::Shell_NotifyIconW(NIM_ADD, &pump.icon) == 0) {
        // No shell notification area (or Explorer mid-restart): the tray
        // surface honestly does not exist in this session (DEC-018's
        // capability-honesty posture).
        report.diagnostic = "notification area refused the tray icon" + last_error_suffix();
        if (pump.icon_owned)
            ::DestroyIcon(pump.icon.hIcon);
        ::DestroyWindow(pump.window);
        return report;
    }
    pump.icon_added = true;
    if (context.on_ready)
        context.on_ready();
    callback_window_.store(pump.window, std::memory_order_release);

    report.clean = true;
    MSG message;
    bool quit_posted = false;
    while (!stop_requested() && !quit_posted) {
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                quit_posted = true;
                break;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (stop_requested() || quit_posted) {
            break;
        }
        if (pump.context.load_state) {
            const TrayState newest = pump.context.load_state();
            if (newest.status != pump.state.status || newest.can_pause != pump.state.can_pause ||
                newest.can_resume != pump.state.can_resume ||
                newest.can_open_shell != pump.state.can_open_shell) {
                pump.state = newest;
                pump.status = win32_util::utf8_to_utf16(pump.state.status).value_or(std::wstring{});
                refresh_tooltip(pump);
            }
        }
        (void)::MsgWaitForMultipleObjectsEx(0, nullptr, kPollSliceMs, QS_ALLINPUT,
                                            MWMO_INPUTAVAILABLE);
    }

    // Teardown on the pump thread (thread affinity, DEC-018): remove the
    // icon before the callback window dies.
    callback_window_.store(nullptr, std::memory_order_release);
    if (pump.icon_added) {
        ::Shell_NotifyIconW(NIM_DELETE, &pump.icon);
    }
    if (pump.icon_owned)
        ::DestroyIcon(pump.icon.hIcon);
    ::DestroyWindow(pump.window);
    return report;
}

void Win32TrayCarrier::wakeup() {
    const HWND target = static_cast<HWND>(callback_window_.load(std::memory_order_acquire));
    if (target != nullptr) {
        ::PostMessageW(target, kWakeupMessage, 0, 0);
    }
}

std::unique_ptr<mirage::desktop::TrayCarrier> open_tray_carrier() {
    return Win32TrayCarrier::open();
}

} // namespace mirage::platform::windows_backend
