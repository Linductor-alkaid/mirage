#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// One application known to the environment. `id` is the backend's stable
/// application identifier (on Linux the desktop file id, e.g.
/// "org.gnome.Nautilus.desktop"); `running` mirrors whether the backend
/// currently sees a live instance.
struct ApplicationInfo {
    std::string id;
    std::string name;
    bool running = false;
};

/// Budget for one application enumeration (RULE-07). A result that would
/// exceed `max_applications` entries is refused — never silently truncated.
struct ApplicationListLimits {
    std::size_t max_applications = 512;
};

struct ApplicationListOutcome {
    bool ok = false;
    std::vector<ApplicationInfo> applications;
    ProviderError error; ///< meaningful only when ok is false
};

struct ApplicationQueryOutcome {
    bool ok = false;
    bool running = false;
    std::string instance_id; ///< backend instance handle while running
    ProviderError error;     ///< meaningful only when ok is false
};

/// Budget for one application launch (RULE-07). Both caps are enforced
/// before any process is created.
struct ApplicationLaunchLimits {
    std::chrono::milliseconds timeout{10000};
    std::size_t max_command_bytes = 4u << 10;
};

struct ApplicationLaunchOutcome {
    bool ok = false;
    bool cancelled = false;
    std::string instance_id;
    ProviderError error; ///< meaningful only when ok is false
};

/// Application discovery, launch and termination (design doc section 5).
///
/// Launch and termination are real side effects: callers judge the
/// `application.launch` / `application.terminate` capabilities through the
/// runtime permission gate (RULE-05, DEC-010) before invoking. The provider
/// stays permission-agnostic. Methods are synchronous and bounded; callers
/// decide the execution context.
class ApplicationProvider {
  public:
    virtual ~ApplicationProvider() = default;

    /// Enumerates known applications within the budget; refuses over-budget
    /// results instead of truncating.
    virtual ApplicationListOutcome list_applications(const ApplicationListLimits &limits,
                                                     const CancelToken &cancel) = 0;

    ApplicationListOutcome list_applications() {
        return list_applications(ApplicationListLimits{}, CancelToken{});
    }

    /// Reports whether the application currently has a live instance.
    virtual ApplicationQueryOutcome running_state(const std::string &application_id,
                                                  const CancelToken &cancel) = 0;

    ApplicationQueryOutcome running_state(const std::string &application_id) {
        return running_state(application_id, CancelToken{});
    }

    /// Launches the application by id. Unknown ids fail with "not_found"
    /// before any process is created; an already-running single-instance app
    /// fails with "already_running" instead of spawning a duplicate.
    virtual ApplicationLaunchOutcome launch(const std::string &application_id,
                                            const ApplicationLaunchLimits &limits,
                                            const CancelToken &cancel) = 0;

    /// Asks the running instance to terminate and waits up to the launch
    /// budget for it to exit. Unknown ids fail with "not_found"; a live
    /// instance that ignores the request fails with "deadline_exceeded"
    /// without forcing a kill (forced termination is out of contract scope).
    virtual ApplicationLaunchOutcome terminate(const std::string &application_id,
                                               const ApplicationLaunchLimits &limits,
                                               const CancelToken &cancel) = 0;

    /// Same launch / termination under default limits and without
    /// cancellation.
    ApplicationLaunchOutcome launch(const std::string &application_id) {
        return launch(application_id, ApplicationLaunchLimits{}, CancelToken{});
    }

    ApplicationLaunchOutcome terminate(const std::string &application_id) {
        return terminate(application_id, ApplicationLaunchLimits{}, CancelToken{});
    }
};

} // namespace mirage::desktop
