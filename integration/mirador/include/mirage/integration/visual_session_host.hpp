#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <kairo/blocking_io.hpp>
#include <kairo/executor.hpp>

#include <mirador/continuous_perception.hpp>
#include <mirador/detector_backend.hpp>
#include <mirador/frame.hpp>
#include <mirador/fusion.hpp>
#include <mirador/geometric_proposal.hpp>
#include <mirador/line_detector.hpp>
#include <mirador/ocr_backend.hpp>
#include <mirador/ocr_batch.hpp>
#include <mirador/segment_growing_line_detector.hpp>
#include <mirador/status.hpp>
#include <mirador/visual_index.hpp>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/integration/environment_visual_pipeline.hpp>
#include <mirage/integration/visual_template_index.hpp>

namespace mirage::integration {

namespace detail {
struct VisualSessionCore;
}

class VisualSessionHost;

/// DEC-061: verified external pixels owned by the session configuration.
struct VisualTemplateAsset {
    std::string template_id;
    desktop::ImageFrame pixels;
};

/// A captured-frame enrollment (M3-04), cropped from the request's rotation-k0 frame.
struct VisualTemplateEnrollment {
    /// Enrolled identity; non-empty. Re-enrolling an existing identity
    /// replaces its fingerprint.
    std::string template_id;
    /// Patch location in the request frame's oriented pixel space.
    mirador::RectI patch;
};

/// Geometric closed-region proposal stage (plan item `M3-04`, upstream
/// DEC-018 stage 1 contract): gray conversion -> line detection -> filtering
/// -> collinear merge -> closed-shape proposals. Proposals are geometric
/// facts only; their tight/context bounds supply detection ROIs and capture
/// cropping, and (opt-in) they enter the fusion as semantics-free evidence
/// that maps onto the geometry-form observation entries.
struct VisualGeometryStage {
    bool run = false;
    /// First-party detector tuning (mirador defaults when untouched).
    mirador::SegmentGrowingParams segment_detector;
    mirador::LineDetectRequest line_detect;
    mirador::LineFilterParams line_filter;
    mirador::CollinearMergeParams merge;
    /// Proposal budgets (`max_segments` / `max_proposals`): exceeding one
    /// fails the stage with kBudgetExceeded, never truncation.
    mirador::GeometricProposalParams proposal;
    /// Run the detector once per proposal `context_bounds` (clamped to the
    /// frame) instead of the caller's single request: the closed regions
    /// *supply* the detection requests. With no proposals the caller's
    /// request runs unchanged.
    bool detector_roi_from_geometry = false;
    /// Add each proposal's tight bounds to the fusion as an ExternalRegion
    /// with no platform semantics (empty role, non-interactive, confidence =
    /// closure_score) — the geometry-form observation entries.
    bool fuse_proposals = false;
    /// Product candidate size policy, not a proposal budget: only fuse shapes
    /// whose tight area is at most this fraction of the captured frame.
    /// Raw proposals remain in the result; (0, 1], default preserves all.
    double max_fused_area_fraction = 1.0;
    /// Budget of the frame-to-gray conversion (mirador `convert_color`);
    /// non-positive values reject the request at admission.
    std::int64_t gray_convert_max_bytes = std::int64_t{4} * 1024 * 1024;
};

/// One visual analysis request against the host's per-source perception
/// session (DEC-016 decision 4). The frame must carry the host's source id.
/// The host always executes at most one request at a time on the session's
/// blocking worker; a newer submission supersedes: the queued predecessor is
/// settled `kRejected`-cancelled and an in-flight predecessor is cancelled
/// through its execution context (latest-wins, explicit settlement).
///
/// Stage order (each stage optional): template enrollment -> change gating ->
/// geometry proposals -> OCR -> detection (optionally ROI-split per geometry
/// proposal) -> template probes at the detection regions -> fusion (OCR +
/// detection + template hits + geometry evidence, then the enrolled template
/// identities are stamped onto the fused template regions). The pixel-space
/// stages (enrollment, probes, geometry) require the adapter's rotation-k0
/// capture form; violations reject the request at admission.
enum class VisualAnalysisAdmission { always, when_changed };

/// M9-05/07/18: Experimental Mirador consumption, one background-only host.
/// Budgets may only be tightened. Does not enable production consumers.
struct VisualContinuousConfig {
    std::int64_t max_coordination_bytes = 512 * 1024;
    std::int64_t max_relation_bytes = 512 * 1024;
    std::int64_t max_plan_bytes = 64 * 1024;
    std::int32_t max_entities = 256;
    std::int32_t max_relations = 1024;
    std::int32_t max_focus_conditions = 16;
    std::int32_t max_events = 256;
};

struct VisualContinuousRequest {
    /// Trusted source instance/geometry/authority revision, NOT a demand epoch.
    /// Positive and monotonic; advance before a source or sampling geometry reset.
    std::uint64_t source_revision = 0;
    mirador::AttentionPolicy attention;
    mirador::ImportancePolicy importance;
    /// Zero: advisory cycle, no adoption. Nonzero: trusted capability/model/
    /// parameter identity for completed full-frame evidence. Requires at least
    /// one OCR/detector stage, all enabled stages kRefresh without ROI, and no
    /// budgeted OCR or geometry. Advance identity when that stack changes.
    std::uint64_t full_frame_evidence_digest = 0;
};

struct VisualContinuousResult {
    std::uint64_t source_revision = 0, source_epoch = 0;
    mirador::RelationView relations;
    /// Derived AFTER current fusion. Coverage describes snapshot regions only;
    /// it does not establish full-screen coverage or authorize input actions.
    mirador::WorkPlan cycle;
    std::optional<mirador::WorkAcceptReport> accepted;
    mirador::TemporalSemanticState temporal;
    std::vector<mirador::FocusStateResolution> focus;
    mirador::EventDrain events;
    /// plan bytes include token strings; state output is bounded by the same
    /// entity/focus/event caps and coordinator string budget, separately counted.
    std::int64_t coordination_owned_bytes = 0, relation_owned_bytes = 0, plan_owned_bytes = 0;
    std::int64_t state_owned_bytes = 0;
};

struct VisualAnalysisRequest {
    /// Required exactly when the host is configured for continuous background
    /// analysis. Fusion then produces kOriented, with no marks/action references.
    std::optional<VisualContinuousRequest> continuous;
    /// Capture to analyze (from `to_mirador_frame`); `source_id` must equal
    /// the host's.
    mirador::Frame frame;
    /// Refresh the session's change state first (stage-one change gating).
    bool analyze_change = true;
    /// Template enrollments applied before any analysis; the first failure
    /// (identity, format, budget) settles the request explicitly and nothing
    /// further runs.
    std::vector<VisualTemplateEnrollment> enrollments;
    bool run_ocr = false;
    mirador::OcrRequest ocr;
    bool run_detector = false;
    mirador::DetectionRequest detector;
    /// Probe the enrolled templates at every detection region of this
    /// request: each region's pixels are fingerprinted and matched through
    /// the three index tiers; a clear-winner hit becomes template evidence.
    /// Requires `run_detector` with kOriented output.
    bool probe_templates = false;
    /// DEC-061: match configured assets at eligible geometric proposals, no detector required.
    bool probe_geometry_templates = false;
    /// Probe thresholds and candidate cap (mirador defaults when untouched).
    mirador::VisualQueryParams template_query;
    /// Clear-winner reuse threshold in [0, 1] (mirador example discipline:
    /// only a strict winner counts as the enrolled icon).
    double template_reuse_threshold = 0.95;
    /// Geometric closed-region proposal stage (see `VisualGeometryStage`).
    VisualGeometryStage geometry;
    /// Fuse the produced OCR / detection / template / geometry evidence into
    /// a SemanticSnapshot published by the session. Fusion target space is
    /// kDisplay by default (DEC-016 decision 3); `display_transform`
    /// (kOriented -> kDisplay) is then mandatory, even when numerically
    /// identity. An opt-in continuous host uses kOriented. Evidence is declared in each capability
    /// request's `output_space`; mirador recovers it.
    bool fuse = false;
    std::optional<mirador::Transform2D> display_transform;
    /// Analysis deadline on the steady clock; nullopt disables it. Expiry
    /// surfaces as a kTimedOut outcome, never as a hang.
    std::optional<std::chrono::steady_clock::time_point> deadline;
    /// Extra caller-side cancellation: the analysis is abandoned when either
    /// this token or the host's latest-wins replacement requests it.
    desktop::CancelToken external_cancel;
    /// M7-07: render on this worker after fusion. ROI in oriented pixels.
    bool render_marks = false;
    std::optional<mirador::RectI> mark_roi;
    /// Opt-in approximate background screening only. A skipped request produces
    /// no current evidence or snapshot. Never use for action/verification reads.
    /// Refresh, enrollment and marks override screening. Requires analyze_change.
    VisualAnalysisAdmission admission = VisualAnalysisAdmission::always;
    /// Required nonzero for background admission. The trusted caller advances
    /// this when stage parameters, target/placement or authority change.
    std::uint64_t analysis_policy_revision = 0;
    /// M8-07 (upstream DEC-025): run the OCR stage through the budgeted
    /// entry (`run_ocr_budgeted` + the configured `budgeted_ocr_backend`)
    /// instead of the all-or-nothing `run_ocr` path. Recognized lines land
    /// in `result.text` as before; per-line completion states, stop reason
    /// and deferred geometry surface in `result.ocr_batch`. Budget fields
    /// mirror the mirador request; the frozen reference is K=64 with a
    /// 500 ms finalize reserve under the caller's hard deadline.
    struct OcrBudget {
        std::int32_t max_rec_lines = 64;
        mirador::OcrSelectionStrategy selection = mirador::OcrSelectionStrategy::kReadingOrder;
        std::optional<std::chrono::steady_clock::duration> soft_budget;
        std::optional<std::chrono::steady_clock::duration> finalize_reserve;
        std::vector<mirador::OcrPriorityEntry> priorities;
        mirador::CoordinateSpaceId priority_space = mirador::CoordinateSpaceId::kOriented;
    };
    std::optional<OcrBudget> ocr_budget;
};

/// Terminal state of one analysis request. Every admitted request settles
/// exactly once; superseded, cancelled and shutdown requests settle
/// explicitly instead of being dropped (RULE-10).
struct VisualAnalysisOutcome {
    enum class Kind {
        kCompleted,        ///< requested capabilities ran; results are meaningful
        kFailed,           ///< a mirador call failed; `code`/`message` say why
        kCancelled,        ///< superseded, externally cancelled or host stopped
        kTimedOut,         ///< the request deadline expired
        kRejected,         ///< admission refused; the session never saw the request
        kSkippedUnchanged, ///< background screening skipped all expensive stages; no evidence
    };
    Kind kind = Kind::kRejected;
    /// mirador error code for kFailed (diagnostic); kOk otherwise.
    mirador::ErrorCode code = mirador::ErrorCode::kOk;
    std::string message; ///< short human-readable reason when not completed
};

/// Result payload of one settled analysis request. Only the fields of the
/// requested capabilities are meaningful.
struct VisualAnalysisResult {
    /// Present only for completed opt-in continuous fusion; no payload on skip.
    std::optional<VisualContinuousResult> continuous;
    VisualAnalysisOutcome outcome;
    /// Change decision of this frame (when `analyze_change` ran); the first
    /// submission of a session reports kGlobal / kFirstFrame.
    mirador::ChangeReport change;
    /// Background admission compares against the last fully analyzed background
    /// frame, never a skipped frame. Approximate, not current action evidence.
    std::optional<mirador::ChangeReport> admission_change;
    std::uint64_t admission_baseline_sequence = 0;
    std::vector<mirador::TextRegion> text;
    /// M8-07 (upstream DEC-025): budgeted OCR report diagnostics, set when
    /// the OCR stage ran through `ocr_budget`. `text` then carries exactly
    /// the kRecognized lines (each with its text and rec confidence);
    /// `deferred_bounds` keeps every deferred line's det geometry queryable
    /// in `geometry_space`. A kPartial report is a caller-side fact, never
    /// an authorization to act on the screen.
    struct OcrBatchDiagnostics {
        mirador::OcrBatchCompletion completion = mirador::OcrBatchCompletion::kComplete;
        mirador::OcrBatchStopReason stop_reason = mirador::OcrBatchStopReason::kCompleted;
        std::int32_t detected = 0;
        std::int32_t recognized = 0;
        std::int32_t deferred = 0;
        std::int32_t det_limit = 0;
        bool det_limit_hit = false;
        mirador::CoordinateSpaceId geometry_space = mirador::CoordinateSpaceId::kOriented;
        std::vector<mirador::RectF> deferred_bounds;
    };
    std::optional<OcrBatchDiagnostics> ocr_batch;
    std::vector<mirador::DetectionRegion> detections;
    /// Accepted template probes in detection then geometry order; raw hits
    /// remain diagnostic when fusion conflicts suppress the published identity.
    std::vector<VisualTemplateHit> template_hits;
    /// Closed-region proposals of this frame, in mirador's deterministic
    /// output order, bounds in the frame's oriented pixel space (when the
    /// geometry stage ran).
    std::vector<mirador::GeometricRegionProposal> geometry_proposals;
    /// Fusion snapshot: kDisplay by default, kOriented on continuous hosts.
    /// The pointee is immutable; only completed outcomes are current evidence.
    std::shared_ptr<const mirador::SemanticSnapshot> snapshot;
    /// Budget occupancy read on the worker after execution (plan item
    /// `M3-04`): the session's caches and the template index are all inside
    /// their configured byte budgets; entries never exceed the capacities.
    struct CacheStats {
        std::int64_t frame_cache_bytes = 0;
        std::size_t frame_cache_entries = 0;
        std::int64_t result_cache_bytes = 0;
        std::size_t result_cache_entries = 0;
        std::int64_t template_index_bytes = 0;
        std::size_t template_index_entries = 0;
        /// Additional background baseline, outside the session cache budgets; <=4096.
        std::int64_t background_signature_bytes = 0;
        std::int64_t continuous_coordination_bytes = 0;
    };
    CacheStats cache_stats;
    desktop::ImageFrame marked_frame;
    std::vector<VisualMark> marks;
};

/// Configuration of one visual session host (DEC-016 decision 4).
struct VisualSessionConfig {
    /// M9-05/07/18: consume Experimental relation/attention APIs on this same
    /// worker. A separate source host outputs image-space background observations;
    /// legacy hosts retain display-space behavior. No template or marks paths.
    std::optional<VisualContinuousConfig> continuous;
    /// mirador cache budgets; non-positive values fail `start()` through
    /// `PerceptionSession::create` (mirador rejects them, never clamps).
    std::int64_t frame_cache_bytes = std::int64_t{4} * 1024 * 1024;
    std::int64_t result_cache_bytes = std::int64_t{16} * 1024 * 1024;
    /// Template index budgets (plan item `M3-04`); invalid values fail
    /// `start()` like the session caches. The index lives and dies with the
    /// session: stopping the host discards every enrollment (a restarted
    /// host starts empty).
    VisualTemplateIndexConfig templates;
    /// Immutable local product assets; bounded and validated at start, enrolled on worker.
    std::vector<VisualTemplateAsset> template_assets;
    /// Upper bound of one idle worker wait; bounds stop latency and covers
    /// missed wakeups. Not a polling schedule: requests are delivered
    /// through the latest-value mailbox plus an immediate wakeup.
    std::chrono::milliseconds idle_wait{100};
    /// Backends used by the run_ocr / run_detector capabilities; null means
    /// the capability fails with kBackendUnavailable when requested
    /// (mirador's null-backend contract). Non-owning; callers keep the
    /// backend objects alive for the host's lifetime.
    mirador::OcrBackend *ocr_backend = nullptr;
    mirador::DetectorBackend *detector_backend = nullptr;
    /// M8-07 (upstream DEC-025): budgeted OCR backend for requests that opt
    /// in through `VisualAnalysisRequest::ocr_budget`. Separate SPI — the
    /// static_assert in mirador keeps it non-derivable from `OcrBackend`.
    /// Null makes every budgeted request fail with kBackendUnavailable
    /// (fail-closed, never a silent legacy fallback). Non-owning; the
    /// caller keeps the backend alive for the host's lifetime.
    mirador::BudgetedOcrBackend *budgeted_ocr_backend = nullptr;
};

/// Serial, per-image-source visual analysis host (plan item `M3-02`, DEC-016
/// decision 4): owns one pinned `mirador::PerceptionSession` for one image
/// source and executes every session call (`analyze_change` / `run_ocr` /
/// `run_detector` / `fuse`) on that source's own Executor blocking worker —
/// the session is non-thread-safe and stays in one scheduling context. The
/// executor reference must outlive the host; the process-wide executor
/// ownership stays with the Runtime Service (EXEC-01).
///
/// Concurrency model: submissions publish into an kairo::comm
/// LatestMailbox (latest-wins transport; overwrites are observable in its
/// statistics) and wake the worker. At most one analysis executes at a time;
/// a newer submission supersedes the queued predecessor (settled kCancelled)
/// and cancels the in-flight one through its mirador ExecutionContext.
/// Cancellation and deadlines ride the request; every request settles its
/// future exactly once. start()/stop() bracket the host's life; the host is
/// one-shot, like the runtime service: a stopped host cannot restart (the
/// executor registers workers by name for its whole lifetime).
class VisualSessionHost {
  public:
    /// `executor` must be initialized; it is not owned. `source_id` must be
    /// non-empty and names both the session and the executor worker.
    VisualSessionHost(kairo::Executor &executor, std::string source_id,
                      VisualSessionConfig config = {});
    ~VisualSessionHost();
    VisualSessionHost(const VisualSessionHost &) = delete;
    VisualSessionHost &operator=(const VisualSessionHost &) = delete;

    /// Creates the perception session and starts the source's blocking
    /// worker. Fails closed with `error` set on any refusal (empty source
    /// id, rejected cache budgets, executor admission failure). Idempotent
    /// rejection after a start or stop.
    [[nodiscard]] bool start(std::string &error);

    /// Submits one analysis request; the future settles exactly once.
    /// Admission refusals (host not running, frame/source mismatch) settle
    /// kRejected instead of throwing.
    [[nodiscard]] std::future<VisualAnalysisResult> submit(VisualAnalysisRequest request);

    /// Stops accepting requests, cancels the in-flight analysis, and joins
    /// the worker. Queued requests settle kCancelled. Safe from any thread
    /// except a worker callback; the destructor calls it.
    void stop();

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const std::string &source_id() const noexcept { return source_id_; }

  private:
    kairo::Executor &executor_;
    std::string source_id_;
    VisualSessionConfig config_;
    enum class Lifecycle { New, Running, Stopped };
    std::atomic<Lifecycle> lifecycle_{Lifecycle::New};
    kairo::WorkerHandle worker_;
    std::shared_ptr<detail::VisualSessionCore> core_;
};

} // namespace mirage::integration
