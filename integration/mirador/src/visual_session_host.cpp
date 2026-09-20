#include <mirage/integration/visual_session_host.hpp>

#include <executor/comm/mailbox.hpp>
#include <executor/stop_token.hpp>

#include <mirador/evidence.hpp>
#include <mirador/perception_session.hpp>
#include <mirador/status.hpp>

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

} // namespace

/// State shared between the host (admission, supersede, stop) and the
/// blocking worker (execution). Mutex and condition variable are the sleep /
/// wakeup primitive the blocking-worker contract requires; the request
/// payload itself travels through the executor::comm LatestMailbox, never
/// through the lock (AGENTS.md rule 4).
struct detail::VisualSessionCore {
    VisualSessionCore(std::string host_source_id, VisualSessionConfig host_config,
                      mirador::PerceptionSession perception_session)
        : source_id(std::move(host_source_id)), config(std::move(host_config)),
          session(std::move(perception_session)) {}

    const std::string source_id;
    const VisualSessionConfig config;
    /// Only ever touched on the worker thread: one session, one scheduling
    /// context (DEC-016 decision 4).
    mirador::PerceptionSession session;

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

/// Runs the requested capabilities serially on the session: change gating
/// first, then OCR / detection, then fusion of the produced visual evidence
/// only (DEC-016 decision 3: accessibility regions are never fusion input).
/// Every mirador call polls the request's execution context, so a
/// cancellation or deadline lands at the next stage boundary as an explicit
/// status.
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
    if (request.run_detector) {
        mirador::Result<std::vector<mirador::DetectionRegion>> regions = core.session.run_detector(
            request.frame, core.config.detector_backend, request.detector, context);
        if (!regions.ok()) {
            result.outcome = map_status(regions.status());
            settle(state, std::move(result));
            return;
        }
        result.detections = regions.take_value();
    }
    if (request.fuse) {
        if (!request.display_transform) {
            VisualAnalysisOutcome outcome;
            outcome.kind = VisualAnalysisOutcome::Kind::kFailed;
            outcome.code = mirador::ErrorCode::kInvalidArgument;
            outcome.message =
                "fusion requires the kOriented -> kDisplay display transform (DEC-016)";
            result.outcome = std::move(outcome);
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
        result.snapshot = std::make_shared<const mirador::SemanticSnapshot>(fused.take_value());
    }
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
    auto core =
        std::make_shared<detail::VisualSessionCore>(source_id_, config_, session.take_value());
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
