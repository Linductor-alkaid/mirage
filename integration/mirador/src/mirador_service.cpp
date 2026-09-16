#include <mirage/integration/mirador_service.hpp>

#include <mirador/backend_info.hpp>
#include <mirador/pixel_format.hpp>

#include <optional>

namespace mirage::integration {
namespace {

std::optional<mirador::PixelFormat> to_mirador_format(const std::string &name) {
    if (name == "gray8") {
        return mirador::PixelFormat::kGray8;
    }
    if (name == "rgb8") {
        return mirador::PixelFormat::kRgb8;
    }
    if (name == "bgr8") {
        return mirador::PixelFormat::kBgr8;
    }
    if (name == "rgba8") {
        return mirador::PixelFormat::kRgba8;
    }
    if (name == "bgra8") {
        return mirador::PixelFormat::kBgra8;
    }
    if (name == "nv12") {
        return mirador::PixelFormat::kNv12;
    }
    return std::nullopt;
}

} // namespace

bool validate_backend_identity(const VisualBackendIdentity &identity) {
    mirador::BackendInfo info;
    info.name = identity.name;
    info.implementation_version = identity.implementation_version;
    info.model_id = identity.model_id;
    info.model_revision = identity.model_revision;
    info.accepted_formats.reserve(identity.accepted_formats.size());
    for (const std::string &format : identity.accepted_formats) {
        const std::optional<mirador::PixelFormat> mapped = to_mirador_format(format);
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
