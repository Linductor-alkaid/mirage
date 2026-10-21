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
    // DEC-110 (M7-26): same-origin pointer origin for glide travel — the
    // session's own last dispatched pointer position. Portal offers no
    // absolute pointer read-back; an unknown origin refuses glide before
    // any effect instead of teleporting from an invented start.
    bool has_pointer_origin = false;
    double pointer_origin_x = 0, pointer_origin_y = 0;
    // Out: the last successfully notified pointer position (set only when
    // at least one motion event was accepted).
    bool pointer_known = false;
    double pointer_x = 0, pointer_y = 0;
    std::function<bool()> stopped;
    std::function<bool(const PortalInputEvent &)> notify;
};
desktop::InputOutcome dispatch_portal_input(PortalInputContext &,
                                            const desktop::SharedInputAction &,
                                            const desktop::InputLimits &,
                                            const desktop::CancelToken &);
} // namespace mirage::platform::linux_backend
