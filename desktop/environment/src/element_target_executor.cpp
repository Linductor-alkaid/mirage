#include <mirage/desktop/element_target_executor.hpp>

namespace mirage::desktop {

namespace {

/// Center of a region's bounds in global desktop coordinates (component
/// sums happen in int64 so wide regions cannot overflow the intermediate).
PointerState bounds_center(const WindowGeometry &bounds) {
    return {static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.x) + bounds.width / 2),
            static_cast<std::int32_t>(static_cast<std::int64_t>(bounds.y) + bounds.height / 2)};
}

/// True when the error means "this ring's hints did not resolve" — the
/// fallback may try the next ring. Any other failure (the element or
/// region was found but the action failed) decides the outcome: falling
/// through would silently act somewhere else.
bool is_resolution_failure(const ProviderError &error) {
    return error.code == "not_found" || error.code == "unsupported_platform" ||
           error.code == "unsupported_hint";
}

/// Appends a degraded attempt to the trace.
void record_step(std::vector<ResolutionStep> &steps, ResolutionRing ring,
                 const ProviderError &error) {
    steps.push_back(ResolutionStep{ring, false, error});
}

} // namespace

const char *resolution_ring_name(ResolutionRing ring) {
    switch (ring) {
    case ResolutionRing::kAccessibilityReference:
        return "accessibility.reference";
    case ResolutionRing::kAccessibilityHint:
        return "accessibility.hint";
    case ResolutionRing::kVisualReference:
        return "visual.reference";
    case ResolutionRing::kVisualHint:
        return "visual.hint";
    case ResolutionRing::kSpatial:
        return "spatial";
    case ResolutionRing::kRaw:
        return "raw";
    }
    return "unknown";
}

bool is_visual_reference(const std::string &id) { return id.rfind("@v", 0) == 0; }

ElementTargetExecutor::ElementTargetExecutor(AccessibilityProvider *accessibility,
                                             VisualReferenceRegistry *visual_registry,
                                             InputProvider *input)
    : accessibility_(accessibility), visual_registry_(visual_registry), input_(input) {}

TargetResolution ElementTargetExecutor::click(const ElementTarget &target,
                                              const SemanticSnapshot *semantic_context,
                                              const InputLimits &limits,
                                              const CancelToken &cancel) {
    TargetResolution outcome;
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = {"cancelled", "click cancelled before resolution"};
        return outcome;
    }
    if (!target.has_any_hint()) {
        outcome.error = {"invalid_argument", "target carries no hint"};
        return outcome;
    }

    // ---- accessibility rings (DEC-005: reference, then semantic/structural)
    // Only accessibility-owned hints are offered: a masked target keeps the
    // "@v"/visual/spatial/raw groups from tripping the provider's
    // unsupported-hint rejection on multi-hint targets.
    const bool accessibility_addressed =
        (!target.reference.id.empty() && !is_visual_reference(target.reference.id)) ||
        !target.semantic.role.empty() || !target.semantic.name.empty() ||
        !target.structural.path.empty();
    if (accessibility_addressed) {
        ElementTarget masked;
        if (!target.reference.id.empty() && !is_visual_reference(target.reference.id)) {
            masked.reference = target.reference;
        }
        masked.semantic = target.semantic;
        masked.structural = target.structural;
        if (accessibility_ == nullptr) {
            record_step(outcome.steps,
                        masked.reference.id.empty() ? ResolutionRing::kAccessibilityHint
                                                    : ResolutionRing::kAccessibilityReference,
                        {"unsupported_platform", "no accessibility provider is bound"});
        } else {
            const ResolutionRing attempted = masked.reference.id.empty()
                                                 ? ResolutionRing::kAccessibilityHint
                                                 : ResolutionRing::kAccessibilityReference;
            const ElementActionOutcome action = accessibility_->activate_element(masked, cancel);
            if (action.cancelled) {
                outcome.cancelled = true;
                outcome.error = action.error;
                return outcome;
            }
            if (action.ok) {
                outcome.ok = true;
                outcome.ring = attempted;
                outcome.steps.push_back(ResolutionStep{outcome.ring, true, {}});
                return outcome;
            }
            record_step(outcome.steps, attempted, action.error);
            if (!is_resolution_failure(action.error)) {
                outcome.error = action.error;
                return outcome;
            }
        }
    }

    // ---- visual reference ring ("@v" handles, DEC-016 decision 6)
    if (!target.reference.id.empty() && is_visual_reference(target.reference.id)) {
        if (visual_registry_ == nullptr) {
            record_step(outcome.steps, ResolutionRing::kVisualReference,
                        {"unsupported_platform", "no visual registry is bound"});
        } else {
            const std::optional<VisualRegionEntry> region =
                visual_registry_->resolve(target.reference.id);
            if (region.has_value()) {
                outcome.ok = true;
                outcome.ring = ResolutionRing::kVisualReference;
                outcome.point = bounds_center(region->bounds);
                outcome.steps.push_back(ResolutionStep{outcome.ring, true, {}});
            } else {
                record_step(outcome.steps, ResolutionRing::kVisualReference,
                            {"not_found", "visual reference is stale or unknown"});
            }
        }
    }

    // ---- visual hint ring (ocr_text, then template_id; first region in
    // publication order wins)
    if (!outcome.ok && (!target.visual.ocr_text.empty() || !target.visual.template_id.empty())) {
        if (visual_registry_ == nullptr) {
            record_step(outcome.steps, ResolutionRing::kVisualHint,
                        {"unsupported_platform", "no visual registry is bound"});
        } else {
            const VisualSnapshot active = visual_registry_->current();
            std::optional<VisualRegionEntry> region;
            if (!target.visual.ocr_text.empty()) {
                for (const VisualRegionEntry &candidate : active.regions) {
                    if (candidate.source == VisualRegionSource::kOcr &&
                        candidate.text == target.visual.ocr_text) {
                        region = candidate;
                        break;
                    }
                }
            }
            if (!region.has_value() && !target.visual.template_id.empty()) {
                for (const VisualRegionEntry &candidate : active.regions) {
                    if (candidate.source == VisualRegionSource::kTemplate &&
                        candidate.template_id == target.visual.template_id) {
                        region = candidate;
                        break;
                    }
                }
            }
            if (region.has_value()) {
                outcome.ok = true;
                outcome.ring = ResolutionRing::kVisualHint;
                outcome.point = bounds_center(region->bounds);
                outcome.steps.push_back(ResolutionStep{outcome.ring, true, {}});
            } else {
                record_step(outcome.steps, ResolutionRing::kVisualHint,
                            {"not_found", "no visual region matches the hint"});
            }
        }
    }

    // ---- spatial ring: anchor bounds + declared offset
    if (!outcome.ok && !target.spatial.relative_to.id.empty()) {
        const std::string &anchor = target.spatial.relative_to.id;
        std::optional<WindowGeometry> anchor_bounds;
        ProviderError anchor_error;
        if (is_visual_reference(anchor)) {
            if (visual_registry_ == nullptr) {
                anchor_error = {"unsupported_platform", "no visual registry is bound"};
            } else if (const std::optional<VisualRegionEntry> region =
                           visual_registry_->resolve(anchor)) {
                anchor_bounds = region->bounds;
            } else {
                anchor_error = {"not_found", "visual anchor is stale or unknown"};
            }
        } else if (semantic_context == nullptr) {
            anchor_error = {"not_found", "no semantic context resolves this anchor"};
        } else {
            for (const SemanticNode &node : semantic_context->nodes) {
                if (node.ref == anchor) {
                    anchor_bounds = node.geometry;
                    break;
                }
            }
            if (!anchor_bounds.has_value()) {
                anchor_error = {"not_found", "anchor is not part of the semantic context"};
            }
        }
        if (anchor_bounds.has_value()) {
            outcome.ok = true;
            outcome.ring = ResolutionRing::kSpatial;
            outcome.point = {static_cast<std::int32_t>(static_cast<std::int64_t>(anchor_bounds->x) +
                                                       target.spatial.dx),
                             static_cast<std::int32_t>(static_cast<std::int64_t>(anchor_bounds->y) +
                                                       target.spatial.dy)};
            outcome.steps.push_back(ResolutionStep{outcome.ring, true, {}});
        } else {
            record_step(outcome.steps, ResolutionRing::kSpatial, anchor_error);
        }
    }

    // ---- raw ring: the last resort acts at the declared global point
    if (!outcome.ok && (target.raw.x != 0 || target.raw.y != 0)) {
        outcome.ok = true;
        outcome.ring = ResolutionRing::kRaw;
        outcome.point = {target.raw.x, target.raw.y};
        outcome.steps.push_back(ResolutionStep{outcome.ring, true, {}});
    }

    if (!outcome.ok) {
        outcome.error = {"not_found", "target does not resolve in any ring"};
        return outcome;
    }

    // ---- pointer execution (the accessibility rings returned above)
    const bool pointer_ring = outcome.ring != ResolutionRing::kAccessibilityReference &&
                              outcome.ring != ResolutionRing::kAccessibilityHint;
    if (!pointer_ring) {
        return outcome;
    }
    if (input_ == nullptr) {
        outcome.ok = false;
        outcome.error = {"unsupported_platform", "no input provider is bound"};
        return outcome;
    }
    if (cancel.cancelled()) {
        outcome.ok = false;
        outcome.cancelled = true;
        outcome.error = {"cancelled", "click cancelled before injection"};
        return outcome;
    }
    const InputOutcome move =
        input_->pointer_move(outcome.point.x, outcome.point.y, limits, cancel);
    if (!move.ok) {
        outcome.ok = false;
        outcome.cancelled = move.cancelled;
        outcome.error = move.error;
        return outcome;
    }
    const InputOutcome press = input_->pointer_button("left", true, limits, cancel);
    if (!press.ok) {
        outcome.ok = false;
        outcome.cancelled = press.cancelled;
        outcome.error = press.error;
        return outcome; // no press landed: the input state stays consistent
    }
    const InputOutcome release = input_->pointer_button("left", false, limits, cancel);
    if (!release.ok) {
        if (release.cancelled) {
            // Converge the input state before reporting the cancellation
            // (design doc section 13): a takeover must not leave the button
            // held, so the release retries on a token no one can cancel.
            static_cast<void>(input_->pointer_button("left", false, limits, CancelToken{}));
        }
        outcome.ok = false;
        outcome.cancelled = release.cancelled;
        outcome.error = release.error;
        return outcome;
    }
    return outcome;
}

} // namespace mirage::desktop
