#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/overlay_carrier.hpp>
#include <mirage/desktop/path_scope.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/desktop/tray_carrier.hpp>

namespace mirage::platform::linux_backend {

class X11Backend;          // private in src/: no X11 types may appear here (RULE-01)
class AtspiBackend;        // ditto for the AT-SPI2 frontend (M2-03)
class ApplicationBackend;  // ditto for the GIO application frontend (M2-05)
class NotificationBackend; // ditto for the GDBus notification frontend (M2-05)
class PortalScreenBackend; // private Wayland observation adapter (DEC-073)

/// Desktop Overlay carrier of the Linux backend (M5-09, DEC-029): the X11
/// shape overlay (topmost override-redirect window, shape-composed
/// transparency, click-through except the confirm entry). Null without an
/// X/XWayland connection (e.g. a Wayland-native session) or without X11
/// Xshape support in the build — capability honesty, never a degraded
/// carrier. The carrier runs on the caller's Executor blocking worker; it
/// holds its own X connection.
std::unique_ptr<mirage::desktop::OverlayCarrier>
open_overlay_carrier(const std::string &display = {});

/// Tray indicator carrier of the Linux backend (M5-10, DEC-030): the
/// session-bus StatusNotifierItem indicator (AppIndicator protocol family)
/// with its context menu. Null without a session bus or without a
/// StatusNotifierWatcher host (e.g. stock GNOME without the AppIndicator
/// extension) or without gio support in the build — capability honesty,
/// never a degraded carrier. The carrier runs on the caller's Executor
/// blocking worker (its GLib loop is the encapsulated platform event loop).
std::unique_ptr<mirage::desktop::TrayCarrier> open_tray_carrier();

/// Opt-in X11/XWayland surface of the Linux backend (M2-02, DEC-015). When
/// enabled, an explicit display selects X11; an empty display auto-selects
/// the session. Native Wayland exposes Portal Screen observation and task-owned
/// interaction (DEC-077), with global Window / Input / Clipboard unavailable. Other sessions use
/// $DISPLAY and expose X11 providers when the connection succeeds.
struct X11Options {
    bool enabled = false;
    /// Explicit X display; empty selects the current session backend.
    std::string display;
};

/// Opt-in AT-SPI2 accessibility surface (M2-03, DEC-015). When enabled the
/// environment initializes libatspi at construction and exposes the
/// AccessibilityProvider; a failed initialization (no accessibility bus)
/// leaves the accessor null — fail closed, never a broken provider.
struct AtspiOptions {
    bool enabled = false;
};

/// Opt-in application management surface (M2-05): desktop application
/// discovery, launch, running state and cooperative termination via GIO
/// (DEC-015 decision 3). Like every other option it defaults to off; open()
/// cannot fail (discovery happens per call), so the accessor is non-null
/// exactly when enabled and gio-unix support is compiled in.
struct ApplicationOptions {
    bool enabled = false;
};

/// Opt-in notification surface (M2-05): freedesktop Notifications delivery
/// over the session bus (DEC-015 decision 3). A missing session bus leaves
/// the accessor null — fail closed, never a broken provider.
struct NotificationsOptions {
    bool enabled = false;
};

/// M10-04 (DEC-095): write/edit containment roots, strictly separate from
/// the read scope — read roots never imply write authority. Empty roots
/// deny every mutation (fail closed). Append-only constructor option:
/// existing call sites keep their behavior with no write surface at all.
struct FilesystemWriteOptions {
    std::vector<std::filesystem::path> roots;
};

/// Linux Desktop Environment (design doc sections 5 and 10): M1 scoped
/// read-only filesystem access and bounded, cancellable shell execution,
/// plus the M2 X11 window / capture / input surface (M2-02), the AT-SPI2
/// accessibility provider (M2-03), the X11 clipboard (M2-04) and the GIO
/// application / notification frontends (M2-05) when opted in. The Desktop
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
    /// constructor declares no roots and no desktop surface, so a
    /// default-constructed environment exposes no filesystem and no desktop
    /// surface at all (fail closed).
    explicit LinuxDesktopEnvironment(
        std::vector<std::filesystem::path> filesystem_read_roots = {}, X11Options x11_options = {},
        AtspiOptions atspi_options = {}, ApplicationOptions application_options = {},
        NotificationsOptions notifications_options = {},
        FilesystemWriteOptions filesystem_write_options = {});

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
    mirage::desktop::ClipboardProvider *clipboard() override;
    mirage::desktop::ApplicationProvider *application() override;
    mirage::desktop::NotificationProvider *notification() override;

    /// DEC-085 capability honesty: a Wayland session (including XWayland,
    /// where an X11 connection enumerates windows but root-window capture
    /// fails) never reports `Full`, even when only the X11 screen provider
    /// answers because the Portal backend failed to open.
    mirage::desktop::ScreenCapability screen_capability() override;

    // The three-argument overrides would hide the base conveniences.
    using mirage::desktop::FilesystemProvider::read_text_file;
    using mirage::desktop::ProcessProvider::execute;

    mirage::desktop::FileReadOutcome
    read_text_file(const std::filesystem::path &path, const mirage::desktop::FileReadLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ProcessOutcome execute(const std::string &command,
                                            const mirage::desktop::ProcessLimits &limits,
                                            const mirage::desktop::CancelToken &cancel) override;

    // M10-04/05 (DEC-095): windowed reads with version facts, create /
    // observed-replace / literal-edit commits, and exec with an explicit
    // working directory plus bounded output retention.
    [[nodiscard]] mirage::desktop::FilesystemCapabilities capabilities() const override;
    mirage::desktop::TextWindowOutcome
    read_text_window(const std::filesystem::path &path, std::uint64_t skip_lines,
                     const mirage::desktop::TextWindowLimits &limits,
                     const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::FileWriteOutcome
    create_file(const std::filesystem::path &path, const std::string &content,
                const mirage::desktop::FileModifyLimits &limits,
                const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::FileWriteOutcome
    replace_file(const std::filesystem::path &path, const std::string &content,
                 const mirage::desktop::FileObservation &observation,
                 const mirage::desktop::FileModifyLimits &limits,
                 const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::FileEditOutcome
    edit_file(const std::filesystem::path &path, const std::string &old_string,
              const std::string &new_string, bool replace_all,
              const mirage::desktop::FileObservation &observation,
              const mirage::desktop::FileModifyLimits &limits,
              const mirage::desktop::CancelToken &cancel) override;

    [[nodiscard]] bool supports_working_directory() const override { return true; }
    [[nodiscard]] bool supports_output_retention() const override { return true; }
    mirage::desktop::ProcessOutcome execute_request(const mirage::desktop::ProcessRequest &request,
                                                    const mirage::desktop::ProcessLimits &limits,
                                                    const mirage::desktop::CancelToken &cancel)
        override;

  private:
    mirage::desktop::PathScope read_scope_;
    mirage::desktop::PathScope write_scope_;
    /// DEC-095: bounded single-writer leases per on-disk identity; a second
    /// concurrent modify of one file reports busy instead of queueing.
    struct FileLeaseTable {
        std::mutex mutex;
        std::map<std::pair<std::uint64_t, std::uint64_t>, bool> held;
        bool try_acquire(std::uint64_t device, std::uint64_t index);
        void release(std::uint64_t device, std::uint64_t index);
    };
    std::unique_ptr<FileLeaseTable> file_leases_;

    /// Shared execution body of execute() and execute_request(): a negative
    /// working_dir_fd inherits the caller's directory (legacy semantics), a
    /// null output sink keeps preview-only capture.
    [[nodiscard]] static mirage::desktop::ProcessOutcome
    execute_common(const std::string &command, const mirage::desktop::ProcessLimits &limits,
                   const mirage::desktop::CancelToken &cancel, int working_dir_fd,
                   mirage::desktop::ProcessOutputSink *output_sink);
    std::unique_ptr<X11Backend> x11_;
    /// M7-18: Wayland fallback window enumeration over the accessibility tree.
    std::unique_ptr<::mirage::desktop::WindowProvider> atspi_windows_;
    std::unique_ptr<PortalScreenBackend> portal_screen_;
    std::unique_ptr<AtspiBackend> atspi_;
    std::unique_ptr<ApplicationBackend> application_backend_;
    std::unique_ptr<NotificationBackend> notification_backend_;
    /// DEC-085: WAYLAND_DISPLAY / XDG_SESSION_TYPE said "wayland" at open().
    /// XWayland sessions land here too; direct X11 capture is then known
    /// broken and the Portal observation path is preferred.
    bool wayland_session_ = false;
    /// An explicitly configured display pinned an X server on purpose
    /// (tests, forced X11); it opts out of session-based capability marking.
    bool x11_display_explicit_ = false;
};

} // namespace mirage::platform::linux_backend
