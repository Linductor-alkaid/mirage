#pragma once

// Internal helper shared by the mirador integration's translation units:
// maps the wire-facing format names of VisualBackendIdentity onto the pinned
// mirador pixel format. Unknown names yield nullopt — a translation failure
// on the Mirage side, reported as an invalid identity.

#include <mirador/pixel_format.hpp>

#include <optional>
#include <string>

namespace mirage::integration::detail {

[[nodiscard]] inline std::optional<mirador::PixelFormat>
to_mirador_format(const std::string &name) {
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

} // namespace mirage::integration::detail
