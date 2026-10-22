// M8-07 (upstream DEC-025) host-layer budgeted OCR path: the VisualSessionHost
// branch that routes `run_ocr` requests carrying `ocr_budget` through the
// configured `budgeted_ocr_backend` instead of the all-or-nothing legacy
// backend. The scenarios pin, against a scripted BudgetedOcrBackend on a real
// Executor blocking worker: the OcrBudget/OcrRequest -> BudgetedOcrRequest
// field mapping observed at the backend SPI boundary (pipeline fields, budget
// knobs, caller priorities), the kRecognized -> result.text mapping (bounds,
// text, rec confidence), the kDeferred -> diagnostics.deferred_bounds mapping
// (never in text), the diagnostics mapping of completion / the three stop
// reasons / counts / det-limit fields / geometry space, the fail-closed
// kBackendUnavailable when a budgeted request arrives without a configured
// budgeted backend (no silent legacy fallback), the untouched legacy path,
// the coexistence of a partial budgeted report with detection in one request,
// and the never-cached nature of the budgeted entry.

#include "../support/test.hpp"

#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_session_host.hpp>

#include <kairo/executor.hpp>

#include <mirador/execution_context.hpp>
#include <mirador/frame.hpp>
#include <mirador/image_view.hpp>
#include <mirador/ocr_backend.hpp>
#include <mirador/ocr_batch.hpp>
#include <mirador/pixel_format.hpp>
#include <mirador/status.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace desktop = mirage::desktop;

using mirage::integration::FakeDetectorBackend;
using mirage::integration::FakeDetectorConfig;
using mirage::integration::FakeOcrBackend;
using mirage::integration::FakeOcrConfig;
using mirage::integration::VisualAnalysisOutcome;
using mirage::integration::VisualAnalysisRequest;
using mirage::integration::VisualAnalysisResult;
using mirage::integration::VisualSessionConfig;
using mirage::integration::VisualSessionHost;

/// Upper bound for every wait in this suite; a hang means a broken
/// cancellation or settlement path, never a test expectation.
constexpr std::chrono::seconds kWaitBound{10};

bool settle_within(std::future<VisualAnalysisResult> &future, VisualAnalysisResult &out) {
    if (future.wait_for(kWaitBound) != std::future_status::ready) {
        return false;
    }
    out = future.get();
    return true;
}

/// Builds a valid owning uniform Bgra8 frame (identity semantics for the fake
/// backends), the same discipline as visual_session_test.
mirador::Frame owned_bgra8_frame(std::int32_t width, std::int32_t height,
                                 const std::string &source_id, std::uint64_t sequence) {
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(
        stride * static_cast<std::size_t>(height), std::uint8_t{0x40});
    mirador::ImageView view;
    view.data = reinterpret_cast<const std::byte *>(pixels->data());
    view.width = width;
    view.height = height;
    view.row_stride_bytes = static_cast<std::int64_t>(stride);
    view.format = mirador::PixelFormat::kBgra8;
    view.rotation = mirador::Rotation::k0;
    MIRAGE_CHECK(mirador::validate(view).ok());

    mirador::Frame frame;
    frame.image = view;
    frame.sequence = sequence;
    frame.timestamp = std::chrono::steady_clock::time_point{} + std::chrono::seconds{7};
    frame.source_id = source_id;
    frame.owner = std::move(pixels);
    return frame;
}

mirador::Frame session_frame(const std::string &source_id, std::uint64_t sequence) {
    return owned_bgra8_frame(32, 16, source_id, sequence);
}

/// One scripted line of a budgeted report. `recognized` builds a kRecognized
/// record (text + rec confidence + execution order); otherwise a kDeferred
/// record (no text, no rec score, execution order -1), keeping the det
/// geometry either way.
mirador::OcrLineRecord scripted_line(std::int32_t candidate_id, mirador::RectF bounds,
                                     float det_confidence, bool recognized, std::string text,
                                     float rec_confidence, std::int32_t execution_order) {
    mirador::OcrLineRecord line;
    line.candidate_id = candidate_id;
    line.bounds = bounds;
    line.det_confidence = det_confidence;
    if (recognized) {
        line.state = mirador::OcrCompletionState::kRecognized;
        line.utf8_text = std::move(text);
        line.rec_confidence = rec_confidence;
        line.execution_order = execution_order;
    } else {
        line.state = mirador::OcrCompletionState::kDeferred;
        line.execution_order = -1;
    }
    return line;
}

/// A structurally consistent report (mirador rejects inconsistent backend
/// output with kBackendFailure, so every scripted report conserves counts and
/// pairs stop reason / completion).
mirador::OcrBatchResult batch_report(std::vector<mirador::OcrLineRecord> lines,
                                     mirador::OcrBatchStopReason stop_reason,
                                     std::int32_t det_limit = 0, bool det_limit_hit = false) {
    mirador::OcrBatchResult report;
    report.lines = std::move(lines);
    report.detected_count = static_cast<std::int32_t>(report.lines.size());
    report.det_limit = det_limit;
    report.det_limit_hit = det_limit_hit;
    for (const mirador::OcrLineRecord &line : report.lines) {
        if (line.state == mirador::OcrCompletionState::kRecognized) {
            ++report.recognized_count;
        } else if (line.state == mirador::OcrCompletionState::kDeferred) {
            ++report.deferred_count;
        }
    }
    report.rec_forward_count = report.recognized_count;
    report.stop_reason = stop_reason;
    report.completion =
        report.deferred_count == 0 && stop_reason == mirador::OcrBatchStopReason::kCompleted
            ? mirador::OcrBatchCompletion::kComplete
            : mirador::OcrBatchCompletion::kPartial;
    return report;
}

/// Scriptable budgeted backend: records the prepared image and the SPI-layer
/// request of every call, then returns the scripted result or status. The
/// session keeps it to one call at a time; counters stay atomic anyway.
class ScriptedBudgetedOcrBackend final : public mirador::BudgetedOcrBackend {
  public:
    ScriptedBudgetedOcrBackend() {
        info_value_.name = "mirage-fake-budgeted-ocr";
        info_value_.implementation_version = "1.0.0";
        info_value_.model_id = "fake-budget-model";
        info_value_.model_revision = "r1";
        info_value_.accepted_formats = {mirador::PixelFormat::kBgra8};
        info_value_.thread_safe = false;
    }

    [[nodiscard]] mirador::BackendInfo info() const override { return info_value_; }

    mirador::Result<mirador::OcrBatchResult>
    recognize_budgeted(const mirador::ImageView &prepared_image,
                       const mirador::OcrBatchRequest &request,
                       const mirador::ExecutionContext & /*context*/) override {
        calls_.fetch_add(1, std::memory_order_acq_rel);
        last_prepared_width_ = prepared_image.width;
        last_prepared_height_ = prepared_image.height;
        last_format_ = prepared_image.format;
        last_request_ = request;
        if (!next_status_.ok()) {
            return next_status_;
        }
        return next_result_;
    }

    // --- scripting knobs -------------------------------------------------------------

    void set_next_result(mirador::OcrBatchResult result) { next_result_ = std::move(result); }

    void set_next_status(mirador::Status status) { next_status_ = std::move(status); }

    // --- observations of the last call -----------------------------------------------

    [[nodiscard]] std::size_t calls() const noexcept {
        return calls_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int32_t last_prepared_width() const noexcept { return last_prepared_width_; }

    [[nodiscard]] std::int32_t last_prepared_height() const noexcept {
        return last_prepared_height_;
    }

    [[nodiscard]] mirador::PixelFormat last_format() const noexcept { return last_format_; }

    [[nodiscard]] const mirador::OcrBatchRequest &last_request() const noexcept {
        return last_request_;
    }

  private:
    mirador::BackendInfo info_value_;
    mirador::OcrBatchResult next_result_;
    mirador::Status next_status_{mirador::ErrorCode::kOk, ""};
    std::atomic<std::size_t> calls_{0};
    std::int32_t last_prepared_width_ = 0;
    std::int32_t last_prepared_height_ = 0;
    mirador::PixelFormat last_format_ = mirador::PixelFormat::kBgra8;
    mirador::OcrBatchRequest last_request_;
};

FakeOcrBackend bgra_ocr() {
    FakeOcrConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    return FakeOcrBackend(config);
}

FakeDetectorBackend bgra_detector() {
    FakeDetectorConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    return FakeDetectorBackend(config);
}

/// A default budgeted request: opt in through the OCR stage, no ROI, no
/// deadline reserve, one recognized line over the full view.
VisualAnalysisRequest budgeted_request(const std::string &source_id, std::uint64_t sequence) {
    VisualAnalysisRequest request;
    request.frame = session_frame(source_id, sequence);
    request.analyze_change = false;
    request.run_ocr = true;
    request.ocr_budget = VisualAnalysisRequest::OcrBudget{};
    return request;
}

// ---- A. request field mapping --------------------------------------------------------

/// The host must translate `request.ocr` pipeline fields and `request.ocr_budget`
/// budget knobs into the session's BudgetedOcrRequest; at the backend SPI
/// boundary the pass-through fields arrive verbatim and the caller priorities
/// arrive converted into prepared-image pixel space (identity here: no ROI,
/// no downscale, rotation k0). `finalize_reserve` is only legal with a hard
/// deadline, so this scenario also carries one.
void budgeted_request_maps_pipeline_and_budget_fields(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{0.0F, 0.0F, 32.0F, 16.0F}, 0.8F, true, "mapped", 0.9F, 0)},
        mirador::OcrBatchStopReason::kCompleted));
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "map-fields-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request = budgeted_request("map-fields-src", 1);
    request.ocr.language_hint = "zh-Hans";
    request.ocr.min_confidence = 0.25F;
    request.ocr.backend_params = "params-m8-07";
    request.ocr.output_space = mirador::CoordinateSpaceId::kOriented;
    request.ocr_budget->max_rec_lines = 7;
    request.ocr_budget->selection = mirador::OcrSelectionStrategy::kDetScore;
    request.ocr_budget->soft_budget = std::chrono::milliseconds{250};
    request.ocr_budget->finalize_reserve = std::chrono::milliseconds{500};
    request.ocr_budget->priorities.push_back({mirador::RectF{0.0F, 0.0F, 32.0F, 16.0F}, 3.0F});
    request.ocr_budget->priority_space = mirador::CoordinateSpaceId::kOriented;
    request.deadline = std::chrono::steady_clock::now() + kWaitBound; // finalize_reserve needs it

    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(budgeted.calls() == 1);

    const mirador::OcrBatchRequest &seen = budgeted.last_request();
    MIRAGE_CHECK(seen.language_hint == "zh-Hans");
    MIRAGE_CHECK(seen.min_confidence == 0.25F);
    MIRAGE_CHECK(seen.backend_params == "params-m8-07");
    MIRAGE_CHECK(seen.max_rec_lines == 7);
    MIRAGE_CHECK(seen.selection == mirador::OcrSelectionStrategy::kDetScore);
    MIRAGE_CHECK(seen.soft_budget == std::optional<std::chrono::steady_clock::duration>{
                                         std::chrono::milliseconds{250}});
    MIRAGE_CHECK(seen.finalize_reserve == std::optional<std::chrono::steady_clock::duration>{
                                              std::chrono::milliseconds{500}});
    MIRAGE_CHECK(seen.priorities.size() == 1);
    if (seen.priorities.size() == 1) {
        MIRAGE_CHECK(seen.priorities[0].priority == 3.0F);
        MIRAGE_CHECK((seen.priorities[0].roi == mirador::RectF{0.0F, 0.0F, 32.0F, 16.0F}));
    }
    host.stop();
}

/// The pipeline-owned fields of `request.ocr` (ROI crop and max_side downscale)
/// shape the prepared image the backend receives: a half-frame ROI crops to
/// 16x8, and `max_side = 8` caps the long edge, downscaling it to 8x4.
void budgeted_request_applies_roi_and_max_side(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{0.0F, 0.0F, 16.0F, 8.0F}, 0.8F, true, "roi", 0.9F, 0)},
        mirador::OcrBatchStopReason::kCompleted));
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "roi-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest cropped = budgeted_request("roi-src", 1);
    cropped.ocr.roi = mirador::RectF{8.0F, 4.0F, 16.0F, 8.0F};
    cropped.ocr.roi_space = mirador::CoordinateSpaceId::kOriented;
    std::future<VisualAnalysisResult> cropped_future = host.submit(std::move(cropped));
    VisualAnalysisResult cropped_result;
    MIRAGE_CHECK(settle_within(cropped_future, cropped_result));
    MIRAGE_CHECK(cropped_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(budgeted.last_prepared_width() == 16);
    MIRAGE_CHECK(budgeted.last_prepared_height() == 8);
    MIRAGE_CHECK(budgeted.last_format() == mirador::PixelFormat::kBgra8);

    VisualAnalysisRequest downscaled = budgeted_request("roi-src", 2);
    downscaled.ocr.roi = mirador::RectF{8.0F, 4.0F, 16.0F, 8.0F};
    downscaled.ocr.max_side = 8; // caps the long edge: the 16x8 crop becomes 8x4
    std::future<VisualAnalysisResult> downscaled_future = host.submit(std::move(downscaled));
    VisualAnalysisResult downscaled_result;
    MIRAGE_CHECK(settle_within(downscaled_future, downscaled_result));
    MIRAGE_CHECK(downscaled_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(budgeted.last_prepared_width() == 8);
    MIRAGE_CHECK(budgeted.last_prepared_height() == 4);
    MIRAGE_CHECK(budgeted.calls() == 2);
    host.stop();
}

// ---- B. line-state mapping -----------------------------------------------------------

/// kRecognized lines land in `result.text` in report order, each carrying the
/// line's recovered bounds, decoded text and rec confidence; a recognized line
/// with empty decoded text is still a text fact (the state is the only source
/// of truth). kDeferred lines never enter `result.text`; their det geometry
/// stays queryable through `ocr_batch->deferred_bounds` in report order.
void budgeted_lines_split_into_text_and_deferred_bounds(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{1.0F, 2.0F, 10.0F, 4.0F}, 0.9F, true, "first", 0.91F, 0),
         scripted_line(1, mirador::RectF{3.0F, 6.0F, 8.0F, 4.0F}, 0.5F, false, "", 0.0F, -1),
         scripted_line(2, mirador::RectF{5.0F, 10.0F, 12.0F, 4.0F}, 0.7F, true, "", 0.42F, 1),
         scripted_line(3, mirador::RectF{7.0F, 14.0F, 6.0F, 2.0F}, 0.4F, false, "", 0.0F, -1)},
        mirador::OcrBatchStopReason::kRecLineLimit));
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "split-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request = budgeted_request("split-src", 1);
    request.ocr_budget->max_rec_lines = 2;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);

    MIRAGE_CHECK(result.text.size() == 2);
    if (result.text.size() == 2) {
        MIRAGE_CHECK((result.text[0].bounds == mirador::RectF{1.0F, 2.0F, 10.0F, 4.0F}));
        MIRAGE_CHECK(result.text[0].utf8_text == "first");
        MIRAGE_CHECK(result.text[0].confidence == 0.91F);
        MIRAGE_CHECK((result.text[1].bounds == mirador::RectF{5.0F, 10.0F, 12.0F, 4.0F}));
        MIRAGE_CHECK(result.text[1].utf8_text.empty()); // recognized-but-empty is still text
        MIRAGE_CHECK(result.text[1].confidence == 0.42F);
    }

    MIRAGE_CHECK(result.ocr_batch.has_value());
    if (result.ocr_batch.has_value()) {
        MIRAGE_CHECK(result.ocr_batch->deferred_bounds.size() == 2);
        if (result.ocr_batch->deferred_bounds.size() == 2) {
            MIRAGE_CHECK(
                (result.ocr_batch->deferred_bounds[0] == mirador::RectF{3.0F, 6.0F, 8.0F, 4.0F}));
            MIRAGE_CHECK(
                (result.ocr_batch->deferred_bounds[1] == mirador::RectF{7.0F, 14.0F, 6.0F, 2.0F}));
        }
    }
    host.stop();
}

// ---- C. diagnostics mapping ----------------------------------------------------------

/// The report's completion, stop reason and count fields map verbatim onto
/// `ocr_batch`: kCompleted/kComplete when every line was recognized,
/// kRecLineLimit/kPartial at the line budget, kSoftBudget/kPartial on a caller
/// soft budget.
void budgeted_diagnostics_map_the_stop_reasons(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "stop-reasons-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    // kCompleted: all three lines recognized.
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{0.0F, 0.0F, 8.0F, 2.0F}, 0.9F, true, "a", 0.9F, 0),
         scripted_line(1, mirador::RectF{0.0F, 4.0F, 8.0F, 2.0F}, 0.8F, true, "b", 0.8F, 1),
         scripted_line(2, mirador::RectF{0.0F, 8.0F, 8.0F, 2.0F}, 0.7F, true, "c", 0.7F, 2)},
        mirador::OcrBatchStopReason::kCompleted));
    VisualAnalysisRequest completed = budgeted_request("stop-reasons-src", 1);
    std::future<VisualAnalysisResult> completed_future = host.submit(std::move(completed));
    VisualAnalysisResult completed_result;
    MIRAGE_CHECK(settle_within(completed_future, completed_result));
    MIRAGE_CHECK(completed_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(completed_result.ocr_batch.has_value());
    if (completed_result.ocr_batch.has_value()) {
        MIRAGE_CHECK(completed_result.ocr_batch->completion ==
                     mirador::OcrBatchCompletion::kComplete);
        MIRAGE_CHECK(completed_result.ocr_batch->stop_reason ==
                     mirador::OcrBatchStopReason::kCompleted);
        MIRAGE_CHECK(completed_result.ocr_batch->detected == 3);
        MIRAGE_CHECK(completed_result.ocr_batch->recognized == 3);
        MIRAGE_CHECK(completed_result.ocr_batch->deferred == 0);
        MIRAGE_CHECK(completed_result.text.size() == 3);
    }

    // kRecLineLimit: exactly max_rec_lines recognized, the rest deferred.
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{0.0F, 0.0F, 8.0F, 2.0F}, 0.9F, true, "a", 0.9F, 0),
         scripted_line(1, mirador::RectF{0.0F, 4.0F, 8.0F, 2.0F}, 0.8F, true, "b", 0.8F, 1),
         scripted_line(2, mirador::RectF{0.0F, 8.0F, 8.0F, 2.0F}, 0.7F, false, "", 0.0F, -1)},
        mirador::OcrBatchStopReason::kRecLineLimit));
    VisualAnalysisRequest line_limited = budgeted_request("stop-reasons-src", 2);
    line_limited.ocr_budget->max_rec_lines = 2;
    std::future<VisualAnalysisResult> line_limited_future = host.submit(std::move(line_limited));
    VisualAnalysisResult line_limited_result;
    MIRAGE_CHECK(settle_within(line_limited_future, line_limited_result));
    MIRAGE_CHECK(line_limited_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(line_limited_result.ocr_batch.has_value());
    if (line_limited_result.ocr_batch.has_value()) {
        MIRAGE_CHECK(line_limited_result.ocr_batch->completion ==
                     mirador::OcrBatchCompletion::kPartial);
        MIRAGE_CHECK(line_limited_result.ocr_batch->stop_reason ==
                     mirador::OcrBatchStopReason::kRecLineLimit);
        MIRAGE_CHECK(line_limited_result.ocr_batch->detected == 3);
        MIRAGE_CHECK(line_limited_result.ocr_batch->recognized == 2);
        MIRAGE_CHECK(line_limited_result.ocr_batch->deferred == 1);
    }

    // kSoftBudget: a caller soft budget stopped new work before the line limit.
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{0.0F, 0.0F, 8.0F, 2.0F}, 0.9F, true, "a", 0.9F, 0),
         scripted_line(1, mirador::RectF{0.0F, 4.0F, 8.0F, 2.0F}, 0.8F, false, "", 0.0F, -1),
         scripted_line(2, mirador::RectF{0.0F, 8.0F, 8.0F, 2.0F}, 0.7F, false, "", 0.0F, -1)},
        mirador::OcrBatchStopReason::kSoftBudget));
    VisualAnalysisRequest soft_stopped = budgeted_request("stop-reasons-src", 3);
    soft_stopped.ocr_budget->soft_budget = std::chrono::milliseconds{1};
    std::future<VisualAnalysisResult> soft_future = host.submit(std::move(soft_stopped));
    VisualAnalysisResult soft_result;
    MIRAGE_CHECK(settle_within(soft_future, soft_result));
    MIRAGE_CHECK(soft_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(soft_result.ocr_batch.has_value());
    if (soft_result.ocr_batch.has_value()) {
        MIRAGE_CHECK(soft_result.ocr_batch->completion == mirador::OcrBatchCompletion::kPartial);
        MIRAGE_CHECK(soft_result.ocr_batch->stop_reason ==
                     mirador::OcrBatchStopReason::kSoftBudget);
        MIRAGE_CHECK(soft_result.ocr_batch->detected == 3);
        MIRAGE_CHECK(soft_result.ocr_batch->recognized == 1);
        MIRAGE_CHECK(soft_result.ocr_batch->deferred == 2);
    }
    MIRAGE_CHECK(budgeted.calls() == 3);
    host.stop();
}

/// The remaining diagnostics fields: the backend's detection cap and its hit
/// flag pass through, and `geometry_space` follows the request's
/// `output_space` (kFrame here), so deferred bounds are queryable in the
/// declared space (numerically identity for a rotation-k0 capture).
void budgeted_diagnostics_map_det_limit_and_geometry_space(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{2.0F, 2.0F, 8.0F, 4.0F}, 0.9F, true, "kept", 0.9F, 0),
         scripted_line(1, mirador::RectF{4.0F, 8.0F, 8.0F, 4.0F}, 0.5F, false, "", 0.0F, -1)},
        mirador::OcrBatchStopReason::kSoftBudget, 128, true));
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "det-limit-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request = budgeted_request("det-limit-src", 1);
    request.ocr.output_space = mirador::CoordinateSpaceId::kFrame;
    request.ocr_budget->soft_budget = std::chrono::milliseconds{1};
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(result.ocr_batch.has_value());
    if (result.ocr_batch.has_value()) {
        MIRAGE_CHECK(result.ocr_batch->det_limit == 128);
        MIRAGE_CHECK(result.ocr_batch->det_limit_hit);
        MIRAGE_CHECK(result.ocr_batch->geometry_space == mirador::CoordinateSpaceId::kFrame);
        MIRAGE_CHECK(result.ocr_batch->deferred_bounds.size() == 1);
        if (result.ocr_batch->deferred_bounds.size() == 1) {
            MIRAGE_CHECK(
                (result.ocr_batch->deferred_bounds[0] == mirador::RectF{4.0F, 8.0F, 8.0F, 4.0F}));
        }
    }
    host.stop();
}

// ---- D. fail-closed backend selection -------------------------------------------------

/// A budgeted request without a configured budgeted backend fails explicitly
/// with kBackendUnavailable; the legacy backend configured on the same host is
/// never silently used as a fallback (that would promise all-or-nothing
/// semantics the caller opted out of).
void budgeted_request_without_backend_fails_closed(kairo::Executor &executor) {
    FakeOcrBackend legacy = bgra_ocr();
    VisualSessionConfig config;
    config.ocr_backend = &legacy; // legacy path stays available, but is not a fallback
    VisualSessionHost host(executor, "fail-closed-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request = budgeted_request("fail-closed-src", 1);
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBackendUnavailable);
    MIRAGE_CHECK(result.outcome.message.find("budgeted") != std::string::npos);
    MIRAGE_CHECK(result.text.empty());
    MIRAGE_CHECK(!result.ocr_batch.has_value());
    MIRAGE_CHECK(legacy.calls() == 0);
    host.stop();
}

/// Without `ocr_budget` the legacy path is untouched in both directions: with
/// both backends configured the legacy backend serves the request and no
/// budgeted diagnostics are produced; with only a budgeted backend configured
/// the legacy capability still fails with kBackendUnavailable (null-backend
/// contract, no budgeted substitution).
void legacy_ocr_path_unaffected_by_the_budgeted_config(kairo::Executor &executor) {
    FakeOcrBackend legacy = bgra_ocr();
    ScriptedBudgetedOcrBackend budgeted;
    VisualSessionConfig config;
    config.ocr_backend = &legacy;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "legacy-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest legacy_request = budgeted_request("legacy-src", 1);
    legacy_request.ocr_budget.reset(); // plain run_ocr
    std::future<VisualAnalysisResult> legacy_future = host.submit(std::move(legacy_request));
    VisualAnalysisResult legacy_result;
    MIRAGE_CHECK(settle_within(legacy_future, legacy_result));
    MIRAGE_CHECK(legacy_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(legacy_result.text.size() == 1);
    if (legacy_result.text.size() == 1) {
        MIRAGE_CHECK(legacy_result.text[0].utf8_text == "mirage-fake");
    }
    MIRAGE_CHECK(!legacy_result.ocr_batch.has_value()); // legacy runs carry no budget diagnostics
    MIRAGE_CHECK(legacy.calls() == 1);
    MIRAGE_CHECK(budgeted.calls() == 0);
    host.stop();

    // Budgeted-only configuration never serves the legacy capability.
    VisualSessionConfig budgeted_only;
    budgeted_only.budgeted_ocr_backend = &budgeted;
    VisualSessionHost budgeted_only_host(executor, "legacy-null-src", budgeted_only);
    std::string budgeted_only_error;
    MIRAGE_CHECK(budgeted_only_host.start(budgeted_only_error));

    VisualAnalysisRequest plain = budgeted_request("legacy-null-src", 1);
    plain.ocr_budget.reset();
    std::future<VisualAnalysisResult> plain_future = budgeted_only_host.submit(std::move(plain));
    VisualAnalysisResult plain_result;
    MIRAGE_CHECK(settle_within(plain_future, plain_result));
    MIRAGE_CHECK(plain_result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(plain_result.outcome.code == mirador::ErrorCode::kBackendUnavailable);
    MIRAGE_CHECK(budgeted.calls() == 0); // still never invoked
    budgeted_only_host.stop();
}

// ---- E. coexistence with the detection stage ------------------------------------------

/// A partial budgeted report is a per-stage fact: the detection stage of the
/// same request still runs and lands in `result.detections`, and the request
/// completes.
void budgeted_partial_coexists_with_detection(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{0.0F, 0.0F, 8.0F, 2.0F}, 0.9F, true, "kept", 0.9F, 0),
         scripted_line(1, mirador::RectF{0.0F, 4.0F, 8.0F, 2.0F}, 0.5F, false, "", 0.0F, -1)},
        mirador::OcrBatchStopReason::kSoftBudget));
    FakeDetectorBackend detector = bgra_detector();
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "partial-det-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request = budgeted_request("partial-det-src", 1);
    request.run_detector = true;
    request.ocr_budget->soft_budget = std::chrono::milliseconds{1};
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(result.text.size() == 1);
    MIRAGE_CHECK(result.detections.size() == 1);
    if (result.detections.size() == 1) {
        MIRAGE_CHECK(result.detections[0].label == "button");
    }
    MIRAGE_CHECK(result.ocr_batch.has_value());
    if (result.ocr_batch.has_value()) {
        MIRAGE_CHECK(result.ocr_batch->stop_reason == mirador::OcrBatchStopReason::kSoftBudget);
        MIRAGE_CHECK(result.ocr_batch->deferred == 1);
    }
    MIRAGE_CHECK(detector.calls() == 1);
    host.stop();
}

// ---- F. cache discipline ---------------------------------------------------------------

/// The budgeted entry never reads or writes the capability result cache
/// (AC8-08): two identical budgeted submissions of the same frame execute the
/// backend twice, unlike the legacy path whose reuse is proven by flat call
/// counters in visual_session_test.
void budgeted_requests_are_never_served_from_the_cache(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    budgeted.set_next_result(batch_report(
        {scripted_line(0, mirador::RectF{0.0F, 0.0F, 32.0F, 16.0F}, 0.8F, true, "again", 0.9F, 0)},
        mirador::OcrBatchStopReason::kCompleted));
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "no-cache-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    const mirador::Frame frame = session_frame("no-cache-src", 5);
    for (int round = 0; round < 2; ++round) {
        VisualAnalysisRequest request = budgeted_request("no-cache-src", 5);
        request.frame = frame;
        std::future<VisualAnalysisResult> future = host.submit(std::move(request));
        VisualAnalysisResult result;
        MIRAGE_CHECK(settle_within(future, result));
        MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    }
    MIRAGE_CHECK(budgeted.calls() == 2);
    host.stop();
}

// ---- G. failure propagation -------------------------------------------------------------

/// A backend failure keeps the whole call failed (never a partial success):
/// the host surfaces the backend's status code through the outcome.
void budgeted_backend_failure_fails_the_request(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend budgeted;
    budgeted.set_next_status(
        mirador::Status{mirador::ErrorCode::kBackendFailure, "scripted budgeted failure"});
    VisualSessionConfig config;
    config.budgeted_ocr_backend = &budgeted;
    VisualSessionHost host(executor, "failure-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request = budgeted_request("failure-src", 1);
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBackendFailure);
    MIRAGE_CHECK(result.text.empty());
    MIRAGE_CHECK(!result.ocr_batch.has_value());
    MIRAGE_CHECK(budgeted.calls() == 1); // executed, then failed instead of lying
    host.stop();
}

template <typename Scenario> void run_scenario(const char *name, Scenario scenario) {
    std::fprintf(stderr, "[visual_session_budgeted_test] scenario: %s\n", name);
    scenario();
}

void native_companion_preserves_partial_coverage(kairo::Executor &executor) {
    ScriptedBudgetedOcrBackend backend;
    backend.set_next_result(
        batch_report({scripted_line(0, {0, 0, 4, 4}, 0.9F, true, "fresh", 0.9F, 0),
                      scripted_line(1, {5, 0, 4, 4}, 0.9F, false, "", 0, -1)},
                     mirador::OcrBatchStopReason::kRecLineLimit));
    VisualSessionConfig config;
    config.enable_image_session = true;
    config.budgeted_ocr_backend = &backend;
    VisualSessionHost host(executor, "native-partial", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    auto request = budgeted_request("native-partial", 1);
    request.image_session = true;
    request.continuous = mirage::integration::VisualContinuousRequest{};
    request.continuous->source_revision = 1;
    request.fuse = true;
    request.analyze_change = true;
    request.ocr.cache_policy = mirador::CachePolicy::kRefresh;
    auto future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(result.ocr_batch && result.ocr_batch->deferred == 1);
    MIRAGE_CHECK(result.continuous && !result.continuous->accepted);
    MIRAGE_CHECK(result.continuous && result.continuous->temporal.confirmed_frame == 0);
    MIRAGE_CHECK(backend.calls() == 1);
    host.stop();
}

} // namespace

int main() {
    // Session scenarios share one default-configured Executor instance with
    // its own isolated ExecutorManager; every host owns a unique source id
    // (and therefore a unique lifetime-registered worker name) and stops
    // itself before the ordered executor shutdown below.
    kairo::Executor executor;
    const bool executor_ready = executor.initialize(kairo::ExecutorConfig{}).ok;
    MIRAGE_CHECK(executor_ready);
    if (executor_ready) {
        native_companion_preserves_partial_coverage(executor);
        run_scenario("budgeted_request_maps_pipeline_and_budget_fields",
                     [&] { budgeted_request_maps_pipeline_and_budget_fields(executor); });
        run_scenario("budgeted_request_applies_roi_and_max_side",
                     [&] { budgeted_request_applies_roi_and_max_side(executor); });
        run_scenario("budgeted_lines_split_into_text_and_deferred_bounds",
                     [&] { budgeted_lines_split_into_text_and_deferred_bounds(executor); });
        run_scenario("budgeted_diagnostics_map_the_stop_reasons",
                     [&] { budgeted_diagnostics_map_the_stop_reasons(executor); });
        run_scenario("budgeted_diagnostics_map_det_limit_and_geometry_space",
                     [&] { budgeted_diagnostics_map_det_limit_and_geometry_space(executor); });
        run_scenario("budgeted_request_without_backend_fails_closed",
                     [&] { budgeted_request_without_backend_fails_closed(executor); });
        run_scenario("legacy_ocr_path_unaffected_by_the_budgeted_config",
                     [&] { legacy_ocr_path_unaffected_by_the_budgeted_config(executor); });
        run_scenario("budgeted_partial_coexists_with_detection",
                     [&] { budgeted_partial_coexists_with_detection(executor); });
        run_scenario("budgeted_requests_are_never_served_from_the_cache",
                     [&] { budgeted_requests_are_never_served_from_the_cache(executor); });
        run_scenario("budgeted_backend_failure_fails_the_request",
                     [&] { budgeted_backend_failure_fails_the_request(executor); });
        executor.shutdown(true);
    }
    return mirage::testing::finish("visual_session_budgeted_test");
}
