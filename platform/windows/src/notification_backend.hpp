#pragma once

// Private Windows notification frontend (M4-05, DEC-018): balloon / banner
// notifications carried by a hidden callback window and a resident tray
// carrier icon (Shell_NotifyIcon NIM_ADD at open, NIM_MODIFY NIF_INFO per
// notification, NIM_DELETE at teardown). The Windows analog of the Linux
// freedesktop-Notifications frontend — the same frozen contract, the same
// refusal order, and the same honesty rules: ok means the shell accepted
// the notification, never that the user saw it (delivery follows the
// platform; the balloon vs banner vs quiet-time presentation is the
// system's choice, DEC-018 decision 1/3).
//
// Threading and lifetime (DEC-018 decision 4): the carrier window and the
// tray icon are created on the open() thread and torn down on the
// destroying thread (DestroyWindow is thread-affine — a cross-thread
// teardown removes the icon and skips the window loudly); one mutex
// serializes every call; no Mirage code creates threads and no message
// loop is pumped (RULE-03, DEC-017 decision 3) — the tray callback
// messages pile up in the creating thread's queue, which is inert because
// the provider only ever calls Shell_NotifyIcon directly. All Win32 types
// stay inside the .cpp (RULE-01).

#include <memory>
#include <mutex>
#include <string>

#include <mirage/desktop/notification_provider.hpp>

namespace mirage::platform::windows_backend {

class NotificationBackend final : public mirage::desktop::NotificationProvider {
  public:
    /// Builds the hidden carrier window and adds the tray icon. Returns
    /// null when either fails (no interactive desktop session / no shell
    /// notification area — a service context fails per the SendInput
    /// precedent, DEC-017 decision 3): callers expose no notification
    /// provider at all, the capability-honesty discipline of the Linux
    /// backend's missing session bus (DEC-018 decision 2).
    static std::unique_ptr<NotificationBackend> open();

    ~NotificationBackend() override;
    NotificationBackend(const NotificationBackend &) = delete;
    NotificationBackend &operator=(const NotificationBackend &) = delete;

    mirage::desktop::NotificationProvider *notification() { return this; }

    mirage::desktop::NotificationOutcome
    notify(const std::string &title, const std::string &body,
           const mirage::desktop::NotificationLimits &limits,
           const mirage::desktop::CancelToken &cancel) override;

  private:
    NotificationBackend() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
};

} // namespace mirage::platform::windows_backend
