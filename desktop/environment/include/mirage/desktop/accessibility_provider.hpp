#pragma once

#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

namespace mirage::desktop {

/// Provider of structured UI information (design doc section 5): turns the
/// platform accessibility tree into a budgeted SemanticSnapshot whose nodes
/// carry snapshot-issued ElementReference handles. The provider reports the
/// current state; snapshot refresh policy (after actions or window changes)
/// belongs to the caller per design doc section 7.
class AccessibilityProvider {
  public:
    virtual ~AccessibilityProvider() = default;

    /// Generates the SemanticSnapshot for one window (by WindowProvider id).
    /// Fails closed with stable ProviderError codes: unknown window
    /// ("not_found"), window without an accessibility tree
    /// ("unsupported_window"), tree larger than the budget
    /// ("snapshot_too_large" — never silently truncated), cancelled. An
    /// empty-but-valid snapshot (no nodes) is a successful result.
    virtual SnapshotOutcome semantic_snapshot(const std::string &window_id,
                                              const SemanticSnapshotLimits &limits,
                                              const CancelToken &cancel) = 0;

    /// Same generation under default limits and without cancellation.
    SnapshotOutcome semantic_snapshot(const std::string &window_id) {
        return semantic_snapshot(window_id, SemanticSnapshotLimits{}, CancelToken{});
    }
};

} // namespace mirage::desktop
