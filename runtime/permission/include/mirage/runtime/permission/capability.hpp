#pragma once

#include <optional>
#include <string_view>

namespace mirage::runtime::permission {

/// Desktop capabilities the permission framework judges (design doc
/// section 15). The M1 set covers the capabilities the M1 desktop surface
/// can reach plus filesystem.write, whose judgment exists from the start so
/// M2+ write providers land on a frozen vocabulary (DEC-010). M2-02 appends
/// the X11 backend's side-effectful actions (DEC-015), M2-04 appends the
/// clipboard directions and M2-05 appends the application / notification
/// actions (DEC-015 decision 6); the enumerators are append-only because
/// the order indexes the per-capability policy array.
enum class Capability {
    FilesystemRead,
    FilesystemWrite,
    ProcessExecute,
    WindowActivate,
    ScreenCapture,
    InputInject,
    ClipboardRead,
    ClipboardWrite,
    ApplicationLaunch,
    ApplicationTerminate,
    NotificationPost,
};

/// Stable string form of a capability ("filesystem.read",
/// "filesystem.write", "process.execute"); never null.
const char *capability_name(Capability capability);

/// Parses a stable capability name; nullopt for anything else.
std::optional<Capability> capability_from_name(std::string_view name);

} // namespace mirage::runtime::permission
