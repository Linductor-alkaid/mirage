#pragma once

#include <string>
#include <vector>

#include <mirage/desktop/accessibility_provider.hpp>
#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/element_reference.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/provider_error.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>

namespace mirage::desktop {

/// Which hint group decided (or was attempted by) a target resolution, in
/// DEC-005 contract order: snapshot reference -> accessibility -> Mirador
/// cache / OCR / geometry -> (VLM stays out of M3: explicit opt-in only).
/// The reference ring splits by handle kind ("@e" accessibility vs "@v"
/// visual), and the accessibility ring covers semantic and structural hints.
enum class ResolutionRing {
    kAccessibilityReference, ///< "@eN" resolved to a live platform element
    kAccessibilityHint,      ///< semantic role/name or structural path
    kVisualReference,        ///< "@vN" resolved against the visual registry
    kVisualHint,             ///< ocr_text / template_id matched a region
    kSpatial,                ///< anchor bounds + offset
    kRaw,                    ///< raw global point
};

/// Stable name of a ring ("accessibility.reference", "visual.reference",
/// ...); never null. Trace records and logs use it instead of enum dumps.
const char *resolution_ring_name(ResolutionRing ring);

/// One ring attempt in a resolution trace. `resolved` marks the ring that
/// decided the outcome; every failed attempt is a traceable degraded
/// event (DEC-005, DEC-016 decision 6) carrying its stable error.
struct ResolutionStep {
    ResolutionRing ring = ResolutionRing::kRaw;
    bool resolved = false;
    ProviderError error; ///< meaningful when !resolved
};

/// Outcome of one ElementTarget resolution (and execution). `point` is
/// meaningful only when the deciding ring is a pointer ring
/// (kVisualReference / kVisualHint / kSpatial / kRaw); accessibility rings
/// act through the platform semantic activation instead.
struct TargetResolution {
    bool ok = false;
    bool cancelled = false;
    ResolutionRing ring = ResolutionRing::kRaw; ///< deciding ring when ok
    PointerState point;                         ///< global coordinates to act on
    /// Every ring attempt in DEC-005 order, the deciding attempt last;
    /// empty only for the no-hint rejection.
    std::vector<ResolutionStep> steps;
    ProviderError error; ///< meaningful only when !ok
};

/// True when `id` addresses the visual registry ("@v" prefix, DEC-016
/// decision 6). Scope handles ("@vs<generation>") share the prefix and are
/// refused by the registry itself: they name a generation, not a region.
bool is_visual_reference(const std::string &id);

/// Executes desktop actions addressed by an ElementTarget (design doc
/// sections 7-9, DEC-005 resolution order, DEC-016 decision 6): the visual,
/// spatial and raw hint groups that failed closed before M3 resolve here —
/// against the active visual generation — and execute as pointer actions
/// through the InputProvider, while reference ("@e"), semantic and
/// structural hints stay on the accessibility semantic activation.
///
/// Resolution order and fall-through: rings are tried in DEC-005 order and
/// only the hints of the ring under attempt are offered to it (each ring
/// sees a masked target, so cross-ring hint groups never trip a provider's
/// unsupported-hint rejection). A ring whose hints fail to resolve records
/// a traceable degraded step and the next ring's hints are tried; a ring
/// that resolves *decides* — an accessibility action that found its element
/// but failed to execute reports the failure without falling through (the
/// fallback must not silently act somewhere else). An ElementTarget with no
/// hint group is rejected with "invalid_argument" before any side effect.
///
/// Pointer execution: the resolved point is the center of the deciding
/// region's bounds (spatial and raw targets act at their point directly).
/// The click is pointer_move -> left press -> left release, each step
/// observing cancellation before it starts. A cancellation that lands
/// between press and release converges the input state with a best-effort
/// release (design doc section 13: a takeover must not leave a button
/// held) and reports the outcome as cancelled.
///
/// Dependencies may be null; their rings then fail closed with
/// "unsupported_platform" as a degraded step (never a crash, never a
/// silent skip): no accessibility provider keeps reference/semantic/
/// structural targets from resolving, no visual registry keeps "@v" and
/// visual hints from resolving, no input provider keeps any resolution
/// from completing but blocks the injection. The semantic context (the
/// observation the target was formed against) supplies bounds for spatial
/// anchors that reference an accessibility element; visual anchors resolve
/// against the live registry.
class ElementTargetExecutor {
  public:
    /// Does not take ownership; every non-null dependency must outlive the
    /// executor.
    ElementTargetExecutor(AccessibilityProvider *accessibility,
                          VisualReferenceRegistry *visual_registry, InputProvider *input);

    /// Resolves `target` per the contract order and performs the resulting
    /// action: the semantic activation for accessibility rings, a left
    /// pointer click at the resolved point for the pointer rings.
    /// `semantic_context` (may be null) is the SemanticSnapshot the target
    /// was formed against; it is read-only resolution input for spatial
    /// anchors, never mutated.
    [[nodiscard]] TargetResolution click(const ElementTarget &target,
                                         const SemanticSnapshot *semantic_context,
                                         const InputLimits &limits, const CancelToken &cancel);

    TargetResolution click(const ElementTarget &target, const SemanticSnapshot *semantic_context,
                           const InputLimits &limits) {
        return click(target, semantic_context, limits, CancelToken{});
    }

    TargetResolution click(const ElementTarget &target, const SemanticSnapshot *semantic_context) {
        return click(target, semantic_context, InputLimits{}, CancelToken{});
    }

  private:
    AccessibilityProvider *accessibility_;
    VisualReferenceRegistry *visual_registry_;
    InputProvider *input_;
};

} // namespace mirage::desktop
