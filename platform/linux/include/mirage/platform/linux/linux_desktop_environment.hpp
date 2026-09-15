#pragma once

#include <filesystem>
#include <string>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>

namespace mirage::platform::linux_backend {

/// M1 reference Desktop Environment for Linux (design doc sections 5 and 10):
/// read-only filesystem access via std::filesystem and bounded shell
/// execution via the POSIX process API. The window / accessibility / capture
/// / input providers of the full Linux Backend are M2 scope and report null
/// here; path scoping (M1-05) and permission gating (M1-06) are not part of
/// this surface yet, so bind it only in development and test topologies
/// (DEC-008).
///
/// The class plays both roles of the adapter pattern: it is the concrete
/// environment an owner in the runtime layer constructs and keeps alive for
/// as long as the bound Mira instance runs, and it implements the M1 provider
/// interfaces itself (adapter depends on the desktop core interfaces).
class LinuxDesktopEnvironment final : public mirage::desktop::DesktopEnvironment,
                                      public mirage::desktop::FilesystemProvider,
                                      public mirage::desktop::ProcessProvider {
public:
    LinuxDesktopEnvironment() = default;

    mirage::desktop::EnvironmentInfo info() const override;
    mirage::desktop::FilesystemProvider* filesystem() override { return this; }
    mirage::desktop::ProcessProvider* process() override { return this; }

    mirage::desktop::FileReadOutcome read_text_file(
        const std::filesystem::path& path) override;
    mirage::desktop::ProcessOutcome execute(
        const std::string& command,
        const mirage::desktop::ProcessLimits& limits) override;
};

} // namespace mirage::platform::linux_backend
