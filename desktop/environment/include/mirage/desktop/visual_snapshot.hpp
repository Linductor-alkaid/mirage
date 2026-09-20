#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Provenance of one visual region (DEC-016 decision 2). The vocabulary maps
/// mirador evidence sources onto the three agent-facing reference forms of
/// design doc section 8: OCR text -> "@vN text ...", template/cache hit ->
/// "@vN icon cache:<id>", detector/geometry -> "@vN geometry ...".
enum class VisualRegionSource {
    /// OCR text evidence.
    kOcr,
    /// Object / UI detection evidence (label carried in `text`).
    kDetector,
    /// Visual-index template or capability-cache hit (`template_id` set).
    kTemplate,
    /// Geometric closed-region proposal (no text of its own).
    kGeometry,
};

/// One agent-visible visual object of a Visual Snapshot (design doc section
/// 8, DEC-016 decision 2). `ref` is the snapshot-issued temporary handle
/// ("@v1", "@v2", ...) the agent uses to address the object, e.g.
/// `click(@v2)`. `bounds` is in global desktop coordinates — the same space
/// as `window_geometry` and `pointer_state` — because the producing fusion
/// targets the display space with a Mirage-supplied transform, so the
/// reference is directly executable as an input action.
struct VisualRegionEntry {
    /// Snapshot-issued handle, e.g. "@v3". Never empty in a valid snapshot.
    std::string ref;
    VisualRegionSource source = VisualRegionSource::kGeometry;
    /// Global desktop coordinates; directly drives input actions.
    WindowGeometry bounds;
    /// OCR text, or the detector label; empty for template/geometry entries
    /// without one.
    std::string text;
    /// Template / capability-cache identifier; non-empty only for
    /// kTemplate entries.
    std::string template_id;
    /// Producer confidence in [0, 1]; 0 when unknown.
    double confidence = 0.0;
};

/// Structured visual result of one perception pass over a captured frame
/// (design doc section 8). mirador fusion output mapped onto the Mirage
/// perception surface (DEC-016 decision 3): accessibility data is not part
/// of it (SemanticSnapshot already owns that surface), pixels and pipeline
/// internals never enter it.
struct VisualSnapshot {
    /// Scope handle of this generation, e.g. "@vs1"; mirrored into
    /// DesktopObservation::visual_snapshot_ref.
    std::string scope_ref;
    std::vector<VisualRegionEntry> regions;
};

/// Budget for one visual snapshot publication (RULE-07, DEC-016 decision 1).
/// A fusion result that would exceed `max_regions` is refused as a whole —
/// never silently truncated. The default matches the pinned mirador fusion
/// output budget.
struct VisualSnapshotLimits {
    std::size_t max_regions = 1024;
};

/// Outcome of one registry publication (DEC-016 decision 1). `snapshot` is
/// meaningful only when ok: it is the published snapshot with refs and scope
/// handle assigned, i.e. exactly what became active.
struct VisualPublishOutcome {
    bool ok = false;
    VisualSnapshot snapshot;
    ProviderError error; ///< meaningful only when !ok
};

/// Renders a visual snapshot in the compact line-per-region form of design
/// doc section 8 ("@v2 icon cache:settings"). Deterministic and stable: the
/// same snapshot always renders to the same text, region order preserved.
std::string render_visual_snapshot(const VisualSnapshot &snapshot);

} // namespace mirage::desktop
