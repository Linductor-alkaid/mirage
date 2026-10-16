#pragma once

// Shared window-geometry and capture-frame predicates for the desktop tool
// adapters (M3/M7): one copy of the observation-identity equality, the
// overflow-safe containment check and the BGRA frame budget contract that
// every visual/input tool applies before acting on a frame. Internal to the
// integration_mira target — never installed.

#include <cstddef>
#include <cstdint>

#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/screen_provider.hpp>

namespace mirage::integration::detail {

/// Component-wise geometry equality: the observation identity check that
/// ties a cached region to the window it was captured from.
inline bool geometry_equals(const desktop::WindowGeometry &a, const desktop::WindowGeometry &b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

/// True when `inner` has positive extent and lies inside `outer`. The bound
/// arithmetic is overflow-safe; a positive outer extent is implied by the
/// checks rather than assumed separately.
inline bool geometry_contains(const desktop::WindowGeometry &outer,
                              const desktop::WindowGeometry &inner) {
    return inner.width > 0 && inner.height > 0 && inner.x >= outer.x && inner.y >= outer.y &&
           std::int64_t{inner.x} + inner.width <= std::int64_t{outer.x} + outer.width &&
           std::int64_t{inner.y} + inner.height <= std::int64_t{outer.y} + outer.height;
}

/// Capture-frame contract: one BGRA8 frame whose stride covers every row,
/// whose pixel buffer is exactly stride*height, bounded by `budget_bytes`
/// and — when positive — by `max_dimension` per axis.
inline bool valid_image_frame(const desktop::ImageFrame &frame, std::size_t budget_bytes,
                              std::int32_t max_dimension = 0) {
    return frame.format == desktop::ImageFormat::Bgra8 && frame.width > 0 && frame.height > 0 &&
           (max_dimension <= 0 ||
            (frame.width <= max_dimension && frame.height <= max_dimension)) &&
           frame.stride >= static_cast<std::size_t>(frame.width) * 4 &&
           frame.stride <= budget_bytes / static_cast<std::size_t>(frame.height) &&
           frame.pixels.size() == frame.stride * static_cast<std::size_t>(frame.height);
}

} // namespace mirage::integration::detail
