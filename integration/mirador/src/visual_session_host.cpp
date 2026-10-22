#include "image_ocr_validation.hpp"
#include "visual_mark_renderer.hpp"
#include <mirage/integration/visual_session_host.hpp>

#include <kairo/comm/mailbox.hpp>
#include <kairo/stop_token.hpp>

#include <mirador/crop.hpp>
#include <mirador/evidence.hpp>
#include <mirador/perception_session.hpp>
#include <mirador/status.hpp>

#include <mirage/integration/visual_frame.hpp>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <future>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace mirage::integration {
namespace {

/// One admitted analysis request: the immutable payload, the cancellation
/// channel the mirador pipeline polls, and the one-shot settlement promise.
struct RequestState {
    explicit RequestState(VisualAnalysisRequest analysis_request)
        : request(std::move(analysis_request)) {}
    explicit RequestState(VisualContinuousQuery query_request) : query(std::move(query_request)) {}

    VisualAnalysisRequest request;
    std::optional<VisualContinuousQuery> query;
    desktop::CancelToken cancel; ///< latest-wins / stop cancellation channel
    std::promise<VisualAnalysisResult> done;
};

/// Settlement must never throw, even for an abandoned future.
void settle(RequestState &state, VisualAnalysisResult result) {
    // Mailbox version slots can outlive this completed request. Release its
    // source pixels/policy before waking the consumer, so transport history
    // cannot pin all generations of ContinuousCaptureHost's pixel budget.
    state.request.frame = {};
    state.request.continuous.reset();
    state.query.reset();
    try {
        state.done.set_value(std::move(result));
    } catch (...) {
        // Only a double settlement or a promise without state can refuse;
        // the one-slot request discipline above rules both out.
    }
}

void settle_cancelled(RequestState &state, std::string message) {
    VisualAnalysisResult result;
    result.outcome.kind = VisualAnalysisOutcome::Kind::kCancelled;
    result.outcome.message = std::move(message);
    settle(state, std::move(result));
}

void settle_rejected(RequestState &state, std::string message) {
    VisualAnalysisResult result;
    result.outcome.kind = VisualAnalysisOutcome::Kind::kRejected;
    result.outcome.message = std::move(message);
    settle(state, std::move(result));
}

/// Maps a mirador call status onto the outcome kinds: kCancelled and
/// kTimeout are first-class results, everything else is a failure.
VisualAnalysisOutcome map_status(const mirador::Status &status) {
    VisualAnalysisOutcome outcome;
    if (status.code() == mirador::ErrorCode::kCancelled) {
        outcome.kind = VisualAnalysisOutcome::Kind::kCancelled;
    } else if (status.code() == mirador::ErrorCode::kTimeout) {
        outcome.kind = VisualAnalysisOutcome::Kind::kTimedOut;
    } else {
        outcome.kind = VisualAnalysisOutcome::Kind::kFailed;
        outcome.code = status.code();
    }
    outcome.message = status.message();
    return outcome;
}

void settle_failed(RequestState &state, const mirador::Status &status) {
    VisualAnalysisResult result;
    result.outcome = map_status(status);
    settle(state, std::move(result));
}

/// Marks the accumulating result failed and settles it: every stage output
/// produced before the failure rides along (diagnostics, not a fresh shell).
void fail_result(VisualAnalysisResult &result, const mirador::Status &status) {
    result.outcome = map_status(status);
}

void fail_result(VisualAnalysisResult &result, mirador::ErrorCode code, std::string message) {
    result.outcome.kind = VisualAnalysisOutcome::Kind::kFailed;
    result.outcome.code = code;
    result.outcome.message = std::move(message);
}

/// Rounds one continuous coordinate half away from zero — the same rule the
/// observation mapper applies, so a probe crop and its published entry agree.
std::int32_t round_component(float value) { return static_cast<std::int32_t>(std::lroundf(value)); }

/// Rounds a continuous region and clamps it into the view extent. A region
/// that degenerates to zero area stays empty (zero-area probes contribute no
/// pixels to fingerprint).
mirador::RectI clamp_region(const mirador::RectF &bounds, const mirador::RectI &extent) {
    const std::int32_t width = round_component(bounds.width);
    const std::int32_t height = round_component(bounds.height);
    if (width <= 0 || height <= 0) {
        return mirador::RectI{0, 0, 0, 0};
    }
    const mirador::RectI rounded{round_component(bounds.x), round_component(bounds.y), width,
                                 height};
    return mirador::intersect(rounded, extent);
}

mirador::RectF to_rectf(const mirador::RectI &rect) {
    return mirador::RectF{static_cast<float>(rect.x), static_cast<float>(rect.y),
                          static_cast<float>(rect.width), static_cast<float>(rect.height)};
}

/// Template evidence recorded by the probe stage, awaiting the fusion's
/// evidence ids (assigned in add order).
struct TemplateProbe {
    std::uint64_t entry_id = 0;
    mirador::RectF bounds; ///< the probing detection's bounds (kOriented)
    double similarity = 0.0;
    std::uint64_t evidence_id = 0;
};

/// Crops `roi` out of the request frame under the template fingerprint
/// budget (the crop buffer and the fingerprint intermediates share it).
mirador::Result<mirador::ImageBuffer> crop_patch(const mirador::Frame &frame,
                                                 const mirador::RectI &roi,
                                                 const VisualTemplateIndexConfig &templates) {
    return mirador::crop(frame.image, roi, templates.fingerprint_max_bytes);
}

} // namespace

/// State shared between the host (admission, supersede, stop) and the
/// blocking worker (execution). Mutex and condition variable are the sleep /
/// wakeup primitive the blocking-worker contract requires; the request
/// payload itself travels through the kairo::comm LatestMailbox, never
/// through the lock (AGENTS.md rule 4).
struct detail::VisualSessionCore {
    VisualSessionCore(std::string host_source_id, VisualSessionConfig host_config,
                      mirador::PerceptionSession perception_session,
                      VisualTemplateIndex template_index,
                      std::optional<mirador::ContinuousPerception> continuous_state)
        : source_id(std::move(host_source_id)), config(std::move(host_config)),
          session(std::move(perception_session)), templates(std::move(template_index)),
          coordinator(std::move(continuous_state)) {}

    std::unique_ptr<VisualSessionCore> image_core;
    const std::string source_id;
    const VisualSessionConfig config;
    /// Only ever touched on the worker thread: one session, one scheduling
    /// context (DEC-016 decision 4).
    mirador::PerceptionSession session;
    /// The template index shares the session's serial context (plan item
    /// `M3-04`); it dies with the core when the host stops.
    VisualTemplateIndex templates;
    /// Experimental coordination remains on this same worker. It stores no pixels.
    std::optional<mirador::ContinuousPerception> coordinator;
    std::uint64_t source_revision = 0, last_continuous_sequence = 0;
    std::int32_t last_width = 0, last_height = 0;
    mirador::Rotation last_rotation = mirador::Rotation::k0;
    std::optional<mirador::ImportancePolicy> importance;
    std::uint64_t evidence_digest = 0;
    bool spatial_mode = false;
    std::uint64_t last_analyzed_frame = 0;
    std::optional<std::chrono::steady_clock::time_point> last_analyzed_at;
    struct BackgroundBaseline {
        mirador::ChangeSignature signature;
        std::uint64_t sequence = 0, policy_revision = 0;
    };
    // One 64x64 grayscale signature (4096 bytes), serial worker only.
    std::optional<BackgroundBaseline> background_baseline;
    bool assets_enrolled = false; // Only touched by this source worker.

    std::mutex mutex;
    bool accepting = true; // Protected by mutex, including transport publication.
    /// Wakes the worker for a published request, a stop, or the bounded
    /// idle slice; the executor's stop path calls wakeup() through the
    /// worker. The worker never holds this mutex while a request runs.
    std::condition_variable cv;
    /// Latest-wins transport of requests; overwritten slots are observable
    /// in the mailbox statistics (overwritten-request count).
    kairo::comm::LatestMailbox<std::shared_ptr<RequestState>> mailbox{"mirage-visual-request"};
    /// Queued (published, not yet picked up) and executing requests, kept
    /// for explicit supersede and shutdown settlement (RULE-10).
    std::shared_ptr<RequestState> queued;
    std::shared_ptr<RequestState> in_flight;
};

namespace {

void publish_request(detail::VisualSessionCore &core, const std::shared_ptr<RequestState> &state) {
    std::shared_ptr<RequestState> superseded;
    bool accepted = false;
    {
        std::lock_guard lock(core.mutex);
        // Keep the transport order and queued identity identical across producers.
        // A failed bounded publication leaves the previous command untouched.
        if (core.accepting && core.mailbox.try_publish(state)) {
            superseded = std::move(core.queued);
            if (core.in_flight)
                core.in_flight->cancel.request_cancel();
            core.queued = state;
            accepted = true;
        }
    }
    if (!accepted) {
        settle_rejected(*state, "visual command admission closed or mailbox publication rejected");
        return;
    }
    core.cv.notify_all();
    if (superseded)
        settle_cancelled(*superseded, "superseded by a newer visual command");
}

std::uint64_t evidence_digest(const VisualContinuousRequest &input) {
    return input.spatial_evidence_digest != 0 ? input.spatial_evidence_digest
                                              : input.full_frame_evidence_digest;
}

bool query_policy_fits(const mirador::AttentionPolicy &policy,
                       const VisualContinuousConfig &limits) {
    auto remaining = static_cast<std::size_t>(limits.max_coordination_bytes);
    if (policy.conditions.capacity() > remaining / sizeof(mirador::AttentionCondition))
        return false;
    remaining -= policy.conditions.capacity() * sizeof(mirador::AttentionCondition);
    for (const auto &condition : policy.conditions) {
        for (const auto *text : {&condition.text_equals, &condition.label_equals}) {
            if (text->capacity() + 1 > remaining)
                return false;
            remaining -= text->capacity() + 1;
        }
    }
    return true;
}
/// No algorithm is reproduced here: apply owner configuration, scope the old
/// session's caches/IDs to their source revision, and let Mirador plan/relate.
mirador::Result<bool> prepare_continuous(detail::VisualSessionCore &core,
                                         const VisualAnalysisRequest &request) {
    const auto &input = *request.continuous;
    const auto &limits = *core.config.continuous;
    if (const auto valid = mirador::validate(request.frame.image); !valid)
        return valid.status();
    if (input.attention.conditions.size() > static_cast<std::size_t>(limits.max_focus_conditions))
        return mirador::Status{mirador::ErrorCode::kBudgetExceeded,
                               "attention condition cap exceeded"};
    auto &coordinator = *core.coordinator;
    const auto previous_policy = coordinator.policy_revision();
    if (const auto applied = coordinator.set_attention_policy(input.attention); !applied)
        return applied.status();
    const bool new_source = core.source_revision != input.source_revision;
    if (new_source && core.source_revision != 0) {
        auto session = mirador::PerceptionSession::create(
            {core.source_id, core.config.frame_cache_bytes, core.config.result_cache_bytes});
        if (!session)
            return session.status();
        core.session = session.take_value();
        (void)coordinator.notify_source_reset();
    }
    if (new_source) {
        core.last_analyzed_frame = 0;
        core.last_analyzed_at.reset();
    }
    const bool invalidate = new_source || previous_policy != coordinator.policy_revision() ||
                            !core.importance || *core.importance != input.importance ||
                            core.evidence_digest != evidence_digest(input) ||
                            core.spatial_mode != (input.spatial_evidence_digest != 0);
    core.source_revision = input.source_revision;
    core.last_continuous_sequence = request.frame.sequence;
    core.last_width = request.frame.image.width;
    core.last_height = request.frame.image.height;
    core.last_rotation = request.frame.image.rotation;
    core.importance = input.importance;
    core.evidence_digest = evidence_digest(input);
    core.spatial_mode = input.spatial_evidence_digest != 0;
    return invalidate;
}

std::optional<std::int64_t> plan_owned_bytes(const mirador::WorkPlan &plan, std::int64_t budget) {
    std::int64_t bytes = 0;
    const auto add = [&](std::size_t capacity, std::size_t element_size) {
        if (capacity > static_cast<std::size_t>(budget - bytes) / element_size)
            return false;
        bytes += static_cast<std::int64_t>(capacity * element_size);
        return true;
    };
    if (!add(plan.attention.work.capacity(), sizeof(mirador::PlannedWork)) ||
        !add(plan.attention.resolutions.capacity(), sizeof(mirador::FocusResolution)) ||
        !add(plan.attention.reconsiderations.capacity(), sizeof(mirador::FocusReconsideration)) ||
        !add(plan.work.capacity(), sizeof(mirador::PlannedWorkBinding)) ||
        !add(plan.reuse.capacity(), sizeof(mirador::WorkReuse)) ||
        !add(plan.invalidations.capacity(), sizeof(mirador::WorkInvalidation)))
        return std::nullopt;
    for (const auto &binding : plan.work)
        if (!add(binding.token.source_id.capacity() + 1, sizeof(char)))
            return std::nullopt;
    return bytes;
}

std::int64_t state_owned_bytes(const mirador::TemporalSemanticState &temporal,
                               const std::vector<mirador::FocusStateResolution> &focus_states,
                               const mirador::EventDrain *events,
                               const mirador::WorkAcceptReport *accepted = nullptr) {
    std::size_t bytes = temporal.entities.capacity() * sizeof(mirador::TemporalEntityState) +
                        focus_states.capacity() * sizeof(mirador::FocusStateResolution);
    for (const auto &focus : focus_states)
        bytes += focus.stable_ids.capacity() * sizeof(std::uint64_t);
    if (events) {
        bytes += events->events.capacity() * sizeof(mirador::SemanticEvent);
        for (const auto &event : events->events)
            bytes += event.window.source_id.capacity() + 1;
    }
    if (accepted)
        bytes += accepted->item_rejections.capacity() *
                 sizeof(std::optional<mirador::WorkRejectionReason>);
    return static_cast<std::int64_t>(bytes);
}

void execute_query(detail::VisualSessionCore &core, RequestState &state) {
    const auto &query = *state.query;
    const auto interrupted = [&] {
        if (state.cancel.cancelled() || query.external_cancel.cancelled()) {
            settle_cancelled(state, "visual state query cancelled before commit");
            return true;
        }
        if (query.deadline && std::chrono::steady_clock::now() >= *query.deadline) {
            settle_failed(state,
                          {mirador::ErrorCode::kTimeout, "visual state query deadline expired"});
            return true;
        }
        return false;
    };
    if (interrupted())
        return;
    if (core.source_revision == 0 || query.expected_source_revision != core.source_revision) {
        settle_rejected(state, "visual state query source revision is not current");
        return;
    }
    if (query.attention) {
        if (query.attention->conditions.size() >
            static_cast<std::size_t>(core.config.continuous->max_focus_conditions)) {
            settle_failed(state, {mirador::ErrorCode::kBudgetExceeded,
                                  "query attention condition cap exceeded"});
            return;
        }
        if (auto valid = mirador::validate_attention_policy(*query.attention); !valid) {
            settle_failed(state, valid.status());
            return;
        }
    }
    if (interrupted())
        return;
    if (query.attention) {
        const auto previous = core.coordinator->policy_revision();
        auto applied = core.coordinator->set_attention_policy(*query.attention);
        if (!applied) {
            settle_failed(state, applied.status());
            return;
        }
        if (previous != core.coordinator->policy_revision())
            core.background_baseline.reset();
    }
    // Bounded synchronous commit: queries do not advance observations. Once a
    // policy has applied / a drain can happen, late cancellation cannot discard it.
    VisualContinuousQueryView view;
    view.source_revision = core.source_revision;
    view.source_epoch = core.coordinator->source_epoch();
    view.policy_revision = core.coordinator->policy_revision();
    view.last_analyzed_frame = core.last_analyzed_frame;
    if (core.last_analyzed_at)
        view.analysis_age = std::chrono::steady_clock::now() - *core.last_analyzed_at;
    view.temporal = core.coordinator->query_temporal_state();
    view.focus = core.coordinator->query_focus_states();
    if (query.drain_events)
        view.events = core.coordinator->drain_events().take_value();
    view.coordination_owned_bytes = core.coordinator->coordination_owned_bytes();
    view.state_owned_bytes =
        state_owned_bytes(view.temporal, view.focus, view.events ? &*view.events : nullptr);
    VisualAnalysisResult result;
    result.outcome.kind = VisualAnalysisOutcome::Kind::kQueried;
    result.cache_stats.continuous_coordination_bytes = view.coordination_owned_bytes;
    result.queried = std::move(view);
    settle(state, std::move(result));
}

mirador::Result<VisualAnalysisResult::SpatialExecution>
plan_spatial(detail::VisualSessionCore &core, const VisualAnalysisRequest &request,
             const mirador::ChangeReport &change, const mirador::ExecutionContext &context) {
    // Current frame, genuinely empty evidence: no old entity is asserted current,
    // and this stateless fusion never advances the live session's tracker.
    auto fused = mirador::fuse_evidence({}, request.frame, {}, context);
    if (!fused)
        return fused.status();
    mirador::SemanticSnapshot unobserved;
    unobserved.coordinate_space = mirador::CoordinateSpaceId::kOriented;
    unobserved.regions = std::move(fused.value().regions);
    unobserved.change = change;
    auto planned = core.coordinator->plan_attention(unobserved, request.frame,
                                                    request.continuous->importance, context);
    if (!planned)
        return planned.status();
    const auto &limits = *core.config.continuous;
    mirador::WorkPlan storage;
    storage.attention = planned.take_value();
    const auto bytes = plan_owned_bytes(storage, limits.max_plan_bytes);
    if (!bytes ||
        storage.attention.work.size() > static_cast<std::size_t>(limits.max_spatial_work_items))
        return mirador::Status{mirador::ErrorCode::kBudgetExceeded,
                               "spatial work/plan budget exceeded before recognition"};
    VisualAnalysisResult::SpatialExecution output;
    output.plan = std::move(storage.attention);
    output.plan_owned_bytes = *bytes;
    for (const auto &work : output.plan.work) {
        const auto &roi = work.roi;
        if (work.kind != mirador::PlannedWorkKind::kFocus ||
            work.space != mirador::CoordinateSpaceId::kOriented || !std::isfinite(roi.x) ||
            !std::isfinite(roi.y) || !std::isfinite(roi.width) || !std::isfinite(roi.height) ||
            roi.x < 0 || roi.y < 0 || roi.width <= 0 || roi.height <= 0 ||
            std::trunc(roi.x) != roi.x || std::trunc(roi.y) != roi.y ||
            std::trunc(roi.width) != roi.width || std::trunc(roi.height) != roi.height ||
            static_cast<double>(roi.x) + roi.width > request.frame.image.width ||
            static_cast<double>(roi.y) + roi.height > request.frame.image.height)
            return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                                   "spatial work must be an in-frame oriented integer ROI"};
        const auto pixels =
            static_cast<std::int64_t>(roi.width) * static_cast<std::int64_t>(roi.height);
        if (pixels > limits.max_spatial_pixels - output.requested_pixels)
            return mirador::Status{mirador::ErrorCode::kBudgetExceeded,
                                   "spatial pixel budget exceeded before recognition"};
        output.requested_pixels += pixels;
    }
    return output;
}

/// Runs the requested capabilities serially on the session: template
/// enrollment, change gating, geometry proposals, OCR, detection (optionally
/// ROI-split per geometry proposal), template probes at the detection
/// regions, then fusion of the produced evidence only (DEC-016 decision 3:
/// accessibility regions are never fusion input) with the enrolled template
/// identities stamped onto the fused template regions. Every mirador call
/// polls the request's execution context, so a cancellation or deadline
/// lands at the next stage boundary as an explicit status.
void execute(detail::VisualSessionCore &core, RequestState &state) {
    if (state.query) {
        execute_query(core, state);
        return;
    }
    const VisualAnalysisRequest &request = state.request;
    // The DEC-016 mapping: internal latest-wins token, the caller's external
    // token (a default token never cancels), and the steady-clock deadline.
    mirador::ExecutionContext context;
    context.deadline = request.deadline;
    context.is_cancelled = [internal = state.cancel, external = request.external_cancel] {
        return internal.cancelled() || external.cancelled();
    };

    auto baseline = std::move(core.background_baseline);
    core.background_baseline.reset();
    std::optional<mirador::ChangeSignature> current_signature;
    std::optional<VisualContinuousResult> continuous_result;
    VisualAnalysisResult result;
    // Cancellation lands before the first side effect (enrollments mutate
    // session state), never silently swallowed.
    if (context.is_cancelled && context.is_cancelled()) {
        result.outcome.kind = VisualAnalysisOutcome::Kind::kCancelled;
        result.outcome.message = "cancelled before the first stage";
        settle(state, std::move(result));
        return;
    }
    if (request.continuous) {
        const auto revision = request.continuous->source_revision;
        const bool same_source = revision == core.source_revision;
        if (revision < core.source_revision ||
            (same_source && request.frame.sequence <= core.last_continuous_sequence)) {
            core.background_baseline = std::move(baseline);
            settle_rejected(state, "stale continuous source revision or frame sequence");
            return;
        }
        if (same_source && (request.frame.image.width != core.last_width ||
                            request.frame.image.height != core.last_height ||
                            request.frame.image.rotation != core.last_rotation)) {
            core.background_baseline = std::move(baseline);
            settle_rejected(state, "continuous geometry change requires a new source revision");
            return;
        }
        if (same_source && core.evidence_digest != 0 &&
            evidence_digest(*request.continuous) == core.evidence_digest &&
            core.spatial_mode != (request.continuous->spatial_evidence_digest != 0)) {
            core.background_baseline = std::move(baseline);
            settle_rejected(state, "execution scope change requires a new evidence digest");
            return;
        }
    }

    // The adapter's rotation-k0 capture form is the only shape the
    // pixel-space stages accept (submit() rejects the rest), so oriented
    // coordinates are the view's own pixel coordinates here.
    const mirador::RectI extent{0, 0, request.frame.image.width, request.frame.image.height};

    const auto interrupted = [&] {
        if (context.is_cancelled()) {
            result.outcome =
                map_status({mirador::ErrorCode::kCancelled, "template stage cancelled"});
        } else if (context.deadline && std::chrono::steady_clock::now() >= *context.deadline) {
            result.outcome = map_status({mirador::ErrorCode::kTimeout, "template stage expired"});
        } else {
            return false;
        }
        if (core.coordinator)
            result.snapshot.reset();
        settle(state, std::move(result));
        return true;
    };
    if (core.coordinator) {
        if (interrupted())
            return;
        const auto prepared = prepare_continuous(core, request);
        if (!prepared) {
            settle_failed(state, prepared.status());
            return;
        }
        if (prepared.value())
            baseline.reset();
    }
    if (request.probe_geometry_templates) {
        if (!core.assets_enrolled) {
            for (const auto &asset : core.config.template_assets) {
                if (interrupted())
                    return;
                const auto &p = asset.pixels;
                mirador::ImageView view;
                view.data = reinterpret_cast<const std::byte *>(p.pixels.data());
                view.width = p.width;
                view.height = p.height;
                view.row_stride_bytes = static_cast<std::int64_t>(p.stride);
                view.format = mirador::PixelFormat::kBgra8;
                const auto enrolled = core.templates.enroll(asset.template_id, view);
                if (!enrolled) {
                    fail_result(result, enrolled.status());
                    settle(state, std::move(result));
                    return;
                }
            }
            core.assets_enrolled = true;
        }
        for (const auto &asset : core.config.template_assets) {
            if (!core.templates.contains(asset.template_id)) {
                fail_result(result, mirador::ErrorCode::kInvalidArgument,
                            "configured template identity was evicted");
                settle(state, std::move(result));
                return;
            }
        }
    }

    // ---- template enrollment (explicit session-state mutation, fail closed)
    for (const VisualTemplateEnrollment &enrollment : request.enrollments) {
        const mirador::RectI roi = mirador::intersect(enrollment.patch, extent);
        if (roi.width <= 0 || roi.height <= 0) {
            fail_result(result, mirador::ErrorCode::kInvalidArgument,
                        "enrollment patch is empty or outside the frame");
            settle(state, std::move(result));
            return;
        }
        const mirador::Result<mirador::ImageBuffer> cropped =
            crop_patch(request.frame, roi, core.config.templates);
        if (!cropped.ok()) {
            fail_result(result, cropped.status());
            settle(state, std::move(result));
            return;
        }
        if (mirador::Result<void> enrolled =
                core.templates.enroll(enrollment.template_id, cropped.value().view());
            !enrolled.ok()) {
            fail_result(result, enrolled.status());
            settle(state, std::move(result));
            return;
        }
    }

    // ---- change gating
    if (request.analyze_change) {
        mirador::Result<mirador::ChangeReport> change =
            core.session.analyze_change(request.frame, {}, context);
        if (!change.ok()) {
            result.outcome = map_status(change.status());
            settle(state, std::move(result));
            return;
        }
        result.change = change.take_value();
    }

    // Compare optional background work to its last completed analysis, rather
    // than to an adjacent skipped frame (which could hide accumulated drift).
    if (request.admission == VisualAnalysisAdmission::when_changed) {
        mirador::ChangeDetectionParams params;
        params.fingerprint_early_exit = false;
        auto signature = mirador::make_change_signature(request.frame.image, params);
        if (!signature) {
            settle_failed(state, signature.status());
            return;
        }
        current_signature = signature.take_value();
        if (baseline && baseline->policy_revision == request.analysis_policy_revision) {
            auto report = mirador::detect_change(baseline->signature, *current_signature, params);
            if (!report) {
                settle_failed(state, report.status());
                return;
            }
            result.admission_change = report.take_value();
            result.admission_baseline_sequence = baseline->sequence;
        }
        const bool force_analysis =
            !request.enrollments.empty() || request.render_marks ||
            (request.run_ocr && request.ocr.cache_policy == mirador::CachePolicy::kRefresh) ||
            (request.run_detector &&
             request.detector.cache_policy == mirador::CachePolicy::kRefresh);
        if (result.admission_change && !force_analysis &&
            result.admission_change->classification == mirador::ChangeClassification::kNone) {
            if (interrupted())
                return;
            core.background_baseline = std::move(baseline);
            result.cache_stats.background_signature_bytes =
                core.background_baseline->signature.thumbnail.byte_size();
            result.outcome.kind = VisualAnalysisOutcome::Kind::kSkippedUnchanged;
            result.outcome.message = "unchanged background screen; no current analysis evidence";
            result.cache_stats.frame_cache_bytes = core.session.frame_cache().byte_size();
            result.cache_stats.frame_cache_entries = core.session.frame_cache().entry_count();
            result.cache_stats.result_cache_bytes = core.session.result_cache().byte_size();
            result.cache_stats.result_cache_entries = core.session.result_cache().entry_count();
            result.cache_stats.template_index_bytes = core.templates.byte_size();
            result.cache_stats.template_index_entries = core.templates.entry_count();
            if (core.coordinator)
                result.cache_stats.continuous_coordination_bytes =
                    core.coordinator->coordination_owned_bytes();
            settle(state, std::move(result));
            return;
        }
    }

    // ---- Spatial selection is complete and budgeted before any expensive call.
    if (request.continuous && request.continuous->spatial_evidence_digest != 0) {
        auto planned = plan_spatial(core, request, result.change, context);
        if (!planned) {
            settle_failed(state, planned.status());
            return;
        }
        result.spatial = planned.take_value();
        const auto &limits = *core.config.continuous;
        for (const auto &work : result.spatial->plan.work) {
            if (interrupted())
                return;
            if (request.run_ocr) {
                auto ocr = request.ocr;
                ocr.roi = work.roi;
                ocr.roi_space = work.space;
                auto regions =
                    core.session.run_ocr(request.frame, core.config.ocr_backend, ocr, context);
                if (!regions) {
                    fail_result(result, regions.status());
                    settle(state, std::move(result));
                    return;
                }
                ++result.spatial->ocr_completed;
                if (result.text.size() + result.detections.size() + regions.value().size() >
                    static_cast<std::size_t>(limits.max_entities)) {
                    fail_result(result, mirador::ErrorCode::kBudgetExceeded,
                                "spatial evidence count budget exceeded");
                    settle(state, std::move(result));
                    return;
                }
                auto values = regions.take_value();
                result.text.insert(result.text.end(), std::make_move_iterator(values.begin()),
                                   std::make_move_iterator(values.end()));
            }
            if (request.run_detector) {
                auto detector = request.detector;
                detector.roi = work.roi;
                detector.roi_space = work.space;
                auto regions = core.session.run_detector(
                    request.frame, core.config.detector_backend, detector, context);
                if (!regions) {
                    fail_result(result, regions.status());
                    settle(state, std::move(result));
                    return;
                }
                ++result.spatial->detector_completed;
                if (result.text.size() + result.detections.size() + regions.value().size() >
                    static_cast<std::size_t>(limits.max_entities)) {
                    fail_result(result, mirador::ErrorCode::kBudgetExceeded,
                                "spatial evidence count budget exceeded");
                    settle(state, std::move(result));
                    return;
                }
                auto values = regions.take_value();
                result.detections.insert(result.detections.end(),
                                         std::make_move_iterator(values.begin()),
                                         std::make_move_iterator(values.end()));
            }
        }
    }

    // ---- geometric closed-region proposals (geometric facts only)
    std::vector<mirador::GeometricRegionProposal> proposals;
    if (request.geometry.run) {
        mirador::Result<mirador::Frame> gray = convert_frame(
            request.frame, mirador::PixelFormat::kGray8, request.geometry.gray_convert_max_bytes);
        if (!gray.ok()) {
            fail_result(result, gray.status());
            settle(state, std::move(result));
            return;
        }
        mirador::SegmentGrowingLineDetector detector{request.geometry.segment_detector};
        mirador::Result<mirador::LineSegmentSet> segments =
            detector.detect(gray.value().image, request.geometry.line_detect, context);
        if (!segments.ok()) {
            fail_result(result, segments.status());
            settle(state, std::move(result));
            return;
        }
        mirador::Result<std::vector<mirador::LineSegment>> filtered =
            mirador::filter_segments(segments.value().segments, request.geometry.line_filter);
        if (!filtered.ok()) {
            fail_result(result, filtered.status());
            settle(state, std::move(result));
            return;
        }
        mirador::Result<std::vector<mirador::LineSegment>> merged =
            mirador::merge_collinear(filtered.value(), request.geometry.merge);
        if (!merged.ok()) {
            fail_result(result, merged.status());
            settle(state, std::move(result));
            return;
        }
        mirador::Result<std::vector<mirador::GeometricRegionProposal>> proposed =
            mirador::propose_regions(merged.value(), request.geometry.proposal, context);
        if (!proposed.ok()) {
            fail_result(result, proposed.status());
            settle(state, std::move(result));
            return;
        }
        proposals = proposed.take_value();
        result.geometry_proposals = proposals;
    }

    // ---- OCR
    if (request.run_ocr && !result.spatial) {
        if (request.ocr_budget) {
            // M8-07 (upstream DEC-025): budgeted execution with per-line
            // completion states. No configured backend fails the request
            // explicitly — a silent legacy fallback would promise the
            // all-or-nothing semantics the caller explicitly opted out of.
            if (!core.config.budgeted_ocr_backend) {
                fail_result(result, mirador::ErrorCode::kBackendUnavailable,
                            "budgeted OCR requested without a configured budgeted backend");
                settle(state, std::move(result));
                return;
            }
            mirador::BudgetedOcrRequest budgeted;
            budgeted.roi = request.ocr.roi;
            budgeted.roi_space = request.ocr.roi_space;
            budgeted.max_side = request.ocr.max_side;
            budgeted.output_space = request.ocr.output_space;
            budgeted.language_hint = request.ocr.language_hint;
            budgeted.min_confidence = request.ocr.min_confidence;
            budgeted.backend_params = request.ocr.backend_params;
            budgeted.max_rec_lines = request.ocr_budget->max_rec_lines;
            budgeted.selection = request.ocr_budget->selection;
            budgeted.soft_budget = request.ocr_budget->soft_budget;
            budgeted.finalize_reserve = request.ocr_budget->finalize_reserve;
            budgeted.priorities = request.ocr_budget->priorities;
            budgeted.priority_space = request.ocr_budget->priority_space;
            mirador::Result<mirador::BudgetedOcrReport> report = core.session.run_ocr_budgeted(
                request.frame, core.config.budgeted_ocr_backend, budgeted, context);
            if (!report.ok()) {
                result.outcome = map_status(report.status());
                settle(state, std::move(result));
                return;
            }
            mirador::BudgetedOcrReport value = report.take_value();
            VisualAnalysisResult::OcrBatchDiagnostics diagnostics;
            diagnostics.completion = value.batch.completion;
            diagnostics.stop_reason = value.batch.stop_reason;
            diagnostics.detected = value.batch.detected_count;
            diagnostics.recognized = value.batch.recognized_count;
            diagnostics.deferred = value.batch.deferred_count;
            diagnostics.det_limit = value.batch.det_limit;
            diagnostics.det_limit_hit = value.batch.det_limit_hit;
            diagnostics.geometry_space = value.geometry_space;
            for (const mirador::OcrLineRecord &line : value.batch.lines) {
                if (line.state == mirador::OcrCompletionState::kRecognized) {
                    mirador::TextRegion region;
                    region.bounds = line.bounds;
                    region.utf8_text = line.utf8_text;
                    region.confidence = line.rec_confidence.value_or(0.0F);
                    result.text.push_back(std::move(region));
                } else if (line.state == mirador::OcrCompletionState::kDeferred) {
                    diagnostics.deferred_bounds.push_back(line.bounds);
                }
            }
            result.ocr_batch = std::move(diagnostics);
        } else {
            mirador::Result<std::vector<mirador::TextRegion>> regions =
                core.session.run_ocr(request.frame, core.config.ocr_backend, request.ocr, context);
            if (!regions.ok()) {
                result.outcome = map_status(regions.status());
                settle(state, std::move(result));
                return;
            }
            result.text = regions.take_value();
        }
    }

    // ---- detection: the caller's single request, or one request per
    // geometry proposal context ROI (the closed regions supply the
    // detection requests; each ROI keeps its own capability-cache key).
    if (request.run_detector && !result.spatial) {
        const bool roi_split = request.geometry.run &&
                               request.geometry.detector_roi_from_geometry && !proposals.empty();
        if (roi_split) {
            for (const mirador::GeometricRegionProposal &proposal : proposals) {
                const mirador::RectI roi = mirador::intersect(proposal.context_bounds, extent);
                if (roi.width <= 0 || roi.height <= 0) {
                    // Unreachable for proposals derived from this frame's
                    // own segments; kept fail-closed instead of skipping.
                    fail_result(result, mirador::ErrorCode::kInvalidArgument,
                                "geometry context ROI is empty or outside the frame");
                    settle(state, std::move(result));
                    return;
                }
                mirador::DetectionRequest per_region = request.detector;
                per_region.roi = to_rectf(roi);
                per_region.roi_space = mirador::CoordinateSpaceId::kOriented;
                mirador::Result<std::vector<mirador::DetectionRegion>> regions =
                    core.session.run_detector(request.frame, core.config.detector_backend,
                                              per_region, context);
                if (!regions.ok()) {
                    fail_result(result, regions.status());
                    settle(state, std::move(result));
                    return;
                }
                std::vector<mirador::DetectionRegion> regions_value = regions.take_value();
                result.detections.insert(result.detections.end(),
                                         std::make_move_iterator(regions_value.begin()),
                                         std::make_move_iterator(regions_value.end()));
            }
        } else {
            mirador::Result<std::vector<mirador::DetectionRegion>> regions =
                core.session.run_detector(request.frame, core.config.detector_backend,
                                          request.detector, context);
            if (!regions.ok()) {
                result.outcome = map_status(regions.status());
                settle(state, std::move(result));
                return;
            }
            result.detections = regions.take_value();
        }
    }

    // ---- template probes at the detection regions (three-tier index match;
    // the reuse policy is the caller's clear-winner threshold)
    std::vector<TemplateProbe> template_probes;
    const auto probe_patch = [&](const mirador::RectF &bounds) {
        if (interrupted())
            return false;
        const auto roi = clamp_region(bounds, extent);
        if (roi.width <= 0 || roi.height <= 0)
            return true;
        const auto cropped = crop_patch(request.frame, roi, core.config.templates);
        if (!cropped) {
            fail_result(result, cropped.status());
            settle(state, std::move(result));
            return false;
        }
        auto hit = core.templates.probe(cropped.value().view(), request.template_query,
                                        request.template_reuse_threshold);
        if (!hit) {
            fail_result(result, hit.status());
            settle(state, std::move(result));
            return false;
        }
        if (hit.value()) {
            result.template_hits.push_back(*hit.value());
            template_probes.push_back({hit.value()->entry_id, bounds, hit.value()->similarity, 0});
        }
        return true;
    };
    if (request.probe_templates && request.run_detector)
        for (const auto &region : result.detections)
            if (!probe_patch(region.bounds))
                return;
    if (request.probe_geometry_templates)
        for (const auto &proposal : proposals) {
            const auto area =
                static_cast<double>(proposal.tight_bounds.width) * proposal.tight_bounds.height;
            if (area <= request.geometry.max_fused_area_fraction *
                            static_cast<double>(extent.width) * extent.height &&
                !probe_patch(to_rectf(proposal.tight_bounds)))
                return;
        }

    // ---- fusion of the produced visual evidence, then the enrolled template
    // identities are stamped onto the fused template regions (the fusion
    // itself carries no template label source; the DEC-016 mapper reads the
    // identity from the label)
    if (request.image_session) {
        for (std::size_t i = 0; i < result.text.size(); ++i) {
            if (const auto *invalid = detail::invalid_image_text(
                    result.text[i], request.frame.image.width, request.frame.image.height)) {
                result.invalid_ocr_region = i;
                result.outcome = {VisualAnalysisOutcome::Kind::kFailed,
                                  mirador::ErrorCode::kInvalidArgument, invalid};
                result.text.clear();
                settle(state, std::move(result));
                return;
            }
        }
    }
    if (request.fuse) {
        if (!core.coordinator && !request.display_transform) {
            fail_result(result, mirador::ErrorCode::kInvalidArgument,
                        "fusion requires the kOriented -> kDisplay display transform (DEC-016)");
            settle(state, std::move(result));
            return;
        }
        mirador::EvidenceSet evidence;
        for (const mirador::TextRegion &region : result.text) {
            // Empty recognition is not a text fact; it can otherwise coalesce
            // unrelated geometry candidates through an oversized OCR box (DEC-060).
            if (region.utf8_text.empty())
                continue;
            if (mirador::Result<std::uint64_t> added =
                    evidence.add_text(region, request.ocr.output_space);
                !added.ok()) {
                settle_failed(state, added.status());
                return;
            }
        }
        for (const mirador::DetectionRegion &region : result.detections) {
            if (mirador::Result<std::uint64_t> added =
                    evidence.add_detection(region, request.detector.output_space);
                !added.ok()) {
                settle_failed(state, added.status());
                return;
            }
        }
        for (TemplateProbe &probe : template_probes) {
            mirador::Result<std::uint64_t> added =
                evidence.add_template(probe.entry_id, probe.bounds, probe.similarity,
                                      mirador::CoordinateSpaceId::kOriented);
            if (!added.ok()) {
                settle_failed(state, added.status());
                return;
            }
            probe.evidence_id = added.value();
        }
        if (request.geometry.run && request.geometry.fuse_proposals) {
            for (const mirador::GeometricRegionProposal &proposal : proposals) {
                const auto area =
                    static_cast<double>(proposal.tight_bounds.width) * proposal.tight_bounds.height;
                const auto frame_area = static_cast<double>(extent.width) * extent.height;
                if (area > request.geometry.max_fused_area_fraction * frame_area)
                    continue; // Explicit candidate policy; raw proposals remain observable.
                mirador::ExternalRegion region;
                region.bounds = to_rectf(proposal.tight_bounds);
                region.confidence = std::clamp(proposal.closure_score, 0.0F, 1.0F);
                // Role, text and interactivity stay empty/false: a proposal
                // is a geometric fact and never carries platform semantics
                // (upstream RULE-11).
                if (mirador::Result<std::uint64_t> added =
                        evidence.add_external(region, mirador::CoordinateSpaceId::kOriented);
                    !added.ok()) {
                    settle_failed(state, added.status());
                    return;
                }
            }
        }
        mirador::FusionOptions options;
        // Legacy display facts and opt-in image-space background observations
        // are separate host configurations. No guessed display transform.
        options.target_space = core.coordinator ? mirador::CoordinateSpaceId::kOriented
                                                : mirador::CoordinateSpaceId::kDisplay;
        if (core.config.continuous)
            options.max_regions = core.config.continuous->max_entities;
        options.display_transform = request.display_transform;
        mirador::Result<mirador::SemanticSnapshot> fused =
            core.session.fuse(request.frame, evidence, options, context);
        if (!fused.ok()) {
            result.outcome = map_status(fused.status());
            settle(state, std::move(result));
            return;
        }
        mirador::SemanticSnapshot enriched = fused.take_value();
        for (mirador::VisualRegion &region : enriched.regions) {
            if (!mirador::has_source(region.source_mask, mirador::RegionSource::kTemplate)) {
                continue;
            }
            bool identity_stamped = false;
            bool conflicting = false;
            std::string matched_identity;
            std::optional<mirador::RectF> matched_bounds;
            const auto same_bounds = [](const auto &a, const auto &b) {
                return std::abs(a.x - b.x) < 0.01f && std::abs(a.y - b.y) < 0.01f &&
                       std::abs(a.width - b.width) < 0.01f && std::abs(a.height - b.height) < 0.01f;
            };
            for (const std::uint64_t evidence_id : region.evidence_ids) {
                const auto probe = std::find_if(template_probes.begin(), template_probes.end(),
                                                [evidence_id](const TemplateProbe &candidate) {
                                                    return candidate.evidence_id == evidence_id;
                                                });
                if (probe == template_probes.end()) {
                    continue;
                }
                if (const std::optional<std::string> identity =
                        core.templates.identity_for(probe->entry_id)) {
                    const auto bounds =
                        core.coordinator
                            ? probe->bounds
                            : mirador::transform_rect(*request.display_transform, probe->bounds);
                    if (identity_stamped &&
                        (matched_identity != *identity ||
                         (matched_bounds && !same_bounds(*matched_bounds, bounds))))
                        conflicting = true;
                    matched_bounds = bounds;
                    matched_identity = *identity;
                    region.label = *identity;
                    identity_stamped = true;
                }
                if (!request.probe_geometry_templates)
                    break; // Existing detector path: lowest evidence id decides.
            }
            if (request.probe_geometry_templates && matched_bounds &&
                !same_bounds(region.bounds, *matched_bounds))
                conflicting = true; // A containing OCR/panel must not move the template anchor.
            if (!identity_stamped || conflicting) {
                // No enrolled identity behind the template bit: degrade to
                // the geometry form instead of carrying a foreign label.
                region.label.clear();
            }
        }
        result.snapshot = std::make_shared<const mirador::SemanticSnapshot>(std::move(enriched));
    }

    if (core.coordinator) {
        const auto &limits = *core.config.continuous;
        mirador::RelationViewOptions relation_options;
        relation_options.expected_generation = result.snapshot->generation;
        relation_options.max_entities = limits.max_entities;
        relation_options.max_relations = limits.max_relations;
        relation_options.max_view_bytes = limits.max_relation_bytes;
        auto relations = mirador::build_relation_view(*result.snapshot, relation_options, context);
        if (!relations) {
            result.snapshot.reset();
            fail_result(result, relations.status());
            settle(state, std::move(result));
            return;
        }
        if (interrupted())
            return;
        auto plan = core.coordinator->plan_cycle(*result.snapshot, request.frame,
                                                 request.continuous->importance,
                                                 evidence_digest(*request.continuous), context);
        if (!plan) {
            result.snapshot.reset();
            fail_result(result, plan.status());
            settle(state, std::move(result));
            return;
        }
        const auto preliminary_bytes = result.spatial ? result.spatial->plan_owned_bytes : 0;
        const auto plan_bytes =
            plan_owned_bytes(plan.value(), limits.max_plan_bytes - preliminary_bytes);
        if (!plan_bytes) {
            result.snapshot.reset();
            fail_result(result, mirador::ErrorCode::kBudgetExceeded,
                        "continuous cycle plan byte budget exceeded");
            settle(state, std::move(result));
            return;
        }
        VisualContinuousResult output;
        if (interrupted())
            return;
        if (evidence_digest(*request.continuous) != 0) {
            std::vector<mirador::WorkToken> tokens;
            tokens.reserve(plan.value().work.size());
            for (const auto &binding : plan.value().work) {
                if (result.spatial &&
                    std::none_of(result.spatial->plan.work.begin(), result.spatial->plan.work.end(),
                                 [&](const auto &executed) {
                                     return executed.roi == binding.token.roi &&
                                            executed.space == binding.token.roi_space;
                                 })) {
                    result.snapshot.reset();
                    fail_result(result, mirador::ErrorCode::kInvalidArgument,
                                "continuous token has no completed spatial work");
                    settle(state, std::move(result));
                    return;
                }
                tokens.push_back(binding.token);
            }
            auto accepted = core.coordinator->accept_work(tokens, request.frame, context);
            if (!accepted || !accepted.value().committed) {
                result.snapshot.reset();
                fail_result(result, accepted
                                        ? mirador::Status{mirador::ErrorCode::kInvalidArgument,
                                                          "continuous evidence adoption rejected"}
                                        : accepted.status());
                settle(state, std::move(result));
                return;
            }
            output.accepted = accepted.take_value();
        }
        // Commit point: completed evidence has been adopted (or the advisory
        // cycle completed). Do not discard drained events for a later cancel.
        // These bounded synchronous queries create no new asynchronous work.
        output.temporal = core.coordinator->query_temporal_state();
        output.focus = core.coordinator->query_focus_states();
        output.events = core.coordinator->drain_events().take_value();
        output.source_revision = core.source_revision;
        output.source_epoch = core.coordinator->source_epoch();
        output.relation_owned_bytes = mirador::relation_view_owned_bytes(relations.value());
        output.coordination_owned_bytes = core.coordinator->coordination_owned_bytes();
        output.plan_owned_bytes = *plan_bytes + preliminary_bytes;
        output.relations = relations.take_value();
        output.cycle = plan.take_value();
        output.state_owned_bytes = state_owned_bytes(output.temporal, output.focus, &output.events,
                                                     output.accepted ? &*output.accepted : nullptr);
        continuous_result = std::move(output);
    }

    if (request.render_marks) {
        const auto rendered = detail::render_marks(request, result, context);
        if (!rendered) {
            result.outcome = map_status(rendered.status());
            settle(state, std::move(result));
            return;
        }
    }

    // ---- budget occupancy, read on the session's own scheduling context
    result.cache_stats.frame_cache_bytes = core.session.frame_cache().byte_size();
    result.cache_stats.frame_cache_entries = core.session.frame_cache().entry_count();
    result.cache_stats.result_cache_bytes = core.session.result_cache().byte_size();
    result.cache_stats.result_cache_entries = core.session.result_cache().entry_count();
    result.cache_stats.template_index_bytes = core.templates.byte_size();
    result.cache_stats.template_index_entries = core.templates.entry_count();
    if (core.coordinator)
        result.cache_stats.continuous_coordination_bytes =
            core.coordinator->coordination_owned_bytes();

    if (!continuous_result && interrupted())
        return;
    if (current_signature)
        core.background_baseline = detail::VisualSessionCore::BackgroundBaseline{
            std::move(*current_signature), request.frame.sequence,
            request.analysis_policy_revision};
    if (core.background_baseline)
        result.cache_stats.background_signature_bytes =
            core.background_baseline->signature.thumbnail.byte_size();
    result.outcome.kind = VisualAnalysisOutcome::Kind::kCompleted;
    if (core.coordinator) {
        core.last_analyzed_frame = request.frame.sequence;
        core.last_analyzed_at = std::chrono::steady_clock::now();
    }
    result.continuous = std::move(continuous_result);
    settle(state, std::move(result));
}

/// The per-source blocking worker: sleeps on the shared condition variable,
/// picks up the newest request, executes it serially on the session, and
/// settles every request exactly once. run() touches nothing after it
/// returns; the executor joins it through the worker handle.
class VisualSessionWorker final : public kairo::IBlockingIoWorker {
  public:
    explicit VisualSessionWorker(std::shared_ptr<detail::VisualSessionCore> core)
        : core_(std::move(core)) {}

    void run(kairo::StopToken stop_token) override {
        std::uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock lock(core_->mutex);
                core_->cv.wait_for(lock, core_->config.idle_wait, [&] {
                    return stop_token.stop_requested() || core_->mailbox.sequence() > seen;
                });
                if (stop_token.stop_requested()) {
                    if (core_->queued) {
                        settle_cancelled(*core_->queued, "visual session worker stopped");
                    }
                    core_->queued = nullptr;
                    return;
                }
            }
            // One bounded latest read per iteration. A concurrent newer publish
            // is picked up by the next predicate check; never spin-drain while
            // producers keep publishing (stop must remain observable).
            std::shared_ptr<RequestState> request;
            if (!core_->mailbox.try_load_newer_than(seen, request, seen) || !request) {
                continue;
            }
            {
                std::lock_guard lock(core_->mutex);
                if (core_->queued.get() != request.get()) {
                    // Superseded between load and pickup; the newer
                    // submission already settled this one.
                    continue;
                }
                core_->queued = nullptr;
                core_->in_flight = request;
            }
            try {
                const bool image =
                    request->query ? request->query->image_session : request->request.image_session;
                execute(image ? *core_->image_core : *core_, *request);
            } catch (...) {
                // Session calls are no-throw by contract; this guards the
                // settlement plumbing instead, so the request still settles.
                settle_failed(*request,
                              mirador::Status{mirador::ErrorCode::kBackendFailure,
                                              "visual analysis raised an unexpected exception"});
            }
            std::lock_guard lock(core_->mutex);
            core_->in_flight = nullptr;
        }
    }

    void wakeup() noexcept override {
        std::lock_guard lock(core_->mutex);
        core_->accepting = false;
        // Executor shutdown can precede the host's supported stop order.
        // Wake must also interrupt the currently executing capability, whose
        // context otherwise only sees host/external cancellation.
        if (core_->in_flight)
            core_->in_flight->cancel.request_cancel();
        // A worker can be between its stop check and queued-command pickup.
        // Cover that window even when there is no in-flight pointer yet.
        if (core_->queued)
            core_->queued->cancel.request_cancel();
        core_->cv.notify_all();
    }

  private:
    std::shared_ptr<detail::VisualSessionCore> core_;
};

} // namespace

VisualSessionHost::VisualSessionHost(kairo::Executor &executor, std::string source_id,
                                     VisualSessionConfig config)
    : executor_(executor), source_id_(std::move(source_id)), config_(std::move(config)) {}

VisualSessionHost::~VisualSessionHost() { stop(); }

bool VisualSessionHost::start(std::string &error) {
    Lifecycle expected = Lifecycle::New;
    if (!lifecycle_.compare_exchange_strong(expected, Lifecycle::Running,
                                            std::memory_order_acq_rel)) {
        error = "visual session host already started or stopped";
        return false;
    }
    if (source_id_.empty()) {
        lifecycle_.store(Lifecycle::New, std::memory_order_release);
        error = "visual session source id must not be empty";
        return false;
    }
    std::optional<mirador::ContinuousPerception> coordinator;
    if (config_.continuous) {
        const auto &limits = *config_.continuous;
        if (!config_.template_assets.empty() || limits.max_coordination_bytes <= 0 ||
            limits.max_coordination_bytes > 512 * 1024 || limits.max_relation_bytes <= 0 ||
            limits.max_relation_bytes > 512 * 1024 || limits.max_plan_bytes <= 0 ||
            limits.max_plan_bytes > 64 * 1024 || limits.max_entities <= 0 ||
            limits.max_entities > 256 || limits.max_relations <= 0 || limits.max_relations > 1024 ||
            limits.max_focus_conditions <= 0 || limits.max_focus_conditions > 16 ||
            limits.max_events <= 0 || limits.max_events > 256 ||
            limits.max_spatial_work_items <= 0 || limits.max_spatial_work_items > 4 ||
            limits.max_spatial_pixels <= 0 || limits.max_spatial_pixels > 4 * 1024 * 1024) {
            lifecycle_.store(Lifecycle::New, std::memory_order_release);
            error = "continuous observation budgets invalid or template assets configured";
            return false;
        }
        mirador::ContinuousPerceptionOptions options;
        options.source_id = source_id_;
        options.max_coordination_bytes = limits.max_coordination_bytes;
        options.max_entities = limits.max_entities;
        options.max_relations = limits.max_relations;
        options.max_focus_conditions = limits.max_focus_conditions;
        options.max_events = limits.max_events;
        auto created = mirador::ContinuousPerception::create(std::move(options));
        if (!created) {
            lifecycle_.store(Lifecycle::New, std::memory_order_release);
            error = "continuous coordinator creation failed: " + created.status().message();
            return false;
        }
        coordinator = created.take_value();
    }
    std::set<std::string> asset_ids;
    std::size_t asset_bytes = 0;
    for (const auto &asset : config_.template_assets) {
        const auto &p = asset.pixels;
        const bool valid_id =
            !asset.template_id.empty() && asset.template_id.size() <= 64 &&
            std::all_of(asset.template_id.begin(), asset.template_id.end(), [](unsigned char c) {
                return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                       c == '.' || c == '_' || c == '-';
            });
        if (!valid_id || !asset_ids.insert(asset.template_id).second ||
            config_.template_assets.size() > 16 || p.format != desktop::ImageFormat::Bgra8 ||
            p.width < 8 || p.width > 256 || p.height < 8 || p.height > 256 ||
            p.stride != static_cast<std::size_t>(p.width) * 4 ||
            p.pixels.size() != p.stride * static_cast<std::size_t>(p.height)) {
            lifecycle_.store(Lifecycle::New, std::memory_order_release);
            error = "template asset identity/pixels/capacity invalid";
            return false;
        }
        asset_bytes += p.pixels.size();
    }
    if (config_.templates.thumb_side < 8 || config_.templates.thumb_side > 64 ||
        asset_bytes > (4u << 20) ||
        config_.template_assets.size() > config_.templates.max_templates ||
        static_cast<std::int64_t>(config_.template_assets.size()) *
                (static_cast<std::int64_t>(config_.templates.thumb_side) *
                     config_.templates.thumb_side +
                 mirador::VisualIndex::kEntryOverheadBytes) >
            config_.templates.max_bytes) {
        lifecycle_.store(Lifecycle::New, std::memory_order_release);
        error = "template asset index budget exceeded";
        return false;
    }
    mirador::Result<VisualTemplateIndex> templates = VisualTemplateIndex::create(config_.templates);
    if (!templates.ok()) {
        lifecycle_.store(Lifecycle::New, std::memory_order_release);
        error = "template index creation failed: " + templates.status().message();
        return false;
    }
    mirador::PerceptionSessionOptions options;
    options.source_id = source_id_;
    options.frame_cache_bytes = config_.frame_cache_bytes;
    options.result_cache_bytes = config_.result_cache_bytes;
    mirador::Result<mirador::PerceptionSession> session =
        mirador::PerceptionSession::create(std::move(options));
    if (!session.ok()) {
        lifecycle_.store(Lifecycle::New, std::memory_order_release);
        error = "perception session creation failed: " + session.status().message();
        return false;
    }
    auto core = std::make_shared<detail::VisualSessionCore>(
        source_id_, config_, session.take_value(), templates.take_value(), std::move(coordinator));
    if (config_.enable_image_session) {
        if (config_.continuous) {
            lifecycle_.store(Lifecycle::New, std::memory_order_release);
            error = "image companion requires a legacy display host";
            return false;
        }
        auto image_config = config_;
        image_config.enable_image_session = false;
        image_config.continuous = VisualContinuousConfig{};
        image_config.template_assets.clear();
        image_config.frame_cache_bytes = 1 << 20;
        image_config.result_cache_bytes = 2 << 20;
        auto image_session = mirador::PerceptionSession::create(
            {source_id_, image_config.frame_cache_bytes, image_config.result_cache_bytes});
        auto image_templates = VisualTemplateIndex::create(image_config.templates);
        mirador::ContinuousPerceptionOptions image_options;
        image_options.source_id = source_id_;
        image_options.max_coordination_bytes = 512 * 1024;
        image_options.max_entities = 256;
        image_options.max_relations = 1024;
        image_options.max_focus_conditions = 16;
        image_options.max_events = 256;
        auto image_coordinator = mirador::ContinuousPerception::create(image_options);
        if (!image_session || !image_templates || !image_coordinator) {
            lifecycle_.store(Lifecycle::New, std::memory_order_release);
            error = "image companion state creation failed";
            return false;
        }
        core->image_core = std::make_unique<detail::VisualSessionCore>(
            source_id_, std::move(image_config), image_session.take_value(),
            image_templates.take_value(), image_coordinator.take_value());
    }
    kairo::BlockingWorkerSpec spec;
    spec.name = "mirage-visual-" + source_id_;
    spec.config.thread_name = spec.name;
    spec.worker = std::make_unique<VisualSessionWorker>(core);
    worker_ = executor_.start_worker(std::move(spec));
    if (!worker_.started()) {
        lifecycle_.store(Lifecycle::New, std::memory_order_release);
        error = "visual session worker start failed: " + worker_.start_result().message;
        return false;
    }
    core_ = std::move(core);
    return true;
}

std::future<VisualAnalysisResult> VisualSessionHost::submit(VisualAnalysisRequest request) {
    auto state = std::make_shared<RequestState>(std::move(request));
    std::future<VisualAnalysisResult> future = state->done.get_future();
    if (lifecycle_.load(std::memory_order_acquire) != Lifecycle::Running) {
        settle_rejected(*state, "visual session host is not running");
        return future;
    }
    if (state->request.frame.source_id != source_id_) {
        settle_rejected(*state, "frame source id does not match the session source");
        return future;
    }
    // Admission-time validation of the pixel-space stages: they crop the
    // frame in oriented coordinates, which is only the view's own pixel
    // space for the adapter's rotation-k0 capture form.
    const VisualAnalysisRequest &analysis = state->request;
    if (analysis.image_session && !config_.enable_image_session) {
        settle_rejected(*state, "image companion is unavailable");
        return future;
    }
    if (analysis.continuous.has_value() !=
        (analysis.image_session || config_.continuous.has_value())) {
        settle_rejected(*state, "continuous requests require a dedicated continuous host");
        return future;
    }
    if (analysis.continuous &&
        (!analysis.fuse || analysis.continuous->source_revision == 0 ||
         analysis.frame.sequence == 0 || !std::isfinite(analysis.continuous->importance.priority) ||
         analysis.render_marks || !analysis.enrollments.empty() || analysis.probe_templates ||
         analysis.probe_geometry_templates)) {
        settle_rejected(*state, "continuous background request identity/stages invalid");
        return future;
    }
    if (analysis.continuous && evidence_digest(*analysis.continuous) != 0 &&
        (!(analysis.run_ocr || analysis.run_detector) || analysis.ocr_budget ||
         analysis.geometry.run ||
         (analysis.run_ocr &&
          (analysis.ocr.roi || analysis.ocr.cache_policy != mirador::CachePolicy::kRefresh)) ||
         (analysis.run_detector &&
          (analysis.detector.roi ||
           analysis.detector.cache_policy != mirador::CachePolicy::kRefresh)))) {
        settle_rejected(*state, "continuous adoption requires completed full-frame refresh stages");
        return future;
    }
    if (analysis.continuous && analysis.continuous->spatial_evidence_digest != 0) {
        const auto &input = *analysis.continuous;
        if (input.full_frame_evidence_digest != 0 || !analysis.analyze_change ||
            analysis.frame.image.rotation != mirador::Rotation::k0 ||
            input.attention.conditions.empty() ||
            (analysis.run_ocr &&
             analysis.ocr.output_space != mirador::CoordinateSpaceId::kOriented) ||
            (analysis.run_detector &&
             analysis.detector.output_space != mirador::CoordinateSpaceId::kOriented) ||
            std::any_of(input.attention.conditions.begin(), input.attention.conditions.end(),
                        [](const auto &condition) {
                            return condition.bind_stable_id || !condition.text_equals.empty() ||
                                   !condition.label_equals.empty() ||
                                   condition.roi_space != mirador::CoordinateSpaceId::kOriented;
                        })) {
            settle_rejected(*state,
                            "spatial scheduling requires explicit oriented ROI-only conditions");
            return future;
        }
    }
    if (analysis.admission != VisualAnalysisAdmission::always &&
        analysis.admission != VisualAnalysisAdmission::when_changed) {
        settle_rejected(*state, "unknown visual analysis admission policy");
        return future;
    }
    if (analysis.admission == VisualAnalysisAdmission::when_changed && !analysis.analyze_change) {
        settle_rejected(*state, "background change admission requires change analysis");
        return future;
    }
    if (analysis.admission == VisualAnalysisAdmission::when_changed &&
        analysis.analysis_policy_revision == 0) {
        settle_rejected(*state, "background admission requires an explicit policy revision");
        return future;
    }
    const bool pixel_space_stages = !analysis.enrollments.empty() || analysis.probe_templates ||
                                    analysis.probe_geometry_templates || analysis.geometry.run ||
                                    analysis.render_marks;
    if (pixel_space_stages && analysis.frame.image.rotation != mirador::Rotation::k0) {
        settle_rejected(*state,
                        "pixel-space stages require the adapter's rotation-k0 capture form");
        return future;
    }
    if (analysis.render_marks && !analysis.fuse) {
        settle_rejected(*state, "marks require fusion");
        return future;
    }
    for (const VisualTemplateEnrollment &enrollment : analysis.enrollments) {
        if (enrollment.template_id.empty()) {
            settle_rejected(*state, "enrollment identity must not be empty");
            return future;
        }
    }
    if (analysis.probe_geometry_templates &&
        (!analysis.geometry.run || config_.template_assets.empty() ||
         analysis.template_query.max_candidates <
             static_cast<std::int32_t>(config_.template_assets.size()) ||
         !(analysis.template_reuse_threshold >= 0.95) ||
         !(analysis.template_reuse_threshold <= 1.0) ||
         !(analysis.template_query.perceptual_similarity_threshold >= 0.95) ||
         !(analysis.template_query.perceptual_similarity_threshold <= 1.0) ||
         !(analysis.template_query.template_ncc_threshold >= 0.95) ||
         !(analysis.template_query.template_ncc_threshold <= 1.0))) {
        settle_rejected(*state, "geometry template policy/candidates invalid");
        return future;
    }
    if (!config_.template_assets.empty() && !analysis.enrollments.empty()) {
        settle_rejected(*state, "configured template library is immutable");
        return future;
    }
    if (analysis.probe_templates) {
        if (!analysis.run_detector) {
            settle_rejected(*state, "template probes require the detection capability");
            return future;
        }
        if (analysis.detector.output_space != mirador::CoordinateSpaceId::kOriented) {
            settle_rejected(*state, "template probes require detection output in kOriented space");
            return future;
        }
        if (!(analysis.template_reuse_threshold >= 0.0) ||
            !(analysis.template_reuse_threshold <= 1.0)) {
            settle_rejected(*state, "template reuse threshold must be in [0, 1]");
            return future;
        }
    }
    if (analysis.geometry.run && (!(analysis.geometry.max_fused_area_fraction > 0.0) ||
                                  !(analysis.geometry.max_fused_area_fraction <= 1.0))) {
        settle_rejected(*state, "geometry fused area fraction must be in (0, 1]");
        return future;
    }
    if (analysis.geometry.run && analysis.geometry.gray_convert_max_bytes <= 0) {
        settle_rejected(*state, "geometry stage requires a positive gray conversion budget");
        return future;
    }
    if (!worker_.status().is_running) {
        // Defense in depth for an executor stopped behind the host's back;
        // the supported discipline stops the host first (DEC-016 decision 4).
        settle_rejected(*state, "visual session worker is not running");
        return future;
    }

    publish_request(*core_, state);
    return future;
}

std::future<VisualAnalysisResult> VisualSessionHost::query_continuous(VisualContinuousQuery query) {
    auto state = std::make_shared<RequestState>(std::move(query));
    auto future = state->done.get_future();
    const bool image = state->query->image_session;
    const auto *limits = image && core_ && core_->image_core
                             ? &*core_->image_core->config.continuous
                             : (config_.continuous ? &*config_.continuous : nullptr);
    if (lifecycle_.load(std::memory_order_acquire) != Lifecycle::Running || !limits ||
        (image && !config_.enable_image_session) || state->query->expected_source_revision == 0) {
        settle_rejected(
            *state,
            "continuous state query requires a running continuous host and source revision");
        return future;
    }
    if (state->query->attention) {
        const auto &policy = *state->query->attention;
        if (policy.conditions.size() > static_cast<std::size_t>(limits->max_focus_conditions) ||
            !query_policy_fits(policy, *limits)) {
            settle_failed(*state, {mirador::ErrorCode::kBudgetExceeded,
                                   "query policy payload budget exceeded"});
            return future;
        }
        if (auto valid = mirador::validate_attention_policy(policy); !valid) {
            settle_failed(*state, valid.status());
            return future;
        }
    }
    if (!worker_.status().is_running) {
        settle_rejected(*state, "visual session worker is not running");
        return future;
    }
    publish_request(*core_, state);
    return future;
}

void VisualSessionHost::stop() {
    Lifecycle expected = Lifecycle::Running;
    if (!lifecycle_.compare_exchange_strong(expected, Lifecycle::Stopped,
                                            std::memory_order_acq_rel)) {
        lifecycle_.store(Lifecycle::Stopped, std::memory_order_release);
        return;
    }
    std::vector<std::shared_ptr<RequestState>> to_settle;
    if (core_) {
        std::lock_guard lock(core_->mutex);
        core_->accepting = false;
        if (core_->queued) {
            to_settle.push_back(std::move(core_->queued));
            core_->queued = nullptr;
        }
        if (core_->in_flight) {
            core_->in_flight->cancel.request_cancel();
        }
    }
    if (worker_.started()) {
        // Requests stop, wakes the worker and joins run(); nothing of the
        // host is touched by the worker after this returns.
        worker_.stop();
    }
    for (std::shared_ptr<RequestState> &state : to_settle) {
        settle_cancelled(*state, "visual session host stopped");
    }
}

bool VisualSessionHost::running() const noexcept {
    return lifecycle_.load(std::memory_order_acquire) == Lifecycle::Running;
}

} // namespace mirage::integration
