#include <mirage/integration/visual_frame.hpp>

#include <mirador/color_convert.hpp>
#include <mirador/image_buffer.hpp>
#include <mirador/status.hpp>

#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace mirage::integration {
namespace {

/// Shared owner for wrapped or converted pixels. mirador frames view memory
/// through a non-owning `ImageView` plus a `shared_ptr<const void>`; this
/// holder keeps the byte buffer alive for every derived view.
struct PixelOwner {
    std::vector<std::byte> bytes;
};

} // namespace

mirador::Result<mirador::Frame> to_mirador_frame(const desktop::ImageFrame &frame,
                                                 std::string source_id, std::uint64_t sequence,
                                                 std::chrono::steady_clock::time_point timestamp) {
    if (source_id.empty()) {
        return mirador::Status{mirador::ErrorCode::kInvalidArgument, "source id must not be empty"};
    }
    if (frame.format != desktop::ImageFormat::Bgra8) {
        // ScreenProvider freezes one canonical capture format; anything else
        // is a contract violation, not a conversion request.
        return mirador::Status{mirador::ErrorCode::kUnsupportedFormat,
                               "only the canonical Bgra8 capture format is supported"};
    }
    if (frame.width <= 0 || frame.height <= 0) {
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "capture extents must be positive"};
    }
    if (frame.width > mirador::kMaxImageDimension || frame.height > mirador::kMaxImageDimension) {
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "capture extents exceed the mirador dimension bound"};
    }
    const std::int64_t min_stride = static_cast<std::int64_t>(frame.width) * 4;
    if (frame.stride < static_cast<std::size_t>(min_stride)) {
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "capture stride is below width * 4"};
    }
    if (frame.stride >
        static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max() / frame.height)) {
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "capture buffer size overflows the addressable range"};
    }
    if (static_cast<std::int64_t>(frame.pixels.size()) <
        static_cast<std::int64_t>(frame.stride) * frame.height) {
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "pixel buffer is smaller than stride * height"};
    }

    auto owner = std::make_shared<PixelOwner>();
    owner->bytes.resize(frame.pixels.size());
    // One copy moves the capture out of the caller's storage; every view into
    // the frame stays valid independently of it (DEC-016 decision 3: pixels
    // never leave the visual session).
    std::memcpy(owner->bytes.data(), frame.pixels.data(), frame.pixels.size());

    mirador::ImageView view;
    view.data = owner->bytes.data();
    view.width = frame.width;
    view.height = frame.height;
    view.row_stride_bytes = static_cast<std::int64_t>(frame.stride);
    view.format = mirador::PixelFormat::kBgra8;
    view.rotation = mirador::Rotation::k0;
    if (const mirador::Result<void> validated = mirador::validate(view); !validated.ok()) {
        return validated.status();
    }

    mirador::Frame wrapped;
    wrapped.image = view;
    wrapped.sequence = sequence;
    wrapped.timestamp = timestamp;
    wrapped.source_id = std::move(source_id);
    wrapped.owner = std::move(owner);
    return wrapped;
}

mirador::Result<mirador::Frame> convert_frame(const mirador::Frame &frame,
                                              mirador::PixelFormat target_format,
                                              std::int64_t max_bytes) {
    mirador::Result<mirador::ImageBuffer> converted =
        mirador::convert_color(frame.image, target_format, max_bytes);
    if (!converted.ok()) {
        return converted.status();
    }
    auto owner = std::make_shared<mirador::ImageBuffer>(converted.take_value());

    mirador::Frame out;
    out.image = owner->view();
    out.image.rotation = frame.image.rotation;
    out.sequence = frame.sequence;
    out.timestamp = frame.timestamp;
    out.source_id = frame.source_id;
    out.owner = std::move(owner);
    return out;
}

} // namespace mirage::integration
