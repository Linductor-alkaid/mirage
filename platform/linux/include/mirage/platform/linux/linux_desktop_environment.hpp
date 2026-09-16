#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/path_scope.hpp>
#include <mirage/desktop/process_provider.hpp>

namespace mirage::platform::linux_backend {

/// M1 reference Desktop Environment for Linux (design doc sections 5 and 10):
/// scoped read-only filesystem access via std::filesystem and bounded,
/// cancellable shell execution via the POSIX process API. The window /
/// accessibility / capture / input providers of the full Linux Backend are
/// M2 scope and report null here; the Desktop Permission gate (RULE-05) is
/// M1-06 and not part of this surface yet, so bind it only in development
/// and test topologies (DEC-008).
///
/// The class plays both roles of the adapter pattern: it is the concrete
/// environment an owner in the runtime layer constructs and keeps alive for
/// as long as the bound Mira instance runs, and it implements the M1 provider
/// interfaces itself (adapter depends on the desktop core interfaces).
class LinuxDesktopEnvironment final : public mirage::desktop::DesktopEnvironment,
                                      public mirage::desktop::FilesystemProvider,
                                      public mirage::desktop::ProcessProvider {
  public:
    /// The read scope is a hard containment boundary (M1-05): only paths at
    /// or beneath one of `filesystem_read_roots` are readable. The default
    /// constructor declares no roots, so a default-constructed environment
    /// exposes no filesystem surface at all (fail closed).
    explicit LinuxDesktopEnvironment(std::vector<std::filesystem::path> filesystem_read_roots = {})
        : read_scope_(std::move(filesystem_read_roots)) {}

    mirage::desktop::EnvironmentInfo info() const override;
    mirage::desktop::FilesystemProvider *filesystem() override { return this; }
    mirage::desktop::ProcessProvider *process() override { return this; }

    // The three-argument overrides would hide the base conveniences.
    using mirage::desktop::FilesystemProvider::read_text_file;
    using mirage::desktop::ProcessProvider::execute;

    mirage::desktop::FileReadOutcome
    read_text_file(const std::filesystem::path &path, const mirage::desktop::FileReadLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ProcessOutcome execute(const std::string &command,
                                            const mirage::desktop::ProcessLimits &limits,
                                            const mirage::desktop::CancelToken &cancel) override;

  private:
    mirage::desktop::PathScope read_scope_;
};

} // namespace mirage::platform::linux_backend
