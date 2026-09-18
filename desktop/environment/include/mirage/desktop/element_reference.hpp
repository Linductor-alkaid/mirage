#pragma once

#include <string>

#include <mirage/desktop/geometry.hpp>

namespace mirage::desktop {

/// Temporary handle for one interactive object of the current Semantic
/// Snapshot (design doc section 7), e.g. "@e5". Mirage resolves the reference
/// back to the platform object when the action executes.
struct ElementReference {
    std::string id;
};

/// Temporary handle for one Mirador-detected visual object (design doc
/// section 8), e.g. "@v2". Mirage converts it to coordinates and an input
/// action when the agent refers to it. Visual resolution lands with M3.
struct VisualReference {
    std::string id;
};

/// Semantic hint: match by accessibility role and/or accessible name. Either
/// field may be empty, but not both at once.
struct SemanticHint {
    std::string role;
    std::string name;
};

/// Structural hint: accessibility path from the window root, e.g.
/// "/menu/file/open" (platform-shaped; backends document their form).
struct StructuralHint {
    std::string path;
};

/// Visual hints (resolved from M3 onward; empty groups fail closed before
/// then): text observed by OCR, or a Mirador template/cache identifier.
struct VisualHint {
    std::string ocr_text;
    std::string template_id;
};

/// Spatial hint: position relative to another element's bounds. `dx`/`dy` are
/// offsets in global desktop coordinates from `relative_to`'s top-left corner.
/// A spatial hint is formed only by a non-empty `relative_to` reference; a
/// raw offset without an anchor element is not a hint.
struct SpatialHint {
    ElementReference relative_to;
    std::int32_t dx = 0;
    std::int32_t dy = 0;
};

/// Raw hint: a point in global desktop coordinates. Last resort — it bypasses
/// every structural check and is only valid while the layout it was taken
/// from still holds.
struct RawPointHint {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// What a desktop action is addressed with (design doc sections 5, 7, 9).
///
/// An ElementTarget combines optional hint groups instead of carrying a
/// single selector field: the resolver picks the strongest group available at
/// execution time. The contract resolution order (DEC-005) is:
///
///   snapshot reference -> accessibility (semantic, then structural)
///   -> Mirador cache / OCR / geometry (M3) -> VLM (explicit opt-in only)
///
/// Every degraded resolution produces a traceable event; the raw point hint
/// is tried last. An ElementTarget with no hint group set is invalid and
/// rejected before any side effect.
struct ElementTarget {
    ElementReference reference;
    SemanticHint semantic;
    StructuralHint structural;
    VisualHint visual;
    SpatialHint spatial;
    RawPointHint raw;

    /// True when at least one hint group is set (non-empty string, or — for
    /// spatial/raw — an explicitly meaningful value).
    bool has_any_hint() const;
    /// True when more than one hint group is set; resolvers use this to
    /// report which group decided the outcome.
    int hint_count() const;
};

} // namespace mirage::desktop
