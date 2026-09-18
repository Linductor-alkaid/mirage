#pragma once

#include <cstddef>
#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Budget for one notification (RULE-07); both caps are enforced before the
/// notification is handed to the platform.
struct NotificationLimits {
    std::size_t max_title_bytes = 256;
    std::size_t max_body_bytes = 4096;
};

struct NotificationOutcome {
    bool ok = false;
    bool cancelled = false;
    ProviderError error; ///< meaningful only when ok is false
};

/// Desktop notification posting (design doc section 5). Delivery semantics
/// follow the platform (on Linux the freedesktop Notifications service):
/// ok means the platform accepted the notification, not that the user saw
/// it. Callers judge the `notification.post` capability through the runtime
/// permission gate (RULE-05, DEC-010) before invoking. The provider stays
/// permission-agnostic. Methods are synchronous and bounded; callers decide
/// the execution context.
class NotificationProvider {
  public:
    virtual ~NotificationProvider() = default;

    /// Posts a notification. Over-budget or empty-title payloads are
    /// rejected before any platform call.
    virtual NotificationOutcome notify(const std::string &title, const std::string &body,
                                       const NotificationLimits &limits,
                                       const CancelToken &cancel) = 0;

    NotificationOutcome notify(const std::string &title, const std::string &body) {
        return notify(title, body, NotificationLimits{}, CancelToken{});
    }
};

} // namespace mirage::desktop
