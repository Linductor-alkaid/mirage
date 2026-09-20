#include <mirage/integration/visual_session_host.hpp>

#include <executor/comm/mailbox.hpp>
#include <executor/stop_token.hpp>

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
#include <utility>
#include <vector>

namespace mirage::integration {
namespace {

/// One admitted analysis request: the immutable payload, the cancellation
/// channel the mirador pipeline polls, and the one-shot settlement promise.
struct RequestState {
    explicit RequestState(VisualAnalysisRequest analysis_request)
        : request(std::move(analysis_request)) {}

    VisualAnalysisRequest request;
    desktop::CancelToken cancel; ///< latest-wins / stop cancellation channel
    std::promise<VisualAnalysisResult> done;
};

/// Settlement must never throw, even for an abandoned future.
void settle(RequestState &state, VisualAnalysisResult result) {
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
/// payload itself travels through the executor::comm LatestMailbox, never
/// through the lock (AGENTS.md rule 4).
struct detail::VisualSessionCore {
    VisualSessionCore(std::string host_source_id, VisualSessionConfig host_config,
                      mirador::PerceptionSession perception_session,
                      VisualTemplateIndex template_index)
        : source_id(std::move(host_source_id)), config(std::move(host_config)),
          session(std::move(perception_session)), templates(std::move(template_index)) {}

    const std::string source_id;
    const VisualSessionConfig config;
    /// Only ever touched on the worker thread: one session, one scheduling
    /// context (DEC-016 decision 4).
    mirador::PerceptionSession session;
    /// The template index shares the session's serial context (plan item
    /// `M3-04`); it dies with the core when the host stops.
    VisualTemplateIndex templates;

    std::mutex mutex;
    /// Wakes the worker for a published request, a stop, or the bounded
    /// idle slice; the executor's stop path calls wakeup() through the
    /// worker. The worker never holds this mutex while a request runs.
    std::condition_variable cv;
    /// Latest-wins transport of requests; overwritten slots are observable
    /// in the mailbox statistics (overwritten-request count).
    executor::comm::LatestMailbox<std::shared_ptr<RequestState>> mailbox{"mirage-visual-request"};
    /// Queued (published, not yet picked up) and executing requests, kept
    /// for explicit supersede and shutdown settlement (RULE-10).
    std::shared_ptr<RequestState> queued;
    std::shared_ptr<RequestState> in_flight;
};

namespace {
/// Runs the requested capabilities serially on the session: template
/// enrollment, change gating, geometry proposals, OCR, detection (optionally
/// ROI-split per geometry proposal), template probes at the detection
/// regions, then fusion of the produced evidence only (DEC-016 decision 3:
/// accessibility regions are never fusion input) with the enrolled template
/// identities stamped onto the fused template regions. Every mirador call
/// polls the request's execution context, so a cancellation or deadline
/// lands at the next stage boundary as an explicit status.
void execute(detail::VisualSessionCore &core, RequestState &state) {
    const VisualAnalysisRequest &request = state.request;
    // The DEC-016 mapping: internal latest-wins token, the caller's external
    // token (a default token never cancels), and the steady-clock deadline.
    mirador::ExecutionContext context;
    context.deadline = request.deadline;
    context.is_cancelled = [internal = state.cancel, external = request.external_cancel] {
        return internal.cancelled() || external.cancelled();
    };

    VisualAnalysisResult result;
    // Cancellation lands before the first side effect (enrollments mutate
    // session state), never silently swallowed.
    if (context.is_cancelled && context.is_cancelled()) {
        result.outcome.kind = VisualAnalysisOutcome::Kind::kCancelled;
        result.outcome.message = "cancelled before the first stage";
        settle(state, std::move(result));
        return;
    }

    // The adapter's rotation-k0 capture form is the only shape the
    // pixel-space stages accept (submit() rejects the rest), so oriented
    // coordinates are the view's own pixel coordinates here.
    const mirador::RectI extent{0, 0, request.frame.image.width, request.frame.image.height};

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
    if (request.run_ocr) {
        mirador::Result<std::vector<mirador::TextRegion>> regions =
            core.session.run_ocr(request.frame, core.config.ocr_backend, request.ocr, context);
        if (!regions.ok()) {
            result.outcome = map_status(regions.status());
            settle(state, std::move(result));
            return;
        }
        result.text = regions.take_value();
    }

    // ---- detection: the caller's single request, or one request per
    // geometry proposal context ROI (the closed regions supply the
    // detection requests; each ROI keeps its own capability-cache key).
    if (request.run_detector) {
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
    if (request.probe_templates && request.run_detector) {
        for (const mirador::DetectionRegion &region : result.detections) {
            const mirador::RectI roi = clamp_region(region.bounds, extent);
            if (roi.width <= 0 || roi.height <= 0) {
                continue; // degenerate crop: no pixels to fingerprint
            }
            const mirador::Result<mirador::ImageBuffer> cropped =
                crop_patch(request.frame, roi, core.config.templates);
            if (!cropped.ok()) {
                fail_result(result, cropped.status());
                settle(state, std::move(result));
                return;
            }
            mirador::Result<std::optional<VisualTemplateHit>> hit = core.templates.probe(
                cropped.value().view(), request.template_query, request.template_reuse_threshold);
            if (!hit.ok()) {
                fail_result(result, hit.status());
                settle(state, std::move(result));
                return;
            }
            if (hit.value().has_value()) {
                result.template_hits.push_back(*hit.value());
                template_probes.push_back(TemplateProbe{hit.value()->entry_id, region.bounds,
                                                        hit.value()->similarity, 0});
            }
        }
    }

    // ---- fusion of the produced visual evidence, then the enrolled template
    // identities are stamped onto the fused template regions (the fusion
    // itself carries no template label source; the DEC-016 mapper reads the
    // identity from the label)
    if (request.fuse) {
        if (!request.display_transform) {
            fail_result(result, mirador::ErrorCode::kInvalidArgument,
                        "fusion requires the kOriented -> kDisplay display transform (DEC-016)");
            settle(state, std::move(result));
            return;
        }
        mirador::EvidenceSet evidence;
        for (const mirador::TextRegion &region : result.text) {
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
        // DEC-016 decision 3: the fusion target space is fixed to kDisplay,
        // with the caller-supplied window placement as the transform.
        options.target_space = mirador::CoordinateSpaceId::kDisplay;
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
                    region.label = *identity;
                    identity_stamped = true;
                }
                break; // lowest evidence id decides, deterministically
            }
            if (!identity_stamped) {
                // No enrolled identity behind the template bit: degrade to
                // the geometry form instead of carrying a foreign label.
                region.label.clear();
            }
        }
        result.snapshot = std::make_shared<const mirador::SemanticSnapshot>(std::move(enriched));
    }

    // ---- budget occupancy, read on the session's own scheduling context
    result.cache_stats.frame_cache_bytes = core.session.frame_cache().byte_size();
    result.cache_stats.frame_cache_entries = core.session.frame_cache().entry_count();
    result.cache_stats.result_cache_bytes = core.session.result_cache().byte_size();
    result.cache_stats.result_cache_entries = core.session.result_cache().entry_count();
    result.cache_stats.template_index_bytes = core.templates.byte_size();
    result.cache_stats.template_index_entries = core.templates.entry_count();

    result.outcome.kind = VisualAnalysisOutcome::Kind::kCompleted;
    settle(state, std::move(result));
}

/// The per-source blocking worker: sleeps on the shared condition variable,
/// picks up the newest request, executes it serially on the session, and
/// settles every request exactly once. run() touches nothing after it
/// returns; the executor joins it through the worker handle.
class VisualSessionWorker final : public executor::IBlockingIoWorker {
  public:
    explicit VisualSessionWorker(std::shared_ptr<detail::VisualSessionCore> core)
        : core_(std::move(core)) {}

    void run(executor::StopToken stop_token) override {
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
            // Drain to the newest published request; intermediate slots were
            // settled by the submissions that overwrote them.
            std::shared_ptr<RequestState> request;
            while (core_->mailbox.try_load_newer_than(seen, request, seen)) {
            }
            if (!request) {
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
                execute(*core_, *request);
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
        core_->cv.notify_all();
    }

  private:
    std::shared_ptr<detail::VisualSessionCore> core_;
};

} // namespace

VisualSessionHost::VisualSessionHost(executor::Executor &executor, std::string source_id,
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
        source_id_, config_, session.take_value(), templates.take_value());
    executor::BlockingWorkerSpec spec;
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
    const bool pixel_space_stages =
        !analysis.enrollments.empty() || analysis.probe_templates || analysis.geometry.run;
    if (pixel_space_stages && analysis.frame.image.rotation != mirador::Rotation::k0) {
        settle_rejected(*state,
                        "pixel-space stages require the adapter's rotation-k0 capture form");
        return future;
    }
    for (const VisualTemplateEnrollment &enrollment : analysis.enrollments) {
        if (enrollment.template_id.empty()) {
            settle_rejected(*state, "enrollment identity must not be empty");
            return future;
        }
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

    std::shared_ptr<RequestState> superseded;
    {
        std::lock_guard lock(core_->mutex);
        superseded = std::move(core_->queued);
        if (core_->in_flight) {
            // Latest-wins: converge the in-flight predecessor through its
            // cancellation channel; it settles explicitly as kCancelled.
            core_->in_flight->cancel.request_cancel();
        }
        core_->queued = state;
    }
    core_->mailbox.publish(state);
    core_->cv.notify_all();
    if (superseded) {
        settle_cancelled(*superseded, "superseded by a newer analysis request");
    }
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
