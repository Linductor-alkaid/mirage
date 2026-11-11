#include "perception_storyboard.hpp"
#include <algorithm>
#include <cstring>
#include <mirador/resize.hpp>
#include <set>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
mira::Error failure(mira::ErrorCode code, const char *message) {
    mira::Error error;
    error.code = code;
    error.domain = "mirage.desktop.storyboard";
    error.safe_message = message;
    return error;
}
PerceptionStoryboard unavailable(const char *reason) {
    return {{}, {{"available", false}, {"reason", reason}, {"input_actionable", false}}};
}
} // namespace
mira::Result<PerceptionStoryboard>
make_perception_storyboard(const PerceptionView &view, const std::vector<std::string> &ids,
                           std::chrono::steady_clock::time_point now,
                           const desktop::CancelToken &cancel) {
    if (cancel.cancelled())
        return failure(mira::ErrorCode::Cancelled, "storyboard cancelled");
    if (ids.size() < 2 || ids.size() > 4)
        return failure(mira::ErrorCode::InvalidArgument, "storyboard requires 2 to 4 frame IDs");
    struct Tile {
        std::shared_ptr<const PerceptionPreview> preview;
        std::uint64_t sequence;
    };
    std::vector<Tile> tiles;
    std::set<std::string> unique;
    for (const auto &id : ids) {
        if (id.empty() || id.size() > 64 || !unique.insert(id).second)
            return failure(mira::ErrorCode::InvalidArgument, "invalid or duplicate frame ID");
        const auto entry = std::find_if(view.previews.begin(), view.previews.end(),
                                        [&](const auto &p) { return p.first == id; });
        const auto preview = entry == view.previews.end() ? nullptr : entry->second.lock();
        if (!preview || preview->id != id || preview->observed_at > now ||
            now - preview->observed_at >= std::chrono::seconds{120})
            return unavailable("expired_consumed_or_evicted");
        const auto event = std::find_if(view.events.begin(), view.events.end(), [&](const auto &e) {
            return e.frame_id == id && e.source_epoch == view.source_epoch &&
                   e.observed_at == preview->observed_at;
        });
        if (!view.source_epoch || event == view.events.end() || !event->frame_sequence)
            return unavailable("source_epoch_or_event_unavailable");
        const auto &image = preview->image;
        if (image.format != desktop::ImageFormat::Bgra8 || image.width <= 0 || image.height <= 0 ||
            image.width > 640 || image.height > 640 ||
            image.stride < std::size_t(image.width) * 4 ||
            image.stride > (1u << 20) / std::size_t(image.height) ||
            image.pixels.size() < image.stride * std::size_t(image.height) ||
            image.pixels.size() > (1u << 20))
            return failure(mira::ErrorCode::InvalidArgument, "invalid retained preview layout");
        if (!tiles.empty() && (image.width != tiles.front().preview->image.width ||
                               image.height != tiles.front().preview->image.height))
            return unavailable("source_geometry_changed");
        tiles.push_back({preview, event->frame_sequence});
    }
    std::sort(tiles.begin(), tiles.end(), [](const auto &a, const auto &b) {
        return a.preview->observed_at < b.preview->observed_at;
    });
    for (std::size_t i = 1; i < tiles.size(); ++i)
        if (tiles[i].preview->observed_at <= tiles[i - 1].preview->observed_at ||
            tiles[i].sequence <= tiles[i - 1].sequence)
            return unavailable("ambiguous_temporal_order");
    constexpr int side = 320;
    PerceptionStoryboard result;
    auto &board = result.image;
    board.format = desktop::ImageFormat::Bgra8;
    board.width = side * 2;
    board.height = side * (tiles.size() > 2 ? 2 : 1);
    board.stride = std::size_t(board.width) * 4;
    board.pixels.assign(board.stride * std::size_t(board.height), 0);
    for (std::size_t i = 3; i < board.pixels.size(); i += 4)
        board.pixels[i] = 255;
    Json::Array mapping;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (cancel.cancelled())
            return failure(mira::ErrorCode::Cancelled, "storyboard cancelled");
        const auto &tile = tiles[i];
        const auto &image = tile.preview->image;
        const int longest = std::max(image.width, image.height);
        const int width = std::max(1, image.width * std::min(side, longest) / longest);
        const int height = std::max(1, image.height * std::min(side, longest) / longest);
        mirador::ImageView source;
        source.data = reinterpret_cast<const std::byte *>(image.pixels.data());
        source.width = image.width;
        source.height = image.height;
        source.row_stride_bytes = static_cast<std::int64_t>(image.stride);
        source.format = mirador::PixelFormat::kBgra8;
        const auto scaled = mirador::resize_area(source, width, height, side * side * 4);
        if (!scaled)
            return failure(mira::ErrorCode::ResourceExhausted, "storyboard resize refused");
        const int x = int(i % 2) * side + (side - width) / 2;
        const int y = int(i / 2) * side + (side - height) / 2;
        const auto pixels = scaled.value().view();
        for (int row = 0; row < height; ++row)
            std::memcpy(board.pixels.data() + (std::size_t(y + row) * board.stride) +
                            std::size_t(x) * 4,
                        pixels.data + row * pixels.row_stride_bytes, std::size_t(width) * 4);
        const auto ms = [](auto duration) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        };
        mapping.emplace_back(Json::Object{
            {"tile", static_cast<std::int64_t>(i + 1)},
            {"frame_id", tile.preview->id},
            {"frame_sequence", static_cast<std::int64_t>(tile.sequence)},
            {"source_epoch", static_cast<std::int64_t>(view.source_epoch)},
            {"offset_ms", ms(tile.preview->observed_at - tiles.front().preview->observed_at)},
            {"age_ms", ms(now - tile.preview->observed_at)},
            {"since_previous_ms",
             i ? ms(tile.preview->observed_at - tiles[i - 1].preview->observed_at)
               : std::int64_t{0}},
            {"x", x},
            {"y", y},
            {"width", width},
            {"height", height},
            {"preview_width", image.width},
            {"preview_height", image.height}});
    }
    result.metadata = {{"available", true},
                       {"storyboard_source_epoch", static_cast<std::int64_t>(view.source_epoch)},
                       {"historical", true},
                       {"input_actionable", false},
                       {"sparse", true},
                       {"audio", false},
                       {"complete_video", false},
                       {"time_basis", "observer_monotonic_relative_not_player_time"},
                       {"layout", "chronological_row_major_2_columns_320px_cells"},
                       {"dropped", static_cast<std::int64_t>(view.dropped)},
                       {"filtered", static_cast<std::int64_t>(view.filtered)},
                       {"resync", view.resync},
                       {"tiles", Json{std::move(mapping)}}};
    return result;
}
} // namespace mirage::integration::detail
