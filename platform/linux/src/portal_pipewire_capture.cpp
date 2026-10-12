#include "portal_pipewire_capture.hpp"
#include <algorithm>
#include <cstring>
#include <optional>
#include <pipewire/pipewire.h>
#include <poll.h>
#include <spa/param/video/format-utils.h>
#include <unistd.h>

namespace mirage::platform::linux_backend {
namespace pipewire_detail {
desktop::CaptureOutcome failure(const char *code, const char *message) {
    return {false, std::string_view(code) == "cancelled", {}, {code, message}};
}
struct Capture {
    pw_main_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_stream *stream = nullptr;
    spa_hook listener{};
    spa_video_info_raw format{};
    bool formatted = false;
    bool error = false;
    pw_stream_state state = PW_STREAM_STATE_UNCONNECTED;
    pw_buffer *pending = nullptr;
    std::uint64_t source_sequence = 0;
    std::optional<desktop::ImageFrame> latest;
    ~Capture() {
        if (pending && stream)
            pw_stream_queue_buffer(stream, pending);
        if (stream) {
            spa_hook_remove(&listener);
            pw_stream_destroy(stream);
        }
        if (core)
            pw_core_disconnect(core);
        if (context)
            pw_context_destroy(context);
        if (loop)
            pw_main_loop_destroy(loop);
    }
};
void state_changed(void *data, pw_stream_state, pw_stream_state state, const char *) {
    auto &capture = *static_cast<Capture *>(data);
    capture.state = state;
    if (state == PW_STREAM_STATE_ERROR || state == PW_STREAM_STATE_UNCONNECTED)
        capture.error = true;
}
void param_changed(void *data, std::uint32_t id, const spa_pod *param) {
    auto &capture = *static_cast<Capture *>(data);
    // Only bounded metadata admission on the caller's loop. Format parsing
    // is a platform continuation; allocation/copy happens outside the callback.
    if (id != SPA_PARAM_Format || !param)
        return;
    // A format change invalidates the previously retained frame before parsing.
    capture.latest.reset();
    if (capture.pending) {
        pw_stream_queue_buffer(capture.stream, capture.pending);
        capture.pending = nullptr;
    }
    capture.formatted =
        SPA_POD_SIZE(param) <= 4096 && spa_format_video_raw_parse(param, &capture.format) >= 0;
    if (!capture.formatted)
        capture.error = true;
}
void process(void *data) {
    auto &capture = *static_cast<Capture *>(data);
    // No RT_PROCESS: this callback runs on our finite caller-owned loop.
    // Retain at most one buffer, and release it before stream teardown.
    // Drain a bounded number of ready buffers, retaining only the newest.
    // This is platform continuation on the finite caller-owned loop.
    for (unsigned i = 0; i < 16; ++i) {
        auto *next = pw_stream_dequeue_buffer(capture.stream);
        if (!next)
            break;
        if (capture.pending)
            pw_stream_queue_buffer(capture.stream, capture.pending);
        capture.pending = next;
        if (capture.source_sequence == UINT64_MAX) {
            capture.error = true;
            break;
        }
        ++capture.source_sequence;
    }
}
desktop::CaptureOutcome copy_frame(const Capture &capture, const desktop::CaptureLimits &limits,
                                   const desktop::CancelToken &cancel) {
    const auto &format = capture.format;
    const auto width = format.size.width;
    const auto height = format.size.height;
    const auto bytes = std::uint64_t{width} * height * 4;
    if (!width || !height || width > 16384 || height > 16384 ||
        bytes > std::min(limits.max_bytes, std::size_t{32u << 20}))
        return failure("capture_too_large", "ScreenCast frame exceeds its pixel budget");
    const bool swap =
        format.format == SPA_VIDEO_FORMAT_RGBA || format.format == SPA_VIDEO_FORMAT_RGBx;
    const bool opaque =
        format.format == SPA_VIDEO_FORMAT_BGRx || format.format == SPA_VIDEO_FORMAT_RGBx;
    if (!swap && format.format != SPA_VIDEO_FORMAT_BGRA && format.format != SPA_VIDEO_FORMAT_BGRx)
        return failure("unsupported_stream_format", "ScreenCast requires a packed RGB frame");
    const auto *buffer = capture.pending->buffer;
    if (!buffer || buffer->n_datas != 1 || !buffer->datas)
        return failure("invalid_stream_frame", "ScreenCast frame has an unsupported plane layout");
    const auto &plane = buffer->datas[0];
    if (!plane.data || !plane.chunk ||
        (plane.type != SPA_DATA_MemPtr && plane.type != SPA_DATA_MemFd) ||
        (plane.chunk->flags & SPA_CHUNK_FLAG_CORRUPTED))
        return failure("invalid_stream_frame",
                       "ScreenCast frame is not mapped readable CPU memory");
    const auto &chunk = *plane.chunk;
    const auto row = std::uint64_t{width} * 4;
    const auto extent = chunk.stride > 0 ? std::uint64_t{height - 1} * chunk.stride + row : 0;
    if (chunk.stride <= 0 || static_cast<std::uint64_t>(chunk.stride) < row ||
        extent > chunk.size || chunk.offset > plane.maxsize ||
        extent > plane.maxsize - chunk.offset)
        return failure("invalid_stream_frame", "ScreenCast stride or chunk exceeds mapped memory");
    const auto packed = swap ? (opaque ? PackedPixels::Rgbx : PackedPixels::Rgba)
                             : (opaque ? PackedPixels::Bgrx : PackedPixels::Bgra);
    return copy_portal_pixels({static_cast<const std::uint8_t *>(plane.data) + chunk.offset,
                               static_cast<std::size_t>(extent)},
                              width, height, chunk.stride, packed, limits, cancel);
}
} // namespace pipewire_detail
using pipewire_detail::Capture;
using pipewire_detail::copy_frame;
using pipewire_detail::failure;
using pipewire_detail::param_changed;
using pipewire_detail::process;
using pipewire_detail::state_changed;

desktop::CaptureOutcome copy_portal_pixels(std::span<const std::uint8_t> pixels,
                                           std::uint32_t width, std::uint32_t height,
                                           std::int32_t stride, PackedPixels packed,
                                           const desktop::CaptureLimits &limits,
                                           const desktop::CancelToken &cancel) {
    const auto row = std::uint64_t{width} * 4;
    const auto bytes = row * height;
    if (!width || !height || width > 16384 || height > 16384 ||
        bytes > std::min(limits.max_bytes, std::size_t{32u << 20}))
        return failure("capture_too_large", "ScreenCast frame exceeds its pixel budget");
    const auto extent = stride > 0 ? std::uint64_t{height - 1} * stride + row : 0;
    if (stride <= 0 || static_cast<std::uint64_t>(stride) < row || extent > pixels.size())
        return failure("invalid_stream_frame", "ScreenCast stride exceeds CPU buffer");
    const bool swap = packed == PackedPixels::Rgba || packed == PackedPixels::Rgbx;
    const bool opaque = packed == PackedPixels::Bgrx || packed == PackedPixels::Rgbx;
    desktop::CaptureOutcome out;
    out.frame.width = static_cast<std::int32_t>(width);
    out.frame.height = static_cast<std::int32_t>(height);
    out.frame.stride = static_cast<std::size_t>(row);
    out.frame.pixels.resize(static_cast<std::size_t>(bytes));
    const auto *source = pixels.data();
    for (std::uint32_t y = 0; y < height; ++y) {
        if (cancel.cancelled())
            return failure("cancelled", "ScreenCast frame cancelled during copy");
        auto *target = out.frame.pixels.data() + y * out.frame.stride;
        std::memcpy(target, source + std::size_t{y} * stride, out.frame.stride);
        if (swap || opaque)
            for (std::uint32_t x = 0; x < width; ++x) {
                if (swap)
                    std::swap(target[x * 4], target[x * 4 + 2]);
                if (opaque)
                    target[x * 4 + 3] = 255;
            }
    }
    out.ok = !cancel.cancelled();
    if (!out.ok)
        return failure("cancelled", "ScreenCast frame cancelled before publication");
    return out;
}

struct PortalPipeWireStream::Impl {
    Capture capture;
    bool connected = false;
};
PortalPipeWireStream::PortalPipeWireStream(int fd, std::uint32_t node)
    : impl_(std::make_unique<Impl>()) {
    struct Fd {
        int value;
        ~Fd() {
            if (value >= 0)
                ::close(value);
        }
    } owned_fd{fd};
    auto &capture = impl_->capture;
    capture.loop = pw_main_loop_new(nullptr);
    if (capture.loop)
        capture.context = pw_context_new(pw_main_loop_get_loop(capture.loop), nullptr, 0);
    if (capture.context) {
        // connect_fd owns fd even when connection fails (public API contract).
        capture.core = pw_context_connect_fd(capture.context, fd, nullptr, 0);
        owned_fd.value = -1;
    }
    if (!capture.core)
        return;
    capture.stream =
        pw_stream_new(capture.core, "Mirage one-frame observation",
                      pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY,
                                        "Capture", PW_KEY_MEDIA_ROLE, "Screen", nullptr));
    if (!capture.stream)
        return;
    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.state_changed = state_changed;
        value.param_changed = param_changed;
        value.process = process;
        return value;
    }();
    pw_stream_add_listener(capture.stream, &capture.listener, &events, &capture);
    std::uint8_t storage[1024];
    auto builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
    const spa_rectangle min{1, 1}, max{16384, 16384}, preferred{1920, 1080};
    const spa_pod *params[] = {static_cast<const spa_pod *>(spa_pod_builder_add_object(
        &builder, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat, SPA_FORMAT_mediaType,
        SPA_POD_Id(SPA_MEDIA_TYPE_video), SPA_FORMAT_mediaSubtype,
        SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw), SPA_FORMAT_VIDEO_format,
        SPA_POD_CHOICE_ENUM_Id(4, SPA_VIDEO_FORMAT_BGRA, SPA_VIDEO_FORMAT_BGRx,
                               SPA_VIDEO_FORMAT_RGBA, SPA_VIDEO_FORMAT_RGBx),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&preferred, &min, &max)))};
    const auto flags =
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS);
    if (pw_stream_connect(capture.stream, PW_DIRECTION_INPUT, node, flags, params, 1) < 0)
        return;
    impl_->connected = true;
}
PortalPipeWireStream::~PortalPipeWireStream() = default;
namespace {
desktop::CaptureOutcome read_stream_frame(Capture &capture, const desktop::CaptureLimits &limits,
                                          const desktop::CancelToken &cancel) {
    while (!capture.error && !cancel.cancelled()) {
        if (pw_loop_iterate(pw_main_loop_get_loop(capture.loop), 0) < 0)
            return failure("pipewire_unavailable", "ScreenCast loop stopped");
        if (capture.pending && capture.formatted) {
            auto frame = copy_frame(capture, limits, cancel);
            pw_stream_queue_buffer(capture.stream, capture.pending);
            capture.pending = nullptr;
            if (!frame.ok)
                return frame;
            frame.frame.source_sequence = capture.source_sequence;
            capture.latest = std::move(frame.frame);
        }
        if (capture.latest && capture.state == PW_STREAM_STATE_STREAMING &&
            capture.latest->source_sequence > limits.newer_than_sequence) {
            if (capture.latest->pixels.size() > std::min(limits.max_bytes, std::size_t{32u << 20}))
                return failure("capture_too_large", "ScreenCast frame exceeds its pixel budget");
            // Own ONE <=32 MiB source frame, returning the PipeWire buffer
            // immediately so even a single-buffer producer can advance. An
            // unchanged damage-driven source remains observable. Post-action
            // callers require a later source sequence, never this cached frame.
            return {true, false, *capture.latest, {}};
        }
        (void)::poll(nullptr, 0, 25);
    }
    auto stopped = cancel.cancelled()
                       ? failure("cancelled", "ScreenCast cancelled or deadline reached")
                       : failure("pipewire_unavailable", "ScreenCast stream closed before a frame");
    stopped.error.message += "; stream_state=" + std::to_string(static_cast<int>(capture.state)) +
                             "; formatted=" + std::to_string(capture.formatted) +
                             "; buffer_ready=" + std::to_string(capture.pending != nullptr);
    return stopped;
}
} // namespace

desktop::CaptureOutcome PortalPipeWireStream::capture(const desktop::CaptureLimits &limits,
                                                      const desktop::CancelToken &cancel) {
    if (!impl_->connected)
        return failure("pipewire_unavailable", "ScreenCast consumer connection failed");
    return read_stream_frame(impl_->capture, limits, cancel);
}
desktop::CaptureOutcome capture_pipewire_frame(int fd, std::uint32_t node,
                                               const desktop::CaptureLimits &limits,
                                               const desktop::CancelToken &cancel) {
    PortalPipeWireStream stream(fd, node);
    return stream.capture(limits, cancel);
}
} // namespace mirage::platform::linux_backend
