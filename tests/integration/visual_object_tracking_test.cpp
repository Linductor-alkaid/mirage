#include "../../integration/mirador/src/perception_journal.hpp"
#include "../../integration/mirador/src/visual_object_tracking.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <atomic>
#include <kairo/executor.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>

namespace {
using namespace mirage::integration;
namespace m = mirador;
namespace d = mirage::desktop;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

d::ImageFrame pixels(int x = 40, int y = 40, bool visible = true, int side = 128, int luma = 10) {
    d::ImageFrame f;
    f.width = f.height = side;
    f.stride = side * 4;
    f.pixels.assign(f.stride * side, 10);
    if (side == 320)
        for (int row = 0; row < side; ++row)
            for (int col = 200; col < side; ++col) {
                auto *p = f.pixels.data() + row * f.stride + col * 4;
                p[0] = p[1] = p[2] = static_cast<std::uint8_t>(luma);
            }
    if (visible) {
        std::uint32_t random = 37;
        for (int row = 0; row < 24; ++row)
            for (int col = 0; col < 24; ++col) {
                random = random * 1664525u + 1013904223u;
                auto *p = f.pixels.data() + (row + y) * f.stride + (col + x) * 4;
                p[0] = p[1] = p[2] = static_cast<std::uint8_t>(random >> 24);
            }
    }
    return f;
}
m::Frame frame(d::ImageFrame f, std::uint64_t sequence) {
    auto result = to_mirador_frame(f, "tracking-test", sequence, Clock::now());
    MIRAGE_CHECK(result.ok());
    return result.take_value();
}
m::ChangeReport changed() {
    m::ChangeReport c;
    c.classification = m::ChangeClassification::kPartial;
    c.changed_regions.push_back({24, 24, 64, 64});
    return c;
}
m::SemanticSnapshot snapshot(std::uint64_t entity = 7, const std::string &text = "button",
                             m::RectF bounds = {40, 40, 24, 24}) {
    m::SemanticSnapshot s;
    m::VisualRegion r;
    r.stable_id = entity;
    r.bounds = bounds;
    r.text = text;
    r.confidence = .99F;
    r.source_mask = static_cast<std::uint32_t>(m::RegionSource::kOcr);
    s.regions.push_back(r);
    return s;
}
m::TemporalSemanticState temporal(std::uint64_t sequence, std::uint64_t entity = 7,
                                  std::uint64_t instance = 1) {
    m::TemporalSemanticState t;
    m::TemporalEntityState e;
    e.stable_id = entity;
    e.instance_generation = instance;
    e.confirmed_frame = 1;
    e.observed_frame = sequence;
    e.observation = m::ObservationState::kObserved;
    t.entities.push_back(e);
    return t;
}
void movement_loss_recapture_and_expiry() {
    detail::VisualObjectTracking tracker;
    auto initial = frame(pixels(), 1);
    auto seeded = tracker.seed(initial, snapshot(), temporal(1), {}, {});
    MIRAGE_CHECK(seeded.error.empty() && seeded.tracks.size() == 1 && seeded.pool_bytes <= 65536);
    auto moved = tracker.sample(frame(pixels(42, 41), 2), changed(), {});
    MIRAGE_CHECK(moved && moved.value().verified == 1);
    if (!moved || moved.value().tracks.empty())
        return;
    const auto &track = moved.value().tracks[0];
    MIRAGE_CHECK(track.state == "tracking" && track.evidence == "appearance_confirmed");
    MIRAGE_CHECK(track.bounds.x == 42 && track.bounds.y == 41 && track.verified_sequence == 2);
    m::ChangeReport none;
    none.classification = m::ChangeClassification::kNone;
    auto reused = tracker.sample(frame(pixels(42, 41), 3), none, {});
    MIRAGE_CHECK(reused && reused.value().reused == 1 &&
                 reused.value().tracks[0].verified_sequence == 2);
    for (int n = 4; n <= 6; ++n) {
        auto blank = tracker.sample(frame(pixels(40, 40, false), n), changed(), {});
        MIRAGE_CHECK(blank && blank.value().requires_recognition);
        MIRAGE_CHECK(blank.value().tracks[0].state == (n == 6 ? "lost" : "uncertain"));
    }
    auto recaptured = tracker.sample(frame(pixels(40, 40), 7), changed(), {});
    MIRAGE_CHECK(recaptured && recaptured.value().tracks[0].reacquired);
    MIRAGE_CHECK(recaptured.value().tracks[0].entity == 7 &&
                 recaptured.value().tracks[0].state == "tracking");
    // Runtime cancellation after work starts invalidates the entire private pool.
    int polls = 0;
    m::ExecutionContext cancelled;
    cancelled.is_cancelled = [&] { return ++polls >= 6; };
    auto stopped = tracker.sample(frame(pixels(42, 40), 8), changed(), cancelled);
    MIRAGE_CHECK(!stopped && stopped.status().code() == m::ErrorCode::kCancelled);
    auto needs_seed = tracker.sample(frame(pixels(), 9), changed(), {});
    MIRAGE_CHECK(needs_seed && needs_seed.value().requires_recognition &&
                 needs_seed.value().pool_bytes == 0);
    MIRAGE_CHECK(tracker.seed(frame(pixels(), 10), snapshot(), temporal(10), {}, {}).error.empty());
    m::ExecutionContext expired;
    expired.deadline = Clock::now() - 1ms;
    auto timed = tracker.sample(frame(pixels(), 11), changed(), expired);
    MIRAGE_CHECK(!timed && timed.status().code() == m::ErrorCode::kTimeout);
}
void ambiguity_budget_and_instance_barriers() {
    detail::VisualObjectTracking tracker;
    auto s = snapshot();
    auto second = snapshot(8);
    s.regions.push_back(second.regions[0]);
    auto t = temporal(1);
    t.entities.push_back(temporal(1, 8).entities[0]);
    MIRAGE_CHECK(tracker.seed(frame(pixels(), 1), s, t, {}, {}).tracks.size() == 2);
    auto ambiguous = tracker.sample(frame(pixels(), 2), changed(), {});
    MIRAGE_CHECK(ambiguous && ambiguous.value().requires_recognition);
    MIRAGE_CHECK(ambiguous.value().tracks.size() == 2);
    for (const auto &track : ambiguous.value().tracks)
        MIRAGE_CHECK(track.evidence == "ambiguous" && track.discontinuous &&
                     track.verified_sequence == 1);
    tracker.reset();
    MIRAGE_CHECK(tracker.seed(frame(pixels(), 1), snapshot(), temporal(1), {}, {}).tracks.size() ==
                 1);
    auto replacement = tracker.seed(frame(pixels(), 2), snapshot(), temporal(2, 7, 2), {}, {});
    MIRAGE_CHECK(replacement.tracks.size() == 1 && replacement.tracks[0].discontinuous &&
                 replacement.tracks[0].instance_generation == 2);
    auto content =
        tracker.seed(frame(pixels(), 3), snapshot(7, "new caption"), temporal(3, 7, 2), {}, {});
    MIRAGE_CHECK(content.tracks[0].discontinuous);
    tracker.reset();
    MIRAGE_CHECK(
        tracker
            .seed(frame(pixels(), 1), snapshot(7, "large", {10, 10, 96, 40}), temporal(1), {}, {})
            .tracks.size() == 1);
    auto budget = tracker.sample(frame(pixels(), 2), changed(), {});
    MIRAGE_CHECK(budget && budget.value().requires_recognition &&
                 budget.value().error == "verification_budget");
    MIRAGE_CHECK(budget.value().pool_bytes <= 65536 &&
                 budget.value().tracks[0].verified_sequence == 1);
    tracker.reset();
    s = {};
    t = {};
    for (std::uint64_t id = 1; id <= 12; ++id) {
        s.regions.push_back(snapshot(id).regions[0]);
        t.entities.push_back(temporal(1, id).entities[0]);
    }
    auto capped = tracker.seed(frame(pixels(), 1), s, t, {}, {});
    MIRAGE_CHECK(capped.tracks.size() == 8 && capped.excluded == 4 && capped.pool_bytes <= 65536);
    s = {};
    t = {};
    for (std::uint64_t id = 20; id < 28; ++id) {
        s.regions.push_back(snapshot(id).regions[0]);
        t.entities.push_back(temporal(2, id).entities[0]);
    }
    auto evicted = tracker.seed(frame(pixels(), 2), s, t, {}, {});
    MIRAGE_CHECK(evicted.tracks.size() == 8 && evicted.evicted == 8 && evicted.pool_bytes <= 65536);
    m::ChangeReport global;
    global.classification = m::ChangeClassification::kGlobal;
    auto barrier = tracker.sample(frame(pixels(), 3), global, {});
    MIRAGE_CHECK(barrier && barrier.value().requires_recognition && barrier.value().tracks.empty());
    MIRAGE_CHECK(barrier.value().error == "global_layout_barrier" &&
                 barrier.value().pool_bytes == 0);
}
void geometry_candidates_and_retention_budget() {
    detail::VisualObjectTracking tracker;
    auto image = pixels(0, 0, false);
    MIRAGE_CHECK(tracker.seed(frame(image, 1), {}, {}, {}, {}).error.empty());
    for (int y = 40; y <= 70; ++y)
        for (int x = 40; x <= 70; ++x)
            if (x == 40 || x == 70 || y == 40 || y == 70) {
                auto *p = image.pixels.data() + y * image.stride + x * 4;
                p[0] = p[1] = p[2] = 240;
            }
    auto c = changed();
    c.changed_regions = {{32, 32, 48, 48}};
    auto proposals = tracker.sample(frame(image, 2), c, {});
    MIRAGE_CHECK(proposals && !proposals.value().candidates.empty() &&
                 proposals.value().candidates.size() <= 8);
    MIRAGE_CHECK(proposals.value().requires_recognition);
    detail::PerceptionJournal journal;
    PerceptionSample sample;
    sample.ok = sample.objects_complete = true;
    sample.source_epoch = sample.frame_sequence = 1;
    sample.tracking = proposals.take_value();
    journal.append(sample, {}, Clock::now());
    MIRAGE_CHECK(journal.view().tracking && !journal.view().tracking->candidates.empty());
    sample.frame_sequence = 2;
    sample.tracking->tracks.resize(9);
    journal.append(sample, {}, Clock::now());
    MIRAGE_CHECK(journal.view().tracking->error == "tracking_projection_budget_exceeded");
    journal.clear();
    MIRAGE_CHECK(!journal.view().tracking);
}
class Ocr final : public m::OcrBackend {
  public:
    FakeOcrBackend identity;
    std::atomic<unsigned> calls{0};
    m::BackendInfo info() const override { return identity.info(); }
    m::Result<std::vector<m::TextRegion>> recognize(const m::ImageView &, const m::OcrRequest &,
                                                    const m::ExecutionContext &) override {
        ++calls;
        m::TextRegion r;
        r.bounds = {208, 40, 24, 24};
        r.utf8_text = "icon";
        r.confidence = .99F;
        return std::vector<m::TextRegion>{r};
    }
};
void production_worker_and_foreground_isolation() {
    kairo::Executor executor;
    MIRAGE_CHECK(executor.initialize({}).ok);
    {
        mirage::testing::FakeDesktopEnvironment env;
        d::VisualReferenceRegistry registry;
        Ocr ocr;
        VisualObservationPipelineConfig config;
        config.source_id = "production-tracking";
        config.run_ocr = true;
        config.session.enable_image_session = config.session.enable_background_session = true;
        config.session.enable_object_tracking = true;
        config.session.ocr_backend = &ocr;
        config.experimental_recognition_refresh = 5000ms;
        VisualObservationPipeline pipeline(executor, *env.screen(), registry, config);
        std::string error;
        MIRAGE_CHECK(pipeline.start(error));
        const auto observe = [&](int luma, int dx = 0, bool visible = true,
                                 const std::string &key = "source") {
            return pipeline.observe_image(pixels(208 + dx, 40, visible, 320, luma), key, {}, {},
                                          Clock::now() + 3s);
        };
        MIRAGE_CHECK(observe(20).ok);
        MIRAGE_CHECK(observe(100).ok);
        auto initialized = observe(140);
        MIRAGE_CHECK(initialized.ok && initialized.media.active && initialized.tracking &&
                     initialized.tracking->tracks.size() == 1);
        const auto calls = ocr.calls.load();
        auto moved = observe(180, 2);
        MIRAGE_CHECK(moved.ok && moved.recognition_deferred && ocr.calls == calls);
        MIRAGE_CHECK(moved.tracking && moved.tracking->tracks[0].bounds.x == 210 &&
                     moved.tracking->verified == 1);
        MIRAGE_CHECK(!moved.objects[0].content_observed && moved.objects[0].object.pending);
        auto foreground = pipeline.read_image_ocr(pixels(208, 40, true, 320), {}, Clock::now() + 3s,
                                                  "foreground");
        MIRAGE_CHECK(foreground.ok && ocr.calls == calls + 1);
        auto lost = observe(220, 0, false);
        MIRAGE_CHECK(lost.ok && !lost.recognition_deferred &&
                     lost.recognition_reason == "tracking_revalidation");
        MIRAGE_CHECK(ocr.calls == calls + 2 && lost.tracking &&
                     lost.tracking->tracks[0].discontinuous);
        auto focused =
            pipeline.observe_image(pixels(208, 40, true, 320, 240), "source",
                                   d::WindowGeometry{0, 0, 100, 100}, {}, Clock::now() + 3s);
        MIRAGE_CHECK(focused.ok && focused.tracking && focused.tracking->tracks.size() == 1);
        MIRAGE_CHECK(focused.tracking->tracks[0].evidence == "adopted_recognition");
        auto replacement = observe(240, 0, true, "replacement");
        MIRAGE_CHECK(replacement.ok && replacement.tracking &&
                     replacement.source_epoch != initialized.source_epoch);
        MIRAGE_CHECK(
            !replacement.tracking->tracks.size()); // new recognition is unconfirmed, no adoption
        MIRAGE_CHECK(env.capture_calls == 0);
        pipeline.stop();
        MIRAGE_CHECK(!pipeline.running());
        // The very first CV verification may fail before any deferred view
        // has invalidated old labels. Same text/position on re-recognition
        // must still break the old appearance/semantic continuity.
        config.source_id = "tracking-early-loss";
        VisualObservationPipeline early(executor, *env.screen(), registry, config);
        MIRAGE_CHECK(early.start(error));
        const auto early_observe = [&](int luma, bool visible = true) {
            return early.observe_image(pixels(208, 40, visible, 320, luma), "early", {}, {},
                                       Clock::now() + 3s);
        };
        MIRAGE_CHECK(early_observe(20).ok);
        MIRAGE_CHECK(early_observe(100).ok);
        MIRAGE_CHECK(early_observe(140).ok);
        const auto first_loss = early_observe(180, false);
        MIRAGE_CHECK(first_loss.ok && !first_loss.recognition_deferred && first_loss.tracking);
        MIRAGE_CHECK(first_loss.tracking->tracks[0].discontinuous);
        MIRAGE_CHECK(first_loss.objects[0].invalidate_content);
        early.stop();
    }
    executor.shutdown(true);
}
} // namespace
int main() {
    try {
        movement_loss_recapture_and_expiry();
        ambiguity_budget_and_instance_barriers();
        geometry_candidates_and_retention_budget();
        production_worker_and_foreground_isolation();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
    return mirage::testing::finish("visual_object_tracking");
}
