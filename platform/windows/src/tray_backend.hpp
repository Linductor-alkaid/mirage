#pragma once

// Private Windows tray frontend (M5-10, DEC-030): the notification-area
// carrier. Lives in src/ so no Win32 header ever appears in a public
// include path (RULE-01); the implementation hides every Win32 type inside
// the .cpp.
//
// Carrier decision (DEC-030 decision 3): the Shell_NotifyIcon surface DEC-018
// landed for the notification frontend (M4-05), extended to the product form
// that record explicitly deferred to M5 — a context menu (status header,
// pause / resume on the tracked task, open the desktop shell, quit) and a
// live tooltip. Still no AUMID carrier, no WinRT (DEC-018 decision 5 and its
// M5 re-opening condition stay untouched: this is the balloon carrier's
// resident-icon face, not toast).
//
// Threading discipline: the icon, its hidden callback window and the menu
// are created inside run() on the calling thread and destroyed there (the
// DEC-018 ownership model); the calling thread is an Executor blocking
// worker chosen by the owner. wakeup() is the only thread-safe member: it
// posts into the callback window's message queue, releasing the external
// wait.

#include <atomic>
#include <memory>

#include <mirage/desktop/tray_carrier.hpp>

namespace mirage::platform::windows_backend {

class Win32TrayCarrier final : public mirage::desktop::TrayCarrier {
  public:
    /// Probes the interactive desktop (the Win32 frontend's open()
    /// discipline, DEC-017 decision 3): a session without a display has no
    /// shell notification area to host the icon.
    static std::unique_ptr<Win32TrayCarrier> open();

    ~Win32TrayCarrier() override;
    Win32TrayCarrier(const Win32TrayCarrier &) = delete;
    Win32TrayCarrier &operator=(const Win32TrayCarrier &) = delete;

    mirage::desktop::TrayCarrier::RunReport
    run(const mirage::desktop::TrayCarrierContext &context,
        const std::function<bool()> &stop_requested) override;

    void wakeup() override;

  private:
    Win32TrayCarrier();

    /// The callback window handle (as void*), published only while a
    /// healthy presentation loop runs. void* keeps the Win32 type out of
    /// this header (RULE-01).
    std::atomic<void *> callback_window_{nullptr};
};

/// Public factory (declared in the platform's public header): the Windows
/// tray carrier, or null when the session has no interactive display.
std::unique_ptr<mirage::desktop::TrayCarrier> open_tray_carrier();

} // namespace mirage::platform::windows_backend
