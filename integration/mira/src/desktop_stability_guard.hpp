#ifndef MIRAGE_INTEGRATION_DESKTOP_STABILITY_GUARD_HPP
#define MIRAGE_INTEGRATION_DESKTOP_STABILITY_GUARD_HPP

// Historical DEC-083 diagnostic only. DEC-117 removed this heuristic from all
// ordinary input paths. Research fixtures retain it to reproduce the old policy.
//
// M7-16 / DEC-082 replaces the zero-tolerance byte comparison inside the
// target rectangle with layered mirador change detection: animation confined
// to the rectangle's edge zone never blocks, and changes inside the rectangle
// only block when the subject's grayscale fingerprint moved beyond a
// conservative similarity bound (caret blink and launch pulses pass; a
// replaced control does not). Any detector error, global change, or
// unmeasurable comparison fails closed to kChanged — the rejection semantics
// are identical to the previous byte comparison.

#include <mirador/change_detection.hpp>
#include <mirador/image_view.hpp>
#include <mirador/patch_fingerprint.hpp>

#include <mirage/desktop/screen_provider.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace mirage::integration::detail {

/// Decision of one target-region stability assessment.
enum class TargetStability {
    kStable,  ///< the subject inside the target region is unchanged; input may proceed
    kChanged, ///< the target moved, was replaced, or stability is unprovable; reject
};

/// Native-pixel rectangle of one input target; right/bottom are exclusive.
struct TargetRegion {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

namespace stability {

/// Width of the ring around the target rectangle where animation is ignored
/// entirely (notification banners sliding past an edge, hover glows). The ring
/// also feeds the change detector a little context so a replaced subject at
/// the rectangle border is still detected.
constexpr int kMarginPx = 8;
/// Side of the grayscale subject fingerprints compared in the core region.
constexpr int kFingerprintSide = 32;
/// Minimum fingerprint similarity (1 - mean absolute luma delta / 255) for a
/// changed core to count as the same subject. A full icon replacement drops
/// far below this; a caret blink or a launch pulse stays near 1.0.
constexpr double kSubjectSimilarity = 0.98;
/// Upper bound handed to the mirador fingerprint allocator (gray intermediate
/// plus thumbnail); bounded regardless of the core region's size (RULE-06).
constexpr std::int64_t kFingerprintBudgetBytes = 4 << 20;

inline mirador::ImageView frame_view(const desktop::ImageFrame &frame) {
    mirador::ImageView view;
    view.data = reinterpret_cast<const std::byte *>(frame.pixels.data());
    view.width = frame.width;
    view.height = frame.height;
    view.row_stride_bytes = static_cast<std::int64_t>(frame.stride);
    view.format = mirador::PixelFormat::kBgra8;
    return view;
}

/// Sub-rectangle view of one frame; shares the frame's stride, no copy.
inline mirador::ImageView sub_view(const desktop::ImageFrame &frame, int left, int top, int width,
                                   int height) {
    auto view = frame_view(frame);
    view.data +=
        static_cast<std::ptrdiff_t>(top) * frame.stride + static_cast<std::ptrdiff_t>(left) * 4;
    view.width = width;
    view.height = height;
    return view;
}

inline bool regions_intersect(const mirador::RectI &a, const mirador::RectI &b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}

} // namespace stability

/// Compares the target region of two frames of the same source and decides
/// whether an input aimed at it may proceed. Both frames must be valid BGRA8
/// frames of identical dimensions (callers already enforce this). The result
/// is deterministic; internal buffers are bounded by mirador budgets.
inline TargetStability assess_target_stability(const desktop::ImageFrame &previous,
                                               const desktop::ImageFrame &current,
                                               const TargetRegion &region) {
    using namespace stability;
    if (previous.format != desktop::ImageFormat::Bgra8 ||
        current.format != desktop::ImageFormat::Bgra8 || previous.width != current.width ||
        previous.height != current.height || previous.width <= 0 || previous.height <= 0)
        return TargetStability::kChanged;
    TargetRegion clipped{std::max(region.left, 0), std::max(region.top, 0),
                         std::min(region.right, previous.width),
                         std::min(region.bottom, previous.height)};
    if (clipped.right - clipped.left <= 0 || clipped.bottom - clipped.top <= 0)
        return TargetStability::kChanged;
    const int pad_left = std::max(clipped.left - kMarginPx, 0);
    const int pad_top = std::max(clipped.top - kMarginPx, 0);
    const int pad_right = std::min(clipped.right + kMarginPx, previous.width);
    const int pad_bottom = std::min(clipped.bottom + kMarginPx, previous.height);
    const int pad_width = pad_right - pad_left;
    const int pad_height = pad_bottom - pad_top;

    // Layered change detection over the padded rectangle. kNone clears the
    // target outright; kGlobal means the scene transitioned and the stale
    // observation must not drive input.
    mirador::ChangeDetectionParams params;
    const auto change =
        detect_change(sub_view(previous, pad_left, pad_top, pad_width, pad_height),
                      sub_view(current, pad_left, pad_top, pad_width, pad_height), params);
    if (!change)
        return TargetStability::kChanged;
    const auto &report = change.value();
    if (report.classification == mirador::ChangeClassification::kNone)
        return TargetStability::kStable;
    if (report.classification != mirador::ChangeClassification::kPartial)
        return TargetStability::kChanged;

    // Changes confined to the edge ring never touch the subject the input is
    // aimed at; the rectangle core is what the pointer will land on.
    const mirador::RectI core{clipped.left - pad_left, clipped.top - pad_top,
                              clipped.right - clipped.left, clipped.bottom - clipped.top};
    const bool core_touched = std::any_of(
        report.changed_regions.begin(), report.changed_regions.end(),
        [&](const mirador::RectI &changed) { return regions_intersect(changed, core); });
    if (!core_touched)
        return TargetStability::kStable;

    // The core changed: the same subject may still sit there (caret blink,
    // launch pulse, spinner). Compare normalized grayscale fingerprints of the
    // core; a replaced control diverges well beyond the similarity bound.
    mirador::PatchFingerprintParams fingerprint_params;
    fingerprint_params.thumb_side = kFingerprintSide;
    const auto before = make_visual_patch_fingerprint(
        sub_view(previous, clipped.left, clipped.top, core.width, core.height), fingerprint_params,
        kFingerprintBudgetBytes);
    const auto after = make_visual_patch_fingerprint(
        sub_view(current, clipped.left, clipped.top, core.width, core.height), fingerprint_params,
        kFingerprintBudgetBytes);
    if (!before || !after ||
        before.value().thumbnail_gray.size() != after.value().thumbnail_gray.size() ||
        before.value().thumbnail_gray.empty())
        return TargetStability::kChanged;
    const auto &a = before.value().thumbnail_gray;
    const auto &b = after.value().thumbnail_gray;
    std::uint64_t delta = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto x = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(a[i]));
        const auto y = static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(b[i]));
        delta += x > y ? x - y : y - x;
    }
    const double similarity =
        1.0 - static_cast<double>(delta) / (static_cast<double>(a.size()) * 255.0);
    return similarity >= kSubjectSimilarity ? TargetStability::kStable : TargetStability::kChanged;
}

} // namespace mirage::integration::detail

#endif
