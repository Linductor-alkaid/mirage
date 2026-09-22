#pragma once

// Private Windows application frontend (M4-04): application discovery over
// the Start Menu shortcut surface (.lnk files under the per-user and the
// common Start Menu Programs folders, the Windows analog of the Linux
// desktop-entry surface), launch through CreateProcess of the resolved
// shortcut target, running-state from the tracked-instance registry plus a
// bounded process-snapshot name scan, and cooperative termination by
// posting WM_CLOSE to the instance's windows — the TERM equivalent — with
// no forced kill anywhere (M2-05 frozen semantics).
//
// Discovery mechanism (decided in implementation, recorded in the M4-04
// verification record): Start Menu shortcuts carry target path, arguments,
// working directory and show command — everything the launch contract
// needs — and honor user-over-machine precedence like XDG data dirs. The
// registry Uninstall keys were rejected: their UninstallString is an
// uninstaller entry point, not a launcher.
//
// All COM / shell types stay inside the .cpp (RULE-01). Threading
// discipline mirrors Win32Backend (DEC-017 decision 3): one mutex
// serializes every call, providers stay synchronous and bounded, no Mirage
// code creates threads (RULE-03), and the calling context is an Executor
// blocking worker (EXEC-04). COM is used call-scoped only (nothing is held
// across calls, unlike the UIA registry), so each method opens and closes
// its own MTA scope without an anchor.

#include <memory>
#include <mutex>
#include <string>

#include <mirage/desktop/application_provider.hpp>

namespace mirage::platform::windows_backend {

class ApplicationBackend final : public mirage::desktop::ApplicationProvider {
  public:
    /// Creates the backend. Unconditional: discovery (filesystem), launch
    /// (CreateProcess), the snapshot scan and WM_CLOSE posting work in any
    /// session, including service contexts — the same availability model as
    /// the ProcessProvider (M4-03).
    static std::unique_ptr<ApplicationBackend> open();

    ~ApplicationBackend() override;
    ApplicationBackend(const ApplicationBackend &) = delete;
    ApplicationBackend &operator=(const ApplicationBackend &) = delete;

    mirage::desktop::ApplicationProvider *application() { return this; }

    mirage::desktop::ApplicationListOutcome
    list_applications(const mirage::desktop::ApplicationListLimits &limits,
                      const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ApplicationQueryOutcome
    running_state(const std::string &application_id,
                  const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ApplicationLaunchOutcome
    launch(const std::string &application_id,
           const mirage::desktop::ApplicationLaunchLimits &limits,
           const mirage::desktop::CancelToken &cancel) override;
    mirage::desktop::ApplicationLaunchOutcome
    terminate(const std::string &application_id,
              const mirage::desktop::ApplicationLaunchLimits &limits,
              const mirage::desktop::CancelToken &cancel) override;

  private:
    ApplicationBackend() = default;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::mutex mutex_;
};

} // namespace mirage::platform::windows_backend
