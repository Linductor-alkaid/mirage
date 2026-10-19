#include "frames.hpp"
#include "asset_digests.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <mira/event_store.hpp>
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
                 DecodedClip &out, std::string &diagnostic,
                 const std::function<bool()> &cancelled) {
    out = {};
    diagnostic.clear();
    if (clip.frame_count == 0 || clip.frame_count > 96 || clip.frame_duration_ms == 0 ||
        static_cast<std::size_t>(clip.frame_count) * kFrameBytes > kMaxDecodedBytes ||
        clip.file.empty() || clip.file.find('/') != std::string_view::npos ||
        clip.file.find('\\') != std::string_view::npos || clip.intro_end > clip.loop_end ||
        clip.loop_end > clip.frame_count || (!clip.once && clip.intro_end == clip.loop_end) ||
        (clip.once && (clip.intro_end != clip.frame_count || clip.loop_end != clip.frame_count))) {
        diagnostic = "invalid v5 clip bounds";
        return false;
    }
    const auto digest =
        std::find_if(pet_asset_digests.begin(), pet_asset_digests.end(),
                     [&clip](const auto &entry) { return entry.first == clip.file; });
    if (digest == pet_asset_digests.end()) {
        diagnostic = "unapproved v5 asset path";
        return false;
    }
    const auto stop = [&] {
        if (!cancelled || !cancelled())
            return false;
        diagnostic = "v5 asset load cancelled";
        return true;
    };
    if (stop())
        return false;
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
    for (std::size_t offset = 0; offset < input.size();) {
        if (stop())
            return false;
        const auto chunk = std::min<std::size_t>(64 * 1024, input.size() - offset);
        if (!stream.read(reinterpret_cast<char *>(input.data() + offset),
                         static_cast<std::streamsize>(chunk))) {
            diagnostic = "v5 asset read failed";
            return false;
        }
        offset += chunk;
    }
    if (stream.peek() != std::char_traits<char>::eof()) {
        diagnostic = "v5 asset read failed";
        return false;
    }
    if (stop())
        return false;
    if (mira::digest_bytes(std::as_bytes(std::span(input))).to_string() != digest->second) {
        diagnostic = "v5 asset digest mismatch";
        return false;
    }
    if (stop())
        return false;
    WebPAnimDecoderOptions options;
    if (!WebPAnimDecoderOptionsInit(&options)) {
        diagnostic = "WebP decoder initialization failed";
        return false;
    }
    options.color_mode = MODE_RGBA;
    options.use_threads = 0; // the process Executor owns all decoding concurrency
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
    candidate.crops.reserve(clip.frame_count);
    int previous_timestamp = 0;
    while (WebPAnimDecoderHasMoreFrames(decoder.get())) {
        if (stop())
            return false;
        std::uint8_t *pixels = nullptr;
        int timestamp = 0;
        if (!WebPAnimDecoderGetNext(decoder.get(), &pixels, &timestamp) || !pixels ||
            timestamp - previous_timestamp != clip.frame_duration_ms ||
            candidate.rgba_frames.size() >= clip.frame_count) {
            diagnostic = "v5 asset frame duration or decode mismatch";
            return false;
        }
        previous_timestamp = timestamp;
        std::size_t left = kWidth, top = kHeight, right = 0, bottom = 0;
        for (std::size_t y = 0; y < kHeight; ++y) {
            if (stop())
                return false;
            for (std::size_t x = 0; x < kWidth; ++x) {
                const auto *pixel = pixels + (y * kWidth + x) * 4;
                if (pixel[0] || pixel[1] || pixel[2] || pixel[3]) {
                    left = std::min(left, x);
                    top = std::min(top, y);
                    right = std::max(right, x + 1);
                    bottom = std::max(bottom, y + 1);
                }
            }
        }
        if (right == 0)
            left = top = 0;
        const auto width = right - left, height = bottom - top;
        auto packed = std::make_shared<std::vector<std::uint8_t>>(width * height * 4);
        for (std::size_t y = 0; y < height; ++y)
            std::copy_n(pixels + ((top + y) * kWidth + left) * 4, width * 4,
                        packed->data() + y * width * 4);
        candidate.crops.push_back(
            {static_cast<std::uint16_t>(left), static_cast<std::uint16_t>(top),
             static_cast<std::uint16_t>(width), static_cast<std::uint16_t>(height)});
        candidate.rgba_frames.push_back(std::move(packed));
    }
    if (stop())
        return false;
    if (candidate.rgba_frames.size() != clip.frame_count) {
        diagnostic = "v5 asset truncated frames";
        return false;
    }
    out = std::move(candidate);
    return true;
}
} // namespace mirage::pet_ui
