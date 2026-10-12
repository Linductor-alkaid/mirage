// M7-16 / DEC-083: unit contract of the target-region stability guard shared
// by the Wayland shared-input re-verification and the X11 numbered-pointer
// re-verification. Synthetic BGRA8 frames (padded strides included) pin the
// layered semantics: margin-ring animation and same-subject noise proceed, a
// replaced subject, a structural scene change and every unprovable comparison
// reject exactly like the previous zero-tolerance byte comparison.
#include "../../integration/mira/src/desktop_stability_guard.hpp"

#include "../support/test.hpp"

#include <cstdint>
#include <vector>

namespace {
using mirage::desktop::ImageFormat;
using mirage::desktop::ImageFrame;
using mirage::integration::detail::assess_target_stability;
using mirage::integration::detail::TargetRegion;
using mirage::integration::detail::TargetStability;

ImageFrame make_frame(int width, int height, std::uint8_t value, std::size_t stride_pad = 0) {
    ImageFrame frame;
    frame.format = ImageFormat::Bgra8;
    frame.width = width;
    frame.height = height;
    frame.stride = static_cast<std::size_t>(width) * 4 + stride_pad;
    frame.pixels.assign(frame.stride * static_cast<std::size_t>(height), value);
    return frame;
}

void paint(ImageFrame &frame, int x0, int y0, int x1, int y1, std::uint8_t value) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto offset =
                static_cast<std::size_t>(y) * frame.stride + static_cast<std::size_t>(x) * 4;
            frame.pixels[offset] = frame.pixels[offset + 1] = frame.pixels[offset + 2] = value;
            frame.pixels[offset + 3] = 255;
        }
}

/// Structural subject texture over the rect; 4px cells survive the pinned
/// mirador resampling so both the frame fingerprint and the block diffs move.
void checkerboard(ImageFrame &frame, int x0, int y0, int x1, int y1, bool swap = false) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const bool high = ((x / 4 + y / 4) % 2 == 0) != swap;
            paint(frame, x, y, x + 1, y + 1, static_cast<std::uint8_t>(high ? 230 : 25));
        }
}

/// A patterned frame whose core (the region under test) carries the
/// checkerboard subject and whose margin stays uniform background.
ImageFrame patterned_frame(int width, int height, int x0, int y0, int x1, int y1,
                           std::size_t stride_pad = 12) {
    auto frame = make_frame(width, height, 3, stride_pad);
    checkerboard(frame, x0, y0, x1, y1);
    return frame;
}

} // namespace

int main() {
    // Identical frames are stable, regardless of row padding.
    {
        const auto previous = patterned_frame(80, 80, 8, 8, 72, 72);
        const auto current = previous;
        const auto current_tight = patterned_frame(80, 80, 8, 8, 72, 72, 0);
        MIRAGE_CHECK(assess_target_stability(previous, current, {8, 8, 72, 72}) ==
                     TargetStability::kStable);
        MIRAGE_CHECK(assess_target_stability(previous, current_tight, {8, 8, 72, 72}) ==
                     TargetStability::kStable);
    }
    // A layout-preserving uniform brightness shift keeps the subject where it
    // was: the gradient-based frame fingerprint cannot see it and the click
    // point is unchanged, so DEC-083 passes it (the old byte guard did not).
    {
        const auto previous = make_frame(96, 96, 1);
        const auto current = make_frame(96, 96, 31);
        MIRAGE_CHECK(assess_target_stability(previous, current, {16, 16, 80, 80}) ==
                     TargetStability::kStable);
    }
    // A structural change of the whole frame is a scene transition: reject.
    {
        auto previous = patterned_frame(80, 80, 0, 0, 80, 80);
        auto current = previous;
        checkerboard(current, 0, 0, 80, 80, true);
        MIRAGE_CHECK(assess_target_stability(previous, current, {8, 8, 72, 72}) ==
                     TargetStability::kChanged);
    }
    // Animation confined to the margin ring around the target never blocks.
    {
        const auto previous = patterned_frame(80, 80, 8, 8, 72, 72);
        auto current = previous;
        checkerboard(current, 0, 0, 8, 80); // left ring band, outside the core
        MIRAGE_CHECK(assess_target_stability(previous, current, {8, 8, 72, 72}) ==
                     TargetStability::kStable);
        auto right = previous;
        checkerboard(right, 72, 0, 80, 80); // right ring band
        MIRAGE_CHECK(assess_target_stability(previous, right, {8, 8, 72, 72}) ==
                     TargetStability::kStable);
    }
    // Same-subject noise inside the core (a caret blink sized 4x4 at full
    // contrast) moves neither the fingerprint similarity nor the block means
    // beyond the conservative bound: proceed.
    {
        const auto previous = patterned_frame(80, 80, 8, 8, 72, 72);
        auto current = previous;
        paint(current, 40, 40, 44, 44, 255);
        MIRAGE_CHECK(assess_target_stability(previous, current, {8, 8, 72, 72}) ==
                     TargetStability::kStable);
    }
    // Threshold sensitivity: a 2x2 full-contrast flicker is ~0.996 similar.
    {
        const auto previous = patterned_frame(80, 80, 8, 8, 72, 72);
        auto current = previous;
        paint(current, 40, 40, 42, 42, 255);
        MIRAGE_CHECK(assess_target_stability(previous, current, {8, 8, 72, 72}) ==
                     TargetStability::kStable);
    }
    // A replaced subject (half of the core's texture inverted) rejects.
    {
        const auto previous = patterned_frame(80, 80, 8, 8, 72, 72);
        auto current = previous;
        checkerboard(current, 8, 8, 40, 72, true);
        MIRAGE_CHECK(assess_target_stability(previous, current, {8, 8, 72, 72}) ==
                     TargetStability::kChanged);
    }
    // Corner regions clip the margin at the frame edge without leaving the
    // frame or changing the verdicts.
    {
        const auto previous = patterned_frame(96, 96, 0, 0, 32, 32);
        auto ring = previous;
        checkerboard(ring, 32, 0, 40, 40); // clipped right ring band
        MIRAGE_CHECK(assess_target_stability(previous, ring, {0, 0, 32, 32}) ==
                     TargetStability::kStable);
        auto replaced = previous;
        checkerboard(replaced, 8, 8, 24, 24, true); // inside the core
        MIRAGE_CHECK(assess_target_stability(previous, replaced, {0, 0, 32, 32}) ==
                     TargetStability::kChanged);

        const auto corner = patterned_frame(96, 96, 64, 64, 96, 96);
        auto corner_ring = corner;
        checkerboard(corner_ring, 88, 56, 96, 96); // clipped ring, bottom right
        MIRAGE_CHECK(assess_target_stability(corner, corner_ring, {64, 64, 96, 96}) ==
                     TargetStability::kStable);
    }
    // Fail closed: regions with no pixel inside the frame, mismatched
    // dimensions and formats, and empty frames all reject.
    {
        const auto previous = make_frame(96, 96, 1);
        const auto current = make_frame(96, 96, 1);
        MIRAGE_CHECK(assess_target_stability(previous, current, {200, 200, 260, 260}) ==
                     TargetStability::kChanged);
        MIRAGE_CHECK(assess_target_stability(previous, current, {-50, -50, -10, -10}) ==
                     TargetStability::kChanged);
        MIRAGE_CHECK(assess_target_stability(previous, current, {10, 10, 10, 30}) ==
                     TargetStability::kChanged);
        const auto other_height = make_frame(96, 48, 1);
        MIRAGE_CHECK(assess_target_stability(previous, other_height, {8, 8, 72, 72}) ==
                     TargetStability::kChanged);
        auto foreign_format = make_frame(96, 96, 1);
        foreign_format.format = static_cast<ImageFormat>(99);
        MIRAGE_CHECK(assess_target_stability(foreign_format, current, {8, 8, 72, 72}) ==
                     TargetStability::kChanged);
        const auto empty = make_frame(0, 0, 1);
        MIRAGE_CHECK(assess_target_stability(empty, empty, {0, 0, 1, 1}) ==
                     TargetStability::kChanged);
    }
    // The assessment is a pure function of its inputs.
    {
        const auto previous = patterned_frame(80, 80, 8, 8, 72, 72);
        auto replaced = previous;
        checkerboard(replaced, 8, 8, 40, 72, true);
        const auto first = assess_target_stability(previous, replaced, {8, 8, 72, 72});
        const auto second = assess_target_stability(previous, replaced, {8, 8, 72, 72});
        MIRAGE_CHECK(first == second && first == TargetStability::kChanged);
        auto noise = previous;
        paint(noise, 40, 40, 44, 44, 255);
        MIRAGE_CHECK(assess_target_stability(previous, noise, {8, 8, 72, 72}) ==
                     assess_target_stability(previous, noise, {8, 8, 72, 72}));
    }
    return mirage::testing::finish("desktop_stability_guard_test");
}
