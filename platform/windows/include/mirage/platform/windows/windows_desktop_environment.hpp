#pragma once

#include <memory>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/process_provider.hpp>

namespace mirage::platform::windows_backend {

class Win32Backend;        // private in src/: no Win32 type may appear here (RULE-01)
class UiaBackend;          // ditto for the UI Automation frontend (M4-02)
class ApplicationBackend;  // ditto for the application frontend (M4-04)
class NotificationBackend; // ditto for the notification frontend (M4-05)

/// Opt-in Win32 surface of the Windows backend (M4-01, DEC-017). When
/// enabled the environment probes the interactive desktop at construction
/// and exposes the Window / Screen / Input providers; a failed probe (a
/// session without an interactive display) leaves those accessors null so
/// consumers fail closed — it never silently degrades.
struct Win32Options {
    bool enabled = false;
};

/// Opt-in UI Automation accessibility surface (M4-02, DEC-017 decision 4).
/// When enabled the environment probes the UIA client core at construction
/// and exposes the AccessibilityProvider; a failed probe (COM in a foreign
/// apartment, UIA core unavailable) leaves the accessor null — fail closed,
/// never a broken provider.
struct UiaOptions {
    bool enabled = false;
};

/// Opt-in application surface (M4-04): Start Menu shortcut discovery,
/// launch, running-state and cooperative termination. Unlike the Win32 and
/// UIA probes, the application frontend's mechanisms (file-system
/// discovery, CreateProcess, the process snapshot, WM_CLOSE posting) work
/// in any session, so opening it cannot fail — the option only decides
/// whether the capability is exposed at all.
struct ApplicationOptions {
    bool enabled = false;
};

/// Opt-in notification surface (M4-05, DEC-018): balloon / banner
/// notifications carried by a hidden window and a resident tray icon
/// (Shell_NotifyIcon). A failed open() probe — no interactive session, no
/// shell notification area — leaves the accessor null: fail closed, the
/// Linux backend's missing-session-bus precedent (capability honesty, not
/// a degraded provider).
struct NotificationsOptions {
    bool enabled = false;
};

/// Windows Desktop Environment (design doc sections 5 and 10): the M4-01
/// Win32 window / capture / input surface, the M4-02 UIA accessibility
/// provider, the M4-03 Win32 clipboard, the M4-04 application provider and
/// the M4-05 notification provider (all opt-in) and the process execution
/// surface (always available: CreateProcess works in any session,
/// including service contexts). This closes the M4 provider set.
///
/// The Desktop Permission gate (RULE-05) is judged by the runtime layer
/// before actions reach any provider; this class itself stays
/// permission-agnostic. It is the concrete environment an owner in the
/// runtime layer constructs and keeps alive for as long as the bound Mira
/// instance runs, and it implements the provider interfaces through the
/// private Win32 / UIA frontends (adapter depends on the desktop core
/// interfaces, DEC-008).
class WindowsDesktopEnvironment final : public mirage::desktop::DesktopEnvironment,
                                        public mirage::desktop::ProcessProvider {
  public:
    explicit WindowsDesktopEnvironment(Win32Options win32_options = {}, UiaOptions uia_options = {},
                                       ApplicationOptions application_options = {},
                                       NotificationsOptions notifications_options = {});

    ~WindowsDesktopEnvironment() override;

    WindowsDesktopEnvironment(const WindowsDesktopEnvironment &) = delete;
    WindowsDesktopEnvironment &operator=(const WindowsDesktopEnvironment &) = delete;

    mirage::desktop::EnvironmentInfo info() const override;

    mirage::desktop::WindowProvider *window() override;
    mirage::desktop::ScreenProvider *screen() override;
    mirage::desktop::InputProvider *input() override;
    mirage::desktop::AccessibilityProvider *accessibility() override;
    mirage::desktop::ClipboardProvider *clipboard() override;
    mirage::desktop::ApplicationProvider *application() override;
    mirage::desktop::NotificationProvider *notification() override;
    mirage::desktop::ProcessProvider *process() override { return this; }

    // The three-argument override would hide the base convenience.
    using mirage::desktop::ProcessProvider::execute;

    mirage::desktop::ProcessOutcome execute(const std::string &command,
                                            const mirage::desktop::ProcessLimits &limits,
                                            const mirage::desktop::CancelToken &cancel) override;

  private:
    std::unique_ptr<Win32Backend> win32_;
    std::unique_ptr<UiaBackend> uia_;
    std::unique_ptr<ApplicationBackend> application_;
    std::unique_ptr<NotificationBackend> notification_;
};

} // namespace mirage::platform::windows_backend
