#pragma once

#include <string>

namespace mirage::desktop {

/// Temporary handle for one interactive object of the current Semantic
/// Snapshot (design doc section 7), e.g. "@e5". Mirage resolves the reference
/// back to the platform object when the action executes.
struct ElementReference {
    std::string id;
};

/// Temporary handle for one Mirador-detected visual object (design doc
/// section 8), e.g. "@v2". Mirage converts it to coordinates and an input
/// action when the agent refers to it.
struct VisualReference {
    std::string id;
};

} // namespace mirage::desktop
