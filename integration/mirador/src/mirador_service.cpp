#include <mirage/integration/mirador_service.hpp>

#include <mirador/backend_info.hpp>

#include "pixel_format_names.hpp"

#include <optional>

namespace mirage::integration {

bool validate_backend_identity(const VisualBackendIdentity &identity) {
    mirador::BackendInfo info;
    info.name = identity.name;
    info.implementation_version = identity.implementation_version;
    info.model_id = identity.model_id;
    info.model_revision = identity.model_revision;
    info.accepted_formats.reserve(identity.accepted_formats.size());
    for (const std::string &format : identity.accepted_formats) {
        const std::optional<mirador::PixelFormat> mapped = detail::to_mirador_format(format);
        if (!mapped) {
            // An unknown format name is a Mirage-side translation failure:
            // the identity cannot reach mirador in a well-formed shape.
            return false;
        }
        info.accepted_formats.push_back(*mapped);
    }
    return mirador::validate(info).ok();
}

} // namespace mirage::integration
