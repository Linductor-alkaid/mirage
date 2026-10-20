#include "../support/test.hpp"
// Compile the private buffer boundary into this isolated test TU. The PipeWire
// loop is real; only buffer ownership calls are wrapped, with no portal grant.
#include "../../platform/linux/src/portal_pipewire_capture.cpp"
#include <array>
#include <spa/pod/filter.h>

namespace {
struct WireStats {
    std::array<pw_buffer *, 4> ready{};
    std::size_t index = 0;
    unsigned recycled = 0, destroyed = 0, negotiations = 0;
    bool reject_metadata = false;
};
WireStats *active_wire = nullptr; // Single synchronous test owner, never shared.
struct FrameBuffer {
    std::array<std::uint8_t, 16> pixels{};
    spa_chunk chunk{};
    spa_data plane{};
    spa_buffer buffer{};
    pw_buffer source{};
    spa_meta_header header{};
    spa_meta meta{};
    void timestamp(std::int64_t pts, std::uint32_t flags = 0) {
        header.pts = pts;
        header.flags = flags;
        meta = {SPA_META_Header, sizeof(header), &header};
        buffer.n_metas = 1;
        buffer.metas = &meta;
    }
    explicit FrameBuffer(std::uint8_t value) {
        pixels.fill(value);
        chunk.size = 16;
        chunk.stride = 8;
        plane.type = SPA_DATA_MemPtr;
        plane.data = pixels.data();
        plane.maxsize = 16;
        plane.chunk = &chunk;
        buffer.n_datas = 1;
        buffer.datas = &plane;
        source.buffer = &buffer;
    }
};
} // namespace
extern "C" pw_buffer *__wrap_pw_stream_dequeue_buffer(pw_stream *) {
    if (!active_wire || active_wire->index >= active_wire->ready.size())
        return nullptr;
    return active_wire->ready[active_wire->index++];
}
extern "C" int __wrap_pw_stream_queue_buffer(pw_stream *, pw_buffer *) {
    if (active_wire)
        ++active_wire->recycled;
    return 0;
}
extern "C" void __wrap_pw_stream_destroy(pw_stream *) {
    if (active_wire)
        ++active_wire->destroyed;
}

extern "C" int __wrap_pw_stream_update_params(pw_stream *, const spa_pod **params, uint32_t count) {
    using namespace mirage;
    MIRAGE_CHECK(active_wire && count == 2);
    if (!active_wire || count != 2)
        return -1;
    ++active_wire->negotiations;
    for (const int side : {384, 1024}) {
        std::uint8_t input[256], output[512];
        auto builder = SPA_POD_BUILDER_INIT(input, sizeof(input));
        auto *producer = static_cast<const spa_pod *>(spa_pod_builder_add_object(
            &builder, SPA_TYPE_OBJECT_ParamMeta, SPA_PARAM_Meta, SPA_PARAM_META_type,
            SPA_POD_Id(SPA_META_Cursor), SPA_PARAM_META_size,
            SPA_POD_Int(sizeof(spa_meta_cursor) + sizeof(spa_meta_bitmap) + side * side * 4)));
        auto filter = SPA_POD_BUILDER_INIT(output, sizeof(output));
        spa_pod *negotiated = nullptr;
        const int result = spa_pod_filter(&filter, &negotiated, producer, params[1]);
        // Real Mutter's fixed allocation must intersect; unbounded sprites must not.
        MIRAGE_CHECK((result >= 0) == (side == 384));
    }
    return active_wire->reject_metadata ? -1 : 0;
}

int main() {
    using namespace mirage;
    using namespace platform::linux_backend;
    pw_init(nullptr, nullptr);
    WireStats wire;
    active_wire = &wire;
    {
        FrameBuffer first(17), replacement(29);
        Capture capture;
        capture.loop = pw_main_loop_new(nullptr);
        MIRAGE_CHECK(capture.loop);
        if (!capture.loop)
            return testing::finish("portal_stream_retention_test");
        capture.stream = reinterpret_cast<pw_stream *>(std::uintptr_t{1});
        spa_list_init(&capture.listener.link);
        std::uint8_t format_storage[512];
        auto builder = SPA_POD_BUILDER_INIT(format_storage, sizeof(format_storage));
        spa_video_info_raw raw{};
        raw.format = SPA_VIDEO_FORMAT_BGRA;
        raw.size = {2, 2};
        const auto *format = spa_format_video_raw_build(&builder, SPA_PARAM_Format, &raw);
        param_changed(&capture, SPA_PARAM_Format, format);
        MIRAGE_CHECK(capture.formatted && !capture.error && wire.negotiations == 1);
        wire.reject_metadata = true;
        param_changed(&capture, SPA_PARAM_Format, format);
        MIRAGE_CHECK(capture.error && wire.negotiations == 2);
        wire.reject_metadata = false;
        capture.error = false;
        capture.formatted = true;
        capture.format.format = SPA_VIDEO_FORMAT_BGRA;
        capture.format.size = {2, 2};
        capture.state = PW_STREAM_STATE_STREAMING;
        int checks = 0;
        desktop::CancelToken bounded([&] { return ++checks >= 4; });
        MIRAGE_CHECK(read_stream_frame(capture, {16}, bounded).cancelled);
        MIRAGE_CHECK(!capture.latest && capture.source_sequence == 0);
        wire.ready[0] = &first.source;
        process(&capture);
        const auto one = read_stream_frame(capture, {16}, {});
        const auto unchanged = read_stream_frame(capture, {16}, {});
        MIRAGE_CHECK(one.ok && unchanged.ok && one.frame.source_sequence == 1 &&
                     unchanged.frame.source_sequence == 1);
        MIRAGE_CHECK(one.frame.pixels == unchanged.frame.pixels &&
                     unchanged.frame.pixels.front() == 17 && wire.recycled == 1);
        // A post-action watermark cannot turn the retained pre-action frame
        // into new evidence. Cancellation exits the actual finite loop.
        checks = 0;
        const auto waiting = read_stream_frame(capture, {16, 1}, bounded);
        MIRAGE_CHECK(waiting.cancelled && !waiting.ok && wire.recycled == 1);
        wire.ready = {&replacement.source, nullptr, nullptr, nullptr};
        wire.index = 0;
        process(&capture);
        const auto changed = read_stream_frame(capture, {16, 1}, {});
        MIRAGE_CHECK(changed.ok && changed.frame.source_sequence == 2 &&
                     changed.frame.pixels.front() == 29 && wire.recycled == 2);
        MIRAGE_CHECK(one.frame.pixels.front() == 17); // Published bytes are owned.
        MIRAGE_CHECK(read_stream_frame(capture, {15}, {}).error.code == "capture_too_large");
        capture.state = PW_STREAM_STATE_PAUSED;
        checks = 0;
        MIRAGE_CHECK(read_stream_frame(capture, {16}, bounded).cancelled);
        capture.state = PW_STREAM_STATE_STREAMING;
        // A negotiated format change discards the owned cache before accepting
        // metadata; a malformed format must never reuse old pixels.
        spa_pod_object invalid{};
        invalid.pod.type = SPA_TYPE_None;
        param_changed(&capture, SPA_PARAM_Format, &invalid.pod);
        MIRAGE_CHECK(!capture.pending && !capture.latest && capture.error && wire.recycled == 2);
        MIRAGE_CHECK(!read_stream_frame(capture, {16}, {}).ok);
        capture.error = false;
        capture.formatted = true;
        wire.ready = {&first.source, nullptr, nullptr, nullptr};
        wire.index = 0;
        process(&capture);
        MIRAGE_CHECK(capture.pending == &first.source && capture.source_sequence == 3);
        state_changed(&capture, PW_STREAM_STATE_STREAMING, PW_STREAM_STATE_UNCONNECTED, nullptr);
        MIRAGE_CHECK(!read_stream_frame(capture, {16}, {}).ok);
    }
    MIRAGE_CHECK(wire.recycled == 3 && wire.destroyed == 1);
    wire = {};
    {
        // DEC-105: source time, not consumer receipt or local sequence,
        // establishes whether pixels were made after the pet disappeared.
        Capture capture;
        capture.loop = pw_main_loop_new(nullptr);
        capture.stream = reinterpret_cast<pw_stream *>(std::uintptr_t{1});
        spa_list_init(&capture.listener.link);
        capture.formatted = true;
        capture.format.format = SPA_VIDEO_FORMAT_BGRA;
        capture.format.size = {2, 2};
        capture.state = PW_STREAM_STATE_STREAMING;
        FrameBuffer frame(42);
        auto offer = [&] {
            wire.ready = {&frame.source, nullptr, nullptr, nullptr};
            wire.index = 0;
            process(&capture);
        };
        int ticks = 0;
        desktop::CancelToken bounded([&] { return ++ticks >= 4; });
        offer();
        MIRAGE_CHECK(read_stream_frame(capture, {16}, {}, 10).error.code ==
                     "pet_capture_fence_unavailable");
        frame.timestamp(10);
        offer();
        ticks = 0;
        MIRAGE_CHECK(read_stream_frame(capture, {16}, bounded, 10).cancelled);
        MIRAGE_CHECK(!capture.latest && wire.recycled == 2);
        frame.timestamp(11);
        offer();
        const auto fresh = read_stream_frame(capture, {16}, {}, 10);
        MIRAGE_CHECK(fresh.ok && fresh.frame.pixels.front() == 42 && capture.latest_pts_ns == 11 &&
                     wire.recycled == 3);
        ticks = 0;
        MIRAGE_CHECK(read_stream_frame(capture, {16}, bounded, 11).cancelled);
        frame.timestamp(12, SPA_META_HEADER_FLAG_CORRUPTED);
        offer();
        MIRAGE_CHECK(read_stream_frame(capture, {16}, {}, 10).error.code ==
                     "pet_capture_fence_unavailable");
        frame.timestamp(INT64_MAX);
        offer();
        MIRAGE_CHECK(read_stream_frame(capture, {16}, {}, 10).error.code ==
                     "pet_capture_fence_unavailable");
        MIRAGE_CHECK(wire.recycled == 5 && !capture.pending);
    }
    wire = {};
    {
        Capture capture;
        capture.stream = reinterpret_cast<pw_stream *>(std::uintptr_t{1});
        spa_list_init(&capture.listener.link);
        capture.format.size = {2, 2};
        FrameBuffer frame(42);
        spa_meta_cursor cursor{};
        cursor.id = 1;
        cursor.position = {1, 1};
        frame.meta = {SPA_META_Cursor, sizeof(cursor), &cursor};
        frame.buffer.metas = &frame.meta;
        frame.buffer.n_metas = 1;
        const auto offer = [&] {
            wire.ready = {&frame.source, nullptr, nullptr, nullptr};
            wire.index = 0;
            process(&capture);
        };
        offer();
        MIRAGE_CHECK(capture.cursor && capture.cursor->x == 1 && capture.cursor->y == 1);
        cursor.id = 0; // SPA contract: no new cursor data within the same stream.
        offer();
        MIRAGE_CHECK(capture.cursor && capture.cursor->x == 1);
        frame.buffer.n_metas = 0;
        offer();
        MIRAGE_CHECK(!capture.cursor);
        frame.buffer.n_metas = 1;
        cursor.id = 1;
        cursor.position.x = 2;
        offer();
        MIRAGE_CHECK(!capture.cursor); // Out of frame is never an injection origin.
    }
    active_wire = nullptr;
    pw_deinit();
    return testing::finish("portal_stream_retention_test");
}
