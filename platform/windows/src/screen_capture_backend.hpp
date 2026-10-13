#pragma once

// Private tiered screen capture engine of the Windows backend (M7-10,
// DEC-086). Lives in src/ so no D3D/DXGI/WinRT type ever appears in a public
// include path (RULE-01): the Pimpl boundary keeps every such type inside
// screen_capture_backend.cpp.
//
// Tier order (DEC-086): DXGI Desktop Duplication first (hardware path), the
// Windows.Graphics.Capture API second (survives duplication occupancy and
// tone-maps HDR), GDI BitBlt last (the DEC-017 decision 6 first version,
// which also stays the only path for virtual-screen-spanning rectangles and
// DPI-virtualized processes). Every tier produces the same Bgra8,
// top-down, root-frame pixels the M2-02 contract defines: the on-screen
// content at the requested rectangle, overlapping content included.
//
// Threading discipline (DEC-017 decision 3): the engine holds no lock and
// creates no thread; calls are serialized by the owning Win32Backend mutex
// and run on the consumer's Executor blocking worker. Every capture is a
// fresh, stateless platform session (per-call duplication / frame pool), so
// a failed or torn-down session never leaks into the next call; the only
// cross-call state (the D3D11 device and one staging texture) is D3D-lifetime
// only and carries no platform session.

#include <memory>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/screen_provider.hpp>

namespace mirage::platform::windows_backend {

class ScreenCaptureEngine {
  public:
    /// Capture implementation of one call. `Auto` walks the DEC-086 tier
    /// chain (gated, see the engine comments); the explicit tiers pin one
    /// implementation — a diagnostic escape hatch for per-tier acceptance
    /// evidence, never set by product code.
    enum class Tier { Auto, Dxgi, GraphicsCapture, BitBlt };

    /// Always succeeds: the engine is cheap state; every platform failure
    /// surfaces per call as a failed CaptureOutcome, never at construction.
    static std::unique_ptr<ScreenCaptureEngine> create();

    ~ScreenCaptureEngine();
    ScreenCaptureEngine(const ScreenCaptureEngine &) = delete;
    ScreenCaptureEngine &operator=(const ScreenCaptureEngine &) = delete;

    /// Opaque: defined (and completed) only inside screen_capture_backend.cpp,
    /// where the D3D/DXGI/WinRT types live. Public so the file-local tier
    /// helpers can take it by reference without making them members.
    struct Impl;

    /// Captures `rect` (provider-space global desktop coordinates, the same
    /// space list_displays / list_windows report) with the selected tier.
    /// Synchronous and bounded; budgets refuse before any capture work and
    /// cancellation is observed before and inside every tier's waits.
    mirage::desktop::CaptureOutcome capture_rect(const mirage::desktop::WindowGeometry &rect,
                                                 const mirage::desktop::CaptureLimits &limits,
                                                 const mirage::desktop::CancelToken &cancel,
                                                 Tier tier = Tier::Auto);

  private:
    ScreenCaptureEngine();
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::platform::windows_backend
