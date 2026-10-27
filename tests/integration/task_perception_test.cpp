#include "../../integration/mirador/src/perception_journal.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <atomic>
#include <kairo/executor.hpp>
#include <mirage/integration/continuous_desktop_session.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <stdexcept>
#include <thread>

namespace {
using namespace mirage::integration;
namespace d = mirage::desktop;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
struct Owner {
    kairo::Executor executor;
    Owner() {
        kairo::ExecutorConfig c;
        c.min_threads = 2;
        c.max_threads = 6;
        c.queue_capacity = 64;
        c.max_in_flight_tasks = 64;
        MIRAGE_CHECK(executor.initialize(c).ok);
    }
    ~Owner() { executor.shutdown(true); }
};
template <class F> bool until(F predicate) {
    const auto end = Clock::now() + 5s;
    while (!predicate()) {
        if (Clock::now() >= end)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
d::ImageFrame pixels(int phase = 1) {
    d::ImageFrame f;
    f.width = 64;
    f.height = 32;
    f.stride = 256;
    f.source_sequence = 1;
    f.pixels.assign(f.stride * f.height, static_cast<std::uint8_t>(phase * 60));
    return f;
}
PerceptionSample event(std::string kind = "text_changed", int x = 0) {
    PerceptionSample s;
    s.ok = true;
    s.source_epoch = 1;
    PerceptionEvent e;
    e.entity = 9;
    e.kind = std::move(kind);
    e.text = "sample";
    e.x = x;
    e.width = 8;
    e.height = 8;
    s.events.push_back(e);
    return s;
}
void retention() {
    detail::PerceptionJournal j;
    const auto now = Clock::now();
    auto p = std::make_shared<PerceptionPreview>();
    p->id = "f1";
    p->image = pixels();
    p->observed_at = now;
    std::weak_ptr<const PerceptionPreview> weak = p;
    j.append(event(), p, now);
    p.reset();
    auto oldview = j.view();
    MIRAGE_CHECK(oldview.events.size() == 1);
    j.acknowledge(oldview.newest, now + 1s);
    j.collect(now + 60s);
    MIRAGE_CHECK(j.view().events.size() == 1 && !weak.expired());
    j.collect(now + 61s);
    MIRAGE_CHECK(j.view().events.empty() && weak.expired());
    // Old views have only weak frame handles and cannot pin retired pixel buffers.
    MIRAGE_CHECK(oldview.previews[0].second.expired());
    j.append(event(), {}, now);
    j.collect(now + 120s);
    MIRAGE_CHECK(j.view().events.empty() && j.view().dropped == 1);
    detail::PerceptionJournal late_read;
    late_read.append(event(), {}, now);
    late_read.acknowledge(1, now + 119s);
    late_read.collect(now + 120s);
    MIRAGE_CHECK(late_read.view().events.size() == 1);
    late_read.collect(now + 179s);
    MIRAGE_CHECK(late_read.view().events.empty());
    for (int i = 0; i < 600; ++i)
        j.append(event(), {}, now);
    MIRAGE_CHECK(j.view().events.size() == 256 && j.view().journal_bytes <= 256 * 1024);
    j.focus({0, 0, 16, 16});
    const auto before = j.view().newest;
    j.append(event("text_changed", 40), {}, now);
    MIRAGE_CHECK(j.view().newest == before && j.view().filtered == 1);
    j.append(event("appeared", 40), {}, now);
    MIRAGE_CHECK(j.view().newest == before + 1);
    auto sample = event();
    sample.dropped = 10;
    j.append(sample, {}, now);
    const auto dropped = j.view().dropped;
    j.append(sample, {}, now);
    MIRAGE_CHECK(j.view().dropped ==
                 dropped + 1); // capacity eviction only; not cumulative double-count.
    for (int i = 0; i < 20; ++i) {
        auto frame = std::make_shared<PerceptionPreview>();
        frame->id = "f" + std::to_string(i);
        frame->image = pixels();
        frame->observed_at = now;
        j.append(event(), frame, now);
    }
    MIRAGE_CHECK(j.view().previews.size() == 8 && j.view().preview_bytes <= 8u << 20);
    const auto id = j.view().previews.back().first;
    j.acknowledge_frame(id);
    MIRAGE_CHECK(j.view().previews.size() == 7);
    sample.source_epoch = 2;
    j.append(sample, {}, now);
    MIRAGE_CHECK(j.view().events.size() == 1 && j.view().previews.empty());
}
class Ocr final : public mirador::OcrBackend {
  public:
    FakeOcrBackend identity;
    std::atomic<bool> hold{false}, entered{false};
    std::atomic<unsigned> calls{0};
    mirador::BackendInfo info() const override { return identity.info(); }
    mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &image, const mirador::OcrRequest &,
              const mirador::ExecutionContext &ctx) override {
        ++calls;
        entered = true;
        while (hold && !mirador::is_cancelled(ctx))
            std::this_thread::sleep_for(1ms);
        if (mirador::is_cancelled(ctx))
            return mirador::Status{mirador::ErrorCode::kCancelled, "cancelled"};
        const auto phase = std::to_integer<unsigned>(image.data[0]) / 60;
        if (!phase)
            return std::vector<mirador::TextRegion>{};
        mirador::TextRegion r;
        r.utf8_text = "temporary notice";
        r.confidence = .99F;
        r.bounds = {phase == 3 ? 20.F : 2.F, 2, 16, 10};
        return std::vector<mirador::TextRegion>{r};
    }
};
class Detector final : public mirador::DetectorBackend {
  public:
    FakeDetectorBackend backend;
    std::atomic<float> threshold{-1};
    mirador::BackendInfo info() const override { return backend.info(); }
    mirador::Result<std::vector<mirador::DetectionRegion>>
    detect(const mirador::ImageView &image, const mirador::DetectionRequest &request,
           const mirador::ExecutionContext &ctx) override {
        threshold = request.min_confidence;
        return backend.detect(image, request, ctx);
    }
};
void production_stage_settings_and_large_frame() {
    Owner owner;
    mirage::testing::FakeDesktopEnvironment env;
    d::VisualReferenceRegistry registry;
    Ocr ocr;
    Detector detector;
    VisualObservationPipelineConfig config;
    config.source_id = "production-settings";
    config.run_ocr = true;
    config.session.enable_image_session = true;
    config.session.enable_background_session = true;
    config.session.ocr_backend = &ocr;
    config.session.detector_backend = &detector;
    config.detector_marks = true;
    config.background_detector = true;
    config.detector_min_confidence = .42F;
    VisualObservationPipeline pipeline(owner.executor, *env.screen(), registry, config);
    std::string error;
    MIRAGE_CHECK(pipeline.start(error));
    auto frame = pixels();
    frame.width = 1080;
    frame.height = 800;
    frame.stride = 4320;
    frame.pixels.assign(frame.stride * frame.height, std::uint8_t{60});
    auto result = pipeline.observe_image(frame, "large", {}, {}, Clock::now() + 3s);
    MIRAGE_CHECK(result.ok && !result.events.empty());
    MIRAGE_CHECK(detector.threshold == .42F);
    pipeline.stop();
}
void pipeline_events() {
    Owner owner;
    mirage::testing::FakeDesktopEnvironment env;
    d::VisualReferenceRegistry registry;
    Ocr ocr;
    VisualObservationPipelineConfig config;
    config.session.enable_image_session = true;
    config.session.enable_background_session = true;
    config.session.ocr_backend = &ocr;
    config.run_ocr = true;
    VisualObservationPipeline pipeline(owner.executor, *env.screen(), registry, config);
    std::string error;
    MIRAGE_CHECK(pipeline.start(error));
    const auto observe = [&](int phase) {
        return pipeline.observe_image(pixels(phase), "task", {}, {}, Clock::now() + 2s);
    };
    auto a = observe(1);
    MIRAGE_CHECK(a.ok);
    MIRAGE_CHECK(!a.events.empty());
    auto b = observe(1);
    MIRAGE_CHECK(b.ok);
    MIRAGE_CHECK(b.objects_complete && b.objects.size() == 1);
    MIRAGE_CHECK(b.objects[0].content_observed && b.objects[0].object.identity_confirmed);
    MIRAGE_CHECK(b.objects[0].object.text == "temporary notice");
    detail::PerceptionJournal scene("pipeline-scene");
    scene.append(b, {}, Clock::now());
    MIRAGE_CHECK(scene.view().scene && scene.view().scene->updated.size() == 1);
    const auto identity = scene.view().scene->updated[0].object;
    MIRAGE_CHECK(std::any_of(b.events.begin(), b.events.end(),
                             [](const auto &e) { return e.kind == "appeared"; }));
    const auto calls = ocr.calls.load();
    auto same = observe(1);
    MIRAGE_CHECK(same.ok && same.skipped && ocr.calls == calls);
    auto moved1 = observe(3);
    auto moved2 = observe(3);
    MIRAGE_CHECK(moved1.ok && moved2.ok);
    scene.append(moved1, {}, Clock::now());
    scene.append(moved2, {}, Clock::now());
    MIRAGE_CHECK(scene.view().scene && !scene.view().scene->updated.empty());
    const auto moved_object = scene.view().scene->updated[0].object;
    if (moved_object.id == identity.id && moved_object.incarnation == identity.incarnation)
        // pixels(3) changes the entire background as well as moving the box:
        // this is a global layout barrier, not evidence of a pure move.
        MIRAGE_CHECK(moved_object.content_revision > identity.content_revision);
    MIRAGE_CHECK(std::any_of(moved2.events.begin(), moved2.events.end(), [](const auto &e) {
        return e.kind == "position_or_appearance_changed" || e.kind == "appeared";
    }));
    auto gone1 = observe(0);
    auto gone2 = observe(0);
    MIRAGE_CHECK(gone1.ok && gone2.ok);
    scene.append(gone1, {}, Clock::now());
    scene.append(gone2, {}, Clock::now());
    MIRAGE_CHECK(scene.view().scene && scene.view().scene->updated.empty());
    MIRAGE_CHECK(std::any_of(gone2.events.begin(), gone2.events.end(),
                             [](const auto &e) { return e.kind == "disappeared"; }));
    // Explicit ROI OCR uses a separate session and does not reset background identity.
    auto explicit_read = pipeline.read_image_ocr(pixels(), {}, Clock::now() + 2s, "roi");
    MIRAGE_CHECK(explicit_read.ok);
    auto after = observe(0);
    MIRAGE_CHECK(after.ok && after.source_epoch == 0 && after.skipped);
    // A background submission may not cancel an in-flight explicit OCR request.
    ocr.hold = true;
    ocr.entered = false;
    auto foreground = owner.executor.submit_auto(
        [&] { return pipeline.read_image_ocr(pixels(), {}, Clock::now() + 3s, "roi2"); });
    MIRAGE_CHECK(until([&] { return ocr.entered.load(); }));
    auto refused = observe(1);
    MIRAGE_CHECK(refused.ok && refused.skipped);
    ocr.hold = false;
    MIRAGE_CHECK(foreground.get().ok);
    ocr.hold = true;
    const auto expired =
        pipeline.observe_image(pixels(), "short-deadline", {}, {}, Clock::now() + 20ms);
    MIRAGE_CHECK(!expired.ok);
    ocr.hold = false;
    MIRAGE_CHECK(pipeline.read_image_ocr(pixels(), {}, Clock::now() + 2s, "after-deadline").ok);
    pipeline.stop();
}
struct SessionState {
    std::atomic<unsigned> calls{0}, inputs{0}, closed{0}, prompts_allowed{0};
    std::atomic<bool> fail{false}, invalid{false}, hold{false}, entered{false};
};
class Raw final : public d::DesktopCaptureSession {
    std::shared_ptr<SessionState> s_;

  public:
    explicit Raw(std::shared_ptr<SessionState> s) : s_(std::move(s)) {}
    ~Raw() override { ++s_->closed; }
    bool reusable() const override { return true; }
    bool input_supported() const override { return true; }
    d::CaptureOutcome capture(const d::CaptureLimits &limits,
                              const d::CancelToken &cancel) override {
        ++s_->calls;
        s_->entered = true;
        while (s_->hold && !cancel.cancelled())
            std::this_thread::sleep_for(1ms);
        if (limits.allow_authorization_prompt)
            ++s_->prompts_allowed;
        if (cancel.cancelled())
            return {false, true, {}, {"cancelled", "cancelled"}};
        if (s_->fail)
            return {false, false, {}, {"source_lost", "lost"}};
        auto f = pixels();
        if (s_->invalid)
            f.width = 0;
        f.source_sequence = s_->calls;
        return {true, false, std::move(f), {}};
    }
    d::InputOutcome input(const d::SharedInputAction &, const d::InputLimits &,
                          const d::CancelToken &) override {
        ++s_->inputs;
        return {true, false, {}, true, true};
    }
};
void resident_lifetime_and_consumers() {
    Owner owner;
    auto state = std::make_shared<SessionState>();
    std::atomic<bool> authorized{true};
    const PerceptionSessionFactory factory = [&](auto raw, auto options) {
        return connect_continuous_desktop(owner.executor, std::move(raw), std::move(options),
                                          [](const auto &, const auto &, const auto &, const auto &,
                                             const auto &) { return event("text_changed"); });
    };
    ResidentDesktopPerception resident(factory);
    PerceptionSessionOptions lifetime;
    lifetime.source_id = "runtime-resident";
    lifetime.authorized = [&] { return authorized.load(); };
    MIRAGE_CHECK(resident.adopt(std::make_unique<Raw>(state), lifetime, true));
    PerceptionSessionOptions task;
    task.source_id = "first-task";
    auto first = resident.attach(true, task);
    MIRAGE_CHECK(first.session && first.feed);
    auto readonly = resident.attach(false, {});
    MIRAGE_CHECK(readonly.session && !readonly.session->input_supported() &&
                 !readonly.session->input({}, {}, {}).ok && state->inputs == 0);
    MIRAGE_CHECK(until([&] { return bool(first.session->latest_frame().image); }));
    {
        auto pause = first.feed->pause();
        auto second = resident.attach(true, {});
        MIRAGE_CHECK(second.session && second.feed->view().events.empty());
        const auto calls = state->calls.load();
        const auto frame = first.session->latest_frame();
        MIRAGE_CHECK(frame.image && second.session->latest_frame().image == frame.image);
        MIRAGE_CHECK(second.session->capture_presented({}, {}, {}).ok && state->calls == calls);
        MIRAGE_CHECK(state->prompts_allowed == 0);
        first.session.reset();
        first.feed.reset();
        MIRAGE_CHECK(state->closed == 0); // Releasing a task does not close the runtime stream.
        // Cancelling one consumer neither revokes the grant nor affects a second consumer.
        task.cancelled.request_cancel();
        MIRAGE_CHECK(second.session->input({}, {}, {}).ok);
        const auto watermark = second.session->source_sequence();
        MIRAGE_CHECK(watermark == frame.image->source_sequence);
    }
    const auto before = state->calls.load();
    MIRAGE_CHECK(until([&] { return state->calls > before; })); // No task lease is required.
    auto a = resident.attach(true, {});
    auto b = resident.attach(true, {});
    // Attachment cursors can differ if publication lands between the two
    // calls. Wait for post-attachment history in both consumers.
    MIRAGE_CHECK(
        until([&] { return !a.feed->view().events.empty() && !b.feed->view().events.empty(); }));
    {
        auto pause = a.feed->pause();
        const auto b_events = b.feed->view().events.size();
        MIRAGE_CHECK(a.feed->acknowledge(a.feed->view().newest));
        MIRAGE_CHECK(b.feed->view().events.size() == b_events);
        const auto cursor = b.feed->view().newest;
        MIRAGE_CHECK(b.feed->wait(cursor, 30ms, {}) == "timeout");
        d::CancelToken cancel_wait;
        auto cancellation = owner.executor.submit_auto([cancel_wait]() mutable {
            std::this_thread::sleep_for(20ms);
            cancel_wait.request_cancel();
        });
        MIRAGE_CHECK(b.feed->wait(cursor, 2s, cancel_wait) == "cancelled");
        cancellation.get();
        a.feed->focus({1000, 1000, 1, 1});
        const auto outside = a.feed->view();
        const auto independent = b.feed->view();
        MIRAGE_CHECK(std::none_of(outside.events.begin(), outside.events.end(),
                                  [](const auto &e) { return e.kind == "text_changed"; }));
        MIRAGE_CHECK(std::any_of(independent.events.begin(), independent.events.end(),
                                 [](const auto &e) { return e.kind == "text_changed"; }));
    }
    MIRAGE_CHECK(b.feed->wait(b.feed->view().newest, 2s, {}) == "event");
    PerceptionSessionOptions cancelled;
    auto own = resident.attach(true, cancelled);
    cancelled.cancelled.request_cancel();
    MIRAGE_CHECK(!own.session->capture({}, {}).ok && own.feed->view().state == "stopped");
    MIRAGE_CHECK(b.session->input({}, {}, {}).ok);
    authorized = false;
    MIRAGE_CHECK(!b.session->latest_frame().image && !b.session->input({}, {}, {}).ok);
    resident.invalidate();
    MIRAGE_CHECK(state->closed == 1);
    authorized = true;
    MIRAGE_CHECK(resident.adopt(std::make_unique<Raw>(state), lifetime, true));
    auto renewed = resident.attach(true, {});
    MIRAGE_CHECK(renewed.session && !b.session->input({}, {}, {}).ok);
    resident.stop();
    MIRAGE_CHECK(!renewed.session->capture({}, {}).ok && state->closed == 2);
    MIRAGE_CHECK(!resident.adopt(std::make_unique<Raw>(state), lifetime, true));
    // Executor admission rejection is surfaced; a dead producer cannot be attached.
    owner.executor.shutdown(true);
    ResidentDesktopPerception rejected(factory);
    MIRAGE_CHECK(!rejected.adopt(std::make_unique<Raw>(state), lifetime, true));
    MIRAGE_CHECK(!rejected.attach(true, {}).session);
    PerceptionSessionOptions fallback_task;
    auto fallback = rejected.connect(std::make_unique<Raw>(state), lifetime, fallback_task, true);
    MIRAGE_CHECK(fallback.session && !fallback.feed && fallback.session->capture({}, {}).ok);
    fallback_task.cancelled.request_cancel();
    MIRAGE_CHECK(!fallback.session->capture({}, {}).ok);
}
void resident_blocking_shutdown_and_failure() {
    Owner owner;
    std::atomic<bool> throwing{false};
    ResidentDesktopPerception resident([&](auto raw, auto options) {
        return connect_continuous_desktop(
            owner.executor, std::move(raw), std::move(options),
            [&](const auto &, const auto &, const auto &, const auto &, const auto &) {
                if (throwing)
                    throw std::runtime_error("resident analysis fault");
                return event();
            });
    });
    auto blocked = std::make_shared<SessionState>();
    blocked->hold = true;
    PerceptionSessionOptions options;
    options.source_id = "resident-blocking";
    MIRAGE_CHECK(resident.adopt(std::make_unique<Raw>(blocked), options, true));
    MIRAGE_CHECK(until([&] { return blocked->entered.load(); }));
    const auto begin = Clock::now();
    resident.invalidate(); // Interrupts the running I/O, not merely the next timer tick.
    MIRAGE_CHECK(Clock::now() - begin < 2s && blocked->closed == 1);
    auto fault = std::make_shared<SessionState>();
    throwing = true;
    MIRAGE_CHECK(resident.adopt(std::make_unique<Raw>(fault), options, true));
    auto lease = resident.attach(true, {});
    // Admission can precede the injected exception; the retained feed reports its outcome.
    if (lease.feed)
        MIRAGE_CHECK(until([&] {
            return lease.feed->view().state == "failed" || lease.feed->view().state == "stopped";
        }));
    MIRAGE_CHECK(until([&] { return !resident.attach(true, {}).session; }));
    resident.stop();
    MIRAGE_CHECK(fault->closed == 1);
}
void task_adapter() {
    Owner owner;
    auto state = std::make_shared<SessionState>();
    std::atomic<bool> authorized{true};
    PerceptionSessionOptions options;
    options.source_id = "task";
    options.authorized = [&] { return authorized.load(); };
    auto connection =
        connect_continuous_desktop(owner.executor, std::make_unique<Raw>(state), options,
                                   [](const auto &, const auto &, const auto &, const auto &,
                                      const auto &) { return event("appeared"); });
    MIRAGE_CHECK(connection.feed && connection.session);
    MIRAGE_CHECK(until([&] { return !connection.feed->view().events.empty(); }));
    {
        auto pause = connection.feed->pause();
        bool rejected = false;
        for (unsigned i = 0; i < 1000; ++i)
            rejected = !connection.feed->acknowledge(1) || rejected;
        MIRAGE_CHECK(rejected && connection.feed->view().ack_rejected > 0);
        const auto count = state->calls.load();
        std::this_thread::sleep_for(600ms);
        MIRAGE_CHECK(state->calls == count);
        auto f = connection.session->capture({}, {});
        MIRAGE_CHECK(f.ok);
        MIRAGE_CHECK(connection.session->input({}, {}, {}).ok && state->inputs == 1);
    }
    MIRAGE_CHECK(until([&] { return connection.feed->view().frames >= 2; }));
    auto before = connection.feed->view();
    MIRAGE_CHECK(before.preview_bytes > 0);
    MIRAGE_CHECK(connection.feed->acknowledge(before.newest));
    state->fail = true;
    // DEC-116: one failed background sample degrades perception without
    // ending it — the journal (events and previews) stays readable, the raw
    // shared session stays usable for the foreground, and the feed view
    // carries the real capture error. The pre-DEC-116 session published
    // "failed" and cleared the journal here instead.
    MIRAGE_CHECK(until([&] { return connection.feed->view().state == "degraded"; }));
    MIRAGE_CHECK(connection.feed->view().error == "source_lost");
    MIRAGE_CHECK(!connection.feed->view().events.empty() &&
                 connection.feed->view().preview_bytes > 0);
    state->fail = false;
    MIRAGE_CHECK(connection.session->capture({}, {}).ok);
    MIRAGE_CHECK(until([&] { return connection.feed->view().state == "sampling"; }));
    authorized = false;
    MIRAGE_CHECK(connection.feed->view().state == "stopped");
    connection.session.reset();
    MIRAGE_CHECK(connection.feed->view().events.empty());
    auto malformed = std::make_shared<SessionState>();
    malformed->invalid = true;
    PerceptionSessionOptions malformed_options;
    malformed_options.source_id = "invalid-frame";
    std::atomic<bool> analyzed_invalid{false};
    auto invalid = connect_continuous_desktop(
        owner.executor, std::make_unique<Raw>(malformed), malformed_options,
        [&](const auto &, const auto &, const auto &, const auto &, const auto &) {
            analyzed_invalid = true;
            return event();
        });
    MIRAGE_CHECK(until([&] { return invalid.feed->view().state == "failed"; }));
    MIRAGE_CHECK(!analyzed_invalid);
    invalid.session.reset();
    // Executor registration rejection is exposed while foreground resource remains usable.
    owner.executor.shutdown(true);
    auto rejected = connect_continuous_desktop(
        owner.executor, std::make_unique<Raw>(std::make_shared<SessionState>()), options,
        [](const auto &, const auto &, const auto &, const auto &, const auto &) {
            return event();
        });
    authorized = true;
    MIRAGE_CHECK(rejected.feed->view().state == "failed");
    MIRAGE_CHECK(rejected.session->capture({}, {}).ok);
}
void committed_event_survives_late_foreground_pause() {
    Owner owner;
    std::atomic<bool> entered{false}, deliver{false};
    PerceptionSessionOptions options;
    options.source_id = "commit-before-pause";
    auto connection = connect_continuous_desktop(
        owner.executor, std::make_unique<Raw>(std::make_shared<SessionState>()), options,
        [&](const auto &, const auto &, const auto &, const auto &, const auto &) {
            entered = true;
            const auto end = Clock::now() + 2s;
            while (!deliver && Clock::now() < end)
                std::this_thread::sleep_for(1ms);
            return event("appeared"); // models an already committed Mirador cycle
        });
    MIRAGE_CHECK(until([&] { return entered.load(); }));
    auto pause = connection.feed->pause();
    deliver = true;
    MIRAGE_CHECK(until([&] { return !connection.feed->view().events.empty(); }));
    MIRAGE_CHECK(connection.feed->view().analysis_failed == 0);
    pause.reset();
    connection.session.reset();
}
void cancellation_exception_and_shutdown() {
    Owner owner;
    const auto exercise = [&](const std::string &id, bool shutdown) {
        std::atomic<bool> entered{false};
        PerceptionSessionOptions options;
        options.source_id = id;
        auto connection = connect_continuous_desktop(
            owner.executor, std::make_unique<Raw>(std::make_shared<SessionState>()), options,
            [&](const auto &, const auto &, const auto &, const auto &cancel, const auto &) {
                entered = true;
                while (!cancel.cancelled())
                    std::this_thread::sleep_for(1ms);
                PerceptionSample out;
                out.cancelled = true;
                return out;
            });
        MIRAGE_CHECK(until([&] { return entered.load(); }));
        if (shutdown)
            owner.executor.shutdown(true);
        else
            options.cancelled.request_cancel();
        MIRAGE_CHECK(until([&] { return connection.feed->view().state == "stopped"; }));
        const auto begin = Clock::now();
        connection.session.reset();
        MIRAGE_CHECK(Clock::now() - begin < 2s);
        MIRAGE_CHECK(connection.feed->view().preview_bytes == 0);
    };
    exercise("cancel-running", false);
    PerceptionSessionOptions exception;
    exception.source_id = "task-exception";
    auto failed = connect_continuous_desktop(
        owner.executor, std::make_unique<Raw>(std::make_shared<SessionState>()), exception,
        [](const auto &, const auto &, const auto &, const auto &, const auto &)
            -> PerceptionSample { throw std::runtime_error("injected observer failure"); });
    MIRAGE_CHECK(until([&] { return failed.feed->view().state == "failed"; }));
    MIRAGE_CHECK(failed.feed->view().events.empty() && failed.feed->view().preview_bytes == 0);
    failed.session.reset();
    exercise("shutdown-running", true);
}

} // namespace
int main() {
    try {
        resident_lifetime_and_consumers();
        resident_blocking_shutdown_and_failure();
        retention();
        production_stage_settings_and_large_frame();
        pipeline_events();
        task_adapter();
        committed_event_survives_late_foreground_pause();
        cancellation_exception_and_shutdown();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
    return mirage::testing::finish("task_perception");
}
