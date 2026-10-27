// DEC-118 significance gating (independent verification): drives
// VisualObservationPipeline::observe_image directly with synthetic desktop
// frames and deterministic config thresholds, pinning every behavioral
// promise of the media-activity state machine:
//   a. entity-free tiny change regions are suppressed;
//   b. a large changing region confirms into a media region (started event,
//      sample.media projection) and then stops per-sample event noise;
//   c. a scene cut inside the active region emits one throttled keyframe
//      event and no bare ROI event;
//   d. consecutive non-overlapping samples deactivate the region (stopped);
//   e. entity (OCR) events are never gated, and the predictive OCR hold
//      engages while changes are media-explained and recovers when a
//      non-tiny change lands outside the region;
//   f. significance_gate=false restores the legacy per-sample ROI events.

#include "../../integration/mirador/src/perception_journal.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <atomic>
#include <cmath>
#include <string>

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

constexpr int kFrameSide = 320; // <= 1600 (continuous source bound)
constexpr std::size_t kStride = kFrameSide * 4;

d::ImageFrame uniform(std::uint8_t luma) {
    d::ImageFrame f;
    f.width = kFrameSide;
    f.height = kFrameSide;
    f.stride = kStride;
    f.source_sequence = 1;
    f.pixels.assign(f.stride * f.height, luma);
    return f;
}
/// Luma `luma` painted into [x0,x1) x [y0,y1) over a `base` background.
d::ImageFrame patched(const d::ImageFrame &base, int x0, int y0, int x1, int y1,
                      std::uint8_t luma) {
    d::ImageFrame f = base;
    ++f.source_sequence;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto offset =
                static_cast<std::size_t>(y) * f.stride + static_cast<std::size_t>(x) * 4;
            f.pixels[offset] = f.pixels[offset + 1] = f.pixels[offset + 2] = luma;
        }
    return f;
}
// The "video player": the right 3/8 of the frame carries the changing content
// (one connected 24/64-block ROI, area ratio 0.375 — strictly below the 0.5
// global-layout barrier so confirmation cycles stay kPartial), the rest is
// stable UI chrome.
constexpr int kMediaX0 = 5 * kFrameSide / 8;
constexpr int kMediaWidth = kFrameSide - kMediaX0;
d::ImageFrame media(std::uint8_t luma) {
    return patched(uniform(10), kMediaX0, 0, kFrameSide, kFrameSide, luma);
}
/// A scene cut inside the media region: horizontal luminance ramp over the
/// same region (the flat region's dHash flips many bits).
d::ImageFrame keyframe(int shift) {
    d::ImageFrame f = uniform(10);
    ++f.source_sequence;
    for (int y = 0; y < kFrameSide; ++y)
        for (int x = kMediaX0; x < kFrameSide; ++x) {
            const auto value =
                static_cast<std::uint8_t>((x - kMediaX0) * 255 / (kMediaWidth - 1) + shift);
            const auto offset =
                static_cast<std::size_t>(y) * f.stride + static_cast<std::size_t>(x) * 4;
            f.pixels[offset] = f.pixels[offset + 1] = f.pixels[offset + 2] = value;
        }
    return f;
}
/// A change confined to the bottom-left 2x2-block corner while the right-half
/// media region stays constant: a non-tiny region that never overlaps it.
d::ImageFrame outside_change(std::uint8_t luma) {
    return patched(media(140), 0, kFrameSide / 2, kFrameSide / 4, kFrameSide, luma);
}
/// A ~20x20 px bright square at the origin: one changed block -> one
/// 1/64-of-frame ROI, below the tiny threshold configured in these scenarios.
d::ImageFrame tiny_spot(const d::ImageFrame &base, std::uint8_t luma) {
    return patched(base, 0, 0, 20, 20, luma);
}

/// Empty OCR stand-in: observe_image always requests OCR, so the gate-only
/// scenarios need a live backend that recognizes nothing (no entities, no
/// semantic events -> the pure pixel-change gating path).
class SilentOcr final : public mirador::OcrBackend {
  public:
    FakeOcrBackend identity;
    mirador::BackendInfo info() const override { return identity.info(); }
    mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &, const mirador::OcrRequest &,
              const mirador::ExecutionContext &) override {
        return std::vector<mirador::TextRegion>{};
    }
};

/// Fixed-caption OCR stand-in for the hold/recovery scenario: stable text and
/// bounds inside the media region, so the entity confirms once and then stops
/// generating semantic events.
class CaptionOcr final : public mirador::OcrBackend {
  public:
    FakeOcrBackend identity;
    std::atomic<unsigned> calls{0};
    std::atomic<unsigned> revision{0};
    std::atomic<bool> change_caption{false};
    mirador::BackendInfo info() const override { return identity.info(); }
    mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &, const mirador::OcrRequest &,
              const mirador::ExecutionContext &) override {
        ++calls;
        if (change_caption.exchange(false))
            ++revision;
        mirador::TextRegion r;
        r.utf8_text = revision.load() ? "live caption " + std::to_string(revision.load())
                                      : "live caption";
        r.confidence = .99F;
        r.bounds = {210.F, 10.F, 100.F, 20.F}; // inside the media region
        return std::vector<mirador::TextRegion>{r};
    }
};

VisualObservationPipelineConfig gate_config(const std::string &source, mirador::OcrBackend &ocr,
                                            std::chrono::milliseconds keyframe_interval,
                                            int miss_samples) {
    VisualObservationPipelineConfig config;
    config.source_id = source;
    config.session.enable_image_session = true;
    config.session.enable_background_session = true;
    config.session.ocr_backend = &ocr;
    // Deterministic thresholds over the 8x8 comparison-block grid (one block
    // = 1/64 of the frame area = 0.0156; the media ROI is 0.375, the outside
    // ROI 0.125, single-block transients 0.0156):
    config.media_region_min_area_ratio = 0.2F; // media = the 3/8-frame ROI only
    config.tiny_region_area_ratio = 0.02F;     // single-block ROIs are transient
    config.media_confirm_samples = 2;
    config.media_miss_samples = miss_samples;
    config.media_keyframe_similarity = 0.92F;
    config.media_event_min_interval = keyframe_interval;
    return config;
}

/// One executor + fake screen + registry; the pipeline itself is created per
/// scenario so every scenario owns its gating state machine.
struct Rig {
    Owner owner;
    mirage::testing::FakeDesktopEnvironment env;
    d::VisualReferenceRegistry registry;

    std::unique_ptr<VisualObservationPipeline> start(VisualObservationPipelineConfig config) {
        auto pipeline = std::make_unique<VisualObservationPipeline>(owner.executor, *env.screen(),
                                                                    registry, std::move(config));
        std::string error;
        MIRAGE_CHECK(pipeline->start(error));
        return pipeline;
    }
    PerceptionSample observe(VisualObservationPipeline &pipeline, const d::ImageFrame &frame) {
        return pipeline.observe_image(frame, "gating", {}, {}, Clock::now() + 3s);
    }
};

std::size_t count_kind(const PerceptionSample &s, const char *kind) {
    return static_cast<std::size_t>(std::count_if(
        s.events.begin(), s.events.end(), [&](const auto &e) { return e.kind == kind; }));
}
bool near(double value, double expected) { return std::fabs(value - expected) < 0.02; }

/// a. Entity-free tiny change regions (cursor / caret stand-in) produce no
/// event and no media state.
void tiny_regions_are_suppressed() {
    Rig rig;
    SilentOcr ocr;
    auto pipeline = rig.start(gate_config("gating-tiny", ocr, 1h, 10));
    MIRAGE_CHECK(rig.observe(*pipeline, uniform(10)).ok); // baseline
    const auto spot = rig.observe(*pipeline, tiny_spot(uniform(10), 200));
    MIRAGE_CHECK(spot.ok);
    MIRAGE_CHECK(spot.events.empty()); // suppressed, never delivered
    MIRAGE_CHECK(!spot.media.active);
    MIRAGE_CHECK(spot.media.x == 0 && spot.media.y == 0 && spot.media.width == 0 &&
                 spot.media.height == 0);
    // A second tiny sample stays suppressed and never starts media tracking.
    const auto spot2 = rig.observe(*pipeline, tiny_spot(uniform(10), 120));
    MIRAGE_CHECK(spot2.ok && spot2.events.empty() && !spot2.media.active);
    pipeline->stop();
}

/// b. Two consecutive overlapping large-region samples confirm a media
/// region: the second emits one bounded started event and the normalized
/// media projection; afterwards the same region changing emits nothing.
void media_region_confirms_and_quiets() {
    Rig rig;
    SilentOcr ocr;
    auto pipeline = rig.start(gate_config("gating-media", ocr, 0ms, 10));
    MIRAGE_CHECK(rig.observe(*pipeline, uniform(10)).ok);    // baseline
    const auto pending = rig.observe(*pipeline, media(100)); // first large sample
    MIRAGE_CHECK(pending.ok);
    MIRAGE_CHECK(!pending.media.active);
    // The confirmation-pending sample is still an ordinary change, not media.
    MIRAGE_CHECK(count_kind(pending, "visual_region_changed") == 1);
    MIRAGE_CHECK(count_kind(pending, "media_playback") == 0);

    const auto started = rig.observe(*pipeline, media(140)); // second sample confirms
    MIRAGE_CHECK(started.ok);
    MIRAGE_CHECK(count_kind(started, "media_playback") == 1);
    const auto playback = std::find_if(started.events.begin(), started.events.end(),
                                       [](const auto &e) { return e.kind == "media_playback"; });
    const bool started_seen = playback != started.events.end();
    MIRAGE_CHECK(started_seen && playback->text == "started");
    MIRAGE_CHECK(count_kind(started, "visual_region_changed") == 0);
    MIRAGE_CHECK(started.media.active);
    MIRAGE_CHECK(near(started.media.x, 0.625) && near(started.media.y, 0.0) &&
                 near(started.media.width, 0.375) && near(started.media.height, 1.0));
    // The started event carries the region in frame pixels.
    if (started_seen)
        MIRAGE_CHECK(playback->x == kMediaX0 && playback->y == 0 &&
                     playback->width == kMediaWidth && playback->height == kFrameSide);

    // Continued playback inside the confirmed region stays silent.
    const auto quiet = rig.observe(*pipeline, media(180));
    MIRAGE_CHECK(quiet.ok);
    MIRAGE_CHECK(quiet.events.empty());
    MIRAGE_CHECK(quiet.media.active);
    const auto quiet2 = rig.observe(*pipeline, media(220));
    MIRAGE_CHECK(quiet2.ok && quiet2.events.empty() && quiet2.media.active);
    pipeline->stop();
}

/// c. A scene cut inside the active region emits exactly one keyframe event
/// (text == "keyframe") when the throttle allows it, and none while the
/// minimum interval has not elapsed; the bare ROI never leaks either way.
void keyframes_are_throttled_scene_events() {
    // Immediate throttle: the keyframe is emitted on the scene cut.
    {
        Rig rig;
        SilentOcr ocr;
        auto pipeline = rig.start(gate_config("gating-keyframe", ocr, 0ms, 10));
        MIRAGE_CHECK(rig.observe(*pipeline, uniform(10)).ok);
        MIRAGE_CHECK(rig.observe(*pipeline, media(100)).ok); // pending
        const auto started = rig.observe(*pipeline, media(140));
        MIRAGE_CHECK(started.media.active);
        const auto cut = rig.observe(*pipeline, keyframe(0));
        MIRAGE_CHECK(cut.ok);
        MIRAGE_CHECK(count_kind(cut, "visual_region_changed") == 1);
        const auto kf =
            std::find_if(cut.events.begin(), cut.events.end(),
                         [](const auto &e) { return e.kind == "visual_region_changed"; });
        MIRAGE_CHECK(kf != cut.events.end() && kf->text == "keyframe");
        MIRAGE_CHECK(count_kind(cut, "media_playback") == 0);
        MIRAGE_CHECK(cut.media.active); // the cut does not end playback
        pipeline->stop();
    }
    // Throttled: a keyframe candidate landing inside the minimum interval
    // (here: the started event's own window) is suppressed entirely.
    {
        Rig rig;
        SilentOcr ocr;
        auto pipeline = rig.start(gate_config("gating-keyframe-throttle", ocr, 1h, 10));
        MIRAGE_CHECK(rig.observe(*pipeline, uniform(10)).ok);
        MIRAGE_CHECK(rig.observe(*pipeline, media(100)).ok);
        const auto started = rig.observe(*pipeline, media(140));
        MIRAGE_CHECK(started.media.active && started.events.size() == 1);
        const auto cut = rig.observe(*pipeline, keyframe(30));
        MIRAGE_CHECK(cut.ok && cut.events.empty() && cut.media.active);
        pipeline->stop();
    }
}

/// d. media_miss_samples consecutive non-overlapping samples deactivate the
/// region with one stopped event.
void media_region_stops_after_misses() {
    Rig rig;
    SilentOcr ocr;
    auto pipeline = rig.start(gate_config("gating-stop", ocr, 0ms, 3));
    MIRAGE_CHECK(rig.observe(*pipeline, uniform(10)).ok);
    MIRAGE_CHECK(rig.observe(*pipeline, media(100)).ok);
    const auto started = rig.observe(*pipeline, media(140));
    MIRAGE_CHECK(started.media.active);

    // Two misses: UI changes elsewhere, region still active.
    const auto miss1 = rig.observe(*pipeline, outside_change(200));
    MIRAGE_CHECK(miss1.ok && miss1.media.active);
    const auto miss2 = rig.observe(*pipeline, outside_change(230));
    MIRAGE_CHECK(miss2.ok && miss2.media.active);
    MIRAGE_CHECK(count_kind(miss1, "media_playback") == 0);
    MIRAGE_CHECK(count_kind(miss2, "media_playback") == 0);
    // The outside change itself is not media-explained and is delivered.
    MIRAGE_CHECK(count_kind(miss1, "visual_region_changed") == 1);

    // Third miss: the region deactivates with exactly one stopped event.
    const auto miss3 = rig.observe(*pipeline, outside_change(90));
    MIRAGE_CHECK(miss3.ok);
    MIRAGE_CHECK(count_kind(miss3, "media_playback") == 1);
    const auto stopped = std::find_if(miss3.events.begin(), miss3.events.end(),
                                      [](const auto &e) { return e.kind == "media_playback"; });
    MIRAGE_CHECK(stopped != miss3.events.end());
    if (stopped != miss3.events.end())
        MIRAGE_CHECK(stopped->text == "stopped");
    MIRAGE_CHECK(!miss3.media.active);
    pipeline->stop();
}

/// e. With the OCR hold withdrawn (MIRADOR-20261027-001), full-frame
/// recognition runs on every completed cycle; the active media region still
/// suppresses per-sample ROI noise, entity events are never gated, and the
/// media projection stays live throughout.
void media_suppression_keeps_full_recognition() {
    Rig rig;
    CaptionOcr ocr;
    auto pipeline = rig.start(gate_config("gating-media-ocr", ocr, 0ms, 10));
    unsigned completed = 0;
    const auto observe = [&](const d::ImageFrame &frame) {
        const auto sample = rig.observe(*pipeline, frame);
        if (sample.ok && !sample.skipped)
            ++completed;
        return sample;
    };

    const auto baseline = observe(uniform(10));
    MIRAGE_CHECK(baseline.ok && ocr.calls.load() == completed);
    MIRAGE_CHECK(std::any_of(baseline.events.begin(), baseline.events.end(),
                             [](const auto &e) { return e.kind == "observed_unconfirmed"; }));

    const auto pending = observe(media(100));
    MIRAGE_CHECK(pending.ok && ocr.calls.load() == completed);
    // The confirming entity event arrives ungated next to the ordinary ROI.
    MIRAGE_CHECK(std::any_of(pending.events.begin(), pending.events.end(),
                             [](const auto &e) { return e.kind == "appeared"; }));
    MIRAGE_CHECK(count_kind(pending, "visual_region_changed") == 1);
    MIRAGE_CHECK(count_kind(pending, "media_playback") == 0);

    const auto started = observe(media(140));
    MIRAGE_CHECK(started.ok && started.media.active && ocr.calls.load() == completed);
    MIRAGE_CHECK(count_kind(started, "media_playback") == 1);
    MIRAGE_CHECK(count_kind(started, "visual_region_changed") == 0);

    // Playback: recognition keeps running each completed cycle; the repeated
    // media ROI stays suppressed and the projection stays active.
    const auto quiet1 = observe(media(180));
    MIRAGE_CHECK(quiet1.ok && ocr.calls.load() == completed);
    MIRAGE_CHECK(quiet1.events.empty() && quiet1.media.active);
    const auto quiet2 = observe(media(220));
    MIRAGE_CHECK(quiet2.ok && ocr.calls.load() == completed);
    MIRAGE_CHECK(quiet2.events.empty() && quiet2.media.active);

    // A caption text change during playback surfaces as an entity event after
    // mirador's two-cycle confirmation debounce — never gated — while the
    // media ROI itself stays suppressed on every cycle.
    ocr.change_caption = true;
    const auto caption1 = observe(media(250));
    MIRAGE_CHECK(caption1.ok && ocr.calls.load() == completed);
    MIRAGE_CHECK(caption1.events.empty() && caption1.media.active);
    const auto caption2 = observe(media(240));
    MIRAGE_CHECK(caption2.ok && ocr.calls.load() == completed);
    MIRAGE_CHECK(std::any_of(caption2.events.begin(), caption2.events.end(),
                             [](const auto &e) { return e.kind == "text_changed"; }));
    MIRAGE_CHECK(count_kind(caption2, "visual_region_changed") == 0);
    MIRAGE_CHECK(caption2.media.active);

    // A non-tiny change outside the region is delivered alongside playback.
    const auto outside =
        observe(patched(media(250), 0, kFrameSide / 2, kFrameSide / 4, kFrameSide, 200));
    MIRAGE_CHECK(outside.ok && ocr.calls.load() == completed);
    MIRAGE_CHECK(count_kind(outside, "visual_region_changed") == 1);
    MIRAGE_CHECK(outside.media.active);
    pipeline->stop();
}

/// e2. A full-screen layout change (kGlobal, ratio >= 0.5) is a barrier, not
/// playback evidence: it never confirms or ends a region by itself, and an
/// already-active region keeps suppressing through its tracked geometry until
/// regular miss counting ends it (windowed video going fullscreen).
void fullscreen_barrier_keeps_active_region() {
    Rig rig;
    SilentOcr ocr;
    auto pipeline = rig.start(gate_config("gating-fullscreen", ocr, 0ms, 3));
    MIRAGE_CHECK(rig.observe(*pipeline, uniform(10)).ok);
    MIRAGE_CHECK(rig.observe(*pipeline, media(100)).ok);
    const auto started = rig.observe(*pipeline, media(140));
    MIRAGE_CHECK(started.media.active && count_kind(started, "media_playback") == 1);

    // Whole-frame changes are kGlobal: no started/stopped/keyframe event, the
    // covered ROI is suppressed through the tracked region, playback continues.
    const auto fullscreen1 = rig.observe(*pipeline, uniform(200));
    MIRAGE_CHECK(fullscreen1.ok && fullscreen1.events.empty() && fullscreen1.media.active);
    MIRAGE_CHECK(count_kind(fullscreen1, "media_playback") == 0);
    const auto fullscreen2 = rig.observe(*pipeline, uniform(230));
    MIRAGE_CHECK(fullscreen2.ok && fullscreen2.events.empty() && fullscreen2.media.active);

    // Playback evidence resumes only through regular cycles: non-overlapping
    // kPartial misses end the region after media_miss_samples with one stop.
    const auto miss1 = rig.observe(*pipeline, patched(uniform(230), 0, kFrameSide / 2,
                                                      kFrameSide / 4, kFrameSide, 40));
    MIRAGE_CHECK(miss1.ok && miss1.media.active && count_kind(miss1, "media_playback") == 0);
    MIRAGE_CHECK(count_kind(miss1, "visual_region_changed") == 1);
    const auto miss2 = rig.observe(*pipeline, patched(uniform(230), 0, kFrameSide / 2,
                                                      kFrameSide / 4, kFrameSide, 60));
    MIRAGE_CHECK(miss2.ok && miss2.media.active);
    const auto miss3 = rig.observe(*pipeline, patched(uniform(230), 0, kFrameSide / 2,
                                                      kFrameSide / 4, kFrameSide, 90));
    MIRAGE_CHECK(miss3.ok && count_kind(miss3, "media_playback") == 1);
    const auto stopped = std::find_if(miss3.events.begin(), miss3.events.end(),
                                      [](const auto &e) { return e.kind == "media_playback"; });
    if (stopped != miss3.events.end())
        MIRAGE_CHECK(stopped->text == "stopped");
    MIRAGE_CHECK(!miss3.media.active);
    pipeline->stop();
}

/// f. significance_gate=false restores the legacy behavior: every changed
/// region is delivered per sample, no media state is tracked or reported.
void gate_disabled_keeps_legacy_events() {
    Rig rig;
    SilentOcr ocr;
    auto config = gate_config("gating-off", ocr, 0ms, 10);
    config.significance_gate = false;
    auto pipeline = rig.start(config);
    MIRAGE_CHECK(rig.observe(*pipeline, uniform(10)).ok);
    for (int cycle = 0; cycle < 3; ++cycle) {
        const auto sample =
            rig.observe(*pipeline, media(static_cast<std::uint8_t>(100 + 40 * cycle)));
        MIRAGE_CHECK(sample.ok);
        MIRAGE_CHECK(count_kind(sample, "visual_region_changed") == 1);
        MIRAGE_CHECK(count_kind(sample, "media_playback") == 0);
        MIRAGE_CHECK(!sample.media.active);
        MIRAGE_CHECK(sample.media.x == 0 && sample.media.width == 0);
    }
    // Tiny regions are legacy events too when the gate is off (the media
    // region itself is held constant, so only the spot ROI differs).
    const auto spot = rig.observe(*pipeline, tiny_spot(media(180), 200));
    MIRAGE_CHECK(spot.ok && count_kind(spot, "visual_region_changed") == 1);
    pipeline->stop();
}

/// Journal passthrough (DEC-118): the media projection survives the
/// PerceptionJournal and is exposed on the consumer view.
void journal_carries_media_projection() {
    detail::PerceptionJournal journal;
    PerceptionSample sample;
    sample.ok = true;
    sample.media.active = true;
    sample.media.x = 0.5;
    sample.media.width = 0.5;
    sample.media.height = 1.0;
    journal.append(std::move(sample), {}, Clock::now());
    const auto view = journal.view();
    MIRAGE_CHECK(view.media.active && near(view.media.x, 0.5) && near(view.media.width, 0.5) &&
                 near(view.media.height, 1.0));
    journal.clear();
    MIRAGE_CHECK(!journal.view().media.active);
}

} // namespace

int main() {
    try {
        tiny_regions_are_suppressed();
        media_region_confirms_and_quiets();
        keyframes_are_throttled_scene_events();
        media_region_stops_after_misses();
        media_suppression_keeps_full_recognition();
        fullscreen_barrier_keeps_active_region();
        gate_disabled_keeps_legacy_events();
        journal_carries_media_projection();
    } catch (const std::exception &e) {
        std::fprintf(stderr, "exception: %s\n", e.what());
        return 1;
    }
    return mirage::testing::finish("significance_gating");
}
