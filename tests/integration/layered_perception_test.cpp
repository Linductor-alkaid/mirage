#include "../../integration/mirador/src/layered_visual_sampler.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <kairo/executor.hpp>
#include <mirage/integration/continuous_desktop_session.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <thread>

namespace {
using namespace mirage::integration;
namespace d = mirage::desktop;
namespace m = mirador;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
template <class F> bool until(F predicate) {
    const auto end = Clock::now() + 6s;
    while (!predicate()) {
        if (Clock::now() >= end)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
struct Owner {
    kairo::Executor executor;
    Owner() {
        kairo::ExecutorConfig config;
        config.min_threads = 2;
        config.max_threads = 6;
        config.queue_capacity = 64;
        config.max_in_flight_tasks = 64;
        MIRAGE_CHECK(executor.initialize(config).ok);
    }
    ~Owner() { executor.shutdown(true); }
};
d::ImageFrame pixels(int value = 100, int width = 128, int height = 128) {
    d::ImageFrame im;
    im.width = width;
    im.height = height;
    im.stride = std::size_t(width) * 4;
    im.pixels.assign(im.stride * height, static_cast<std::uint8_t>(value));
    return im;
}
PerceptionSample recognized(bool confirmed = true) {
    PerceptionSample out;
    out.ok = out.objects_complete = true;
    out.source_epoch = 1;
    out.frame_sequence = 1;
    PerceptionObject item;
    item.content_observed = true;
    item.object.id = 7;
    item.object.incarnation = 1;
    item.object.bounds = {40, 40, 24, 24};
    item.object.text = "button";
    item.object.confidence = .99;
    item.object.identity_confirmed = confirmed;
    item.object.pending = !confirmed;
    out.objects.push_back(item);
    return out;
}
void independent_baselines_age_media_and_cancel() {
    detail::LayeredVisualSampler sampler(false);
    auto original = pixels();
    const auto at = Clock::now();
    auto frame = detail::sampled_frame(original, "sample", 1, at);
    MIRAGE_CHECK(sampler.sample(frame, 2s, {}).value().recognize);
    MIRAGE_CHECK(sampler.mark_submitted());
    auto sample = recognized();
    MIRAGE_CHECK(sampler.recognize(frame, sample, {}).ok());
    auto minor = pixels(106);
    auto second = sampler.sample(detail::sampled_frame(minor, "sample", 2, at + 100ms), 2s, {});
    MIRAGE_CHECK(second && !second.value().recognize);
    MIRAGE_CHECK(second.value().same_as_submitted);
    auto cancelled = pixels(200);
    m::ExecutionContext stop;
    stop.is_cancelled = [] { return true; };
    MIRAGE_CHECK(
        !sampler.sample(detail::sampled_frame(cancelled, "sample", 3, at + 200ms), 2s, stop));
    auto drift = pixels(112);
    auto fourth = sampler.sample(detail::sampled_frame(drift, "sample", 4, at + 300ms), 2s, {});
    MIRAGE_CHECK(fourth && fourth.value().recognize);
    MIRAGE_CHECK(fourth.value().adjacent.classification == m::ChangeClassification::kNone);
    MIRAGE_CHECK(
        !fourth.value().same_as_submitted); // Drift is relative to the actual admitted frame.
    MIRAGE_CHECK(fourth.value().cumulative.classification != m::ChangeClassification::kNone);
    MIRAGE_CHECK(sampler.sample(detail::sampled_frame(original, "sample", 5, at + 2100ms), 2s, {})
                     .value()
                     .recognize);
    sample.recognition_deferred = true;
    MIRAGE_CHECK(!sampler.recognize(frame, sample, {}));
    sampler.reset();
    sample = recognized(false);
    MIRAGE_CHECK(sampler.recognize(frame, sample, {}).ok());
    MIRAGE_CHECK(sampler.sample(detail::sampled_frame(original, "sample", 6, at + 100ms), 2s, {})
                     .value()
                     .reason == "confirmation_pending");
    sampler.reset();
    sample = recognized();
    MIRAGE_CHECK(sampler.sample(frame, 2s, {}).ok());
    sample.media = {true, .25, .25, .5, .5};
    MIRAGE_CHECK(sampler.recognize(frame, sample, {}).ok());
    auto inside = pixels();
    for (int y = 40; y < 64; ++y)
        for (int x = 40; x < 64; ++x)
            for (int c = 0; c < 3; ++c)
                inside.pixels[y * inside.stride + x * 4 + c] = 200;
    auto held = sampler.sample(detail::sampled_frame(inside, "sample", 7, at + 100ms), 2s, {});
    MIRAGE_CHECK(held && !held.value().recognize);
    inside.pixels.assign(inside.pixels.size(), 200);
    MIRAGE_CHECK(sampler.sample(detail::sampled_frame(inside, "sample", 8, at + 200ms), 2s, {})
                     .value()
                     .recognize);
}
void compact_cv_plane_and_coordinate_recovery() {
    const auto at = Clock::now();
    auto original = pixels(100, 1001, 503);
    auto compact = detail::cv_work_image(detail::sampled_frame(original, "compact", 1, at).image);
    MIRAGE_CHECK(compact && compact.value().width() == 250 && compact.value().height() == 125);
    MIRAGE_CHECK(compact.value().format() == m::PixelFormat::kGray8);
    MIRAGE_CHECK(compact.value().byte_size() == 250 * 125);
    MIRAGE_CHECK(compact.value().view().data !=
                 reinterpret_cast<const std::byte *>(original.pixels.data()));
    MIRAGE_CHECK(compact.value().view().data[0] == std::byte{100});
    MIRAGE_CHECK(!detail::cv_work_image({}));
    auto tiny = pixels(50, 17, 9);
    auto small = detail::cv_work_image(detail::sampled_frame(tiny, "small", 1, at).image);
    MIRAGE_CHECK(small && small.value().width() == 17 && small.value().height() == 9);
    auto integral = pixels(100, 1024, 512);
    auto integral_cv =
        detail::cv_work_image(detail::sampled_frame(integral, "integral", 1, at).image);
    MIRAGE_CHECK(integral_cv && integral_cv.value().width() == 256 &&
                 integral_cv.value().height() == 128);

    const d::WindowGeometry b{901, 401, 100, 102};
    const auto reduced = detail::map_image_bounds(b, 1001, 503, 320, 160);
    const auto recovered = detail::map_image_bounds(reduced, 320, 160, 1001, 503);
    MIRAGE_CHECK(recovered.x <= b.x && recovered.y <= b.y);
    MIRAGE_CHECK(recovered.x + recovered.width >= b.x + b.width &&
                 recovered.y + recovered.height >= b.y + b.height);
    MIRAGE_CHECK(recovered.x + recovered.width <= original.width &&
                 recovered.y + recovered.height <= original.height);
    MIRAGE_CHECK(b.x - recovered.x <= 4 && b.y - recovered.y <= 4);

    detail::LayeredVisualSampler sampler(true);
    auto full = recognized();
    full.objects.front().object.bounds = {100, 100, 64, 64};
    MIRAGE_CHECK(sampler.sample(detail::sampled_frame(original, "compact", 1, at), 5s, {}));
    MIRAGE_CHECK(sampler.recognize(detail::sampled_frame(original, "compact", 1, at), full, {}));
    auto stable = sampler.sample(detail::sampled_frame(original, "compact", 2, at + 100ms), 5s, {});
    MIRAGE_CHECK(stable && !stable.value().recognize);
    MIRAGE_CHECK(stable.value().cv_width == 250 && stable.value().cv_height == 125 &&
                 stable.value().cv_bytes == 250 * 125);
    MIRAGE_CHECK(stable.value().tracking && stable.value().tracking->tracks.size() == 1);
    if (!stable.value().tracking || stable.value().tracking->tracks.empty())
        return;
    const auto &tracked = stable.value().tracking->tracks.front().bounds;
    MIRAGE_CHECK(tracked.x <= 100 && tracked.y <= 100 && tracked.x + tracked.width >= 164 &&
                 tracked.y + tracked.height >= 164);
    MIRAGE_CHECK(full.objects.front().object.bounds.x == 100); // Input metadata is untouched.
    auto resized = pixels(100, 1000, 500); // Also 250x125: source layout still invalidates.
    auto barrier = sampler.sample(detail::sampled_frame(resized, "compact", 3, at + 200ms), 5s, {});
    MIRAGE_CHECK(barrier && barrier.value().recognize && !barrier.value().tracking);
    full.objects.front().object.bounds = {100, 100, 10, 10}; // <8 CV pixels, no identity proof.
    MIRAGE_CHECK(sampler.recognize(detail::sampled_frame(original, "compact", 4, at), full, {}));
    auto excluded =
        sampler.sample(detail::sampled_frame(original, "compact", 5, at + 300ms), 5s, {});
    MIRAGE_CHECK(excluded && excluded.value().tracking &&
                 excluded.value().tracking->tracks.empty());
}
void confirmed_recognition_seeds_live_cv_tracking() {
    const auto texture = [](int x, int y) {
        auto image = pixels();
        std::uint32_t random = 12345;
        for (int row = 0; row < 24; ++row)
            for (int col = 0; col < 24; ++col)
                for (int c = 0; c < 3; ++c) {
                    random = random * 1664525u + 1013904223u;
                    image.pixels[(y + row) * image.stride + (x + col) * 4 + c] =
                        static_cast<std::uint8_t>(random >> 24);
                }
        return image;
    };
    detail::LayeredVisualSampler sampler(true);
    const auto at = Clock::now();
    auto initial = texture(40, 40);
    auto first = detail::sampled_frame(initial, "tracking-owner", 1, at);
    MIRAGE_CHECK(sampler.sample(first, 2s, {}).ok());
    MIRAGE_CHECK(sampler.recognize(first, recognized(), {}).ok());
    auto moved = texture(42, 41);
    auto tracked =
        sampler.sample(detail::sampled_frame(moved, "tracking-owner", 2, at + 100ms), 2s, {});
    MIRAGE_CHECK(tracked && tracked.value().tracking && tracked.value().tracking->verified == 1);
    if (tracked && tracked.value().tracking && !tracked.value().tracking->tracks.empty()) {
        const auto &item = tracked.value().tracking->tracks.front();
        MIRAGE_CHECK(item.entity == 7 && item.instance_generation == 1);
        MIRAGE_CHECK(item.bounds.x == 42 && item.bounds.y == 41 && item.verified_sequence == 2);
        MIRAGE_CHECK(item.evidence == "appearance_confirmed");
    }
    auto blank = pixels();
    auto lost =
        sampler.sample(detail::sampled_frame(blank, "tracking-owner", 3, at + 200ms), 2s, {});
    MIRAGE_CHECK(lost && lost.value().recognize && lost.value().tracking->requires_recognition);
}

struct SourceState {
    std::atomic<unsigned> calls{0}, closed{0};
    std::atomic<int> width{128}, height{128}, phase{0};
    std::atomic<int> intensity{100};
    std::atomic<bool> fail{false};
};
class Raw final : public d::DesktopCaptureSession {
    std::shared_ptr<SourceState> state_;

  public:
    explicit Raw(std::shared_ptr<SourceState> s) : state_(std::move(s)) {}
    ~Raw() override { ++state_->closed; }
    bool reusable() const override { return true; }
    d::CaptureOutcome capture(const d::CaptureLimits &, const d::CancelToken &cancel) override {
        if (cancel.cancelled())
            return {false, true, {}, {"cancelled", "retired"}};
        ++state_->calls;
        if (state_->fail)
            return {false, false, {}, {"source_lost", "lost"}};
        auto image = pixels(state_->intensity, state_->width, state_->height);
        if (state_->phase && image.width >= 256 && image.height >= 256)
            for (int y = 64; y < 80; ++y)
                for (int x = 64; x < 80; ++x)
                    for (int c = 0; c < 3; ++c)
                        image.pixels[y * image.stride + x * 4 + c] = state_->phase == 1 ? 200 : 30;
        image.source_sequence = state_->calls;
        return {true, false, std::move(image), {}};
    }
};
struct Ocr final : m::OcrBackend {
    FakeOcrBackend identity;
    std::atomic<unsigned> calls{0}, cancelled{0};
    std::atomic<bool> hold{true};
    std::atomic<unsigned> hold_call{0};
    m::BackendInfo info() const override { return identity.info(); }
    m::Result<std::vector<m::TextRegion>> recognize(const m::ImageView &, const m::OcrRequest &,
                                                    const m::ExecutionContext &ctx) override {
        const auto call = ++calls;
        while (hold && (hold_call == 0 || hold_call == call) && !m::is_cancelled(ctx))
            std::this_thread::sleep_for(1ms);
        if (m::is_cancelled(ctx)) {
            ++cancelled;
            return m::Status{m::ErrorCode::kCancelled, "retired"};
        }
        m::TextRegion region;
        region.bounds = {40, 40, 24, 24};
        region.utf8_text = "button";
        region.confidence = .99F;
        return std::vector<m::TextRegion>{region};
    }
};
void production_pipeline_cv_progresses_during_ocr_and_preserves_history() {
    Owner owner;
    mirage::testing::FakeDesktopEnvironment environment;
    d::VisualReferenceRegistry registry;
    Ocr ocr;
    VisualObservationPipelineConfig config;
    config.source_id = "layered-pipeline";
    config.run_ocr = true;
    config.session.enable_image_session = config.session.enable_background_session = true;
    config.session.ocr_backend = &ocr;
    VisualObservationPipeline pipeline(owner.executor, *environment.screen(), registry, config);
    std::string error;
    MIRAGE_CHECK(pipeline.start(error));
    const auto invalid =
        pipeline.observe_image(pixels(), "invalid-provenance", {}, {}, Clock::now() + 2s, {}, 1);
    MIRAGE_CHECK(!invalid.ok && invalid.error.code == "invalid_argument" && ocr.calls == 0);
    auto state = std::make_shared<SourceState>();
    PerceptionSessionOptions options;
    options.source_id = "layered-source";
    options.sample_period = 100ms;
    auto source = connect_layered_desktop(
        owner.executor, std::make_unique<Raw>(state), options,
        [&](const auto &image, const auto &key, const auto &focus, const auto &cancel,
            auto deadline, auto at, auto generation, const auto &work) {
            return pipeline.observe_image(image, key, focus, cancel, deadline, at, generation,
                                          work);
        },
        2s, true);
    MIRAGE_CHECK(until([&] {
        auto v = source.feed->view();
        return ocr.calls == 1 && v.dispatch && v.dispatch->sampled >= 5;
    }));
    auto during = source.feed->view();
    MIRAGE_CHECK(during.frames >= 5 && during.dispatch->admitted == 1 &&
                 during.dispatch->superseded == 0); // Static pending work is coalesced.
    MIRAGE_CHECK(ocr.cancelled == 0 && !during.scene);
    state->intensity = 106;
    const auto before_drift = during.dispatch->sampled;
    MIRAGE_CHECK(until([&] { return source.feed->view().dispatch->sampled >= before_drift + 2; }));
    MIRAGE_CHECK(source.feed->view().dispatch->admitted == 1);
    state->intensity = 112;
    MIRAGE_CHECK(until([&] { return source.feed->view().dispatch->admitted >= 2; }));
    MIRAGE_CHECK(during.dispatch->retained_pixel_bytes <= 20u * 1024 * 1024);
    ocr.hold = false;
    MIRAGE_CHECK(until([&] {
        auto v = source.feed->view();
        return v.analyzed >= 2 && v.tracking && !v.tracking->tracks.empty() && v.dispatch &&
               !v.dispatch->pending;
    }));
    const auto after = source.feed->view();
    MIRAGE_CHECK(after.tracking && after.tracking->pool_bytes <= 65536);
    MIRAGE_CHECK(!after.scene && after.recognition_deferred);
    MIRAGE_CHECK(!after.events.empty() && after.preview_bytes > 0);
    MIRAGE_CHECK(after.scene_observed_at < Clock::now());
    const auto calls = ocr.calls.load();
    const auto sampled = after.dispatch->sampled;
    MIRAGE_CHECK(until([&] { return source.feed->view().dispatch->sampled >= sampled + 3; }));
    MIRAGE_CHECK(ocr.calls == calls); // Static CV does not run OCR, or fake its age.
    const auto now = source.feed->view();
    MIRAGE_CHECK(now.scene_observed_at == after.scene_observed_at);
    MIRAGE_CHECK(now.newest == after.newest);
    MIRAGE_CHECK(now.tracking && now.tracking->reused > 0);
    MIRAGE_CHECK(environment.capture_calls == 0);
    // Explicit foreground OCR cancels a blocked background request on the
    // existing analysis worker; independent sampling remains advisory.
    ocr.hold_call = ocr.calls + 1;
    ocr.hold = true;
    source.feed->focus({0, 0, 80, 80});
    MIRAGE_CHECK(until([&] { return ocr.calls == ocr.hold_call; }));
    MIRAGE_CHECK(pipeline.read_image_ocr(pixels(), {}, Clock::now() + 2s, "explicit-layered").ok);
    MIRAGE_CHECK(ocr.cancelled > 0);
    ocr.hold = false;
    MIRAGE_CHECK(environment.capture_calls == 0);
    MIRAGE_CHECK(until([&] { return !source.feed->view().dispatch->pending; }));
    const auto next_call = ocr.calls.load();
    // A short pause cancels via a monotonic serial, even between capture ticks.
    const auto cancelled_before = now.dispatch->cancelled;
    ocr.hold_call = next_call + 1;
    ocr.hold = true;
    source.feed->focus({0, 0, 64, 64});
    const auto resumed = until([&] { return ocr.calls > next_call; });
    if (!resumed) {
        const auto v = source.feed->view();
        std::fprintf(stderr,
                     "focus recovery: state=%s error=%s reason=%s frames=%llu analyzed=%llu "
                     "failed=%llu cv=%llu admitted=%llu rejected=%llu\n",
                     v.state.c_str(), v.error.c_str(), v.recognition_reason.c_str(),
                     static_cast<unsigned long long>(v.frames),
                     static_cast<unsigned long long>(v.analyzed),
                     static_cast<unsigned long long>(v.analysis_failed),
                     static_cast<unsigned long long>(v.dispatch->sampled),
                     static_cast<unsigned long long>(v.dispatch->admitted),
                     static_cast<unsigned long long>(v.dispatch->rejected));
    }
    MIRAGE_CHECK(resumed);
    { auto pause = source.feed->pause(); }
    ocr.hold = false;
    MIRAGE_CHECK(until([&] { return source.feed->view().dispatch->cancelled > cancelled_before; }));
    source.session.reset();
    MIRAGE_CHECK(state->closed == 1 && source.feed->view().state == "stopped");
    pipeline.stop();
}
void original_dimensions_late_result_and_generation_barrier() {
    Owner owner;
    auto state = std::make_shared<SourceState>();
    state->width = 2048;
    // Keep the 1.28 mapping in BOTH axes; full-height work is a separate
    // diagnostic because TSAN can exhaust the unchanged 100ms CV budget.
    // MSVC alone raises the CRT getenv as a warnings-as-errors deprecation
    // (C4996) that the Linux gate never sees; the flag only toggles size.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const bool large_replay = std::getenv("MIRAGE_TEST_LARGE_REPLAY") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    state->height = large_replay ? 512 : 128;
    std::atomic<bool> hold{true};
    std::atomic<unsigned> calls{0};
    PerceptionSessionOptions options;
    options.source_id = "original";
    options.sample_period = 100ms;
    auto source = connect_layered_desktop(
        owner.executor, std::make_unique<Raw>(state), options,
        [&](const auto &image, const auto &, const auto &, const auto &cancel, auto, auto,
            std::uint64_t generation, const auto &) {
            ++calls;
            while (hold && !cancel.cancelled())
                std::this_thread::sleep_for(1ms);
            auto out = recognized();
            out.source_epoch = generation;
            out.frame_sequence = image.source_sequence;
            PerceptionEvent e;
            e.kind = "appeared";
            e.x = 10;
            e.y = 20;
            e.width = 30;
            e.height = 40;
            e.frame_sequence = image.source_sequence;
            out.events.push_back(e);
            return out;
        },
        5s);
    const bool delayed = until([&] { return calls == 1 && state->calls >= 3; });
    MIRAGE_CHECK(delayed);
    const auto released = Clock::now();
    hold = false;
    const bool history = until([&] { return !source.feed->view().events.empty(); });
    MIRAGE_CHECK(history);
    const auto view = source.feed->view();
    if (!delayed || !history || view.events.empty()) {
        std::cerr << "Delayed source failed: callbacks=" << calls.load()
                  << " captures=" << state->calls.load() << " state=" << view.state
                  << " error=" << view.error << " failures=" << view.analysis_failed
                  << " timeouts=" << (view.dispatch ? view.dispatch->timed_out : 0) << '\n';
        source.session.reset();
        return; // Preserve failed assertions without reading an empty event vector.
    }
    const auto &event = view.events.front();
    MIRAGE_CHECK(event.frame_sequence == 1 && event.observed_at < released);
    MIRAGE_CHECK(event.x > 12.7 && event.x < 12.9 && event.y > 25.5 && event.y < 25.7);
    MIRAGE_CHECK(!view.scene && view.recognition_deferred);
    if (!view.previews.empty()) {
        auto preview = view.previews.front().second.lock();
        MIRAGE_CHECK(preview && preview->image.source_sequence == 1);
        MIRAGE_CHECK(preview && preview->observed_at == event.observed_at);
    }
    hold = true;
    const auto previous = calls.load();
    source.feed->focus({0, 0, 500, large_replay ? 500 : 100});
    const bool focus_started = until([&] { return calls > previous; });
    MIRAGE_CHECK(focus_started);
    if (!focus_started) {
        const auto failed = source.feed->view();
        std::cerr << "Focus failed: callbacks=" << calls.load() << " previous=" << previous
                  << " captures=" << state->calls.load() << " state=" << failed.state
                  << " error=" << failed.error << " failures=" << failed.analysis_failed;
        if (failed.dispatch)
            std::cerr << " cv=" << failed.dispatch->sampled
                      << " rejected=" << failed.dispatch->rejected
                      << " cancelled=" << failed.dispatch->cancelled
                      << " timed_out=" << failed.dispatch->timed_out;
        std::cerr << '\n';
        source.session.reset();
        return;
    }
    state->width = 1024;
    MIRAGE_CHECK(until([&] { return source.feed->view().dispatch->cancelled > 0; }));
    hold = false;
    const bool resized_history = until([&] { return !source.feed->view().events.empty(); });
    MIRAGE_CHECK(resized_history);
    if (!resized_history) {
        const auto failed = source.feed->view();
        std::cerr << "Resize history failed: callbacks=" << calls.load()
                  << " captures=" << state->calls.load() << " state=" << failed.state
                  << " error=" << failed.error << " failures=" << failed.analysis_failed;
        if (failed.dispatch)
            std::cerr << " cv=" << failed.dispatch->sampled
                      << " cancelled=" << failed.dispatch->cancelled
                      << " timed_out=" << failed.dispatch->timed_out;
        std::cerr << '\n';
    }
    for (const auto &e : source.feed->view().events)
        MIRAGE_CHECK(e.frame_sequence > 1); // Retired geometry's journal is cleared.
    state->fail = true;
    MIRAGE_CHECK(until([&] { return source.feed->view().state == "degraded"; }));
    state->fail = false;
    source.session.reset();
}
void rejection_failure_timeout_and_authority() {
    Owner owner;
    auto state = std::make_shared<SourceState>();
    std::atomic<bool> authorized{true};
    PerceptionSessionOptions options;
    options.source_id = "failed";
    options.sample_period = 100ms;
    options.authorized = [&] { return authorized.load(); };
    auto source = connect_layered_desktop(
        owner.executor, std::make_unique<Raw>(state), options,
        [](const auto &, const auto &, const auto &, const auto &, auto, auto, auto,
           const auto &) -> PerceptionSample { throw std::runtime_error("backend"); },
        2s);
    MIRAGE_CHECK(until([&] { return source.feed->view().analysis_failed > 0; }));
    MIRAGE_CHECK(source.feed->view().error == "analysis_exception");
    authorized = false;
    MIRAGE_CHECK(source.feed->view().state == "stopped" && source.feed->view().events.empty());
    source.session.reset();
    authorized = true;
    options.source_id = "invalid-refresh";
    auto invalid = connect_layered_desktop(
        owner.executor, std::make_unique<Raw>(state), options,
        [](const auto &, const auto &, const auto &, const auto &, auto, auto, auto, const auto &) {
            return recognized();
        },
        1ms);
    MIRAGE_CHECK(invalid.feed->view().state == "failed");
    MIRAGE_CHECK(invalid.session->capture({}, {}).ok); // Foreground source is preserved.
    invalid.session.reset();
    owner.executor.shutdown(true);
    options.source_id = "closed-executor";
    auto closed = connect_layered_desktop(
        owner.executor, std::make_unique<Raw>(state), options,
        [](const auto &, const auto &, const auto &, const auto &, auto, auto, auto, const auto &) {
            return recognized();
        },
        2s);
    MIRAGE_CHECK(closed.feed->view().state == "failed");
}
void timeout_and_executor_shutdown_close_the_source() {
    Owner owner;
    auto state = std::make_shared<SourceState>();
    PerceptionSessionOptions options;
    options.source_id = "timeout-source";
    options.sample_period = 100ms;
    auto source = connect_layered_desktop(
        owner.executor, std::make_unique<Raw>(state), options,
        [](const auto &, const auto &, const auto &, const auto &cancel, auto, auto, auto,
           const auto &) {
            while (!cancel.cancelled())
                std::this_thread::sleep_for(1ms);
            return recognized(); // A late success must not become semantic evidence.
        },
        2s, true);
    MIRAGE_CHECK(until([&] {
        auto v = source.feed->view();
        return v.dispatch && v.dispatch->timed_out > 0 && v.analysis_failed > 0;
    }));
    const auto timed = source.feed->view();
    MIRAGE_CHECK(timed.events.empty() && !timed.scene && timed.error == "deadline_exceeded");
    MIRAGE_CHECK(state->calls > 5 && timed.dispatch->sampled > 5);
    owner.executor.shutdown(true);
    MIRAGE_CHECK(source.feed->view().state == "stopped");
    MIRAGE_CHECK(!source.session->reusable());
    source.session.reset();
    MIRAGE_CHECK(state->closed == 1);
}

void paint(d::ImageFrame &image, int x, int y, int width, int height, int value) {
    for (int row = y; row < y + height; ++row)
        for (int col = x; col < x + width; ++col)
            for (int c = 0; c < 3; ++c)
                image.pixels[row * image.stride + col * 4 + c] = static_cast<std::uint8_t>(value);
}
void regional_gates_select_capabilities_without_renewing_full_baseline() {
    detail::LayeredVisualSampler sampler(false);
    auto original = pixels(100, 256, 256);
    const auto at = Clock::now();
    auto full = recognized();
    full.objects.front().object.bounds = {32, 32, 96, 96};
    full.objects.front().object.source = d::VisualRegionSource::kOcr;
    MIRAGE_CHECK(sampler.sample(detail::sampled_frame(original, "roi", 1, at), 2s, {}));
    MIRAGE_CHECK(sampler.recognize(detail::sampled_frame(original, "roi", 1, at), full, {}));
    auto changed = original;
    paint(changed, 64, 64, 16, 16, 200);
    auto frame = detail::sampled_frame(changed, "roi", 2, at + 100ms);
    const auto probe = sampler.sample(frame, 2s, {}).value();
    MIRAGE_CHECK(probe.recognize && probe.work.roi && probe.work.ocr && !probe.work.detector);
    if (!probe.work.roi)
        return;
    MIRAGE_CHECK(probe.work.roi->x == 24 && probe.work.roi->width == 112);
    PerceptionSample partial;
    partial.ok = true;
    partial.source_epoch = 1;
    partial.recognition = probe.work;
    PerceptionEvent event;
    event.kind = "regional_text_observed";
    event.text = "actual";
    event.x = event.y = 64;
    event.width = event.height = 16;
    partial.events.push_back(event);
    const auto accepted = sampler.recognize_region(frame, partial, {});
    MIRAGE_CHECK(accepted && accepted.value());
    auto static_probe =
        sampler.sample(detail::sampled_frame(changed, "roi", 3, at + 200ms), 2s, {});
    MIRAGE_CHECK(static_probe && !static_probe.value().recognize);
    paint(changed, 64, 64, 16, 16, 30);
    frame = detail::sampled_frame(changed, "roi", 4, at + 300ms);
    MIRAGE_CHECK(sampler.sample(frame, 2s, {}).value().recognize);
    m::ExecutionContext cancel;
    cancel.is_cancelled = [] { return true; };
    MIRAGE_CHECK(!sampler.recognize_region(frame, partial, cancel));
    MIRAGE_CHECK(sampler.sample(frame, 2s, {}).value().recognize);
    const auto identical_output = sampler.recognize_region(frame, partial, {});
    MIRAGE_CHECK(identical_output && !identical_output.value());
    MIRAGE_CHECK(!sampler.sample(frame, 2s, {}).value().recognize);
    // Partial success never postpones the peripheral/full-frame audit.
    const auto audit =
        sampler.sample(detail::sampled_frame(changed, "roi", 5, at + 2100ms), 2s, {});
    MIRAGE_CHECK(audit && audit.value().recognize && !audit.value().work.roi);
    paint(changed, 224, 224, 16, 16, 200);
    const auto outside =
        sampler.sample(detail::sampled_frame(changed, "roi", 6, at + 500ms), 2s, {});
    MIRAGE_CHECK(outside && outside.value().recognize && !outside.value().work.roi);
    MIRAGE_CHECK(!sampler.recognize(frame, partial, {}));
    sampler.reset();
    MIRAGE_CHECK(!sampler.recognize_region(frame, partial, {}));
    full.objects.front().object.source = d::VisualRegionSource::kDetector;
    full.objects.front().object.text.clear();
    full.objects.front().object.label = "icon";
    MIRAGE_CHECK(sampler.recognize(detail::sampled_frame(original, "roi", 7, at), full, {}));
    changed = original;
    paint(changed, 64, 64, 16, 16, 200);
    const auto detected =
        sampler.sample(detail::sampled_frame(changed, "roi", 8, at + 100ms), 2s, {});
    MIRAGE_CHECK(detected && detected.value().work.roi && !detected.value().work.ocr &&
                 detected.value().work.detector);
    changed = original;
    paint(changed, 192, 192, 16, 16, 200);
    const auto unknown =
        sampler.sample(detail::sampled_frame(changed, "roi", 9, at + 200ms), 2s, {});
    MIRAGE_CHECK(unknown && unknown.value().work.roi && unknown.value().work.ocr &&
                 unknown.value().work.detector);
    // Missing optional detector must not cause every static sample to retry OCR.
    partial.recognition = unknown.value().work;
    partial.recognition->detector = false;
    partial.events.front().x = partial.recognition->roi->x;
    partial.events.front().y = partial.recognition->roi->y;
    auto unknown_frame = detail::sampled_frame(changed, "roi", 9, at + 200ms);
    MIRAGE_CHECK(sampler.recognize_region(unknown_frame, partial, {}));
    MIRAGE_CHECK(!sampler.sample(unknown_frame, 2s, {}).value().recognize);
}
// Real PP-OCRv5 replay produced [45,133,218,45], while the coarse CV block
// ends at y=180. Two extra pixels must not force a 1.5s icon inference.
void thin_text_routing_tolerates_edges_but_preserves_ambiguity() {
    const auto at = Clock::now();
    auto original = pixels(100, 640, 360);
    auto changed = original;
    paint(changed, 160, 144, 80, 25, 200);
    auto baseline = recognized();
    auto &o = baseline.objects.front().object;
    o.bounds = {45, 133, 218, 45};
    o.source = d::VisualRegionSource::kOcr;
    const auto probe = [&](const PerceptionSample &sample, d::ImageFrame &image) {
        detail::LayeredVisualSampler sampler(false);
        MIRAGE_CHECK(sampler.recognize(detail::sampled_frame(original, "thin", 1, at), sample, {}));
        return sampler.sample(detail::sampled_frame(image, "thin", 2, at + 100ms), 5s, {})
            .take_value();
    };
    const auto text = probe(baseline, changed);
    MIRAGE_CHECK(text.recognize && text.work.roi && text.work.ocr && !text.work.detector);
    MIRAGE_CHECK(text.work.roi && text.work.roi->y + text.work.roi->height >= 180);
    auto near = original;
    paint(near, 160, 180, 80, 25, 200); // Not bounded by the 8px tolerance.
    auto unknown = probe(baseline, near);
    MIRAGE_CHECK(unknown.work.ocr && unknown.work.detector);
    auto ambiguous = baseline;
    auto detection = ambiguous.objects.front();
    detection.object.id = 8;
    detection.object.bounds = {160, 144, 16, 16};
    detection.object.source = d::VisualRegionSource::kDetector;
    detection.object.text.clear();
    detection.object.label = "icon";
    ambiguous.objects.push_back(detection);
    const auto mixed = probe(ambiguous, changed);
    MIRAGE_CHECK(mixed.work.ocr && mixed.work.detector);
    ambiguous.objects.back().object.source = d::VisualRegionSource::kOcr;
    ambiguous.objects.back().object.text = "mixed";
    const auto blocker = probe(ambiguous, changed);
    MIRAGE_CHECK(blocker.work.ocr && blocker.work.detector);
    auto many = baseline;
    for (std::uint64_t n = 1; n <= 32; ++n) {
        auto entry = baseline.objects.front();
        entry.object.id = n + 10;
        entry.object.bounds = {400, 20 + int(n) * 8, 20, 6};
        many.objects.push_back(entry);
    }
    const auto budget = probe(many, changed);
    MIRAGE_CHECK(budget.work.ocr && budget.work.detector);
    // Real 1600x900 replay: changed blocks reach y=450 while OCR ends at
    // y=436. Carry the tolerance through the 5:1 CV scale; retain full text.
    detail::LayeredVisualSampler large(false);
    auto larger = pixels(100, 1600, 900);
    auto large_sample = recognized();
    large_sample.objects.front().object.bounds = {119, 341, 535, 95};
    large_sample.objects.front().object.source = d::VisualRegionSource::kOcr;
    MIRAGE_CHECK(
        large.recognize(detail::sampled_frame(larger, "large-text", 1, at), large_sample, {}));
    auto large_change = larger;
    paint(large_change, 400, 360, 200, 63, 200);
    auto large_probe =
        large.sample(detail::sampled_frame(large_change, "large-text", 2, at + 100ms), 5s, {});
    MIRAGE_CHECK(large_probe && large_probe.value().work.roi && large_probe.value().work.ocr &&
                 !large_probe.value().work.detector);
    if (large_probe && large_probe.value().work.roi) {
        const auto &r = *large_probe.value().work.roi;
        MIRAGE_CHECK(r.x <= 119 && r.y <= 341 && r.x + r.width >= 654 && r.y + r.height >= 450);
    }
}
struct RegionalOcr final : m::OcrBackend {
    FakeOcrBackend identity;
    unsigned calls = 0;
    int width = 0, height = 0;
    bool fail = false, many = false;
    m::BackendInfo info() const override { return identity.info(); }
    m::Result<std::vector<m::TextRegion>> recognize(const m::ImageView &im, const m::OcrRequest &,
                                                    const m::ExecutionContext &) override {
        ++calls;
        width = im.width;
        height = im.height;
        if (fail)
            return m::Status{m::ErrorCode::kBackendFailure, "injected"};
        m::TextRegion line;
        line.bounds = {2, 3, 12, 8};
        line.utf8_text = "region";
        line.confidence = .99F;
        return std::vector<m::TextRegion>(many ? 33 : 1, line);
    }
};
void actual_regional_backend_execution_preserves_global_state() {
    Owner owner;
    mirage::testing::FakeDesktopEnvironment environment;
    d::VisualReferenceRegistry registry;
    RegionalOcr ocr;
    FakeDetectorBackend detector;
    VisualObservationPipelineConfig config;
    config.source_id = "regional-pipeline";
    config.session.enable_image_session = config.session.enable_background_session = true;
    config.session.ocr_backend = &ocr;
    config.session.detector_backend = &detector;
    config.background_detector = true;
    config.experimental_recognition_refresh = 2s;
    VisualObservationPipeline pipeline(owner.executor, *environment.screen(), registry, config);
    std::string error;
    MIRAGE_CHECK(pipeline.start(error));
    auto image = pixels(100, 256, 256);
    const PerceptionRecognitionWork text{true, false, d::WindowGeometry{40, 50, 48, 32}};
    const auto run = [&](const PerceptionRecognitionWork &work = PerceptionRecognitionWork{},
                         std::uint64_t generation = 1, d::CancelToken cancel = {}) {
        image.source_sequence++;
        return pipeline.observe_image(image, "regional", {}, cancel, Clock::now() + 2s,
                                      Clock::now() - 1ms, generation, work);
    };
    MIRAGE_CHECK(!run(text).ok && ocr.calls == 0 && detector.calls() == 0);
    const auto baseline = run();
    MIRAGE_CHECK(baseline.ok && baseline.objects_complete && baseline.recognition &&
                 baseline.recognition->ocr && baseline.recognition->detector);
    MIRAGE_CHECK(ocr.calls == 1 && ocr.width == 256 && detector.calls() == 1);
    const auto region = run(text);
    MIRAGE_CHECK(region.ok && !region.objects_complete && region.objects.empty() &&
                 region.source_epoch == baseline.source_epoch);
    MIRAGE_CHECK(region.events.size() == 1 && region.events.front().x == 42 &&
                 region.events.front().y == 53 && !region.events.front().confirmed &&
                 !region.events.front().entity && !region.events.front().instance_generation);
    MIRAGE_CHECK(ocr.calls == 2 && ocr.width == 48 && ocr.height == 32 && detector.calls() == 1);
    PerceptionRecognitionWork detect{false, true, text.roi};
    const auto detected = run(detect);
    MIRAGE_CHECK(detected.ok && detected.events.size() == 1 &&
                 detected.events.front().kind == "regional_detection_observed");
    MIRAGE_CHECK(ocr.calls == 2 && detector.calls() == 2);
    PerceptionRecognitionWork both{true, true, text.roi};
    MIRAGE_CHECK(run(both).ok && ocr.calls == 3 && detector.calls() == 3);
    ocr.fail = true;
    const auto failed = run(both);
    MIRAGE_CHECK(!failed.ok && failed.events.empty() && detector.calls() == 3);
    ocr.fail = false;
    ocr.many = true;
    MIRAGE_CHECK(!run(text).ok && detector.calls() == 3);
    ocr.many = false;
    auto invalid = text;
    invalid.roi = {0, 0, 256, 256};
    const auto calls = ocr.calls;
    MIRAGE_CHECK(!run(invalid).ok && ocr.calls == calls);
    MIRAGE_CHECK(!run(text, 2).ok && ocr.calls == calls);
    d::CancelToken cancelled;
    cancelled.request_cancel();
    MIRAGE_CHECK(!run(text, 1, cancelled).ok && ocr.calls == calls);
    // The next full pass retains the same source epoch after arbitrary regional
    // successes/failures. No raw probe reset the coordinator or source policy.
    const auto after = run();
    MIRAGE_CHECK(after.ok && after.objects_complete && after.source_epoch == baseline.source_epoch);
    MIRAGE_CHECK(environment.capture_calls == 0);
    pipeline.stop();
}

struct AdaptiveOcr final : m::OcrBackend {
    FakeOcrBackend identity;
    std::atomic<unsigned> calls{0};
    m::BackendInfo info() const override { return identity.info(); }
    m::Result<std::vector<m::TextRegion>> recognize(const m::ImageView &im, const m::OcrRequest &,
                                                    const m::ExecutionContext &) override {
        ++calls;
        m::TextRegion line;
        line.bounds = im.width == 256 ? m::RectF{32, 32, 96, 96} : m::RectF{8, 8, 96, 96};
        line.utf8_text = im.width == 256 ? "baseline" : "regional";
        line.confidence = .99F;
        return std::vector<m::TextRegion>{line};
    }
};
void legacy_host_cv_mapping_preserves_recognition_inputs_and_cache() {
    Owner owner;
    RegionalOcr ocr;
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    VisualSessionHost host(owner.executor, "legacy-resolution", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto image = pixels(100, 1600, 900);
    VisualAnalysisRequest request;
    request.analyze_change = request.run_ocr = request.fuse = true;
    request.display_transform =
        m::make_identity(m::CoordinateSpaceId::kOriented, m::CoordinateSpaceId::kDisplay);
    request.ocr.output_space = m::CoordinateSpaceId::kOriented;
    request.ocr.max_side = 1600;
    request.ocr.cache_policy = m::CachePolicy::kRefresh;
    const auto run = [&](std::uint64_t n) {
        request.frame = detail::sampled_frame(image, "legacy-resolution", n, Clock::now());
        return host.submit(request).get();
    };
    request.run_ocr = request.fuse = false;
    const auto cv_only = run(1);
    MIRAGE_CHECK(cv_only.cache_stats.frame_cache_entries == 0 &&
                 cv_only.cache_stats.result_cache_entries == 0 && ocr.calls == 0);
    request.run_ocr = request.fuse = true;
    auto first = run(2);
    MIRAGE_CHECK(first.snapshot && ocr.width == 1600 && ocr.height == 900);
    paint(image, 1200, 400, 120, 100, 220);
    auto changed = run(3);
    MIRAGE_CHECK(changed.snapshot &&
                 changed.change.classification == m::ChangeClassification::kPartial);
    MIRAGE_CHECK(!changed.change.changed_regions.empty() &&
                 changed.change.changed_regions.front().x >= 1000);
    MIRAGE_CHECK(changed.snapshot && !changed.snapshot->change.changed_regions.empty() &&
                 changed.snapshot->change.changed_regions.front().x >= 1000);
    MIRAGE_CHECK(ocr.calls == 2 && ocr.width == 1600 && ocr.height == 900);
    request.ocr.cache_policy = m::CachePolicy::kReadWrite;
    auto cached = run(4);
    MIRAGE_CHECK(cached.snapshot && ocr.calls == 2); // Refresh entry shared with ReadWrite.
    image = pixels(100, 1000, 500);
    request.ocr.cache_policy = m::CachePolicy::kRefresh;
    MIRAGE_CHECK(run(5).change.classification == m::ChangeClassification::kGlobal);
    image = pixels(100, 1001, 503); // Both shrink to 250x125.
    auto resized = run(6);
    MIRAGE_CHECK(resized.change.classification == m::ChangeClassification::kGlobal);
    MIRAGE_CHECK(resized.snapshot &&
                 resized.snapshot->change.classification == m::ChangeClassification::kGlobal);
    host.stop();
}
void capture_owner_routes_real_regional_work_and_suppresses_equal_content() {
    Owner owner;
    mirage::testing::FakeDesktopEnvironment environment;
    d::VisualReferenceRegistry registry;
    AdaptiveOcr ocr;
    FakeDetectorConfig dc;
    dc.regions = {{{192, 192, 24, 24}, 0, "icon", .99F}};
    FakeDetectorBackend detector(dc);
    VisualObservationPipelineConfig config;
    config.source_id = "adaptive-owner";
    config.session.enable_image_session = config.session.enable_background_session = true;
    config.session.ocr_backend = &ocr;
    config.session.detector_backend = &detector;
    config.background_detector = true;
    config.experimental_recognition_refresh = 5s;
    VisualObservationPipeline pipeline(owner.executor, *environment.screen(), registry, config);
    std::string error;
    MIRAGE_CHECK(pipeline.start(error));
    auto state = std::make_shared<SourceState>();
    state->width = state->height = 256;
    PerceptionSessionOptions options;
    options.source_id = "adaptive-source";
    options.sample_period = 100ms;
    auto source = connect_layered_desktop(
        owner.executor, std::make_unique<Raw>(state), options,
        [&](const auto &image, const auto &key, const auto &focus, const auto &cancel,
            auto deadline, auto at, auto generation, const auto &work) {
            return pipeline.observe_image(image, key, focus, cancel, deadline, at, generation,
                                          work);
        },
        5s);
    MIRAGE_CHECK(until([&] {
        auto v = source.feed->view();
        return v.analyzed >= 2 && v.dispatch && !v.dispatch->pending;
    }));
    const auto before = source.feed->view();
    const auto full_detector_calls = detector.calls();
    state->phase = 1;
    MIRAGE_CHECK(until([&] {
        const auto v = source.feed->view();
        return v.dispatch && v.dispatch->regional >= 1 && !v.dispatch->pending;
    }));
    const auto first = source.feed->view();
    MIRAGE_CHECK(first.dispatch->ocr_completed > before.dispatch->ocr_completed);
    MIRAGE_CHECK(detector.calls() == full_detector_calls &&
                 first.dispatch->detector_completed == before.dispatch->detector_completed);
    MIRAGE_CHECK(!first.scene && first.recognition_deferred);
    MIRAGE_CHECK(std::any_of(first.events.begin(), first.events.end(), [](const auto &e) {
        return e.kind == "regional_text_observed" && !e.confirmed && !e.entity &&
               e.text == "regional";
    }));
    const auto calls = ocr.calls.load();
    const auto frames = first.frames;
    MIRAGE_CHECK(until([&] { return source.feed->view().frames >= frames + 3; }));
    MIRAGE_CHECK(ocr.calls == calls); // Changed relative to full baseline, but region is unchanged.
    state->phase = 2;
    MIRAGE_CHECK(until([&] {
        const auto v = source.feed->view();
        return v.dispatch && v.dispatch->regional > first.dispatch->regional &&
               !v.dispatch->pending;
    }));
    const auto second = source.feed->view();
    MIRAGE_CHECK(second.newest ==
                 first.newest); // Pixels changed; actual recognized content did not.
    MIRAGE_CHECK(detector.calls() == full_detector_calls && !second.scene);
    source.session.reset();
    MIRAGE_CHECK(state->closed == 1);
    pipeline.stop();
}

void raw_regional_host_leaves_temporal_age_and_full_baseline_untouched() {
    Owner owner;
    FakeOcrBackend ocr;
    VisualSessionConfig config;
    config.enable_image_session = config.enable_background_session = true;
    config.ocr_backend = &ocr;
    VisualSessionHost host(owner.executor, "raw-history", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto image = pixels(100, 256, 256);
    VisualAnalysisRequest full;
    full.frame = detail::sampled_frame(image, "raw-history", 1, Clock::now());
    full.image_session = full.background_session = true;
    full.run_ocr = full.fuse = true;
    full.analysis_policy_revision = 1;
    full.ocr.cache_policy = m::CachePolicy::kRefresh;
    full.ocr.output_space = m::CoordinateSpaceId::kOriented;
    full.continuous = VisualContinuousRequest{};
    full.continuous->source_revision = 1;
    full.continuous->full_frame_evidence_digest = 7;
    m::AttentionCondition condition;
    condition.roi = m::RectF{0, 0, 256, 256};
    full.continuous->attention.conditions.push_back(condition);
    auto completed = host.submit(full).get();
    MIRAGE_CHECK(completed.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    VisualContinuousQuery query;
    query.image_session = query.background_session = true;
    query.expected_source_revision = 1;
    const auto before = host.query_continuous(query).get();
    MIRAGE_CHECK(before.queried);
    VisualAnalysisRequest raw;
    raw.frame = detail::sampled_frame(image, "raw-history", 2, Clock::now());
    raw.image_session = raw.background_session = raw.regional_only = raw.run_ocr = true;
    raw.analyze_change = false;
    raw.ocr.roi = m::RectF{40, 50, 48, 32};
    raw.ocr.cache_policy = m::CachePolicy::kRefresh;
    raw.ocr.output_space = m::CoordinateSpaceId::kOriented;
    const auto region = host.submit(raw).get();
    MIRAGE_CHECK(region.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted &&
                 !region.snapshot && !region.continuous && !region.spatial &&
                 region.text.size() == 1);
    MIRAGE_CHECK(region.cache_stats.background_signature_bytes == 4096 &&
                 region.cache_stats.result_cache_entries >= 1 &&
                 region.cache_stats.continuous_coordination_bytes > 0);
    const auto after = host.query_continuous(query).get();
    MIRAGE_CHECK(after.queried);
    if (before.queried && after.queried) {
        MIRAGE_CHECK(before.queried->last_analyzed_frame == after.queried->last_analyzed_frame);
        MIRAGE_CHECK(before.queried->source_epoch == after.queried->source_epoch);
        MIRAGE_CHECK(before.queried->policy_revision == after.queried->policy_revision);
        MIRAGE_CHECK(before.queried->temporal == after.queried->temporal);
        MIRAGE_CHECK(after.queried->analysis_age >= before.queried->analysis_age);
    }
    raw.fuse = true;
    const auto calls = ocr.calls();
    MIRAGE_CHECK(host.submit(raw).get().outcome.kind == VisualAnalysisOutcome::Kind::kRejected);
    MIRAGE_CHECK(ocr.calls() == calls);
    raw.fuse = false;
    raw.ocr.roi_space = m::CoordinateSpaceId::kDisplay;
    MIRAGE_CHECK(host.submit(raw).get().outcome.kind == VisualAnalysisOutcome::Kind::kRejected);
    MIRAGE_CHECK(ocr.calls() == calls);
    raw.ocr.roi_space = m::CoordinateSpaceId::kOriented;
    raw.external_cancel.request_cancel();
    MIRAGE_CHECK(host.submit(raw).get().outcome.kind == VisualAnalysisOutcome::Kind::kCancelled);
    // A CV-only probe still recognizes the original full-frame bootstrap.
    auto sample = full;
    sample.frame.sequence = 3;
    sample.run_ocr = sample.fuse = false;
    sample.sample_only = true;
    MIRAGE_CHECK(host.submit(sample).get().outcome.kind == VisualAnalysisOutcome::Kind::kSampled);
    MIRAGE_CHECK(ocr.calls() == calls);
    host.stop();
}

} // namespace
int main() {
    legacy_host_cv_mapping_preserves_recognition_inputs_and_cache();
    compact_cv_plane_and_coordinate_recovery();
    thin_text_routing_tolerates_edges_but_preserves_ambiguity();
    raw_regional_host_leaves_temporal_age_and_full_baseline_untouched();
    capture_owner_routes_real_regional_work_and_suppresses_equal_content();
    regional_gates_select_capabilities_without_renewing_full_baseline();
    actual_regional_backend_execution_preserves_global_state();
    independent_baselines_age_media_and_cancel();
    confirmed_recognition_seeds_live_cv_tracking();
    production_pipeline_cv_progresses_during_ocr_and_preserves_history();
    original_dimensions_late_result_and_generation_barrier();
    rejection_failure_timeout_and_authority();
    timeout_and_executor_shutdown_close_the_source();
    return mirage::testing::finish("layered_perception_test");
}
