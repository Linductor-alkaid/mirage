#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include <mirage/desktop/provider_error.hpp>
#include <mirage/desktop/visual_snapshot.hpp>

namespace mirage::desktop {

/// Registry of the active Visual Snapshot's references (DEC-016 decision 1).
/// Mirrors the Element Reference registry discipline (DEC-005 / @eN): every
/// publication replaces the active set as a whole, handles are numbered
/// deterministically from "@v1" within one snapshot, and resolving a stale
/// (replaced) handle reports not_found.
///
/// Thread-safety contract: internally synchronized (mutex-guarded immutable
/// set swap, same shape as the platform backends' reference registries).
/// Publication happens on the owning visual session's serial execution
/// context; resolution may run on the action execution path. A reader always
/// observes one complete snapshot — never a mix of old and new entries.
///
/// Capacity (RULE-07): a snapshot exceeding `limits.max_regions` is refused
/// as a whole (`snapshot_too_large`); the previously active set stays intact
/// and nothing is truncated.
class VisualReferenceRegistry {
  public:
    /// Publishes `snapshot` as the new active generation: renumbers its
    /// regions "@v1..@vN" in region order, assigns the next scope handle
    /// ("@vs<generation>", starting at "@vs1"), and replaces the active set
    /// atomically. The outcome carries the published snapshot (with refs and
    /// scope assigned) so callers hand out exactly what became active.
    /// Errors: `snapshot_too_large` when the region count exceeds the
    /// budget — the active set is unchanged. Never throws.
    [[nodiscard]] VisualPublishOutcome publish(const VisualSnapshot &snapshot,
                                               const VisualSnapshotLimits &limits = {});

    /// Resolves a handle ("@v3") against the active set. Empty result for a
    /// stale or unknown handle: the caller reports not_found as a traceable
    /// degraded-resolution event (DEC-016 decision 6).
    [[nodiscard]] std::optional<VisualRegionEntry> resolve(const std::string &ref) const;

    /// The active snapshot as published (scope and refs assigned); returned
    /// by value — publications swap the whole set, never mutate in place.
    [[nodiscard]] VisualSnapshot current() const;

    /// Number of active entries.
    [[nodiscard]] std::size_t size() const;

    /// Drops the active set (used on session teardown); subsequent resolves
    /// report not_found until the next publish. The generation counter is
    /// not reset.
    void clear();

  private:
    /// The active published set; swapped wholesale on publish (decision 1:
    /// readers see the old or the new complete snapshot).
    VisualSnapshot active_;
    mutable std::mutex mutex_;
    std::uint64_t generation_ = 0;
};

} // namespace mirage::desktop
