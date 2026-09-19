#pragma once

// Private Linux application frontend (M2-05): desktop application
// discovery, launch, running-state queries and cooperative termination on
// top of GIO's GDesktopAppInfo (DEC-015 decision 3: the glib family is the
// Linux backend's D-Bus / service stack). All gio/glib types stay inside
// the .cpp (RULE-01); the threading discipline mirrors X11Backend — one
// mutex serializes every call, providers stay synchronous and bounded, no
// Mirage code creates threads (RULE-03). GIO may run internal worker
// threads inside the library, exactly like libatspi does (M2-03).
//
// Running-instance model: the backend tracks every instance it launched in
// a bounded registry (RULE-07) and additionally scans /proc (bounded) for
// live processes whose executable name matches the desktop entry's Exec
// binary — the same heuristic desktop shells use, because Linux has no
// authoritative application -> process mapping. Foreign (user-started)
// instances are therefore recognized by name; the known limitation is that
// two different desktop entries sharing one binary name cannot be told
// apart, and an instance that renamed or re-exec'd itself after start is
// invisible to the name scan until it is tracked.

#include <memory>
#include <mutex>
#include <string>

#include <mirage/desktop/application_provider.hpp>

namespace mirage::platform::linux_backend {

class ApplicationBackend final : public mirage::desktop::ApplicationProvider {
  public:
    /// Initializes the backend. Returns null when gio-unix support is not
    /// compiled in (stub build) — capability honesty, DEC-015.
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

} // namespace mirage::platform::linux_backend
