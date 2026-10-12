#pragma once

#include <algorithm>
#include <string>

namespace mirage::testing {
// Scripted model fixtures choose from ModelLayer's provider-visible declarations.
// Fixture action tables and execution/permission assertions retain canonical names.
inline std::string fixture_tool_name(std::string canonical) {
    std::replace(canonical.begin(), canonical.end(), '.', '_');
    return canonical;
}
} // namespace mirage::testing
