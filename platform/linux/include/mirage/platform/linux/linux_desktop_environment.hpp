#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/path_scope.hpp>
#include <mirage/desktop/process_provider.hpp>

namespace mirage::platform::linux_backend {

class X11Backend;   // private in src/: no X11 types may appear here (RULE-01)
class AtspiBackend; // ditto for the AT-SPI2 frontend (M2-03)

/// Opt-in X11/XWayland surface of the Linux backend (M2-02, DEC-015). When
/// enabled the environment connects to `display` (empty = $DISPLAY) at
/// construction and exposes the Window / Screen / Input providers; a failed
/// connection (e.g. a Wayland-native session without XWayland) leaves those
/// accessors null so consumers fail closed — it never silently degrades.
struct X11Options {
    bool enabled = false;
    /// X display name; empty uses the DISPLAY environment variable.
    std::string display;
};

/// Opt-in AT-SPI2 accessibility surface (M2-03, DEC-015). When enabled the
/// environment initializes libatspi at construction and exposes the
/// AccessibilityProvider; a failed initialization (no accessibility bus)
/// leaves the accessor null — fail closed, never a broken provider.
struct AtspiOptions {
    bool enabled = false;
};

/// Linux Desktop Environment (design doc sections 5 and 10): M1 scoped
/// read-only filesystem access and bounded, cancellable shell execution,
/// plus the M2-02 X11 window / capture / input skeleton when opted in. The
/// accessibility provider remains M2-03 scope and reports null. The Desktop
/// Permission gate (RULE-05) is judged by the runtime layer before actions
/// reach any provider; this class itself stays permission-agnostic.
///
/// The class plays both roles of the adapter pattern: it is the concrete
/// environment an owner in the runtime layer constructs and keeps alive for
/// as long as the bound Mira instance runs, and it implements provider
/// interfaces itself (adapter depends on the desktop core interfaces).
class LinuxDesktopEnvironment final : public mirage::desktop::DesktopEnvironment,
                                      public mirage::desktop::FilesystemProvider,
                                      public mirage::desktop::ProcessProvider {
  public:
    /// The read scope is a hard containment boundary (M1-05): only paths at
    /// or beneath one of `filesystem_read_roots` are readable. The default
    /// constructor declares no roots and no X11 surface, so a
    /// default-constructed environment exposes no filesystem and no desktop
    /// surface at all (fail closed).
    explicit LinuxDesktopEnvironment(std::vector<std::filesystem::path> filesystem_read_roots = {},
                                     X11Options x11_options = {}, AtspiOptions atspi_options = {});

    ~LinuxDesktopEnvironment() override;

    LinuxDesktopEnvironment(const LinuxDesktopEnvironment &) = delete;
    LinuxDesktopEnvironment &operator=(const LinuxDesktopEnvironment &) = delete;

    mirage::desktop::EnvironmentInfo info() const override;
    mirage::desktop::FilesystemProvider *filesystem() override { return this; }
    mirage::desktop::ProcessProvider *process() override { return this; }

    mirage::desktop::WindowProvider *window() override;
    mirage::desktop::ScreenProvider *screen() override;
    mirage::desktop::InputProvider *input() override;
    mirage::desktop::AccessibilityProvider *accessibility() override;

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
    std::unique_ptr<X11Backend> x11_;
    std::unique_ptr<AtspiBackend> atspi_;
};

} // namespace mirage::platform::linux_backend
