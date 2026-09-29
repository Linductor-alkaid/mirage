#pragma once

// Private Windows overlay frontend (M5-09, DEC-029): the layered-window
// overlay carrier. Lives in src/ so no Win32 header ever appears in a
// public include path (RULE-01); the implementation hides every Win32 type
// inside the .cpp.
//
// Carrier decision (DEC-029 decision 3): two topmost layered windows on the
// pump thread — a full-virtual-screen visual window with WS_EX_TRANSPARENT
// (whole-surface cross-process click-through, per-pixel alpha through
// UpdateLayeredWindow) and an interaction window that exists only while a
// confirmation is shown, positioned exactly over the confirm banner; its
// transparent pixels pass hits through while the buttons stay clickable
// (layered hit testing). HTTRANSPARENT is deliberately not used: it only
// forwards within one thread and cannot carry cross-process pass-through.

#include <atomic>
#include <memory>

#include <mirage/desktop/overlay_carrier.hpp>

namespace mirage::platform::windows_backend {

class Win32OverlayCarrier final : public mirage::desktop::OverlayCarrier {
  public:
    /// Probes the interactive desktop (same discipline as the Win32
    /// frontend's open(), DEC-017 decision 3). Null when the session has no
    /// display — capability honesty, never a degraded carrier.
    static std::unique_ptr<Win32OverlayCarrier> open();

    ~Win32OverlayCarrier() override;
    Win32OverlayCarrier(const Win32OverlayCarrier &) = delete;
    Win32OverlayCarrier &operator=(const Win32OverlayCarrier &) = delete;

    mirage::desktop::OverlayCarrier::RunReport
    run(const mirage::desktop::OverlayCarrierContext &context,
        const std::function<bool()> &stop_requested) override;

    void wakeup() override;

  private:
    Win32OverlayCarrier();

    /// Per-run window and presentation state; all Win32 types stay in the
    /// .cpp definition (RULE-01). Declared here because C++ requires a
    /// nested type to be declared before its out-of-class definition — the
    /// member is still an opaque pointer in every other translation unit.
    struct State;
    void *state_ = nullptr;
    /// The visual window handle (as void*), published only while a healthy
    /// presentation loop runs; wakeup() posts WM_NULL to it. void* keeps
    /// the Win32 type out of this header (RULE-01).
    std::atomic<void *> wakeup_target_{nullptr};
};

/// Public factory (declared in the platform's public header): the Windows
/// overlay carrier, or null when the session has no interactive display.
std::unique_ptr<mirage::desktop::OverlayCarrier> open_overlay_carrier();

} // namespace mirage::platform::windows_backend
