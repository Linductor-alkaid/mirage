#include <mirage/integration/visual_observation_pipeline.hpp>

#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_observation_mapper.hpp>

#include <mirador/transform.hpp>

#include <future>
#include <utility>

namespace mirage::integration {
namespace {

/// Upper bound for one session wait when the caller passed no deadline: a
/// hang is a broken path, not a slow machine. The operation deadline
/// overrides it whenever it is earlier.
constexpr std::chrono::seconds kDefaultAnalysisBound{10};

} // namespace

VisualObservationPipeline::VisualObservationPipeline(executor::Executor &executor,
                                                     desktop::ScreenProvider &screen,
                                                     desktop::VisualReferenceRegistry &registry,
                                                     VisualObservationPipelineConfig config)
    : executor_(executor), screen_(screen), registry_(registry), config_(std::move(config)),
      session_(executor_, config_.source_id, config_.session) {}

VisualObservationPipeline::~VisualObservationPipeline() { stop(); }

bool VisualObservationPipeline::start(std::string &error) { return session_.start(error); }

void VisualObservationPipeline::stop() { session_.stop(); }

bool VisualObservationPipeline::running() const { return session_.running(); }

desktop::VisualReferenceRegistry &VisualObservationPipeline::registry() { return registry_; }

EnvironmentVisualRefresh VisualObservationPipeline::refresh(
    bool analyze, const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    EnvironmentVisualRefresh refresh;

    // Capture stage: resolve the target display through the provider itself,
    // then capture it. Cancellation is observed through the token between
    // and inside the provider calls.
    const auto displays = screen_.list_displays(desktop::DisplayListLimits{}, cancel);
    if (cancel.cancelled()) {
        refresh.capture_cancelled = true;
        return refresh;
    }
    if (!displays.ok) {
        refresh.capture_error = displays.error;
        return refresh;
    }
    const desktop::DisplayInfo *target = nullptr;
    if (!config_.display_id.empty()) {
        for (const auto &display : displays.displays) {
            if (display.id == config_.display_id) {
                target = &display;
                break;
            }
        }
        if (target == nullptr) {
            refresh.capture_error = {"not_found", "configured display id was not reported"};
            return refresh;
        }
    } else {
        for (const auto &display : displays.displays) {
            if (display.primary) {
                target = &display;
                break;
            }
        }
        if (target == nullptr && !displays.displays.empty()) {
            target = &displays.displays.front();
        }
    }
    if (target == nullptr) {
        refresh.capture_error = {"not_found", "no display was reported"};
        return refresh;
    }

    const auto capture = screen_.capture_display(target->id, config_.capture_limits, cancel);
    if (cancel.cancelled() || capture.cancelled) {
        refresh.capture_cancelled = true;
        return refresh;
    }
    if (!capture.ok) {
        refresh.capture_error = capture.error;
        return refresh;
    }
    refresh.captured = true;
    refresh.display_id = target->id;
    refresh.frame = std::move(capture.frame);
    if (!analyze) {
        return refresh;
    }

    // Analysis stage: the session host runs every mirador call on the
    // source's serialized blocking worker; this thread only waits, bounded.
    if (!session_.running()) {
        refresh.analysis_error = {"invalid_state", "visual session is not running"};
        return refresh;
    }
    std::uint64_t sequence = 0;
    {
        const std::lock_guard<std::mutex> guard(sequence_mutex_);
        sequence = ++sequence_;
    }
    auto frame = to_mirador_frame(refresh.frame, config_.source_id, sequence,
                                  std::chrono::steady_clock::now());
    if (!frame) {
        refresh.analysis_error.code = "invalid_argument";
        refresh.analysis_error.message = frame.status().message();
        return refresh;
    }

    VisualAnalysisRequest request;
    request.frame = std::move(frame.value());
    request.analyze_change = config_.analyze_change;
    request.run_ocr = config_.run_ocr;
    request.run_detector = config_.run_detector;
    request.fuse = true;
    // The display transform (kOriented -> kDisplay) comes from the captured
    // display's placement and is passed even when it is the identity: the
    // fused bounds are global desktop coordinates exactly because of it
    // (DEC-016 decision 3).
    request.display_transform = mirador::make_translation(
        static_cast<double>(target->geometry.x), static_cast<double>(target->geometry.y),
        mirador::CoordinateSpaceId::kOriented, mirador::CoordinateSpaceId::kDisplay);
    request.deadline = deadline;
    request.external_cancel = cancel;

    std::future<VisualAnalysisResult> future = session_.submit(std::move(request));
    const auto bound = deadline.value_or(std::chrono::steady_clock::now() + kDefaultAnalysisBound);
    if (future.wait_until(bound) != std::future_status::ready) {
        // Defensive: the request carries the same deadline, so the session
        // settles it explicitly first. Reaching here means the wait bound
        // was exceeded without a settlement — fail loudly, never hang.
        refresh.analysis_error = {"deadline_exceeded",
                                  "visual analysis did not settle within the bound"};
        return refresh;
    }
    const VisualAnalysisResult result = future.get();
    switch (result.outcome.kind) {
    case VisualAnalysisOutcome::Kind::kCompleted:
        if (result.snapshot == nullptr) {
            refresh.analysis_error = {"io_error",
                                      "completed analysis published no fusion snapshot"};
            break;
        }
        refresh.publish = publish_visual_snapshot(*result.snapshot, registry_);
        refresh.analyzed = refresh.publish.ok;
        break;
    case VisualAnalysisOutcome::Kind::kCancelled:
        refresh.analysis_cancelled = true;
        break;
    case VisualAnalysisOutcome::Kind::kTimedOut:
        refresh.analysis_error = {"deadline_exceeded", result.outcome.message};
        break;
    case VisualAnalysisOutcome::Kind::kFailed:
        if (result.outcome.code == mirador::ErrorCode::kBudgetExceeded) {
            refresh.analysis_error = {"result_too_large", result.outcome.message};
        } else {
            refresh.analysis_error = {"io_error", result.outcome.message};
        }
        break;
    case VisualAnalysisOutcome::Kind::kRejected:
        refresh.analysis_error = {"invalid_state", result.outcome.message};
        break;
    }
    return refresh;
}

} // namespace mirage::integration
