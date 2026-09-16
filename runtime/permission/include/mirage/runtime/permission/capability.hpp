#pragma once

#include <optional>
#include <string_view>

namespace mirage::runtime::permission {

/// Desktop capabilities the permission framework judges (design doc
/// section 15). The M1 set covers the capabilities the M1 desktop surface
/// can reach plus filesystem.write, whose judgment exists from the start so
/// M2+ write providers land on a frozen vocabulary (DEC-010). The enumerator
/// order indexes the per-capability policy array; do not reorder.
enum class Capability {
    FilesystemRead,
    FilesystemWrite,
    ProcessExecute,
};

/// Stable string form of a capability ("filesystem.read",
/// "filesystem.write", "process.execute"); never null.
const char* capability_name(Capability capability);

/// Parses a stable capability name; nullopt for anything else.
std::optional<Capability> capability_from_name(std::string_view name);

} // namespace mirage::runtime::permission
