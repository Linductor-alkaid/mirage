#pragma once

#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/element_reference.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/provider_error.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

namespace mirage::desktop {

/// Outcome of one semantic element action.
struct ElementActionOutcome {
    bool ok = false;
    bool cancelled = false;
    ProviderError error; ///< meaningful only when ok is false
};

/// Provider of structured UI information (design doc section 5): turns the
/// platform accessibility tree into a budgeted SemanticSnapshot whose nodes
/// carry snapshot-issued ElementReference handles, and executes semantic
/// element actions addressed by ElementTarget (design doc sections 7 and 9:
/// the semantic path comes before any keyboard/mouse fallback). The provider
/// reports the current state; snapshot refresh policy (after actions or
/// window changes) belongs to the caller per design doc section 7.
class AccessibilityProvider {
  public:
    virtual ~AccessibilityProvider() = default;

    /// Generates the SemanticSnapshot for one window (by WindowProvider id).
    /// Fails closed with stable ProviderError codes: unknown window
    /// ("not_found"), window without an accessibility tree
    /// ("unsupported_window"), tree larger than the budget
    /// ("snapshot_too_large" — never silently truncated), cancelled. An
    /// empty-but-valid snapshot (no nodes) is a successful result. A fresh
    /// snapshot replaces the provider's reference registry: handles from
    /// older snapshots stop resolving ("not_found").
    virtual SnapshotOutcome semantic_snapshot(const std::string &window_id,
                                              const SemanticSnapshotLimits &limits,
                                              const CancelToken &cancel) = 0;

    /// Same generation under default limits and without cancellation.
    SnapshotOutcome semantic_snapshot(const std::string &window_id) {
        return semantic_snapshot(window_id, SemanticSnapshotLimits{}, CancelToken{});
    }

    /// Resolves `target` per the DEC-005 contract order and invokes the
    /// platform semantic activation (the accessibility "activate" of design
    /// doc section 9). Resolution order here: snapshot reference ->
    /// accessibility semantic (role/name) -> accessibility structural path
    /// ("/role/name/..." from an application root, first application that
    /// matches in desktop enumeration order). Visual, spatial and raw hints
    /// are not accessibility-resolvable and fail closed with
    /// "unsupported_hint" (visual resolution is M3). Unresolvable targets
    /// report "not_found" before any action is invoked.
    virtual ElementActionOutcome activate_element(const ElementTarget &target,
                                                  const CancelToken &cancel) = 0;

    /// Resolves `target` like activate_element and replaces the element's
    /// text content (semantic text input, e.g. an editable field). The text
    /// budget follows the InputLimits contract: non-UTF-8 or over-budget
    /// payloads are rejected with "invalid_argument" before the element is
    /// touched.
    virtual ElementActionOutcome set_text(const ElementTarget &target, const std::string &text,
                                          const InputLimits &limits, const CancelToken &cancel) = 0;

    ElementActionOutcome activate_element(const ElementTarget &target) {
        return activate_element(target, CancelToken{});
    }

    ElementActionOutcome set_text(const ElementTarget &target, const std::string &text,
                                  const InputLimits &limits) {
        return set_text(target, text, limits, CancelToken{});
    }

    ElementActionOutcome set_text(const ElementTarget &target, const std::string &text) {
        return set_text(target, text, InputLimits{}, CancelToken{});
    }
};

} // namespace mirage::desktop
