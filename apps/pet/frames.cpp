#include "frames.hpp"

#include <fstream>
#include <limits>
#include <webp/demux.h>

namespace mirage::pet_ui {
namespace {
constexpr std::size_t kMaxWebpBytes = 8 * 1024 * 1024;
constexpr std::size_t kWidth = 512;
constexpr std::size_t kHeight = 512;
constexpr std::size_t kFrameBytes = kWidth * kHeight * 4;
constexpr std::size_t kMaxDecodedBytes = 96 * 1024 * 1024;
} // namespace

bool decode_clip(const std::filesystem::path &asset_root, const runtime::pet::Clip &clip,
                 DecodedClip &out, std::string &diagnostic) {
    out = {};
    diagnostic.clear();
    if (clip.frame_count == 0 || clip.frame_count > 96 || clip.frame_duration_ms == 0 ||
        static_cast<std::size_t>(clip.frame_count) * kFrameBytes > kMaxDecodedBytes ||
        clip.file.empty() || clip.file.find('/') != std::string_view::npos ||
        clip.file.find('\\') != std::string_view::npos) {
        diagnostic = "invalid v5 clip bounds";
        return false;
    }
    const auto file = asset_root / clip.file;
    std::error_code error;
    if (std::filesystem::is_symlink(file, error) || error ||
        !std::filesystem::is_regular_file(file, error) || error) {
        diagnostic = "v5 asset is missing or not a regular file";
        return false;
    }
    const auto bytes = std::filesystem::file_size(file, error);
    if (error || bytes == 0 || bytes > kMaxWebpBytes ||
        bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        diagnostic = "v5 asset exceeds size budget";
        return false;
    }
    std::vector<std::uint8_t> input(static_cast<std::size_t>(bytes));
    std::ifstream stream(file, std::ios::binary);
    if (!stream.read(reinterpret_cast<char *>(input.data()), static_cast<std::streamsize>(bytes))) {
        diagnostic = "v5 asset read failed";
        return false;
    }
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) {
        diagnostic = "WebP decoder initialization failed";
        return false;
    }
    options.color_mode = MODE_RGBA;
    const WebPData data{input.data(), input.size()};
    std::unique_ptr<WebPAnimDecoder, decltype(&WebPAnimDecoderDelete)> decoder(
        WebPAnimDecoderNew(&data, &options), &WebPAnimDecoderDelete);
    if (!decoder) {
        diagnostic = "v5 asset WebP decode rejected";
        return false;
    }
    WebPAnimInfo info;
    if (!WebPAnimDecoderGetInfo(decoder.get(), &info) || info.canvas_width != kWidth ||
        info.canvas_height != kHeight || info.frame_count != clip.frame_count ||
        info.loop_count != 0) {
        diagnostic = "v5 asset canvas, frame count or loop flag mismatch";
        return false;
    }
    DecodedClip candidate;
    candidate.clip = &clip;
    candidate.rgba_frames.reserve(clip.frame_count);
    int previous_timestamp = 0;
    while (WebPAnimDecoderHasMoreFrames(decoder.get())) {
        std::uint8_t *pixels = nullptr;
        int timestamp = 0;
        if (!WebPAnimDecoderGetNext(decoder.get(), &pixels, &timestamp) || !pixels ||
            timestamp - previous_timestamp != clip.frame_duration_ms ||
            candidate.rgba_frames.size() >= clip.frame_count) {
            diagnostic = "v5 asset frame duration or decode mismatch";
            return false;
        }
        previous_timestamp = timestamp;
        candidate.rgba_frames.push_back(
            std::make_shared<const std::vector<std::uint8_t>>(pixels, pixels + kFrameBytes));
    }
    if (candidate.rgba_frames.size() != clip.frame_count) {
        diagnostic = "v5 asset truncated frames";
        return false;
    }
    out = std::move(candidate);
    return true;
}
} // namespace mirage::pet_ui
