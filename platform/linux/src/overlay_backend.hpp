#pragma once

// Private Linux overlay frontend (M5-09, DEC-029): the X11/XWayland shape
// overlay carrier. Lives in src/ so no X11 header ever appears in a public
// include path (RULE-01); the implementation hides every X type inside the
// .cpp.
//
// Carrier decision (DEC-029): one override-redirect topmost window whose
// content shape (XShape bounding) is the union of the drawn primitives —
// transparency by shape, no compositor dependency (Xvfb and CI can verify).
// The input shape covers only the confirmation buttons, so the surface is
// click-through everywhere else; per-pixel alpha is a compositor-dependent
// evolution this build does not claim.
//
// Threading discipline: the carrier holds its own Display connection; every
// X call happens inside run() on the calling thread (an Executor blocking
// worker chosen by the owner, EXEC-04). wakeup() is the only thread-safe
// member: it writes one byte into a self-pipe that the pump's poll includes,
// releasing the external wait (the blocking-I/O card's wakeup contract).

#include <memory>
#include <string>

#include <mirage/desktop/overlay_carrier.hpp>

namespace mirage::platform::linux_backend {

class X11OverlayCarrier final : public mirage::desktop::OverlayCarrier {
  public:
    /// Opens the overlay's own X connection (empty name = $DISPLAY). Null
    /// when the connection cannot be established — a Wayland-native session
    /// without XWayland, or a dead display — so callers expose no overlay
    /// capability at all (capability honesty, DEC-015 precedent).
    static std::unique_ptr<X11OverlayCarrier> open(const std::string &display_name);

    ~X11OverlayCarrier() override;
    X11OverlayCarrier(const X11OverlayCarrier &) = delete;
    X11OverlayCarrier &operator=(const X11OverlayCarrier &) = delete;

    mirage::desktop::OverlayCarrier::RunReport
    run(const mirage::desktop::OverlayCarrierContext &context,
        const std::function<bool()> &stop_requested) override;

    void wakeup() override;

    /// X connection and per-run window state; all X types stay in the .cpp.
    /// Named from the file-local compose helpers, which is why it is public
    /// (the X11Backend::XConnection precedent).
    struct Surface;

  private:
    X11OverlayCarrier() = default;

    std::unique_ptr<Surface> surface_;
};

/// Public factory (declared in the platform's public header, defined here
/// and in overlay_backend_stub.cpp): null without X11/Xshape support or
/// without an X connection (DEC-029 decision 4).
std::unique_ptr<mirage::desktop::OverlayCarrier>
open_overlay_carrier(const std::string &display_name);

} // namespace mirage::platform::linux_backend
