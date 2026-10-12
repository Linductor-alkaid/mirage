#include <mirage/integration/visual_observation_pipeline.hpp>

#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_observation_mapper.hpp>

#include <mirador/transform.hpp>
#include <mirage/desktop/input_provider.hpp>

#include <algorithm>
#include <cmath>
#include <future>
#include <utility>

namespace mirage::integration {
namespace {

/// Upper bound for one session wait when the caller passed no deadline: a
/// hang is a broken path, not a slow machine. The operation deadline
/// overrides it whenever it is earlier.
constexpr std::chrono::seconds kDefaultAnalysisBound{10};

} // namespace

VisualObservationPipeline::VisualObservationPipeline(kairo::Executor &executor,
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
    return refresh_impl(analyze, false, std::nullopt, cancel, deadline);
}
EnvironmentVisualRefresh VisualObservationPipeline::refresh_marked(
    const std::optional<desktop::WindowGeometry> &crop, const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    return refresh_impl(true, true, crop, cancel, deadline);
}
EnvironmentVisualRefresh VisualObservationPipeline::refresh_region(
    const desktop::WindowGeometry &area, bool analyze, const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    return refresh_impl(analyze, false, area, cancel, deadline);
}
ImageOcrOutcome VisualObservationPipeline::read_image_ocr(
    const desktop::ImageFrame &image, const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    ImageOcrOutcome out;
    const auto end =
        std::min(deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds(5)),
                 std::chrono::steady_clock::now() + std::chrono::seconds(5));
    const desktop::CancelToken bounded(
        [cancel, end] { return cancel.cancelled() || std::chrono::steady_clock::now() >= end; });
    if (bounded.cancelled()) {
        out.cancelled = cancel.cancelled();
        out.error = {out.cancelled ? "cancelled" : "deadline_exceeded",
                     "image OCR cancelled or expired"};
        return out;
    }
    if (!session_.running() || !config_.run_ocr || !config_.session.ocr_backend) {
        out.error = {"unavailable", "image OCR pipeline is not running"};
        return out;
    }
    if (image.width <= 0 || image.height <= 0 || std::max(image.width, image.height) > 1600 ||
        image.stride < std::size_t(image.width) * 4 ||
        image.stride > (8u << 20) / std::size_t(image.height) ||
        image.pixels.size() != image.stride * std::size_t(image.height)) {
        out.error = {"invalid_argument", "image OCR preview exceeds its bounded layout"};
        return out;
    }
    std::uint64_t sequence = 0;
    {
        const std::lock_guard<std::mutex> guard(sequence_mutex_);
        sequence = ++sequence_;
    }
    auto frame =
        to_mirador_frame(image, config_.source_id, sequence, std::chrono::steady_clock::now());
    if (!frame) {
        out.error = {"invalid_argument", "image OCR frame is invalid"};
        return out;
    }
    VisualAnalysisRequest request;
    request.frame = std::move(frame.value());
    request.analyze_change = false;
    request.run_ocr = true;
    request.ocr.cache_policy = mirador::CachePolicy::kRefresh;
    request.ocr.output_space = mirador::CoordinateSpaceId::kOriented;
    request.fuse = false; // No global transform, snapshot or actionable refs.
    request.deadline = end;
    request.external_cancel = bounded;
    auto future = session_.submit(std::move(request));
    if (future.wait_until(end) != std::future_status::ready) {
        out.error = {"deadline_exceeded", "image OCR did not settle within its bound"};
        return out;
    }
    auto result = future.get();
    if (bounded.cancelled() || result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled) {
        out.cancelled =
            cancel.cancelled() || result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled;
        out.error = {out.cancelled ? "cancelled" : "deadline_exceeded",
                     "image OCR cancelled or expired"};
        return out;
    }
    if (result.outcome.kind != VisualAnalysisOutcome::Kind::kCompleted) {
        out.error = {
            result.outcome.kind == VisualAnalysisOutcome::Kind::kTimedOut   ? "deadline_exceeded"
            : result.outcome.code == mirador::ErrorCode::kBudgetExceeded    ? "result_too_large"
            : result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected ? "unavailable"
                                                                            : "analysis_failed",
            "image OCR analysis failed"};
        return out;
    }
    if (result.text.size() > 512) {
        out.error = {"result_too_large", "image OCR result exceeds its region budget"};
        return out;
    }
    for (const auto &r : result.text) {
        const auto &b = r.bounds;
        if (r.utf8_text.size() > 1024 || !desktop::is_valid_utf8(r.utf8_text) ||
            !std::isfinite(r.confidence) || r.confidence < 0 || r.confidence > 1 ||
            !std::isfinite(b.x) || !std::isfinite(b.y) || !std::isfinite(b.width) ||
            !std::isfinite(b.height) || b.x < 0 || b.y < 0 || b.width <= 0 || b.height <= 0 ||
            double(b.x) + b.width > image.width || double(b.y) + b.height > image.height) {
            out.error = {"invalid_result", "image OCR text or geometry is invalid"};
            out.regions.clear();
            return out;
        }
        if (out.regions.size() < 128)
            out.regions.push_back({r.utf8_text, r.confidence, b.x, b.y, b.width, b.height});
    }
    out.total_region_count = result.text.size();
    out.ok = true;
    return out;
}

EnvironmentVisualRefresh VisualObservationPipeline::refresh_impl(
    bool analyze, bool marks, const std::optional<desktop::WindowGeometry> &crop,
    const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    EnvironmentVisualRefresh refresh;
    if (cancel.cancelled()) {
        refresh.capture_cancelled = true;
        return refresh;
    }
    if (deadline && std::chrono::steady_clock::now() >= *deadline) {
        refresh.capture_error = {"deadline_exceeded", "visual capture deadline expired"};
        return refresh;
    }

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

    const auto &display = target->geometry;
    const auto area = crop.value_or(display);
    if ((marks || crop) &&
        (area.width <= 0 || area.height <= 0 || area.x < display.x || area.y < display.y ||
         std::int64_t{area.x} + area.width > std::int64_t{display.x} + display.width ||
         std::int64_t{area.y} + area.height > std::int64_t{display.y} + display.height)) {
        refresh.capture_error = {"invalid_argument", "mark crop outside configured display"};
        return refresh;
    }
    refresh.display_geometry = display;
    refresh.display_primary = target->primary;
    refresh.image_geometry = area;
    const bool regional = crop && (!marks || config_.roi_capture);
    if (regional)
        refresh.capture_roi = area;
    const auto capture = regional
                             ? screen_.capture_roi(area, config_.capture_limits, cancel)
                             : screen_.capture_display(target->id, config_.capture_limits, cancel);
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
    const auto capture_area = refresh.capture_roi.value_or(display);
    if ((marks || regional) && (refresh.frame.width != capture_area.width ||
                                refresh.frame.height != capture_area.height)) {
        refresh.analysis_error = {"invalid_argument", "marks require a 1:1 display pixel mapping"};
        return refresh;
    }
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
    request.ocr.cache_policy =
        config_.refresh_ocr ? mirador::CachePolicy::kRefresh : mirador::CachePolicy::kReadWrite;
    request.run_detector = config_.run_detector;
    request.fuse = true;
    request.render_marks = marks;
    // Reuse the existing bounded Mirador geometry stage; no semantic detector.
    request.geometry.run = marks && config_.geometry_marks;
    request.geometry.fuse_proposals = request.geometry.run;
    request.geometry.max_fused_area_fraction = 0.25;
    request.probe_geometry_templates =
        request.geometry.run && !config_.session.template_assets.empty();
    if (request.probe_geometry_templates) {
        request.template_query.perceptual_similarity_threshold = 0.95;
        request.template_query.template_ncc_threshold = 0.95;
        request.template_query.max_candidates = 16;
        request.template_reuse_threshold = 0.95;
    }
    if (marks)
        request.mark_roi = mirador::RectI{area.x - capture_area.x, area.y - capture_area.y,
                                          area.width, area.height};
    // The display transform (kOriented -> kDisplay) uses the captured pixel
    // area's origin (ROI or display), even for the identity transform: the
    // fused bounds are global desktop coordinates exactly because of it
    // (DEC-016 decision 3).
    request.display_transform = mirador::make_translation(
        static_cast<double>(capture_area.x), static_cast<double>(capture_area.y),
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
    VisualAnalysisResult result = future.get();
    if (cancel.cancelled() || (deadline && std::chrono::steady_clock::now() >= *deadline)) {
        refresh.analysis_cancelled = cancel.cancelled();
        refresh.analysis_error = {"deadline_exceeded", "visual result expired before publication"};
        return refresh;
    }
    switch (result.outcome.kind) {
    case VisualAnalysisOutcome::Kind::kCompleted:
        if (result.snapshot == nullptr) {
            refresh.analysis_error = {"io_error",
                                      "completed analysis published no fusion snapshot"};
            break;
        }
        refresh.publish = publish_visual_snapshot(*result.snapshot, registry_);
        refresh.analyzed = refresh.publish.ok;
        if (refresh.analyzed && marks) {
            refresh.marked_frame = std::move(result.marked_frame);
            refresh.marks = std::move(result.marks);
            refresh.visual_generation = result.snapshot->generation;
            refresh.frame_sequence = result.snapshot->frame_sequence;
            for (auto &mark : refresh.marks)
                mark.ref = refresh.publish.snapshot.regions.at(mark.number - 1).ref;
        }
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
