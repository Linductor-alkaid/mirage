#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <executor/blocking_io.hpp>
#include <executor/executor.hpp>

#include <mirador/detector_backend.hpp>
#include <mirador/frame.hpp>
#include <mirador/fusion.hpp>
#include <mirador/geometric_proposal.hpp>
#include <mirador/line_detector.hpp>
#include <mirador/ocr_backend.hpp>
#include <mirador/segment_growing_line_detector.hpp>
#include <mirador/status.hpp>
#include <mirador/visual_index.hpp>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/integration/visual_template_index.hpp>

namespace mirage::integration {

namespace detail {
struct VisualSessionCore;
}

class VisualSessionHost;

/// One template enrollment riding an analysis request (plan item `M3-04`):
/// the patch is cropped from the request frame itself at `patch` (oriented
/// pixel coordinates of the rotation-k0 capture form the adapter produces),
/// so enrollment introduces no second pixel lifetime.
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
struct VisualAnalysisRequest {
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
    /// Probe thresholds and candidate cap (mirador defaults when untouched).
    mirador::VisualQueryParams template_query;
    /// Clear-winner reuse threshold in [0, 1] (mirador example discipline:
    /// only a strict winner counts as the enrolled icon).
    double template_reuse_threshold = 0.95;
    /// Geometric closed-region proposal stage (see `VisualGeometryStage`).
    VisualGeometryStage geometry;
    /// Fuse the produced OCR / detection / template / geometry evidence into
    /// a SemanticSnapshot published by the session. Fusion target space is
    /// fixed to kDisplay (DEC-016 decision 3); `display_transform`
    /// (kOriented -> kDisplay) is then mandatory, even when numerically
    /// identity. Evidence is declared in each capability request's
    /// `output_space`; mirador recovers it.
    bool fuse = false;
    std::optional<mirador::Transform2D> display_transform;
    /// Analysis deadline on the steady clock; nullopt disables it. Expiry
    /// surfaces as a kTimedOut outcome, never as a hang.
    std::optional<std::chrono::steady_clock::time_point> deadline;
    /// Extra caller-side cancellation: the analysis is abandoned when either
    /// this token or the host's latest-wins replacement requests it.
    desktop::CancelToken external_cancel;
};

/// Terminal state of one analysis request. Every admitted request settles
/// exactly once; superseded, cancelled and shutdown requests settle
/// explicitly instead of being dropped (RULE-10).
struct VisualAnalysisOutcome {
    enum class Kind {
        kCompleted, ///< requested capabilities ran; results are meaningful
        kFailed,    ///< a mirador call failed; `code`/`message` say why
        kCancelled, ///< superseded, externally cancelled or host stopped
        kTimedOut,  ///< the request deadline expired
        kRejected,  ///< admission refused; the session never saw the request
    };
    Kind kind = Kind::kRejected;
    /// mirador error code for kFailed (diagnostic); kOk otherwise.
    mirador::ErrorCode code = mirador::ErrorCode::kOk;
    std::string message; ///< short human-readable reason when not completed
};

/// Result payload of one settled analysis request. Only the fields of the
/// requested capabilities are meaningful.
struct VisualAnalysisResult {
    VisualAnalysisOutcome outcome;
    /// Change decision of this frame (when `analyze_change` ran); the first
    /// submission of a session reports kGlobal / kFirstFrame.
    mirador::ChangeReport change;
    std::vector<mirador::TextRegion> text;
    std::vector<mirador::DetectionRegion> detections;
    /// Accepted template probes of this request, in detection order (when
    /// `probe_templates` ran).
    std::vector<VisualTemplateHit> template_hits;
    /// Closed-region proposals of this frame, in mirador's deterministic
    /// output order, bounds in the frame's oriented pixel space (when the
    /// geometry stage ran).
    std::vector<mirador::GeometricRegionProposal> geometry_proposals;
    /// Published fusion snapshot in kDisplay space (when `fuse` ran); the
    /// pointee is immutable.
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
    };
    CacheStats cache_stats;
};

/// Configuration of one visual session host (DEC-016 decision 4).
struct VisualSessionConfig {
    /// mirador cache budgets; non-positive values fail `start()` through
    /// `PerceptionSession::create` (mirador rejects them, never clamps).
    std::int64_t frame_cache_bytes = std::int64_t{4} * 1024 * 1024;
    std::int64_t result_cache_bytes = std::int64_t{16} * 1024 * 1024;
    /// Template index budgets (plan item `M3-04`); invalid values fail
    /// `start()` like the session caches. The index lives and dies with the
    /// session: stopping the host discards every enrollment (a restarted
    /// host starts empty).
    VisualTemplateIndexConfig templates;
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
};

/// Serial, per-image-source visual analysis host (plan item `M3-02`, DEC-016
/// decision 4): owns one pinned `mirador::PerceptionSession` for one image
/// source and executes every session call (`analyze_change` / `run_ocr` /
/// `run_detector` / `fuse`) on that source's own Executor blocking worker —
/// the session is non-thread-safe and stays in one scheduling context. The
/// executor reference must outlive the host; the process-wide executor
/// ownership stays with the Runtime Service (EXEC-01).
///
/// Concurrency model: submissions publish into an executor::comm
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
    VisualSessionHost(executor::Executor &executor, std::string source_id,
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
    executor::Executor &executor_;
    std::string source_id_;
    VisualSessionConfig config_;
    enum class Lifecycle { New, Running, Stopped };
    std::atomic<Lifecycle> lifecycle_{Lifecycle::New};
    executor::WorkerHandle worker_;
    std::shared_ptr<detail::VisualSessionCore> core_;
};

} // namespace mirage::integration
