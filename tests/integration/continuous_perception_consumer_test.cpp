#include "../support/test.hpp"

#include <mirage/integration/continuous_capture_host.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_observation_mapper.hpp>
#include <mirage/integration/visual_session_host.hpp>

#include <kairo/executor.hpp>
#include <mirador/transform.hpp>

#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using namespace mirage::integration;
namespace desktop = mirage::desktop;
using Kind = VisualAnalysisOutcome::Kind;

template <class Predicate> bool wait_until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= end)
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
        config.max_threads = 4;
        config.queue_capacity = 64;
        config.max_in_flight_tasks = 64;
        MIRAGE_CHECK(executor.initialize(config).ok);
    }
    ~Owner() { executor.shutdown(true); }
};

desktop::CaptureOutcome pixels(int width = 8) {
    desktop::ImageFrame frame;
    frame.width = width;
    frame.height = 8;
    frame.stride = static_cast<std::size_t>(width) * 4;
    frame.pixels.assign(frame.stride * 8, std::uint8_t{0x80});
    return {true, false, std::move(frame), {}};
}

VisualAnalysisRequest request_with_frame(mirador::Frame frame, std::uint64_t source_revision = 1) {
    VisualAnalysisRequest request;
    request.frame = std::move(frame);
    request.run_ocr = true;
    request.fuse = true;
    request.admission = VisualAnalysisAdmission::when_changed;
    request.analysis_policy_revision = 1;
    request.continuous = VisualContinuousRequest{};
    request.continuous->source_revision = source_revision;
    mirador::AttentionCondition condition;
    condition.text_equals = "mirage-fake";
    request.continuous->attention.conditions.push_back(condition);
    return request;
}

VisualAnalysisRequest request_for(const std::string &source, std::uint64_t sequence,
                                  std::uint64_t source_revision = 1, int width = 8) {
    auto shared = std::make_shared<SharedCaptureFrame>();
    shared->image = pixels(width).frame;
    shared->source_id = source;
    shared->sequence = sequence;
    shared->received_at = std::chrono::steady_clock::now();
    auto frame = to_mirador_frame(shared);
    MIRAGE_CHECK(frame.ok());
    return frame ? request_with_frame(frame.take_value(), source_revision)
                 : VisualAnalysisRequest{};
}

VisualSessionConfig config_for(mirador::OcrBackend &ocr) {
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    config.continuous = VisualContinuousConfig{};
    return config;
}

VisualAnalysisResult get(std::future<VisualAnalysisResult> future) {
    const auto ready = future.wait_for(5s) == std::future_status::ready;
    MIRAGE_CHECK(ready);
    return ready ? future.get() : VisualAnalysisResult{};
}

VisualAnalysisRequest refreshed(const std::string &source, std::uint64_t sequence,
                                std::uint64_t revision = 1, std::uint64_t digest = 42) {
    auto request = request_for(source, sequence, revision);
    request.ocr.cache_policy = mirador::CachePolicy::kRefresh;
    request.continuous->full_frame_evidence_digest = digest;
    request.continuous->attention.conditions[0] = {};
    request.continuous->attention.conditions[0].roi = {0, 0, 8, 8};
    return request;
}

void completed_evidence_confirms_and_reset_terminates() {
    Owner owner;
    FakeOcrBackend ocr;
    VisualSessionHost host(owner.executor, "confirmed", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto first = get(host.submit(refreshed("confirmed", 1)));
    MIRAGE_CHECK(first.outcome.kind == Kind::kCompleted && first.continuous);
    if (!first.continuous)
        return;
    const auto &fill = *first.continuous;
    MIRAGE_CHECK(fill.accepted && fill.accepted->committed && fill.accepted->adopted_count == 1);
    MIRAGE_CHECK(fill.cycle.work.size() == 1);
    MIRAGE_CHECK(fill.cycle.work[0].token.request_digest == 42);
    MIRAGE_CHECK(fill.cycle.work[0].token.frame_sequence == 1);
    MIRAGE_CHECK(fill.temporal.entities.size() == 1);
    MIRAGE_CHECK(fill.temporal.confirmed_frame == 0);
    MIRAGE_CHECK(fill.events.events.empty());
    MIRAGE_CHECK(!fill.focus[0].resolved);

    auto second = get(host.submit(refreshed("confirmed", 2)));
    MIRAGE_CHECK(second.outcome.kind == Kind::kCompleted && second.continuous);
    if (!second.continuous)
        return;
    const auto &confirmed = *second.continuous;
    MIRAGE_CHECK(confirmed.cycle.work.empty() && !confirmed.cycle.reuse.empty());
    MIRAGE_CHECK(confirmed.cycle.coverage == mirador::CoverageState::kComplete);
    MIRAGE_CHECK(confirmed.accepted && confirmed.accepted->adopted_count == 0);
    MIRAGE_CHECK(confirmed.temporal.confirmed_frame == 2);
    MIRAGE_CHECK(confirmed.focus[0].resolved);
    MIRAGE_CHECK(confirmed.temporal.pending_events == confirmed.events.events.size());
    MIRAGE_CHECK(confirmed.events.events.size() == 1);
    MIRAGE_CHECK(confirmed.events.events[0].semantic == mirador::EventSemantic::kBegin);
    MIRAGE_CHECK(mirador::semantic_event_consistent(confirmed.events.events[0]));
    MIRAGE_CHECK(confirmed.state_owned_bytes > 0);
    MIRAGE_CHECK(confirmed.coordination_owned_bytes <= 512 * 1024);
    MIRAGE_CHECK(ocr.calls() == 2); // Reuse in the ledger does not yet schedule OCR.

    const auto third = get(host.submit(refreshed("confirmed", 3)));
    MIRAGE_CHECK(third.continuous && third.continuous->events.events.empty());
    const auto reset = get(host.submit(refreshed("confirmed", 1, 2)));
    MIRAGE_CHECK(reset.continuous && reset.continuous->cycle.resync_required);
    if (!reset.continuous)
        return;
    MIRAGE_CHECK(reset.continuous->source_epoch == 2);
    MIRAGE_CHECK(reset.continuous->events.events.size() == 1);
    const auto &end = reset.continuous->events.events[0];
    MIRAGE_CHECK(end.semantic == mirador::EventSemantic::kEnd);
    MIRAGE_CHECK(end.change_kind == mirador::SemanticChangeKind::kSourceReset);
    MIRAGE_CHECK(end.window.source_epoch == 1);
    MIRAGE_CHECK(end.event_sequence > confirmed.events.events[0].event_sequence);
    MIRAGE_CHECK(reset.continuous->temporal.confirmed_frame == 0);
    const auto next = get(host.submit(refreshed("confirmed", 2, 2)));
    MIRAGE_CHECK(next.continuous && next.continuous->events.events.size() == 1);
    MIRAGE_CHECK(next.continuous && next.continuous->events.events[0].window.source_epoch == 2);
}

void adoption_admission_and_identity_are_explicit() {
    Owner owner;
    FakeOcrBackend ocr;
    VisualSessionHost host(owner.executor, "proof", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    for (int mode = 0; mode < 7; ++mode) {
        auto request = refreshed("proof", 1);
        if (mode == 0)
            request.ocr.cache_policy = mirador::CachePolicy::kReadWrite;
        if (mode == 1)
            request.ocr.roi = mirador::RectF{0, 0, 4, 4};
        if (mode == 2)
            request.ocr_budget = VisualAnalysisRequest::OcrBudget{};
        if (mode == 3)
            request.run_ocr = false;
        if (mode == 4)
            request.geometry.run = true;
        if (mode == 5)
            request.run_detector = true;
        if (mode == 6) {
            request.run_detector = true;
            request.detector.cache_policy = mirador::CachePolicy::kRefresh;
            request.detector.roi = mirador::RectF{0, 0, 4, 4};
        }
        MIRAGE_CHECK(get(host.submit(std::move(request))).outcome.kind == Kind::kRejected);
    }
    MIRAGE_CHECK(ocr.calls() == 0);
    const auto advisory = get(host.submit(request_for("proof", 1)));
    MIRAGE_CHECK(advisory.continuous && !advisory.continuous->accepted);
    MIRAGE_CHECK(advisory.continuous && advisory.continuous->temporal.confirmed_frame == 0);
    const auto fill = get(host.submit(refreshed("proof", 2)));
    MIRAGE_CHECK(fill.continuous && fill.continuous->accepted->adopted_count == 1);
    MIRAGE_CHECK(fill.continuous && fill.continuous->cycle.replaced_inflight == 1);
    const auto changed = get(host.submit(refreshed("proof", 3, 1, 43)));
    MIRAGE_CHECK(changed.continuous && changed.continuous->accepted->adopted_count == 1);
    MIRAGE_CHECK(changed.continuous && !changed.continuous->cycle.invalidations.empty());
    if (changed.continuous && !changed.continuous->cycle.invalidations.empty())
        MIRAGE_CHECK(changed.continuous->cycle.invalidations[0].reason ==
                     mirador::WorkInvalidationReason::kIdentityChanged);
    const auto old_frame = get(host.submit(refreshed("proof", 2)));
    MIRAGE_CHECK(old_frame.outcome.kind == Kind::kRejected && !old_frame.continuous);
    const auto next = get(host.submit(refreshed("proof", 4, 1, 43)));
    MIRAGE_CHECK(next.continuous && next.continuous->events.events.empty());
}

void notification_filter_and_overflow_are_visible() {
    for (const bool notify : {false, true}) {
        Owner owner;
        FakeOcrConfig fake;
        mirador::TextRegion left, right;
        left.bounds = {0, 0, 4, 8};
        left.utf8_text = "left";
        left.confidence = 0.9F;
        right.bounds = {4, 0, 4, 8};
        right.utf8_text = "right";
        right.confidence = 0.9F;
        fake.regions = {left, right};
        FakeOcrBackend ocr(fake);
        auto config = config_for(ocr);
        config.continuous->max_events = 1;
        VisualSessionHost host(owner.executor, "events", config);
        std::string error;
        MIRAGE_CHECK(host.start(error));
        for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
            auto request = refreshed("events", sequence);
            request.continuous->importance.notify_enabled = notify;
            const auto result = get(host.submit(std::move(request)));
            MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted && result.continuous);
            if (!result.continuous)
                continue;
            const auto &facts = *result.continuous;
            MIRAGE_CHECK(facts.temporal.entities.size() == 2);
            if (sequence == 2) {
                MIRAGE_CHECK(facts.temporal.confirmed_frame == 2);
                MIRAGE_CHECK(facts.focus[0].resolved && facts.focus[0].stable_ids.size() == 2);
                MIRAGE_CHECK(facts.events.events.size() == (notify ? 1 : 0));
                MIRAGE_CHECK(facts.events.dropped_total == (notify ? 1 : 0));
                MIRAGE_CHECK(facts.temporal.dropped_events == facts.events.dropped_total);
                MIRAGE_CHECK(facts.cycle.resync_required == notify);
            }
            if (sequence == 3) {
                MIRAGE_CHECK(facts.events.events.empty());
                MIRAGE_CHECK(!facts.cycle.resync_required);
            }
        }
    }
}

void exact_pixels_invalidate_even_when_change_gate_is_quiet() {
    Owner owner;
    FakeOcrBackend ocr;
    VisualSessionHost host(owner.executor, "drift", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(get(host.submit(refreshed("drift", 1))).outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(get(host.submit(refreshed("drift", 2))).outcome.kind == Kind::kCompleted);
    auto shared = std::make_shared<SharedCaptureFrame>();
    shared->image = pixels().frame;
    shared->image.pixels[0] += 1;
    shared->source_id = "drift";
    shared->sequence = 3;
    shared->received_at = std::chrono::steady_clock::now();
    auto request = refreshed("drift", 3);
    request.frame = to_mirador_frame(shared).take_value();
    const auto result = get(host.submit(std::move(request)));
    MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted && result.continuous);
    if (!result.continuous)
        return;
    MIRAGE_CHECK(result.admission_change &&
                 result.admission_change->classification == mirador::ChangeClassification::kNone);
    MIRAGE_CHECK(result.continuous->cycle.reuse.empty());
    MIRAGE_CHECK(result.continuous->accepted->adopted_count == 1);
    MIRAGE_CHECK(result.continuous->cycle.invalidations.size() == 1);
    if (!result.continuous->cycle.invalidations.empty())
        MIRAGE_CHECK(result.continuous->cycle.invalidations[0].reason ==
                     mirador::WorkInvalidationReason::kContentMismatch);
    MIRAGE_CHECK(ocr.calls() == 3);
}

void current_snapshot_relations_and_policy_changes() {
    Owner owner;
    FakeOcrBackend ocr;
    VisualSessionHost host(owner.executor, "current", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(owner.executor.get_blocking_io_worker_list().size() == 1);
    const auto first = get(host.submit(request_for("current", 1)));
    MIRAGE_CHECK(first.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(first.snapshot && first.continuous);
    if (!first.snapshot || !first.continuous)
        return;
    MIRAGE_CHECK(first.snapshot->coordinate_space == mirador::CoordinateSpaceId::kOriented);
    desktop::VisualReferenceRegistry registry;
    // The fixture has an identity display transform; seed a known display set.
    auto display = *first.snapshot;
    display.coordinate_space = mirador::CoordinateSpaceId::kDisplay;
    const auto seeded = publish_visual_snapshot(display, registry);
    MIRAGE_CHECK(seeded.ok);
    const auto rejected = publish_visual_snapshot(*first.snapshot, registry);
    MIRAGE_CHECK(!rejected.ok);
    MIRAGE_CHECK(rejected.error.code == "invalid_coordinate_space");
    MIRAGE_CHECK(registry.current().scope_ref == seeded.snapshot.scope_ref);
    MIRAGE_CHECK(registry.size() == 1);
    const auto &facts = *first.continuous;
    MIRAGE_CHECK(facts.source_revision == 1 && facts.source_epoch == 1);
    MIRAGE_CHECK(mirador::relation_view_consistent(facts.relations));
    MIRAGE_CHECK(facts.relations.entities.size() == 1);
    MIRAGE_CHECK(facts.relations.coverage == mirador::CoverageState::kIncomplete);
    MIRAGE_CHECK(facts.relations.entities[0].freshness == mirador::Freshness::kUnvalidated);
    MIRAGE_CHECK(facts.cycle.attention.coverage == mirador::CoverageState::kIncomplete);
    MIRAGE_CHECK(facts.cycle.attention.work.size() == 1);
    MIRAGE_CHECK(facts.cycle.attention.resolutions[0].resolved);
    MIRAGE_CHECK(facts.cycle.attention.policy_revision == 1);
    MIRAGE_CHECK(facts.coordination_owned_bytes <= 512 * 1024);
    MIRAGE_CHECK(facts.relation_owned_bytes <= 512 * 1024);
    MIRAGE_CHECK(facts.plan_owned_bytes <= 64 * 1024);
    MIRAGE_CHECK(ocr.calls() == 1);

    const auto skipped = get(host.submit(request_for("current", 2)));
    MIRAGE_CHECK(skipped.outcome.kind == Kind::kSkippedUnchanged);
    MIRAGE_CHECK(!skipped.snapshot && !skipped.continuous);
    MIRAGE_CHECK(skipped.cache_stats.continuous_coordination_bytes > 0);
    MIRAGE_CHECK(ocr.calls() == 1);

    auto changed_policy = request_for("current", 3);
    auto &condition = changed_policy.continuous->attention.conditions[0];
    condition.text_equals.clear();
    condition.roi = mirador::RectF{1, 1, 2, 2};
    const auto changed = get(host.submit(changed_policy));
    MIRAGE_CHECK(changed.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(changed.continuous && changed.continuous->cycle.attention.policy_revision == 2);
    MIRAGE_CHECK(changed.continuous &&
                 changed.continuous->cycle.attention.work[0].roi == (mirador::RectF{1, 1, 2, 2}));

    changed_policy.frame.sequence = 4;
    changed_policy.continuous->importance.priority = 7;
    const auto priority = get(host.submit(std::move(changed_policy)));
    MIRAGE_CHECK(priority.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(priority.continuous && priority.continuous->cycle.attention.policy_revision == 2);
    MIRAGE_CHECK(priority.continuous && priority.continuous->cycle.attention.work[0].priority == 7);
    MIRAGE_CHECK(priority.snapshot &&
                 priority.snapshot->regions[0].confidence == first.snapshot->regions[0].confidence);

    const auto reset = get(host.submit(request_for("current", 1, 9)));
    MIRAGE_CHECK(reset.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(reset.continuous && reset.continuous->source_epoch == 2);
    MIRAGE_CHECK(reset.continuous && reset.continuous->source_revision == 9);
    MIRAGE_CHECK(ocr.calls() == 2); // New source cannot replay the old source's OCR cache.
    MIRAGE_CHECK(reset.snapshot &&
                 reset.snapshot->change.reason == mirador::ChangeReason::kFirstFrame);
    MIRAGE_CHECK(get(host.submit(request_for("current", 20, 8))).outcome.kind == Kind::kRejected);
    MIRAGE_CHECK(get(host.submit(request_for("current", 1, 9))).outcome.kind == Kind::kRejected);
    MIRAGE_CHECK(get(host.submit(request_for("current", 2, 9, 16))).outcome.kind ==
                 Kind::kRejected);
    MIRAGE_CHECK(ocr.calls() == 2);
    const auto next = get(host.submit(request_for("current", 2, 9)));
    MIRAGE_CHECK(next.outcome.kind == Kind::kSkippedUnchanged);
    MIRAGE_CHECK(next.admission_baseline_sequence == 1);
}

void unresolved_focus_needs_explicit_bootstrap() {
    Owner owner;
    FakeOcrBackend ocr;
    VisualSessionHost host(owner.executor, "focus", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto request = request_for("focus", 1);
    request.continuous->attention.conditions[0].text_equals = "unseen text";
    const auto unresolved = get(host.submit(request));
    MIRAGE_CHECK(unresolved.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(unresolved.continuous && unresolved.continuous->cycle.attention.work.empty());
    MIRAGE_CHECK(unresolved.continuous &&
                 !unresolved.continuous->cycle.attention.resolutions[0].resolved);
    request.frame.sequence = 2;
    request.continuous->importance.allow_bootstrap_full_frame = true;
    const auto bootstrap = get(host.submit(std::move(request)));
    MIRAGE_CHECK(bootstrap.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(bootstrap.continuous && bootstrap.continuous->cycle.attention.work.size() == 1);
    MIRAGE_CHECK(bootstrap.continuous && bootstrap.continuous->cycle.attention.work[0].kind ==
                                             mirador::PlannedWorkKind::kBootstrap);
}

void budgets_fail_without_current_payload_and_recover() {
    Owner owner;
    FakeOcrBackend ocr;
    for (int mode = 0; mode < 3; ++mode) {
        const auto source = "budget-" + std::to_string(mode);
        auto config = config_for(ocr);
        if (mode == 0)
            config.continuous->max_coordination_bytes = 1;
        if (mode == 1)
            config.continuous->max_relation_bytes = 1;
        if (mode == 2)
            config.continuous->max_plan_bytes = 1;
        VisualSessionHost host(owner.executor, source, config);
        std::string error;
        MIRAGE_CHECK(host.start(error));
        for (std::uint64_t sequence = 1; sequence <= 2; ++sequence) {
            const auto result = get(host.submit(request_for(source, sequence)));
            MIRAGE_CHECK(result.outcome.kind == Kind::kFailed);
            MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBudgetExceeded);
            MIRAGE_CHECK(!result.snapshot && !result.continuous);
            MIRAGE_CHECK(result.admission_baseline_sequence == 0);
        }
    }
    VisualSessionHost host(owner.executor, "recover", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto invalid = request_for("recover", 1);
    invalid.continuous->attention.conditions.resize(17,
                                                    invalid.continuous->attention.conditions[0]);
    const auto failed = get(host.submit(std::move(invalid)));
    MIRAGE_CHECK(failed.outcome.kind == Kind::kFailed);
    MIRAGE_CHECK(failed.outcome.code == mirador::ErrorCode::kBudgetExceeded);
    MIRAGE_CHECK(get(host.submit(request_for("recover", 2))).outcome.kind == Kind::kCompleted);
}

class InterruptibleOcr final : public mirador::OcrBackend {
  public:
    std::atomic_bool entered{false}, release{false};
    bool throws = false;
    FakeOcrBackend backend;
    mirador::BackendInfo info() const override { return backend.info(); }
    mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &image, const mirador::OcrRequest &request,
              const mirador::ExecutionContext &context) override {
        entered = true;
        if (throws)
            throw std::runtime_error("injected continuous analysis failure");
        while (!release && !mirador::is_cancelled(context) && !mirador::deadline_reached(context))
            std::this_thread::sleep_for(1ms);
        if (mirador::is_cancelled(context))
            return mirador::Status{mirador::ErrorCode::kCancelled, "cancelled"};
        if (mirador::deadline_reached(context))
            return mirador::Status{mirador::ErrorCode::kTimeout, "expired"};
        return backend.recognize(image, request, context);
    }
};

void cancellation_deadline_exception_and_shutdown_settle() {
    for (int mode = 0; mode < 5; ++mode) {
        std::fprintf(stderr, "[continuous consumer] lifecycle mode %d\n", mode);
        Owner owner;
        InterruptibleOcr ocr;
        ocr.throws = mode == 2;
        VisualSessionHost host(owner.executor, "lifecycle", config_for(ocr));
        std::string error;
        MIRAGE_CHECK(host.start(error));
        auto request = refreshed("lifecycle", 1);
        desktop::CancelToken cancel;
        request.external_cancel = cancel;
        if (mode == 1)
            request.deadline = std::chrono::steady_clock::now() + 50ms;
        auto future = host.submit(std::move(request));
        MIRAGE_CHECK(wait_until([&] { return ocr.entered.load(); }));
        if (mode == 0)
            cancel.request_cancel();
        if (mode == 3)
            host.stop();
        if (mode == 4)
            owner.executor.shutdown(true);
        const auto result = get(std::move(future));
        MIRAGE_CHECK(result.outcome.kind == (mode == 1   ? Kind::kTimedOut
                                             : mode == 2 ? Kind::kFailed
                                                         : Kind::kCancelled));
        MIRAGE_CHECK(!result.snapshot && !result.continuous);
        if (mode == 0 || mode == 1) {
            ocr.release = true;
            const auto next = get(host.submit(refreshed("lifecycle", 2)));
            MIRAGE_CHECK(next.outcome.kind == Kind::kCompleted);
            MIRAGE_CHECK(next.continuous && next.continuous->temporal.confirmed_frame == 0);
            MIRAGE_CHECK(next.continuous && next.continuous->events.events.empty());
        }
    }
}

void background_admission_and_legacy_space_are_explicit() {
    Owner owner;
    FakeOcrBackend ocr;
    auto config = config_for(ocr);
    VisualSessionHost background(owner.executor, "background", config);
    std::string error;
    MIRAGE_CHECK(background.start(error));
    for (int mode = 0; mode < 5; ++mode) {
        auto request = request_for("background", 1);
        if (mode == 0)
            request.continuous.reset();
        if (mode == 1)
            request.continuous->source_revision = 0;
        if (mode == 2)
            request.fuse = false;
        if (mode == 3)
            request.render_marks = true;
        if (mode == 4)
            request.continuous->importance.priority = std::numeric_limits<float>::infinity();
        MIRAGE_CHECK(get(background.submit(std::move(request))).outcome.kind == Kind::kRejected);
    }
    MIRAGE_CHECK(ocr.calls() == 0);
    config.continuous->max_entities = 257;
    VisualSessionHost invalid(owner.executor, "invalid", config);
    MIRAGE_CHECK(!invalid.start(error));
    VisualSessionHost legacy(owner.executor, "legacy");
    MIRAGE_CHECK(legacy.start(error));
    auto request = request_for("legacy", 1);
    MIRAGE_CHECK(get(legacy.submit(request)).outcome.kind == Kind::kRejected);
    request.continuous.reset();
    request.run_ocr = false;
    request.display_transform = mirador::make_translation(
        10, 20, mirador::CoordinateSpaceId::kOriented, mirador::CoordinateSpaceId::kDisplay);
    const auto result = get(legacy.submit(std::move(request)));
    MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(!result.continuous);
    MIRAGE_CHECK(result.snapshot &&
                 result.snapshot->coordinate_space == mirador::CoordinateSpaceId::kDisplay);
}

void shared_capture_analysis_churn_does_not_pin_settled_requests() {
    Owner owner;
    ContinuousCaptureConfig limits;
    limits.max_frame_bytes = 256;
    limits.sample_period = 10ms;
    ContinuousCaptureHost capture(
        owner.executor, "wire", [](const auto &, const auto &) { return pixels(); }, limits);
    FakeOcrBackend ocr;
    VisualSessionHost analysis(owner.executor, "wire", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(capture.start(error));
    MIRAGE_CHECK(analysis.start(error));
    MIRAGE_CHECK(capture.set_demand(true, error));
    std::uint64_t previous = 0;
    bool bounded = true;
    for (int i = 0; i < 64; ++i) {
        std::shared_ptr<const SharedCaptureFrame> frame;
        MIRAGE_CHECK(wait_until([&] {
            frame = capture.latest();
            return frame && frame->sequence > previous;
        }));
        if (!frame || frame->sequence <= previous)
            break;
        previous = frame->sequence;
        auto wrapped = to_mirador_frame(frame);
        MIRAGE_CHECK(wrapped.ok());
        if (!wrapped)
            break;
        MIRAGE_CHECK(wrapped.value().image.data ==
                     reinterpret_cast<const std::byte *>(frame->image.pixels.data()));
        auto request = request_with_frame(wrapped.take_value());
        const auto result = get(analysis.submit(std::move(request)));
        MIRAGE_CHECK(result.outcome.kind == (i == 0 ? Kind::kCompleted : Kind::kSkippedUnchanged));
        const auto stats = capture.stats();
        bounded = bounded && stats.live_generations <= 2 && stats.live_pixel_bytes <= 512;
    }
    MIRAGE_CHECK(previous >= 64);
    MIRAGE_CHECK(bounded);
    MIRAGE_CHECK(ocr.calls() == 1);
    MIRAGE_CHECK(capture.set_demand(false, error));
    // Keep the analysis host and its mailbox alive: settled slots own no pixels.
    MIRAGE_CHECK(wait_until([&] { return capture.stats().live_generations == 0; }));
    analysis.stop();
}
} // namespace

int main() {
    completed_evidence_confirms_and_reset_terminates();
    adoption_admission_and_identity_are_explicit();
    notification_filter_and_overflow_are_visible();
    exact_pixels_invalidate_even_when_change_gate_is_quiet();
    std::fprintf(stderr, "[continuous consumer] current snapshot and policy\n");
    current_snapshot_relations_and_policy_changes();
    std::fprintf(stderr, "[continuous consumer] unresolved focus\n");
    unresolved_focus_needs_explicit_bootstrap();
    std::fprintf(stderr, "[continuous consumer] budgets and recovery\n");
    budgets_fail_without_current_payload_and_recover();
    std::fprintf(stderr, "[continuous consumer] lifecycle\n");
    cancellation_deadline_exception_and_shutdown_settle();
    std::fprintf(stderr, "[continuous consumer] admission and legacy\n");
    background_admission_and_legacy_space_are_explicit();
    std::fprintf(stderr, "[continuous consumer] shared capture churn\n");
    shared_capture_analysis_churn_does_not_pin_settled_requests();
    return mirage::testing::finish("continuous_perception_consumer_test");
}
