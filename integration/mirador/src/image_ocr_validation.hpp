#pragma once
#include <cmath>
#include <mirador/ocr_backend.hpp>
#include <mirage/desktop/input_provider.hpp>

namespace mirage::integration::detail {
// Product boundary validation; diagnostics contain no recognized text.
inline const char *invalid_image_text(const mirador::TextRegion &r, int width, int height) {
    const auto &b = r.bounds;
    if (r.utf8_text.size() > 1024 || !desktop::is_valid_utf8(r.utf8_text))
        return "invalid_result_text";
    if (!std::isfinite(r.confidence) || r.confidence < 0 || r.confidence > 1)
        return "invalid_result_confidence";
    if (!std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.width) ||
        !std::isfinite(b.height) || b.x < 0 || b.y < 0 || b.width <= 0 || b.height <= 0 ||
        double(b.x) + b.width > width || double(b.y) + b.height > height)
        return "invalid_result_geometry";
    return nullptr;
}
} // namespace mirage::integration::detail
