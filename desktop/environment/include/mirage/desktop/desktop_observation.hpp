#pragma once

#include <cstdint>
#include <string>

#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>
#include <mirage/desktop/visual_snapshot.hpp>

namespace mirage::desktop {

/// Pointer position in global desktop coordinates (design doc section 6).
struct PointerState {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// Structured desktop state handed to Mira (design doc section 6). The field
/// set was frozen as schema 1.0 by DEC-005; DEC-016 decision 2 promotes the
/// visual component additively (minor version "1.1"): `visual_snapshot`
/// carries the structured fusion result and `visual_snapshot_ref` carries
/// that generation's scope handle. Breaking changes bump the major version.
struct DesktopObservation {
    std::string active_application;
    std::string active_window;
    WindowGeometry window_geometry;
    bool window_focused = false;
    /// Handle of the focused element inside semantic_snapshot ("" unknown).
    std::string focused_element;
    SemanticSnapshot semantic_snapshot;
    /// Structured visual component (DEC-016 decision 2): regions of the
    /// visual generation this observation carries, in global desktop
    /// coordinates. `scope_ref` is empty when no visual component was
    /// captured, in which case `regions` is empty too.
    VisualSnapshot visual_snapshot;
    /// Scope handle of the visual generation above ("@vs<generation>", e.g.
    /// "@vs1"); empty when the observation carries no visual component.
    std::string visual_snapshot_ref;
    PointerState pointer_state;
    /// Backend-reported environment summary (e.g. "x11/xwayland"); never
    /// carries platform types.
    std::string environment_state;
};

/// Schema tag for observation payloads exchanged with Mira. Bump on any
/// breaking change to DesktopObservation semantics (1.0 frozen by DEC-005;
/// 1.1 adds the visual component, DEC-016 decision 2).
constexpr const char *kObservationSchemaVersion = "1.1";

} // namespace mirage::desktop
