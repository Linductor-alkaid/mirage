// Link-time stub for machines without the gio-unix dev packages: the backend
// exists (so linux_desktop_environment.cpp compiles unchanged) but reports
// no capability, and no gio/glib header is touched. See
// platform/CMakeLists.txt.

#include "application_backend.hpp"

namespace mirage::platform::linux_backend {

struct ApplicationBackend::Impl {};

ApplicationBackend::~ApplicationBackend() = default;

std::unique_ptr<ApplicationBackend> ApplicationBackend::open() {
    return nullptr; // fail closed: no gio-unix support in this build
}

mirage::desktop::ApplicationListOutcome
ApplicationBackend::list_applications(const mirage::desktop::ApplicationListLimits &,
                                      const mirage::desktop::CancelToken &) {
    return {};
}

mirage::desktop::ApplicationQueryOutcome
ApplicationBackend::running_state(const std::string &, const mirage::desktop::CancelToken &) {
    return {};
}

mirage::desktop::ApplicationLaunchOutcome
ApplicationBackend::launch(const std::string &, const mirage::desktop::ApplicationLaunchLimits &,
                           const mirage::desktop::CancelToken &) {
    return {};
}

mirage::desktop::ApplicationLaunchOutcome
ApplicationBackend::terminate(const std::string &, const mirage::desktop::ApplicationLaunchLimits &,
                              const mirage::desktop::CancelToken &) {
    return {};
}

} // namespace mirage::platform::linux_backend
