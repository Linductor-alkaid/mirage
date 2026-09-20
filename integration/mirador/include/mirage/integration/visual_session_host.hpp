#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include <executor/blocking_io.hpp>
#include <executor/executor.hpp>

#include <mirador/detector_backend.hpp>
#include <mirador/frame.hpp>
#include <mirador/fusion.hpp>
#include <mirador/ocr_backend.hpp>
#include <mirador/status.hpp>

#include <mirage/desktop/cancellation.hpp>

namespace mirage::integration {

namespace detail {
struct VisualSessionCore;
}

class VisualSessionHost;

/// One visual analysis request against the host's per-source perception
/// session (DEC-016 decision 4). The frame must carry the host's source id.
/// The host always executes at most one request at a time on the session's
/// blocking worker; a newer submission supersedes: the queued predecessor is
/// settled `kRejected`-cancelled and an in-flight predecessor is cancelled
/// through its execution context (latest-wins, explicit settlement).
struct VisualAnalysisRequest {
    /// Capture to analyze (from `to_mirador_frame`); `source_id` must equal
    /// the host's.
    mirador::Frame frame;
    /// Refresh the session's change state first (stage-one change gating).
    bool analyze_change = true;
    bool run_ocr = false;
    mirador::OcrRequest ocr;
    bool run_detector = false;
    mirador::DetectionRequest detector;
    /// Fuse the produced OCR / detection evidence into a SemanticSnapshot
    /// published by the session. Fusion target space is fixed to kDisplay
    /// (DEC-016 decision 3); `display_transform` (kOriented -> kDisplay) is
    /// then mandatory, even when numerically identity. Evidence is declared
    /// in each capability request's `output_space`; mirador recovers it.
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
    /// Published fusion snapshot in kDisplay space (when `fuse` ran); the
    /// pointee is immutable.
    std::shared_ptr<const mirador::SemanticSnapshot> snapshot;
};

/// Configuration of one visual session host (DEC-016 decision 4).
struct VisualSessionConfig {
    /// mirador cache budgets; non-positive values fail `start()` through
    /// `PerceptionSession::create` (mirador rejects them, never clamps).
    std::int64_t frame_cache_bytes = std::int64_t{4} * 1024 * 1024;
    std::int64_t result_cache_bytes = std::int64_t{16} * 1024 * 1024;
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
