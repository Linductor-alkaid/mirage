#pragma once

#include <mirador/semantic_snapshot.hpp>

#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/desktop/visual_snapshot.hpp>

namespace mirage::integration {

/// Maps one pinned mirador fusion snapshot onto the pinned-free desktop
/// VisualSnapshot (DEC-016 decisions 2 and 3; the pinned boundary is the
/// only place mirador types may appear, DEC-003). The fusion must target
/// the kDisplay space (the session adapter fixes it, DEC-016 decision 3):
/// the mapped bounds are then already global desktop coordinates and the
/// registry entries drive input actions directly.
///
/// The mapping is total and deterministic — every region maps, the same
/// fusion always maps to the same snapshot:
///
/// - Provenance precedence (first matching bit wins): kOcr -> kOcr; then
///   kTemplate with a non-empty label -> kTemplate (`template_id` carries
///   the label, the cache/template identifier the producer resolves
///   against); then kDetector -> kDetector; everything else — including
///   kExternal-only, which DEC-016 pipelines never produce because
///   accessibility is not a fusion input — maps to kGeometry.
/// - `text`: the fusion text for kOcr / kTemplate (label as fallback), the
///   label for kDetector / kGeometry (text as fallback) — the agent-facing
///   line forms of design doc section 8 stay filled.
/// - `bounds`: each component rounds half away from zero to int32.
/// - `confidence`: the fusion value widened to double.
///
/// `ref` and `scope_ref` stay empty: the registry assigns them at publish.
desktop::VisualSnapshot to_visual_snapshot(const mirador::SemanticSnapshot &fusion);

/// Maps and publishes in one step: `to_visual_snapshot` followed by
/// `VisualReferenceRegistry::publish`, which renumbers the regions
/// "@v1..@vN", assigns the next scope handle and replaces the active set
/// atomically. Errors: the registry's (`snapshot_too_large` — the active
/// set stays intact, nothing is truncated).
desktop::VisualPublishOutcome
publish_visual_snapshot(const mirador::SemanticSnapshot &fusion,
                        desktop::VisualReferenceRegistry &registry,
                        const desktop::VisualSnapshotLimits &limits = {});

} // namespace mirage::integration
