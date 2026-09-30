#pragma once

// Private Linux tray frontend (M5-10, DEC-030): the session-bus
// StatusNotifierItem indicator carrier. Lives in src/ so no gio/glib type
// ever appears in a public include path (RULE-01); the implementation hides
// every GLib type inside the .cpp.
//
// Carrier decision (DEC-030 decision 4): the tray indicator is a
// org.kde.StatusNotifierItem exported over the session bus (the AppIndicator
// protocol family — the one modern Linux indicator standard), with the
// minimal com.canonical.dbusmenu server for its context menu. It rides the
// gio-unix-2.0 optional dependency family the GIO application/notification
// frontends already use (DEC-015 decision 3) — no GTK, no libayatana.
// Sessions without a session bus or without a StatusNotifierWatcher host
// (e.g. stock GNOME without the AppIndicator extension) have no carrier:
// open() returns null, the tray process reports and exits (capability
// honesty, never a degraded carrier).
//
// Threading discipline: the exported objects and the GLib main loop live on
// the calling thread (an Executor blocking worker chosen by the owner); the
// loop is the platform event loop encapsulated in the Platform Backend
// (AGENTS.md rule 2). wakeup() is the only thread-safe member: it invokes a
// refresh callback on the loop's context, releasing the external wait.

#include <atomic>
#include <memory>
#include <string>

#include <mirage/desktop/tray_carrier.hpp>

namespace mirage::platform::linux_backend {

class GioTrayCarrier final : public mirage::desktop::TrayCarrier {
  public:
    /// Connects to the session bus and requires a StatusNotifierWatcher
    /// host (fail closed: null without either — capability honesty,
    /// DEC-015 precedent).
    static std::unique_ptr<GioTrayCarrier> open();

    ~GioTrayCarrier() override;
    GioTrayCarrier(const GioTrayCarrier &) = delete;
    GioTrayCarrier &operator=(const GioTrayCarrier &) = delete;

    mirage::desktop::TrayCarrier::RunReport
    run(const mirage::desktop::TrayCarrierContext &context,
        const std::function<bool()> &stop_requested) override;

    void wakeup() override;

    /// Bus connection and per-run exported objects; all gio types stay in
    /// the .cpp definition. Declared public because the file-local vtable
    /// callbacks name it (the X11Backend::XConnection precedent).
    struct Surface;

  private:
    GioTrayCarrier() = default;

    std::unique_ptr<Surface> surface_;
    /// The run()'s Surface (as void*), published only while the loop runs;
    /// wakeup() invokes a refresh on its context. void* keeps the gio types
    /// out of this header (RULE-01).
    std::atomic<void *> wakeup_target_{nullptr};
};

/// Public factory (declared in the platform's public header, defined here
/// and in tray_backend_stub.cpp): null without gio support, a session bus
/// or an indicator host (DEC-030 decision 4).
std::unique_ptr<mirage::desktop::TrayCarrier> open_tray_carrier();

} // namespace mirage::platform::linux_backend
