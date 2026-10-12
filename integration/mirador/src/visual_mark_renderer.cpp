#include "visual_mark_renderer.hpp"
#include <cmath>
#include <cstring>
#include <limits>
#include <mirador/color_convert.hpp>
#include <mirador/crop.hpp>
#include <mirador/set_of_mark.hpp>

namespace mirage::integration::detail {
mirador::Result<void> render_marks(const VisualAnalysisRequest &request,
                                   VisualAnalysisResult &result,
                                   const mirador::ExecutionContext &context) {
    if (!result.snapshot || !request.display_transform)
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "marks require display fusion"};
    const auto roi = request.mark_roi.value_or(
        mirador::RectI{0, 0, request.frame.image.width, request.frame.image.height});
    auto cropped = mirador::crop(request.frame.image, roi, 8u << 20);
    if (!cropped)
        return cropped.status();
    const auto to_pixels = mirador::inverse(*request.display_transform);
    if (!to_pixels)
        return to_pixels.status();
    const auto to_crop = mirador::compose(
        to_pixels.value(),
        mirador::make_crop({static_cast<float>(roi.x), static_cast<float>(roi.y),
                            static_cast<float>(roi.width), static_cast<float>(roi.height)},
                           mirador::CoordinateSpaceId::kOriented,
                           mirador::CoordinateSpaceId::kCropped));
    if (!to_crop)
        return to_crop.status();
    auto local = *result.snapshot;
    local.coordinate_space = mirador::CoordinateSpaceId::kCropped;
    for (auto &region : local.regions) {
        region.bounds = mirador::transform_rect(to_crop.value(), region.bounds);
        region.anchor = mirador::transform_point(to_crop.value(), region.anchor);
    }
    mirador::SoMRenderOptions options;
    options.max_bytes = 8u << 20;
    options.max_marks = 128;
    auto rendered = mirador::render_set_of_mark(cropped.value().view(), local, options, context);
    if (!rendered)
        return rendered.status();
    if (!mirador::is_generation_current(local, rendered.value().generation))
        return mirador::Status{mirador::ErrorCode::kInvalidArgument, "mark generation mismatch"};
    auto bgra = mirador::convert_color(mirador::marked_image_view(rendered.value().image),
                                       mirador::PixelFormat::kBgra8, 8u << 20);
    if (!bgra)
        return bgra.status();
    result.marked_frame.width = bgra.value().width();
    result.marked_frame.height = bgra.value().height();
    result.marked_frame.stride = static_cast<std::size_t>(bgra.value().row_stride_bytes());
    result.marked_frame.pixels.resize(static_cast<std::size_t>(bgra.value().byte_size()));
    std::memcpy(result.marked_frame.pixels.data(), bgra.value().view().data,
                result.marked_frame.pixels.size());
    for (const auto &mark : rendered.value().marks) {
        const auto *region = mirador::find_region(local, mark.stable_id);
        const auto *global = mirador::find_region(*result.snapshot, mark.stable_id);
        if (!region || !global)
            return mirador::Status{mirador::ErrorCode::kInvalidArgument, "mark identity mismatch"};
        const auto &b = region->bounds;
        VisualMark mapped;
        const auto valid_coordinate = [](float value) {
            return std::isfinite(value) &&
                   static_cast<double>(value) >= std::numeric_limits<std::int32_t>::min() &&
                   static_cast<double>(value) <= std::numeric_limits<std::int32_t>::max();
        };
        if (!valid_coordinate(global->anchor.x) || !valid_coordinate(global->anchor.y))
            return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                                   "mark anchor outside desktop coordinate range"};
        mapped.number = mark.mark_id;
        mapped.anchor = {static_cast<std::int32_t>(std::lround(global->anchor.x)),
                         static_cast<std::int32_t>(std::lround(global->anchor.y))};
        mapped.selectable = mark.drawn_bounds.width > 0 && mark.drawn_bounds.height > 0 &&
                            b.width > 0 && b.height > 0 && b.x >= 0 && b.y >= 0 &&
                            b.x + b.width <= roi.width && b.y + b.height <= roi.height &&
                            region->anchor.x >= 0 && region->anchor.x < roi.width &&
                            region->anchor.y >= 0 && region->anchor.y < roi.height;
        result.marks.push_back(std::move(mapped));
    }
    return {};
}
} // namespace mirage::integration::detail
