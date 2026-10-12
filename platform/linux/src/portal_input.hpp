#pragma once
#include <functional>
#include <mirage/desktop/screen_provider.hpp>
namespace mirage::platform::linux_backend {
// Private synchronous protocol seam. No ownership, scheduling or public SDK types.
struct PortalInputEvent {
    enum class Kind { Motion, Button, Key, Axis };
    Kind kind = Kind::Motion;
    int value = 0;
    unsigned state = 0;
    double x = 0, y = 0;
};
struct PortalInputContext {
    unsigned devices = 0;
    int logical_width = 0, logical_height = 0, frame_width = 0, frame_height = 0;
    std::function<bool()> stopped;
    std::function<bool(const PortalInputEvent &)> notify;
};
desktop::InputOutcome dispatch_portal_input(PortalInputContext &,
                                            const desktop::SharedInputAction &,
                                            const desktop::InputLimits &,
                                            const desktop::CancelToken &);
} // namespace mirage::platform::linux_backend
