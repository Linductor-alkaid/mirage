#pragma once

// Internal helper of the model layer (not installed): reads one environment
// variable's value. Split per platform because MSVC raises the CRT getenv as
// a warnings-as-errors deprecation (C4996) that MinGW never sees — the same
// toolchain fact runtime/persistence records for its path resolution
// (M4-06).

#include <string>

namespace mirage::integration::detail {

/// The variable's value; empty when unset or empty (both mean "no
/// credential" to the resolver, which then fails closed).
std::string read_environment_value(const std::string &name);

} // namespace mirage::integration::detail
