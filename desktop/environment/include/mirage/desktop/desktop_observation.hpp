#pragma once

#include <cstdint>
#include <string>

namespace mirage::desktop {

/// Window rectangle in global desktop coordinates (design doc section 6).
struct WindowGeometry {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

/// Structured desktop state handed to Mira (design doc sections 5-6).
///
/// The field set is intentionally minimal for the skeleton: the full
/// DesktopObservation contract (Semantic/Visual snapshot types, Element and
/// Visual references, per-provider state) is frozen by the M2 milestone and
/// recorded as a decision before any consumer depends on it.
struct DesktopObservation {
    std::string active_application;
    std::string active_window;
    WindowGeometry window_geometry;
    /// Compact Semantic Snapshot text (design doc section 7); empty until the
    /// Accessibility provider is attached.
    std::string semantic_snapshot;
    /// Visual snapshot reference produced with Mirador (design doc section 8);
    /// empty until the M3 integration lands.
    std::string visual_snapshot_ref;
    bool window_focused = false;
    std::string environment_state;
};

/// Schema tag for observation payloads exchanged with Mira. Bump on any
/// breaking change to DesktopObservation semantics.
constexpr const char* kObservationSchemaVersion = "0.1";

} // namespace mirage::desktop
