#include "../support/test.hpp"

#include <mirage/integration/continuous_capture_host.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_observation_mapper.hpp>
#include <mirage/integration/visual_session_host.hpp>

#include <kairo/executor.hpp>
#include <mirador/transform.hpp>

#include <array>
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

VisualAnalysisRequest spatial_request(const std::string &source, std::uint64_t sequence) {
    auto request = refreshed(source, sequence);
    request.continuous->full_frame_evidence_digest = 0;
    request.continuous->spatial_evidence_digest = 73;
    request.continuous->attention.conditions[0].roi = {1, 1, 2, 2};
    return request;
}

class MeasuredOcr final : public mirador::OcrBackend {
  public:
    FakeOcrBackend backend;
    std::uint64_t calls = 0, pixels_seen = 0, fail_at = 0;
    mirador::BackendInfo info() const override { return backend.info(); }
    mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &image, const mirador::OcrRequest &request,
              const mirador::ExecutionContext &context) override {
        ++calls;
        pixels_seen += static_cast<std::uint64_t>(image.width) * image.height;
        if (calls == fail_at)
            return mirador::Status{mirador::ErrorCode::kBackendFailure, "injected ROI failure"};
        return backend.recognize(image, request, context);
    }
};

void spatial_work_reduces_backend_input_and_preserves_coordinates() {
    Owner owner;
    MeasuredOcr ocr;
    FakeDetectorBackend detector;
    auto config = config_for(ocr);
    config.detector_backend = &detector;
    VisualSessionHost host(owner.executor, "spatial", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
        auto request = spatial_request("spatial", sequence);
        auto second = request.continuous->attention.conditions[0];
        second.roi = {5, 5, 2, 2};
        request.continuous->attention.conditions.push_back(second);
        request.run_detector = true;
        request.detector.cache_policy = mirador::CachePolicy::kRefresh;
        const auto result = get(host.submit(std::move(request)));
        MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted && result.continuous &&
                     result.spatial);
        if (!result.continuous || !result.spatial || !result.snapshot)
            return;
        MIRAGE_CHECK(result.spatial->plan.work.size() == 2);
        MIRAGE_CHECK(result.spatial->requested_pixels == 8);
        MIRAGE_CHECK(result.spatial->ocr_completed == 2);
        MIRAGE_CHECK(result.spatial->detector_completed == 2);
        MIRAGE_CHECK(result.text.size() == 2 && result.detections.size() == 2);
        MIRAGE_CHECK(result.text[0].bounds == (mirador::RectF{1, 1, 2, 2}));
        MIRAGE_CHECK(result.text[1].bounds == (mirador::RectF{5, 5, 2, 2}));
        MIRAGE_CHECK(result.detections[1].bounds == result.text[1].bounds);
        MIRAGE_CHECK(result.snapshot->regions.size() == 2);
        MIRAGE_CHECK(result.continuous->plan_owned_bytes <= 64 * 1024);
        MIRAGE_CHECK(result.continuous->plan_owned_bytes > result.spatial->plan_owned_bytes);
        MIRAGE_CHECK(result.continuous->accepted && result.continuous->accepted->committed);
        if (sequence == 1) {
            MIRAGE_CHECK(result.continuous->accepted->adopted_count == 2);
            for (const auto &binding : result.continuous->cycle.work)
                MIRAGE_CHECK(binding.token.request_digest == 73);
        }
        if (sequence == 2) {
            MIRAGE_CHECK(result.admission_change && result.admission_change->classification ==
                                                        mirador::ChangeClassification::kNone);
            MIRAGE_CHECK(result.continuous->temporal.confirmed_frame == 2);
            MIRAGE_CHECK(result.continuous->events.events.size() == 2);
        }
        if (sequence == 3)
            MIRAGE_CHECK(result.continuous->events.events.empty());
    }
    MIRAGE_CHECK(ocr.calls == 6 && ocr.pixels_seen == 24); // 8 instead of 64 source pixels/frame.
    MIRAGE_CHECK(detector.calls() == 6);
    auto switch_scope = refreshed("spatial", 4, 1, 73);
    MIRAGE_CHECK(get(host.submit(switch_scope)).outcome.kind == Kind::kRejected);
    switch_scope.continuous->full_frame_evidence_digest = 74;
    MIRAGE_CHECK(get(host.submit(switch_scope)).outcome.kind == Kind::kCompleted);
}

void spatial_validation_and_budgets_precede_backend_calls() {
    Owner owner;
    MeasuredOcr ocr;
    auto config = config_for(ocr);
    config.continuous->max_spatial_work_items = 1;
    config.continuous->max_spatial_pixels = 4;
    VisualSessionHost host(owner.executor, "spatial-limits", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    for (int mode = 0; mode < 15; ++mode) {
        auto request = spatial_request("spatial-limits", static_cast<std::uint64_t>(mode + 1));
        auto &input = *request.continuous;
        if (mode == 0)
            input.full_frame_evidence_digest = 1;
        if (mode == 1)
            request.analyze_change = false;
        if (mode == 2)
            input.attention.conditions.clear();
        if (mode == 3)
            input.attention.conditions[0].text_equals = "unseen";
        if (mode == 4)
            input.attention.conditions[0].bind_stable_id = true;
        if (mode == 5)
            input.attention.conditions[0].roi_space = mirador::CoordinateSpaceId::kDisplay;
        if (mode == 6)
            request.ocr.output_space = mirador::CoordinateSpaceId::kFrame;
        if (mode == 7)
            request.frame.image.rotation = mirador::Rotation::k90;
        if (mode == 8)
            input.attention.conditions[0].roi.x = 0.5F;
        if (mode == 9)
            input.attention.conditions[0].roi = {7, 7, 2, 2};
        if (mode == 10)
            input.attention.conditions[0].roi.width = 0;
        if (mode == 11)
            input.attention.conditions[0].roi.x = std::numeric_limits<float>::quiet_NaN();
        if (mode == 12)
            input.attention.conditions[0].roi = {0, 0, 4, 4};
        if (mode == 13)
            input.attention.conditions.push_back(input.attention.conditions[0]);
        if (mode == 14)
            request.ocr.cache_policy = mirador::CachePolicy::kReadWrite;
        const auto result = get(host.submit(std::move(request)));
        MIRAGE_CHECK(result.outcome.kind ==
                     (mode < 8 || mode == 14 ? Kind::kRejected : Kind::kFailed));
        MIRAGE_CHECK(!result.snapshot && !result.continuous);
        if (mode == 12 || mode == 13)
            MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBudgetExceeded);
        MIRAGE_CHECK(ocr.calls == 0);
    }
    const auto recovered = get(host.submit(spatial_request("spatial-limits", 16)));
    MIRAGE_CHECK(recovered.outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(recovered.continuous && recovered.continuous->accepted->adopted_count == 1);
}

void failed_roi_batch_does_not_advance_confirmation() {
    Owner owner;
    MeasuredOcr ocr;
    ocr.fail_at = 2;
    VisualSessionHost host(owner.executor, "spatial-failure", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
        auto request = spatial_request("spatial-failure", sequence);
        auto second = request.continuous->attention.conditions[0];
        second.roi = {5, 5, 2, 2};
        request.continuous->attention.conditions.push_back(second);
        const auto result = get(host.submit(std::move(request)));
        MIRAGE_CHECK(result.spatial.has_value());
        if (sequence == 1) {
            MIRAGE_CHECK(result.outcome.kind == Kind::kFailed);
            MIRAGE_CHECK(!result.continuous && !result.snapshot);
            MIRAGE_CHECK(result.spatial && result.spatial->ocr_completed == 1);
            MIRAGE_CHECK(result.text.size() ==
                         1); // Diagnostic partial evidence, not a published snapshot.
        } else {
            MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted && result.continuous);
            if (result.continuous) {
                MIRAGE_CHECK(result.continuous->temporal.confirmed_frame ==
                             (sequence == 2 ? 0 : 3));
                MIRAGE_CHECK(result.continuous->events.events.size() == (sequence == 2 ? 0 : 2));
            }
        }
    }
}

void combined_plan_budget_is_checked_before_adoption() {
    Owner owner;
    FakeOcrBackend ocr;
    std::string error;
    VisualSessionHost reference(owner.executor, "plan-reference", config_for(ocr));
    MIRAGE_CHECK(reference.start(error));
    const auto measured = get(reference.submit(spatial_request("plan-reference", 1)));
    MIRAGE_CHECK(measured.spatial && measured.continuous);
    if (!measured.spatial || !measured.continuous)
        return;
    auto config = config_for(ocr);
    // Exactly enough for the preliminary plan, insufficient for both plans.
    config.continuous->max_plan_bytes = measured.spatial->plan_owned_bytes;
    VisualSessionHost limited(owner.executor, "plan-limited", config);
    MIRAGE_CHECK(limited.start(error));
    const auto result = get(limited.submit(spatial_request("plan-limited", 1)));
    MIRAGE_CHECK(result.outcome.kind == Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBudgetExceeded);
    MIRAGE_CHECK(result.spatial && result.spatial->ocr_completed == 1);
    MIRAGE_CHECK(!result.snapshot && !result.continuous);
}

VisualContinuousQuery query_for(std::uint64_t source_revision = 1) {
    VisualContinuousQuery query;
    query.expected_source_revision = source_revision;
    return query;
}

void frame_free_queries_switch_focus_without_observation() {
    Owner owner;
    FakeOcrBackend ocr;
    VisualSessionHost host(owner.executor, "query", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(get(host.query_continuous(query_for())).outcome.kind == Kind::kRejected);
    MIRAGE_CHECK(get(host.submit(refreshed("query", 1))).outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(get(host.submit(refreshed("query", 2))).outcome.kind == Kind::kCompleted);
    auto query = query_for();
    query.attention = mirador::AttentionPolicy{};
    mirador::AttentionCondition text;
    text.text_equals = "mirage-fake";
    query.attention->conditions.push_back(text);
    const auto result = get(host.query_continuous(query));
    MIRAGE_CHECK(result.outcome.kind == Kind::kQueried && result.queried);
    MIRAGE_CHECK(!result.snapshot && !result.continuous && !result.spatial);
    if (!result.queried)
        return;
    const auto &view = *result.queried;
    MIRAGE_CHECK(view.source_revision == 1 && view.source_epoch == 1 && view.policy_revision == 2);
    MIRAGE_CHECK(view.last_analyzed_frame == 2 && view.analysis_age.has_value());
    MIRAGE_CHECK(view.temporal.confirmed_frame == 2 && view.temporal.latest_observed_frame == 2);
    MIRAGE_CHECK(view.focus.size() == 1 && view.focus[0].resolved);
    MIRAGE_CHECK(!view.events);
    MIRAGE_CHECK(view.coordination_owned_bytes <= 512 * 1024 && view.state_owned_bytes > 0);
    MIRAGE_CHECK(ocr.calls() == 2);
    const auto peek = get(host.query_continuous(query_for()));
    MIRAGE_CHECK(peek.queried && peek.queried->analysis_age >= view.analysis_age);
    MIRAGE_CHECK(peek.queried && peek.queried->temporal == view.temporal);
    query.attention->conditions[0].text_equals = "unseen";
    const auto missing = get(host.query_continuous(query));
    MIRAGE_CHECK(missing.queried && missing.queried->policy_revision == 3);
    MIRAGE_CHECK(missing.queried && !missing.queried->focus[0].resolved);
    MIRAGE_CHECK(missing.queried && missing.queried->last_analyzed_frame == 2);
    MIRAGE_CHECK(ocr.calls() == 2);
    // Read-only peeks do not invalidate the background signature; a changed
    // policy does. The owner copies the new policy into subsequent frame work.
    auto next = request_for("query", 3);
    next.continuous->attention = *query.attention;
    MIRAGE_CHECK(get(host.submit(next)).outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(get(host.query_continuous(query_for())).outcome.kind == Kind::kQueried);
    next.frame.sequence = 4;
    MIRAGE_CHECK(get(host.submit(next)).outcome.kind == Kind::kSkippedUnchanged);
    const auto after_skip = get(host.query_continuous(query_for()));
    MIRAGE_CHECK(after_skip.queried && after_skip.queried->last_analyzed_frame == 3);
}

void query_rejection_cancellation_and_drain_preserve_provenance() {
    Owner owner;
    MeasuredOcr ocr;
    VisualSessionHost host(owner.executor, "query-drain", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(get(host.submit(refreshed("query-drain", 1))).outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(get(host.submit(refreshed("query-drain", 2))).outcome.kind == Kind::kCompleted);
    for (int mode = 0; mode < 7; ++mode) {
        auto query = query_for();
        query.attention = mirador::AttentionPolicy{};
        mirador::AttentionCondition text;
        text.text_equals = "other";
        query.attention->conditions.push_back(text);
        if (mode == 0)
            query.expected_source_revision = 2;
        if (mode == 1)
            query.external_cancel.request_cancel();
        if (mode == 2)
            query.deadline = std::chrono::steady_clock::now() - 1ms;
        if (mode == 3)
            query.attention->conditions.resize(17, text);
        if (mode == 4)
            query.attention->conditions[0] = {};
        if (mode == 5)
            query.attention->conditions.reserve(8192);
        if (mode == 6)
            query.attention->conditions[0].text_equals.reserve(600 * 1024);
        const auto result = get(host.query_continuous(std::move(query)));
        MIRAGE_CHECK(result.outcome.kind == (mode == 0   ? Kind::kRejected
                                             : mode == 1 ? Kind::kCancelled
                                             : mode == 2 ? Kind::kTimedOut
                                                         : Kind::kFailed));
        MIRAGE_CHECK(!result.queried && !result.continuous && !result.snapshot);
        const auto intact = get(host.query_continuous(query_for()));
        MIRAGE_CHECK(intact.queried && intact.queried->policy_revision == 1);
    }
    ocr.fail_at = 3;
    MIRAGE_CHECK(get(host.submit(refreshed("query-drain", 1, 2))).outcome.kind == Kind::kFailed);
    MIRAGE_CHECK(get(host.query_continuous(query_for())).outcome.kind == Kind::kRejected);
    const auto peek = get(host.query_continuous(query_for(2)));
    MIRAGE_CHECK(peek.queried && peek.queried->source_epoch == 2);
    MIRAGE_CHECK(peek.queried && !peek.queried->analysis_age &&
                 peek.queried->last_analyzed_frame == 0);
    MIRAGE_CHECK(peek.queried && peek.queried->temporal.pending_events == 1);
    MIRAGE_CHECK(peek.queried && !peek.queried->events);
    auto query = query_for(2);
    query.drain_events = true;
    const auto drained = get(host.query_continuous(query));
    MIRAGE_CHECK(drained.queried && drained.queried->events);
    if (drained.queried && drained.queried->events) {
        const auto &events = drained.queried->events->events;
        MIRAGE_CHECK(events.size() == 1);
        if (!events.empty()) {
            MIRAGE_CHECK(events[0].window.source_epoch == 1);
            MIRAGE_CHECK(events[0].change_kind == mirador::SemanticChangeKind::kSourceReset);
        }
    }
    const auto empty = get(host.query_continuous(query));
    MIRAGE_CHECK(empty.queried && empty.queried->events->events.empty());
    MIRAGE_CHECK(ocr.calls == 3);
    host.stop();
    MIRAGE_CHECK(get(host.query_continuous(query)).outcome.kind == Kind::kRejected);
    VisualSessionHost legacy(owner.executor, "query-legacy");
    MIRAGE_CHECK(legacy.start(error));
    MIRAGE_CHECK(get(legacy.query_continuous(query_for())).outcome.kind == Kind::kRejected);
}

void concurrent_commands_and_stop_settle_every_future() {
    for (const int stop_mode : {0, 1, 2}) {
        Owner owner;
        FakeOcrBackend ocr;
        VisualSessionHost host(owner.executor, "query-race", config_for(ocr));
        std::string error;
        MIRAGE_CHECK(host.start(error));
        MIRAGE_CHECK(get(host.submit(refreshed("query-race", 1))).outcome.kind == Kind::kCompleted);
        // Build payloads on the fixture owner: the test assertion counter is not
        // thread-safe. Each producer owns a disjoint prebuilt request/future row.
        std::array<std::vector<VisualAnalysisRequest>, 3> requests;
        std::array<std::vector<std::future<VisualAnalysisResult>>, 3> futures;
        for (std::size_t row = 0; row < requests.size(); ++row)
            for (std::uint64_t i = 0; i < 32; ++i)
                requests[row].push_back(refreshed("query-race", 2 + i * 3 + row));
        struct Producers {
            std::atomic_bool go{false};
            std::vector<std::thread> threads;
            void join() {
                go = true;
                for (auto &thread : threads)
                    if (thread.joinable())
                        thread.join();
            }
            ~Producers() { join(); }
        };
        std::atomic_int submitted{0};
        Producers producers;
        for (std::size_t row = 0; row < requests.size(); ++row) {
            producers.threads.emplace_back([&, row] {
                while (!producers.go)
                    std::this_thread::sleep_for(1ms);
                for (std::size_t i = 0; i < requests[row].size(); ++i) {
                    futures[row].push_back(i % 2 == 0 ? host.submit(std::move(requests[row][i]))
                                                      : host.query_continuous(query_for()));
                    ++submitted;
                    std::this_thread::sleep_for(1ms);
                }
            });
        }
        producers.go = true;
        MIRAGE_CHECK(wait_until([&] { return submitted.load() >= 8; }));
        if (stop_mode == 1)
            host.stop();
        if (stop_mode == 2)
            owner.executor.shutdown(true);
        producers.join();
        for (auto &row : futures) {
            MIRAGE_CHECK(row.size() == 32);
            for (auto &future : row) {
                const auto result = get(std::move(future));
                MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted ||
                             result.outcome.kind == Kind::kQueried ||
                             result.outcome.kind == Kind::kCancelled ||
                             result.outcome.kind == Kind::kRejected);
            }
        }
    }
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

void query_cancels_inflight_analysis_without_inventing_completion() {
    Owner owner;
    InterruptibleOcr ocr;
    VisualSessionHost host(owner.executor, "query-interrupt", config_for(ocr));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto analysis = host.submit(refreshed("query-interrupt", 1));
    MIRAGE_CHECK(wait_until([&] { return ocr.entered.load(); }));
    auto queried = host.query_continuous(query_for());
    MIRAGE_CHECK(get(std::move(analysis)).outcome.kind == Kind::kCancelled);
    const auto initial = get(std::move(queried));
    MIRAGE_CHECK(initial.outcome.kind == Kind::kQueried && initial.queried);
    MIRAGE_CHECK(initial.queried && initial.queried->last_analyzed_frame == 0);
    MIRAGE_CHECK(initial.queried && !initial.queried->analysis_age);
    MIRAGE_CHECK(initial.queried && initial.queried->temporal.confirmed_frame == 0);
    MIRAGE_CHECK(!initial.snapshot && !initial.continuous);
    ocr.release = true;
    MIRAGE_CHECK(get(host.submit(refreshed("query-interrupt", 2))).outcome.kind ==
                 Kind::kCompleted);
    const auto after = get(host.query_continuous(query_for()));
    MIRAGE_CHECK(after.queried && after.queried->last_analyzed_frame == 2);
    MIRAGE_CHECK(after.queried && after.queried->temporal.confirmed_frame == 0);
    owner.executor.shutdown(true);
    MIRAGE_CHECK(get(host.query_continuous(query_for())).outcome.kind == Kind::kRejected);
}

void cancellation_deadline_exception_and_shutdown_settle() {
    for (int variant = 0; variant < 15; ++variant) {
        const auto mode = variant % 5;
        std::fprintf(stderr, "[continuous consumer] lifecycle mode %d\n", mode);
        Owner owner;
        InterruptibleOcr ocr;
        ocr.throws = mode == 2;
        auto config = config_for(ocr);
        if (variant >= 10) {
            config.continuous.reset();
            config.enable_image_session = true;
        }
        VisualSessionHost host(owner.executor, "lifecycle", config);
        std::string error;
        MIRAGE_CHECK(host.start(error));
        auto request = variant < 5 || variant >= 10 ? refreshed("lifecycle", 1)
                                                    : spatial_request("lifecycle", 1);
        request.image_session = variant >= 10;
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
            auto next_request = variant < 5 || variant >= 10 ? refreshed("lifecycle", 2)
                                                             : spatial_request("lifecycle", 2);
            next_request.image_session = variant >= 10;
            const auto next = get(host.submit(std::move(next_request)));
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
void product_companion_keeps_display_state_separate() {
    Owner owner;
    FakeOcrConfig ocr_config;
    ocr_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    FakeOcrBackend ocr(ocr_config);
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    config.enable_image_session = true;
    VisualSessionHost host(owner.executor, "product-companion", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto image = request_for("product-companion", 1);
    image.image_session = true;
    image.admission = VisualAnalysisAdmission::always;
    image.ocr.cache_policy = mirador::CachePolicy::kRefresh;
    auto result = get(host.submit(std::move(image)));
    MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted && result.continuous && result.snapshot);
    MIRAGE_CHECK(result.snapshot &&
                 result.snapshot->coordinate_space == mirador::CoordinateSpaceId::kOriented);
    const auto calls = ocr.calls();
    VisualContinuousQuery query;
    query.image_session = true;
    query.expected_source_revision = 1;
    auto state = get(host.query_continuous(query));
    MIRAGE_CHECK(state.outcome.kind == Kind::kQueried && state.queried);
    MIRAGE_CHECK(ocr.calls() == calls);
    auto display = request_for("product-companion", 2);
    display.continuous.reset();
    display.display_transform = mirador::make_translation(
        0, 0, mirador::CoordinateSpaceId::kOriented, mirador::CoordinateSpaceId::kDisplay);
    display.admission = VisualAnalysisAdmission::always;
    result = get(host.submit(std::move(display)));
    MIRAGE_CHECK(result.outcome.kind == Kind::kCompleted && result.snapshot && !result.continuous);
    MIRAGE_CHECK(result.snapshot &&
                 result.snapshot->coordinate_space == mirador::CoordinateSpaceId::kDisplay);
    state = get(host.query_continuous(query));
    MIRAGE_CHECK(state.queried && state.queried->last_analyzed_frame == 1);
    auto old = request_for("product-companion", 3);
    old.image_session = true;
    old.continuous->source_revision = 2;
    MIRAGE_CHECK(get(host.submit(std::move(old))).outcome.kind == Kind::kCompleted);
    MIRAGE_CHECK(get(host.query_continuous(query)).outcome.kind != Kind::kQueried);
    host.stop();
    MIRAGE_CHECK(get(host.query_continuous(query)).outcome.kind == Kind::kRejected);
}

} // namespace

int main() {
    product_companion_keeps_display_state_separate();
    frame_free_queries_switch_focus_without_observation();
    query_rejection_cancellation_and_drain_preserve_provenance();
    concurrent_commands_and_stop_settle_every_future();
    query_cancels_inflight_analysis_without_inventing_completion();
    spatial_work_reduces_backend_input_and_preserves_coordinates();
    spatial_validation_and_budgets_precede_backend_calls();
    failed_roi_batch_does_not_advance_confirmation();
    combined_plan_budget_is_checked_before_adoption();
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
