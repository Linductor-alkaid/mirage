#include "../support/test.hpp"
// Compile the private buffer boundary into this isolated test TU. The PipeWire
// loop is real; only buffer ownership calls are wrapped, with no portal grant.
#include "../../platform/linux/src/portal_pipewire_capture.cpp"
#include <array>

namespace {
struct WireStats {
    std::array<pw_buffer *, 4> ready{};
    std::size_t index = 0;
    unsigned recycled = 0, destroyed = 0;
};
WireStats *active_wire = nullptr; // Single synchronous test owner, never shared.
struct FrameBuffer {
    std::array<std::uint8_t, 16> pixels{};
    spa_chunk chunk{};
    spa_data plane{};
    spa_buffer buffer{};
    pw_buffer source{};
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
    active_wire = nullptr;
    pw_deinit();
    return testing::finish("portal_stream_retention_test");
}
