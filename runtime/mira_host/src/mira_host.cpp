#include <mirage/runtime/mira_host.hpp>

#include <mira/version.hpp>

namespace mirage::runtime {

MiraCoreVersion mira_core_version() {
    return {mira::kVersion.major, mira::kVersion.minor, mira::kVersion.patch};
}

std::string mira_core_version_string() {
    const MiraCoreVersion version = mira_core_version();
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "."
           + std::to_string(version.patch);
}

bool mira_core_compatible_with(int expected_major, int expected_minor) {
    const MiraCoreVersion version = mira_core_version();
    return version.major == expected_major && version.minor == expected_minor;
}

HostStatus SkeletonMiraHost::status() const {
    return HostStatus::Stopped;
}

} // namespace mirage::runtime
