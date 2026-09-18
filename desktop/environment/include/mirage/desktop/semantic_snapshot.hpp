#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Index of a node without a parent in SemanticSnapshot::nodes (root node).
inline constexpr std::size_t kNoParent = static_cast<std::size_t>(-1);

/// One interactive (or otherwise relevant) object of a Semantic Snapshot
/// (design doc section 7). `ref` is the snapshot-issued temporary handle
/// ("@e1", "@e2", ...) agents use to address the object; it is stable within
/// one snapshot only and is resolved back to the platform object when the
/// action executes.
struct SemanticNode {
    /// Snapshot-issued handle, e.g. "@e5". Never empty in a valid snapshot.
    std::string ref;
    /// Accessibility role in a stable lowercase vocabulary ("button", "menu",
    /// "treeitem", "editor", ...); backends map platform roles onto it.
    std::string role;
    /// Accessible name (may be empty when the platform exposes none).
    std::string name;
    /// Accessible description, when different from the name.
    std::string description;
    /// Hierarchy: index into SemanticSnapshot::nodes, or kNoParent at roots.
    std::size_t parent = kNoParent;
    /// Screen-space bounds; width/height 0 when the platform provides none.
    WindowGeometry geometry;
    bool focused = false;
    bool enabled = true;
};

/// Structured result of one accessibility pass over a window (design doc
/// section 7). Backends produce it within a node budget (RULE-07); the
/// compact agent-facing text form is rendered deterministically by
/// render_semantic_snapshot().
struct SemanticSnapshot {
    /// Application name, e.g. "Visual Studio Code".
    std::string application;
    /// Titled window the snapshot was taken against.
    std::string window_title;
    std::vector<SemanticNode> nodes;
};

/// Budget for one snapshot generation (RULE-07). A tree that would exceed
/// `max_nodes` is refused — never silently truncated.
struct SemanticSnapshotLimits {
    std::size_t max_nodes = 4096;
};

/// Outcome of one SemanticSnapshot generation.
struct SnapshotOutcome {
    bool ok = false;
    bool cancelled = false;
    SemanticSnapshot snapshot; ///< meaningful only when ok
    ProviderError error;       ///< meaningful only when ok is false
};

/// Renders a snapshot in the compact line-per-node form shown in design doc
/// section 7 ("@e5 button \"Run\" [focused]"). Deterministic and stable: the
/// same snapshot always renders to the same text, node order preserved.
std::string render_semantic_snapshot(const SemanticSnapshot &snapshot);

} // namespace mirage::desktop
