#pragma once

// Private Linux notification frontend (M2-05): desktop notification posting
// through the freedesktop Notifications D-Bus service (org.freedesktop.
// Notifications), reached with GIO GDBus per DEC-015 decision 3. All
// gio/glib types stay inside the .cpp (RULE-01); the threading discipline
// mirrors the other frontends — one mutex serializes every call, the
// provider stays synchronous and bounded, no Mirage code creates threads
// (RULE-03). GDBus runs internal worker threads inside the library for the
// synchronous call, exactly like libatspi does (M2-03); delivery follows
// the platform: ok means the service accepted the notification, not that
// the user saw it.

#include <memory>
#include <mutex>
#include <string>

#include <mirage/desktop/notification_provider.hpp>

namespace mirage::platform::linux_backend {

class NotificationBackend final : public mirage::desktop::NotificationProvider {
  public:
    /// Connects to the session bus (fail closed: null when there is none,
    /// capability honesty, DEC-015). The connection is kept for the backend
    /// lifetime; the Notifications service itself is resolved per call.
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

} // namespace mirage::platform::linux_backend
