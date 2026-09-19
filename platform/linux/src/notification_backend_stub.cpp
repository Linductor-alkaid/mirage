// Link-time stub for machines without the gio-unix dev packages: the backend
// exists (so linux_desktop_environment.cpp compiles unchanged) but reports
// no capability, and no gio/glib header is touched. See
// platform/CMakeLists.txt.

#include "notification_backend.hpp"

namespace mirage::platform::linux_backend {

struct NotificationBackend::Impl {};

NotificationBackend::~NotificationBackend() = default;

std::unique_ptr<NotificationBackend> NotificationBackend::open() {
    return nullptr; // fail closed: no gio-unix support in this build
}

mirage::desktop::NotificationOutcome
NotificationBackend::notify(const std::string &, const std::string &,
                            const mirage::desktop::NotificationLimits &,
                            const mirage::desktop::CancelToken &) {
    return {};
}

} // namespace mirage::platform::linux_backend
