#pragma once

#include <string>
#include <vector>

namespace mirage::integration {

/// Mirage-side identity of one Mirador visual backend. Keeps the pinned
/// mirador types out of Mirage's public interface (DEC-003); the integration
/// layer translates to and from mirador contracts internally.
struct VisualBackendIdentity {
    std::string name;
    std::string implementation_version;
    std::string model_id; ///< logical model id; may be empty for model-free backends
    std::string model_revision;
    /// Accepted pixel formats in preference order, e.g. "rgb8", "bgra8",
    /// "nv12". Unknown format names make the identity invalid.
    std::vector<std::string> accepted_formats;
};

/// True when the identity satisfies the pinned mirador capability contract
/// (name/version non-empty, at least one accepted format, every format
/// defined). Thin wrapper over mirador::validate (design doc section 8).
bool validate_backend_identity(const VisualBackendIdentity &identity);

} // namespace mirage::integration
