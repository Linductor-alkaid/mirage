#include "desktop_image_supply.hpp"
#include <algorithm>
#include <array>

namespace mirage::integration::detail {
namespace {
using Bytes = std::vector<std::byte>;
void u32(Bytes &out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::byte>((value >> shift) & 255u));
}
constexpr auto crc_table = [] {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        auto value = i;
        for (int bit = 0; bit < 8; ++bit)
            value = (value >> 1) ^ ((value & 1u) ? 0xedb88320u : 0u);
        table[i] = value;
    }
    return table;
}();
void chunk(Bytes &out, const char *name, const Bytes &data) {
    u32(out, static_cast<std::uint32_t>(data.size()));
    const auto start = out.size();
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::byte>(name[i]));
    out.insert(out.end(), data.begin(), data.end());
    std::uint32_t crc = 0xffffffffu;
    for (auto i = start; i < out.size(); ++i)
        crc = (crc >> 8) ^ crc_table[(crc ^ std::to_integer<unsigned>(out[i])) & 255u];
    u32(out, crc ^ 0xffffffffu);
}
mira::Error failure(mira::ErrorCode code, const char *message) {
    mira::Error error;
    error.code = code;
    error.domain = "mirage.desktop.image";
    error.safe_message = message;
    return error;
}
} // namespace

mira::Result<Bytes> encode_desktop_png(const mirage::desktop::ImageFrame &frame,
                                       const mirage::desktop::CancelToken &cancel) {
    if (cancel.cancelled())
        return failure(mira::ErrorCode::Cancelled, "image encoding cancelled");
    if (frame.format != mirage::desktop::ImageFormat::Bgra8 || frame.width <= 0 ||
        frame.height <= 0 || frame.width > 8192 || frame.height > 8192)
        return failure(mira::ErrorCode::InvalidArgument, "invalid capture dimensions or format");
    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    if (frame.stride < width * 4 || frame.stride > desktop_image_bytes / height ||
        frame.pixels.size() != frame.stride * height)
        return failure(mira::ErrorCode::ResourceExhausted, "invalid or over-budget capture buffer");
    const auto scan_bytes = (width * 3 + 1) * height;
    const auto encoded_bytes = 57 + 6 + scan_bytes + 5 * ((scan_bytes + 65534) / 65535);
    if (encoded_bytes > desktop_image_bytes)
        return failure(mira::ErrorCode::ResourceExhausted, "PNG exceeds the image byte budget");
    Bytes scans;
    scans.reserve(scan_bytes);
    for (std::size_t y = 0; y < height; ++y) {
        if (cancel.cancelled())
            return failure(mira::ErrorCode::Cancelled, "image encoding cancelled");
        scans.push_back(std::byte{0}); // PNG filter None; preserve pixel identity.
        for (std::size_t x = 0; x < width; ++x) {
            const auto offset = y * frame.stride + x * 4;
            scans.push_back(static_cast<std::byte>(frame.pixels[offset + 2]));
            scans.push_back(static_cast<std::byte>(frame.pixels[offset + 1]));
            scans.push_back(static_cast<std::byte>(frame.pixels[offset]));
        }
    }
    Bytes deflate{std::byte{0x78}, std::byte{0x01}};
    deflate.reserve(encoded_bytes);
    std::uint32_t a = 1, b = 0;
    for (std::size_t offset = 0; offset < scans.size();) {
        if (cancel.cancelled())
            return failure(mira::ErrorCode::Cancelled, "image encoding cancelled");
        const auto count = std::min<std::size_t>(65535, scans.size() - offset);
        deflate.push_back(static_cast<std::byte>(offset + count == scans.size() ? 1 : 0));
        const auto length = static_cast<std::uint16_t>(count);
        for (auto value : {length, static_cast<std::uint16_t>(length ^ 0xffffu)}) {
            deflate.push_back(static_cast<std::byte>(value & 255u));
            deflate.push_back(static_cast<std::byte>(value >> 8));
        }
        for (std::size_t i = offset; i < offset + count; ++i) {
            a = (a + std::to_integer<std::uint32_t>(scans[i])) % 65521;
            b = (b + a) % 65521;
        }
        deflate.insert(deflate.end(), scans.begin() + static_cast<std::ptrdiff_t>(offset),
                       scans.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    u32(deflate, (b << 16) | a);
    Bytes png;
    png.reserve(encoded_bytes);
    for (const auto value : {137, 80, 78, 71, 13, 10, 26, 10})
        png.push_back(static_cast<std::byte>(value));
    Bytes header;
    u32(header, static_cast<std::uint32_t>(width));
    u32(header, static_cast<std::uint32_t>(height));
    for (const auto value : {8, 2, 0, 0, 0})
        header.push_back(static_cast<std::byte>(value));
    chunk(png, "IHDR", header);
    chunk(png, "IDAT", deflate);
    chunk(png, "IEND", {});
    if (cancel.cancelled())
        return failure(mira::ErrorCode::Cancelled, "image encoding cancelled");
    return png;
}
} // namespace mirage::integration::detail
