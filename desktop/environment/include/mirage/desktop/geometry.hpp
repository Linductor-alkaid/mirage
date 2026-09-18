#pragma once

#include <cstdint>

namespace mirage::desktop {

/// Window rectangle in global desktop coordinates (design doc section 6).
/// Raw pointer positions use the same coordinate space.
struct WindowGeometry {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t width = 0;
    std::int32_t height = 0;
};

} // namespace mirage::desktop
