#include "notification_backend.hpp"

#include "win32_util.hpp"

// The Win32 surface lives in win32_util.hpp (macro-neutral include,
// RULE-01, DEC-017 decision 5); the shell notification API is declared in
// shellapi.h and exported by the already-linked shell32 (DEC-018 decision
// 7 — no new import library).
#include <shellapi.h>

#include <cstdio>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

#include <mirage/desktop/input_provider.hpp> // is_valid_utf8 (shared contract helper)

namespace mirage::platform::windows_backend {
namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::NotificationLimits;
using mirage::desktop::NotificationOutcome;
using win32_util::error;
using win32_util::last_error_suffix;
using win32_util::utf8_to_utf16;

/// The balloon payload arrays are the platform's hard caps (DEC-018
/// decision 3): szInfoTitle[64] and szInfo[256] are NUL-terminated UTF-16
/// arrays, so 63 / 255 code units are the most one notification can
/// faithfully carry. An over-cap payload is refused before any side
/// effect — the same payload that lands on Linux refuses here through a
/// stable error code, and nothing is silently truncated on either side.
constexpr std::size_t kMaxTitleUnits = 63;
constexpr std::size_t kMaxBodyUnits = 255;

/// The carrier window's tray callback message. Nothing pumps the carrier
/// window (DEC-018 decision 3): the balloon feedback messages
/// (NIN_BALLOONSHOW / NIN_BALLOONTIMEOUT / ...) pile up in the creating
/// thread's queue — inert for a provider that only ever calls
/// Shell_NotifyIcon directly, and structurally resolved (STA pump on an
/// Executor blocking worker) only if M5 interaction work triggers it.
constexpr UINT kCallbackMessage = WM_APP + 1;

const wchar_t *const kCarrierClassName = L"MirageNotificationCarrier";

/// The carrier window needs an address to receive the tray callback
/// messages, but consumes none: a plain hidden top-level window whose
/// procedure only forwards to the default (the tray callbacks keep
/// queueing, see kCallbackMessage above). No taskbar button (never
/// visible), no menu — the bare carrier of DEC-018 decision 3.
LRESULT CALLBACK carrier_window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    return ::DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

struct NotificationBackend::Impl {
    /// The carrier window (created in open(), destroyed in the destructor)
    /// and the tray icon registration as it was added — the destructor's
    /// NIM_DELETE must address the exact (hWnd, uID) pair.
    HWND window = nullptr;
    NOTIFYICONDATAW icon_data{};
};

NotificationBackend::~NotificationBackend() {
    if (impl_ == nullptr) {
        return;
    }
    // The icon goes first: it must not outlive the carrier window, and
    // Shell_NotifyIcon works from any thread of the session.
    ::Shell_NotifyIconW(NIM_DELETE, &impl_->icon_data);
    // DestroyWindow is thread-affine — only the creating thread can call
    // it. The environment is normally constructed and destroyed by the
    // same owner thread; when a teardown lands on a different thread the
    // window is destroyed when its creating thread exits, and that skip is
    // loud rather than silent (the icon is already gone).
    if (::DestroyWindow(impl_->window) == 0) {
        std::fprintf(stderr, "[win32-notify] carrier window teardown deferred%s\n",
                     last_error_suffix().c_str());
        std::fflush(stderr);
    }
}

std::unique_ptr<NotificationBackend> NotificationBackend::open() {
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = carrier_window_procedure;
    window_class.hInstance = ::GetModuleHandleW(nullptr);
    window_class.lpszClassName = kCarrierClassName;
    // The class name is per-process: a second environment in the same
    // process reuses the registration, so ERROR_CLASS_ALREADY_EXISTS is
    // the expected path, not an error.
    if (::RegisterClassW(&window_class) == 0 && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return nullptr;
    }
    // Invisible top-level window (the conventional tray host shape): no
    // taskbar button, no menu, only the callback address.
    const HWND window = ::CreateWindowExW(0, kCarrierClassName, L"Mirage notification carrier",
                                          WS_OVERLAPPED, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0,
                                          nullptr, nullptr, window_class.hInstance, nullptr);
    if (window == nullptr) {
        return nullptr;
    }
    NOTIFYICONDATAW icon_data{};
    // V3 size (Vista surface): carries szInfo / szInfoTitle / NIF_INFO and
    // NIIF_RESPECT_QUIET_TIME without exposing the hBalloonIcon tail that
    // only newer shells expect.
    icon_data.cbSize = NOTIFYICONDATAW_V3_SIZE;
    icon_data.hWnd = window;
    icon_data.uID = 1; // the (hWnd, uID) pair is this backend's icon identity
    icon_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon_data.uCallbackMessage = kCallbackMessage;
    // A shared stock icon as the bare carrier face (never destroyed — the
    // system owns stock icons); full tray interaction is M5 productization.
    // IDI_APPLICATION expands through the TCHAR MAKEINTRESOURCE, and this
    // TU is deliberately UNICODE-neutral (win32_util.hpp), so the W
    // resource id is spelled directly (winuser.h: #define IDI_APPLICATION
    // MAKEINTRESOURCE(32512)).
    icon_data.hIcon = ::LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    if (icon_data.hIcon == nullptr) {
        // A missing stock icon degrades the carrier's face, not the
        // notification capability.
        icon_data.uFlags &= ~NIF_ICON;
    }
    ::lstrcpynW(icon_data.szTip, L"Mirage", static_cast<int>(std::size(icon_data.szTip)));
    // The probe itself: NIM_ADD fails where no notification area exists
    // (session 0, shell-less session) — the open() fail-closed discipline
    // of DEC-018 decision 2.
    if (::Shell_NotifyIconW(NIM_ADD, &icon_data) == 0) {
        ::DestroyWindow(window);
        return nullptr;
    }
    auto backend = std::unique_ptr<NotificationBackend>(new NotificationBackend());
    backend->impl_ = std::make_unique<Impl>();
    backend->impl_->window = window;
    backend->impl_->icon_data = icon_data;
    return backend;
}

NotificationOutcome NotificationBackend::notify(const std::string &title, const std::string &body,
                                                const NotificationLimits &limits,
                                                const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    NotificationOutcome outcome;
    // The frozen refusal order of the fake contract (M2-05), every check
    // before any side effect: a refused notification must never have
    // reached the shell.
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "notification cancelled");
        return outcome;
    }
    if (title.empty()) {
        outcome.error = error("invalid_argument", "title must not be empty");
        return outcome;
    }
    if (title.size() > limits.max_title_bytes) {
        outcome.error = error("invalid_argument", "title exceeds the byte budget");
        return outcome;
    }
    if (body.size() > limits.max_body_bytes) {
        outcome.error = error("invalid_argument", "body exceeds the byte budget");
        return outcome;
    }
    if (!mirage::desktop::is_valid_utf8(title) || !mirage::desktop::is_valid_utf8(body)) {
        outcome.error = error("invalid_argument", "title and body must be valid UTF-8");
        return outcome;
    }
    // The contract boundary is UTF-8, the balloon arrays are UTF-16
    // (DEC-017 decision 5). Unreachable after the UTF-8 checks above —
    // kept as a guard for the conversion itself.
    const std::optional<std::wstring> title_wide = utf8_to_utf16(title);
    const std::optional<std::wstring> body_wide = utf8_to_utf16(body);
    if (!title_wide.has_value() || !body_wide.has_value()) {
        outcome.error = error("invalid_argument", "title and body must be valid UTF-8");
        return outcome;
    }
    // The platform's own caps, then the faithful-carry check: the
    // NOTIFYICONDATAW payload arrays are NUL-terminated fixed arrays, so
    // an embedded NUL would silently truncate the payload — refused
    // instead (DEC-018 decision 3; stricter than the Linux letter, which
    // carries the C-string truncation silently — recorded as a platform
    // difference visible through the stable error code).
    if (title_wide->size() > kMaxTitleUnits) {
        outcome.error =
            error("invalid_argument", "title exceeds the platform capacity of " +
                                          std::to_string(kMaxTitleUnits) + " characters");
        return outcome;
    }
    if (body_wide->size() > kMaxBodyUnits) {
        outcome.error =
            error("invalid_argument", "body exceeds the platform capacity of " +
                                          std::to_string(kMaxBodyUnits) + " characters");
        return outcome;
    }
    if (title_wide->find(L'\0') != std::wstring::npos ||
        body_wide->find(L'\0') != std::wstring::npos) {
        outcome.error =
            error("invalid_argument", "payload must not contain embedded NUL characters");
        return outcome;
    }

    NOTIFYICONDATAW balloon = impl_->icon_data;
    balloon.uFlags = NIF_INFO;
    // Honor the system's quiet time / focus assist: a suppressed balloon is
    // the platform's delivery decision, inside the frozen ok-means-accepted
    // clause (DEC-018 decision 1/3).
    balloon.dwInfoFlags = NIIF_RESPECT_QUIET_TIME;
    ::lstrcpynW(balloon.szInfoTitle, title_wide->c_str(),
                static_cast<int>(std::size(balloon.szInfoTitle)));
    ::lstrcpynW(balloon.szInfo, body_wide->c_str(), static_cast<int>(std::size(balloon.szInfo)));
    // Shell_NotifyIcon is a fast synchronous shell call with no per-call
    // timeout parameter: the single-call convergence discipline applies
    // (DEC-017 decision 3) — a wedged shell is bounded by the caller's
    // execution context, never by an in-provider wait.
    if (::Shell_NotifyIconW(NIM_MODIFY, &balloon) == 0) {
        outcome.error =
            error("io_error", "notification was not accepted by the shell" + last_error_suffix());
        return outcome;
    }
    // TRUE means the shell accepted the notification — never that the user
    // saw it (contract: delivery follows the platform).
    outcome.ok = true;
    return outcome;
}

} // namespace mirage::platform::windows_backend
