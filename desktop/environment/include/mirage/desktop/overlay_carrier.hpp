#pragma once

#include <functional>
#include <string>

#include <mirage/desktop/overlay_surface.hpp>

namespace mirage::desktop {

/// Inputs the carrier's run() loop consumes (DEC-029). Every callback is
/// bounded, must not throw and must not re-enter the carrier.
struct OverlayCarrierContext {
    /// Loads the newest published frame into `frame`; false means "no new
    /// frame since the previous load" (the carrier keeps showing what it
    /// has). Called on the pump thread only.
    std::function<bool(OverlaySurfaceFrame &frame)> load_frame;
    /// Invoked once per loop iteration before the frame load (pump thread):
    /// the owner revalidates time-based state (a confirmation resolved by
    /// an IPC client must leave the surface) and may publish a fresh frame.
    std::function<void()> on_tick;
    /// Delivers a click made on the overlay surface (pump thread). The
    /// implementation routes it onward; delivery is best effort (the
    /// request may already be decided, DEC-020).
    std::function<void(const OverlayClick &click)> on_click;
};

/// Platform carrier of the Desktop Overlay surface (design doc section 14,
/// DEC-029): the topmost / transparent / click-through window mechanics,
/// implemented by the platform backends (Windows layered windows; Linux
/// X11 shape overlay — Wayland-native sessions have no carrier and the
/// factory returns null, capability honesty).
///
/// Threading contract (DEC-018 ownership model precedent): the surface is
/// created inside run() on the calling thread and destroyed there before
/// run() returns — the calling thread is an Executor blocking worker chosen
/// by the owner (the service); the carrier never creates threads. wakeup()
/// is the only thread-safe member and only releases the current external
/// wait so a concurrent stop is observed within one wait slice.
class OverlayCarrier {
  public:
    virtual ~OverlayCarrier() = default;
    OverlayCarrier(const OverlayCarrier &) = delete;
    OverlayCarrier &operator=(const OverlayCarrier &) = delete;

    struct RunReport {
        bool clean = false;
        /// Meaningful only when clean is false (surface creation failed,
        /// the presentation loop broke or stop was never observed clean).
        std::string diagnostic;
    };

    /// Long-lived presentation loop on the calling thread: creates the
    /// surface, presents the newest frame, delivers clicks and returns
    /// after `stop_requested` was observed true (within one wait slice).
    /// A surface that cannot be created (no interactive desktop, session 0)
    /// is a clean=false return with a diagnostic — never a broken surface.
    virtual RunReport run(const OverlayCarrierContext &context,
                          const std::function<bool()> &stop_requested) = 0;

    /// Thread-safe: releases the current external wait inside run(); must
    /// not throw and must be safe before, during and after run().
    virtual void wakeup() = 0;

  protected:
    OverlayCarrier() = default;
};

} // namespace mirage::desktop
