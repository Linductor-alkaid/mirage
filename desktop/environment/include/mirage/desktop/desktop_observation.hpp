#pragma once

#include <cstdint>
#include <string>

#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

namespace mirage::desktop {

/// Pointer position in global desktop coordinates (design doc section 6).
struct PointerState {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// Structured desktop state handed to Mira (design doc section 6). The field
/// set is frozen as schema 1.0 by DEC-005; additive evolution bumps the minor
/// version, breaking changes the major version. `visual_snapshot_ref` stays
/// empty until the M3 Mirador integration lands.
struct DesktopObservation {
    std::string active_application;
    std::string active_window;
    WindowGeometry window_geometry;
    bool window_focused = false;
    /// Handle of the focused element inside semantic_snapshot ("" unknown).
    std::string focused_element;
    SemanticSnapshot semantic_snapshot;
    std::string visual_snapshot_ref;
    PointerState pointer_state;
    /// Backend-reported environment summary (e.g. "x11/xwayland"); never
    /// carries platform types.
    std::string environment_state;
};

/// Schema tag for observation payloads exchanged with Mira. Bump on any
/// breaking change to DesktopObservation semantics (1.0 frozen by DEC-005).
constexpr const char *kObservationSchemaVersion = "1.0";

} // namespace mirage::desktop
