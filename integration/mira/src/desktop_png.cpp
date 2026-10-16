#include "desktop_image_supply.hpp"
#include <algorithm>
#include <array>
#include <cstring>

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

// ---- PNG scanline filtering (RFC 2083 §6): per-row minimum-sum-of-absolute
// heuristic over None/Sub/Up/Average/Paeth, computed on RGB triples. Filters
// are what make UI screenshots compressible; the choice is deterministic.
constexpr std::size_t kBpp = 3;
inline std::uint8_t paeth_predictor(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = std::abs(p - a), pb = std::abs(p - b), pc = std::abs(p - c);
    return static_cast<std::uint8_t>(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}
inline std::size_t filter_cost(const std::uint8_t *filtered, std::size_t count) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < count; ++i)
        sum += filtered[i] < 128 ? filtered[i] : 256u - filtered[i];
    return sum;
}

// ---- DEFLATE (RFC 1951) with fixed Huffman codes and a 32 KiB-window greedy
// LZ77. Deterministic integer matching only: 15-bit hash chains bounded to 32
// candidates, no dynamic tables, no adaptive heuristics. Falls back to stored
// blocks whenever the compressed body would not beat the raw size, so output
// is never larger than the previous stored-only encoder (plus a bounded
// header).
constexpr std::size_t kWindowSize = 32768;
constexpr unsigned kHashBits = 15;
constexpr int kMaxChain = 32;
constexpr std::size_t kMinMatch = 3;
constexpr std::size_t kMaxMatch = 258;

struct BitWriter {
    Bytes &out;
    std::uint32_t buffer = 0;
    int count = 0;
    void push(std::uint32_t value, int bits) { // DEFLATE packs integers LSB-first.
        buffer |= value << count;
        count += bits;
        while (count >= 8) {
            out.push_back(static_cast<std::byte>(buffer & 255u));
            buffer >>= 8;
            count -= 8;
        }
    }
    void align() {
        if (count > 0) {
            out.push_back(static_cast<std::byte>(buffer & 255u));
            buffer = 0;
            count = 0;
        }
    }
};
inline std::uint32_t reverse_bits(std::uint32_t value, int bits) {
    std::uint32_t reversed = 0;
    for (int i = 0; i < bits; ++i) {
        reversed = (reversed << 1) | (value & 1u);
        value >>= 1;
    }
    return reversed;
}
// Fixed Huffman literal/length codes (RFC 1951 §3.2.6), packed MSB-first.
inline void emit_literal(BitWriter &bits, unsigned symbol) {
    if (symbol < 144)
        bits.push(reverse_bits(0x30 + symbol, 8), 8);
    else if (symbol < 256)
        bits.push(reverse_bits(0x190 + (symbol - 144), 9), 9);
    else if (symbol < 280)
        bits.push(reverse_bits(symbol - 256, 7), 7);
    else
        bits.push(reverse_bits(0xC0 + (symbol - 280), 8), 8);
}
constexpr std::uint16_t kLengthBase[] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                         15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                         67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                         2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
inline void emit_length(BitWriter &bits, unsigned length) {
    unsigned index = 28;
    while (kLengthBase[index] > length)
        --index;
    emit_literal(bits, 257 + index);
    bits.push(length - kLengthBase[index], kLengthExtra[index]);
}
constexpr std::uint16_t kDistanceBase[] = {
    1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistanceExtra[] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                           6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
inline void emit_distance(BitWriter &bits, unsigned distance) {
    unsigned index = 29;
    while (kDistanceBase[index] > distance)
        --index;
    bits.push(reverse_bits(index, 5), 5); // Distance codes are five bits, MSB-first.
    bits.push(distance - kDistanceBase[index], kDistanceExtra[index]);
}

void append_stored(Bytes &deflate, const Bytes &data) {
    std::uint32_t a = 1, b = 0;
    for (std::size_t offset = 0; offset < data.size();) {
        const auto count = std::min<std::size_t>(65535, data.size() - offset);
        deflate.push_back(static_cast<std::byte>(offset + count == data.size() ? 1 : 0));
        const auto length = static_cast<std::uint16_t>(count);
        for (auto value : {length, static_cast<std::uint16_t>(length ^ 0xffffu)}) {
            deflate.push_back(static_cast<std::byte>(value & 255u));
            deflate.push_back(static_cast<std::byte>(value >> 8));
        }
        for (std::size_t i = offset; i < offset + count; ++i) {
            a = (a + std::to_integer<std::uint32_t>(data[i])) % 65521;
            b = (b + a) % 65521;
        }
        deflate.insert(deflate.end(), data.begin() + static_cast<std::ptrdiff_t>(offset),
                       data.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
    }
    u32(deflate, (b << 16) | a);
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
    const auto stored_bound = 57 + 6 + scan_bytes + 5 * ((scan_bytes + 65534) / 65535);
    if (stored_bound > desktop_image_bytes)
        return failure(mira::ErrorCode::ResourceExhausted, "PNG exceeds the image byte budget");

    // Filter + collect the RGB scanlines.
    Bytes scans;
    scans.reserve(scan_bytes);
    Bytes previous(width * 3, std::byte{0}), current(width * 3), filtered(width * 3);
    for (std::size_t y = 0; y < height; ++y) {
        if (cancel.cancelled())
            return failure(mira::ErrorCode::Cancelled, "image encoding cancelled");
        for (std::size_t x = 0; x < width; ++x) {
            const auto offset = y * frame.stride + x * 4;
            current[x * 3 + 0] = static_cast<std::byte>(frame.pixels[offset + 2]);
            current[x * 3 + 1] = static_cast<std::byte>(frame.pixels[offset + 1]);
            current[x * 3 + 2] = static_cast<std::byte>(frame.pixels[offset]);
        }
        std::size_t best = std::size_t(-1);
        int best_filter = 0;
        for (int filter = 0; filter < 5; ++filter) {
            for (std::size_t i = 0; i < width * 3; ++i) {
                const int left = i >= kBpp ? std::to_integer<int>(current[i - kBpp]) : 0;
                const int up = std::to_integer<int>(previous[i]);
                const int upper_left = i >= kBpp ? std::to_integer<int>(previous[i - kBpp]) : 0;
                int prediction;
                switch (filter) {
                case 1:
                    prediction = left;
                    break;
                case 2:
                    prediction = up;
                    break;
                case 3:
                    prediction = (left + up) / 2;
                    break;
                case 4:
                    prediction = paeth_predictor(left, up, upper_left);
                    break;
                default:
                    prediction = 0;
                    break;
                }
                filtered[i] =
                    static_cast<std::byte>((std::to_integer<int>(current[i]) - prediction) & 255);
            }
            const auto cost =
                filter_cost(reinterpret_cast<const std::uint8_t *>(filtered.data()), width * 3);
            if (best == std::size_t(-1) || cost < best) {
                best = cost;
                best_filter = filter;
            }
        }
        // Recompute the winning filter's bytes (cheap, keeps one buffer).
        for (std::size_t i = 0; i < width * 3; ++i) {
            const int left = i >= kBpp ? std::to_integer<int>(current[i - kBpp]) : 0;
            const int up = std::to_integer<int>(previous[i]);
            const int upper_left = i >= kBpp ? std::to_integer<int>(previous[i - kBpp]) : 0;
            int prediction;
            switch (best_filter) {
            case 1:
                prediction = left;
                break;
            case 2:
                prediction = up;
                break;
            case 3:
                prediction = (left + up) / 2;
                break;
            case 4:
                prediction = paeth_predictor(left, up, upper_left);
                break;
            default:
                prediction = 0;
                break;
            }
            filtered[i] =
                static_cast<std::byte>((std::to_integer<int>(current[i]) - prediction) & 255);
        }
        scans.push_back(static_cast<std::byte>(best_filter));
        scans.insert(scans.end(), filtered.begin(), filtered.end());
        previous.swap(current);
    }

    // Compress with fixed-Huffman DEFLATE over a 32 KiB window.
    std::vector<std::uint32_t> head(std::size_t{1} << kHashBits, 0), prev(kWindowSize * 2, 0);
    const auto hash_at = [&](std::size_t i) {
        const auto x = std::to_integer<std::uint32_t>(scans[i]);
        const auto y = std::to_integer<std::uint32_t>(scans[i + 1]);
        const auto z = std::to_integer<std::uint32_t>(scans[i + 2]);
        return static_cast<std::uint32_t>(((x << 10) ^ (y << 5) ^ z) & ((1u << kHashBits) - 1));
    };
    const auto insert_hash = [&](std::size_t position) {
        if (position + kMinMatch > scans.size())
            return;
        const auto hashed = hash_at(position);
        prev[position % (kWindowSize * 2)] = head[hashed];
        head[hashed] = static_cast<std::uint32_t>(position + 1);
    };
    Bytes deflate{std::byte{0x78}, std::byte{0x01}};
    deflate.reserve(std::min(stored_bound, scan_bytes + 64));
    BitWriter bits{deflate};
    bits.push(1, 1); // BFINAL
    bits.push(1, 2); // BTYPE=01 fixed Huffman
    std::size_t position = 0;
    while (position < scans.size()) {
        if ((position & 0xFFFF) == 0 && cancel.cancelled())
            return failure(mira::ErrorCode::Cancelled, "image encoding cancelled");
        std::size_t best_length = 0, best_distance = 0;
        if (position + kMinMatch <= scans.size()) {
            const auto hashed = hash_at(position);
            auto candidate = head[hashed];
            int tries = kMaxChain;
            while (candidate != 0 && tries-- > 0) {
                const auto start = candidate - 1;
                if (position - start > kWindowSize)
                    break;
                std::size_t length = 0;
                const auto limit = std::min(kMaxMatch, scans.size() - position);
                while (length < limit && scans[start + length] == scans[position + length])
                    ++length;
                if (length > best_length) {
                    best_length = length;
                    best_distance = position - start;
                    if (length >= limit)
                        break;
                }
                candidate = prev[start % (kWindowSize * 2)];
            }
        }
        if (best_length >= kMinMatch) {
            emit_length(bits, static_cast<unsigned>(best_length));
            emit_distance(bits, static_cast<unsigned>(best_distance));
            for (std::size_t i = 0; i < best_length; ++i, ++position)
                insert_hash(position);
        } else {
            emit_literal(bits, std::to_integer<unsigned>(scans[position]));
            insert_hash(position);
            ++position;
        }
    }
    emit_literal(bits, 256); // end of block
    bits.align();
    std::uint32_t a = 1, b = 0;
    for (const auto value : scans) {
        a = (a + std::to_integer<std::uint32_t>(value)) % 65521;
        b = (b + a) % 65521;
    }
    u32(deflate, (b << 16) | a);
    if (deflate.size() > stored_bound) {
        // Incompressible content: stored blocks are never worse.
        deflate.clear();
        deflate.push_back(std::byte{0x78});
        deflate.push_back(std::byte{0x01});
        append_stored(deflate, scans);
    }

    Bytes png;
    png.reserve(stored_bound);
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
