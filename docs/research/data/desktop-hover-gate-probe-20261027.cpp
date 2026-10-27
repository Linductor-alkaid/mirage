// Research fixture for M9-22: one button keeps its identity and hitbox while
// its hover palette changes. This exercises the current guard, not a fix.
#include "../../../integration/mira/src/desktop_stability_guard.hpp"

#include <cstdint>
#include <iostream>

namespace {
mirage::desktop::ImageFrame button(bool hovered) {
    mirage::desktop::ImageFrame frame;
    frame.format = mirage::desktop::ImageFormat::Bgra8;
    frame.width = frame.height = 128;
    frame.stride = 128 * 4;
    frame.pixels.assign(frame.stride * 128, 20);
    for (int y = 0; y < 128; ++y)
        for (int x = 0; x < 128; ++x) {
            const bool inside = x >= 32 && x < 96 && y >= 32 && y < 96;
            const bool glyph = (x >= 52 && x < 76 && y >= 60 && y < 68) ||
                               (x >= 60 && x < 68 && y >= 48 && y < 80);
            const auto value = static_cast<std::uint8_t>(glyph    ? (hovered ? 30 : 230)
                                                         : inside ? (hovered ? 220 : 60)
                                                                  : 20);
            const auto offset =
                static_cast<std::size_t>(y) * frame.stride + static_cast<std::size_t>(x) * 4;
            frame.pixels[offset] = frame.pixels[offset + 1] = frame.pixels[offset + 2] = value;
            frame.pixels[offset + 3] = 255;
        }
    return frame;
}
} // namespace

int main() {
    using namespace mirage::integration::detail;
    const auto before = button(false);
    const auto hover = button(true);
    const auto verdict = assess_target_stability(before, hover, {32, 32, 96, 96});
    std::cout << "same_button_same_hitbox_hover_palette: "
              << (verdict == TargetStability::kChanged ? "rejected" : "stable") << '\n';
    const auto change = mirador::detect_change(stability::sub_view(before, 24, 24, 80, 80),
                                               stability::sub_view(hover, 24, 24, 80, 80), {});
    if (!change)
        return 2;
    std::cout << "crop_change_class=" << static_cast<int>(change.value().classification)
              << " changed_area_ratio=" << change.value().changed_area_ratio << '\n';
    // Zero means the reported false-positive mechanism reproduced. It does
    // not mean the desired future behavior is already implemented.
    return verdict == TargetStability::kChanged ? 0 : 1;
}
