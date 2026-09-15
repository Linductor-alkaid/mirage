#pragma once

#include <string>

namespace mirage::runtime {

/// Version of the pinned Mira core this binary was linked against. Sourced
/// from <mira/version.hpp> in the pinned third_party/mira checkout; the
/// runtime layer is the only place that includes Mira headers for identity
/// reporting (integration/mira holds the contract-facing adapter).
struct MiraCoreVersion {
    int major = 0;
    int minor = 0;
    int patch = 0;
};

MiraCoreVersion mira_core_version();

std::string mira_core_version_string();

/// True when the linked Mira core satisfies the major.minor Mirage was built
/// and verified against. Patch differences are accepted.
bool mira_core_compatible_with(int expected_major, int expected_minor);

/// Lifecycle state of the hosted Mira instance (design doc section 11). The
/// full state machine (Idle / Hosting / Degraded / Stopped plus cancellation
/// and event streaming) is frozen by the M1 milestone.
enum class HostStatus {
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed,
};

/// Host for one long-running Mira instance inside the Mirage runtime
/// (design doc section 11). The skeleton carries status only; M1 introduces
/// initialization, Desktop Environment binding, configuration and the agent
/// event stream handed to the product layer.
class MiraHost {
public:
    virtual ~MiraHost() = default;

    virtual HostStatus status() const = 0;
};

/// Placeholder implementation that keeps the dependency wiring exercised
/// until the M1 host lands. Reports Stopped; replace with the real host.
class SkeletonMiraHost final : public MiraHost {
public:
    HostStatus status() const override;
};

} // namespace mirage::runtime
