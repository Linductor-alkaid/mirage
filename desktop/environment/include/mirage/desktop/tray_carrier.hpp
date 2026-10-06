#pragma once

#include <cstddef>
#include <functional>
#include <string>

namespace mirage::desktop {

/// One user action taken on the tray surface (menu click; M5-10, DEC-030).
enum class TrayAction {
    Pause,     ///< pause the tracked task (task.pause)
    Resume,    ///< resume the tracked task (task.resume)
    OpenShell, ///< 快速进入 Mirage: launch the desktop shell binary
    Quit,      ///< request whole-product exit (DEC-045)
};

/// One complete tray presentation state (M5-10, DEC-030): a full snapshot
/// published latest-state-wins by the owning process. `status` is the
/// one-line state (icon tooltip and the menu's header entry, e.g.
/// "Mirage：空闲" / "运行中：clean the cache — Active" / "与 mirage-service
/// 失联"); the booleans enable the corresponding menu entries. Clamped by
/// kMaxStatusBytes (RULE-07).
struct TrayState {
    std::string status;
    bool can_pause = false;
    bool can_resume = false;
    bool can_open_shell = false;

    /// Upper bound for the status line (bytes; RULE-07).
    static constexpr std::size_t kMaxStatusBytes = 256;
};

/// Clamps one tray status line to the state budget (RULE-07): over-budget
/// text is cut at a UTF-8 boundary.
void clamp_tray_status(std::string &status);

/// Inputs the carrier's run() loop consumes (DEC-030). Every callback is
/// bounded, must not throw and must not re-enter the carrier.
struct TrayCarrierContext {
    /// Called once only after the platform accepted indicator registration.
    std::function<void()> on_ready;
    std::string icon_path = {}; // product artwork; empty retains platform default
    /// Loads the newest presentation state (pump thread; called once per
    /// iteration and after wakeup()). Cheap by contract — the owner keeps
    /// the latest state ready.
    std::function<TrayState()> load_state;
    /// Delivers a user action taken on the tray surface (pump thread). The
    /// owner routes it onward; delivery is fire-and-forget.
    std::function<void(TrayAction action)> on_action;
};

/// Platform carrier of the tray indicator surface (M5-10, DEC-030): the
/// notification-area icon with its menu, implemented by the platform
/// backends — Windows through the Shell_NotifyIcon carrier DEC-018 landed
/// (M4-05), its productized menu extension; Linux through the session-bus
/// StatusNotifierItem indicator. A session without the platform's indicator
/// facility (no shell notification area, no StatusNotifierWatcher, session
/// bus absent) has no carrier and the factory returns null — capability
/// honesty, never a degraded carrier.
///
/// Threading contract (DEC-018 ownership model precedent): the icon and its
/// menu are created inside run() on the calling thread and destroyed there
/// before run() returns — the calling thread is an Executor blocking worker
/// chosen by the owner; the carrier never creates threads. wakeup() is the
/// only thread-safe member and only releases the current external wait so
/// the newest state is picked up within one wait slice.
class TrayCarrier {
  public:
    virtual ~TrayCarrier() = default;
    TrayCarrier(const TrayCarrier &) = delete;
    TrayCarrier &operator=(const TrayCarrier &) = delete;

    struct RunReport {
        bool clean = false;
        /// Meaningful only when clean is false (indicator registration
        /// refused, the presentation loop broke).
        std::string diagnostic;
    };

    /// Long-lived presentation loop on the calling thread: registers the
    /// indicator, presents the newest state and delivers menu actions;
    /// returns after `stop_requested` was observed true (within one wait
    /// slice). A surface that cannot be established is a clean=false
    /// return with a diagnostic — never a broken icon.
    virtual RunReport run(const TrayCarrierContext &context,
                          const std::function<bool()> &stop_requested) = 0;

    /// Thread-safe: releases the current external wait inside run(); must
    /// not throw and must be safe before, during and after run().
    virtual void wakeup() = 0;

  protected:
    TrayCarrier() = default;
};

} // namespace mirage::desktop
