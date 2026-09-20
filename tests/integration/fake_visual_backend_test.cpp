// M3-01 fake visual backend tests (DEC-016 decision 5): the deterministic,
// model-free OCR and detection stand-ins must honor every SPI obligation the
// pinned mirador contract declares. The shared gate is judged in order for
// every call — validation failure (kBackendUnavailable), cancellation
// (kCancelled), deadline (kTimeout), unaccepted format (kUnsupportedFormat)
// — and each rejection happens before any side effect (the call counter
// stays flat). Calls that pass the gate count, produce geometry-only
// identity output when unconfigured, fail with kBudgetExceeded instead of
// truncating, honor request.min_confidence, and are deterministic for equal
// inputs.

#include "../support/test.hpp"

#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/mirador_service.hpp>

#include <mirador/status.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

using mirage::integration::FakeDetectorBackend;
using mirage::integration::FakeDetectorConfig;
using mirage::integration::FakeOcrBackend;
using mirage::integration::FakeOcrConfig;
using mirage::integration::VisualBackendIdentity;

/// An owning RGBA8 buffer plus the non-owning view over it. The fake derives
/// output from geometry only, so the pixel content is a constant pattern.
struct PreparedImage {
    std::vector<std::byte> pixels;
    mirador::ImageView view;
};

PreparedImage make_image(std::int32_t width, std::int32_t height, mirador::PixelFormat format) {
    PreparedImage image;
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    image.pixels.assign(stride * static_cast<std::size_t>(height), std::byte{0x7F});
    image.view.data = image.pixels.data();
    image.view.width = width;
    image.view.height = height;
    image.view.row_stride_bytes = static_cast<std::int64_t>(stride);
    image.view.format = format;
    return image;
}

mirador::ExecutionContext active_context() { return {}; }

mirador::ExecutionContext cancelled_context() {
    mirador::ExecutionContext context;
    context.is_cancelled = [] { return true; };
    return context;
}

mirador::ExecutionContext expired_deadline_context() {
    mirador::ExecutionContext context;
    context.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    return context;
}

template <typename T> bool reports(const mirador::Result<T> &result, mirador::ErrorCode code) {
    return !result.ok() && result.status().code() == code;
}

mirador::TextRegion text_region(float x, float y, float width, float height, std::string text,
                                float confidence) {
    mirador::TextRegion region;
    region.bounds = mirador::RectF{x, y, width, height};
    region.utf8_text = std::move(text);
    region.confidence = confidence;
    return region;
}

mirador::DetectionRegion detection_region(float x, float y, float width, float height,
                                          std::int32_t class_id, std::string label,
                                          float confidence) {
    mirador::DetectionRegion region;
    region.bounds = mirador::RectF{x, y, width, height};
    region.class_id = class_id;
    region.label = std::move(label);
    region.confidence = confidence;
    return region;
}

// ---- OCR backend ---------------------------------------------------------------

void ocr_reports_configured_identity() {
    FakeOcrBackend backend;
    const mirador::BackendInfo info = backend.info();
    MIRAGE_CHECK(info.name == "mirage-fake-ocr");
    MIRAGE_CHECK(info.implementation_version == "1.0.0");
    MIRAGE_CHECK(info.accepted_formats.size() == 1);
    MIRAGE_CHECK(info.accepted_formats[0] == mirador::PixelFormat::kRgba8);
}

void ocr_default_region_covers_the_whole_view() {
    FakeOcrBackend backend;
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgba8);
    const auto result = backend.recognize(image.view, mirador::OcrRequest{}, active_context());

    MIRAGE_CHECK(result.ok());
    MIRAGE_CHECK(backend.calls() == 1);
    if (result.ok()) {
        const std::vector<mirador::TextRegion> &regions = result.value();
        MIRAGE_CHECK(regions.size() == 1);
        if (regions.size() == 1) {
            const mirador::RectF full_view{0.0F, 0.0F, 4.0F, 2.0F};
            MIRAGE_CHECK(regions[0].bounds == full_view);
            MIRAGE_CHECK(regions[0].utf8_text == "mirage-fake");
            MIRAGE_CHECK(regions[0].confidence == 0.9F);
            MIRAGE_CHECK(regions[0].polygon.empty());
        }
    }
}

void ocr_invalid_identity_reports_backend_unavailable_without_side_effects() {
    // An empty name fails mirador::validate.
    FakeOcrConfig unnamed;
    unnamed.info.name = "";
    FakeOcrBackend unnamed_backend(unnamed);
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgba8);
    const auto unnamed_result =
        unnamed_backend.recognize(image.view, mirador::OcrRequest{}, active_context());
    MIRAGE_CHECK(reports(unnamed_result, mirador::ErrorCode::kBackendUnavailable));
    MIRAGE_CHECK(unnamed_backend.calls() == 0);

    // So does an empty accepted-format list.
    FakeOcrConfig formatless;
    formatless.info.accepted_formats.clear();
    FakeOcrBackend formatless_backend(formatless);
    const auto formatless_result =
        formatless_backend.recognize(image.view, mirador::OcrRequest{}, active_context());
    MIRAGE_CHECK(reports(formatless_result, mirador::ErrorCode::kBackendUnavailable));
    MIRAGE_CHECK(formatless_backend.calls() == 0);
}

void ocr_cancelled_context_reports_cancelled_without_side_effects() {
    FakeOcrBackend backend;
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgba8);
    const auto result = backend.recognize(image.view, mirador::OcrRequest{}, cancelled_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kCancelled));
    MIRAGE_CHECK(backend.calls() == 0);
}

void ocr_expired_deadline_reports_timeout_without_side_effects() {
    FakeOcrBackend backend;
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgba8);
    const auto result =
        backend.recognize(image.view, mirador::OcrRequest{}, expired_deadline_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kTimeout));
    MIRAGE_CHECK(backend.calls() == 0);
}

void ocr_cancelled_beats_expired_deadline() {
    // The gate judges in order: cancellation is observed before the deadline.
    FakeOcrBackend backend;
    mirador::ExecutionContext context = cancelled_context();
    context.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgba8);
    const auto result = backend.recognize(image.view, mirador::OcrRequest{}, context);
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kCancelled));
    MIRAGE_CHECK(backend.calls() == 0);
}

void ocr_unaccepted_format_reports_unsupported_without_side_effects() {
    FakeOcrBackend backend; // accepts kRgba8 only
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgb8);
    const auto result = backend.recognize(image.view, mirador::OcrRequest{}, active_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kUnsupportedFormat));
    MIRAGE_CHECK(backend.calls() == 0);
}

void ocr_budget_exceeded_fails_without_truncating() {
    FakeOcrConfig config;
    config.max_regions = 4;
    for (int index = 0; index < 5; ++index) {
        config.regions.push_back(text_region(0.0F, static_cast<float>(index), 1.0F, 1.0F,
                                             "r" + std::to_string(index), 0.5F));
    }
    FakeOcrBackend backend(config);
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgba8);
    const auto result = backend.recognize(image.view, mirador::OcrRequest{}, active_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kBudgetExceeded));
    // The call itself executed (the gate passed); the budget failure is the
    // outcome, not a silent truncation to 4 regions.
    MIRAGE_CHECK(backend.calls() == 1);

    // The exact boundary succeeds.
    FakeOcrConfig exact = config;
    exact.max_regions = 5;
    FakeOcrBackend exact_backend(exact);
    const auto exact_result =
        exact_backend.recognize(image.view, mirador::OcrRequest{}, active_context());
    MIRAGE_CHECK(exact_result.ok());
    if (exact_result.ok()) {
        MIRAGE_CHECK(exact_result.value().size() == 5);
    }
}

void ocr_min_confidence_filters_regions() {
    FakeOcrConfig config;
    config.regions.push_back(text_region(0.0F, 0.0F, 1.0F, 1.0F, "low", 0.5F));
    config.regions.push_back(text_region(1.0F, 0.0F, 1.0F, 1.0F, "high", 0.9F));
    config.regions.push_back(text_region(2.0F, 0.0F, 1.0F, 1.0F, "edge", 0.7F));
    FakeOcrBackend backend(config);

    mirador::OcrRequest request;
    request.min_confidence = 0.7F; // the "edge" region stays: >=, not >
    const PreparedImage image = make_image(4, 2, mirador::PixelFormat::kRgba8);
    const auto result = backend.recognize(image.view, request, active_context());

    MIRAGE_CHECK(result.ok());
    if (result.ok()) {
        const std::vector<mirador::TextRegion> &regions = result.value();
        MIRAGE_CHECK(regions.size() == 2);
        if (regions.size() == 2) {
            MIRAGE_CHECK(regions[0].utf8_text == "high");
            MIRAGE_CHECK(regions[0].confidence == 0.9F);
            MIRAGE_CHECK(regions[1].utf8_text == "edge");
            MIRAGE_CHECK(regions[1].confidence == 0.7F);
        }
    }
}

void ocr_equal_inputs_produce_equal_outputs() {
    FakeOcrConfig config;
    config.regions.push_back(text_region(0.0F, 0.0F, 2.0F, 1.0F, "stable", 0.6F));
    FakeOcrBackend backend(config);
    const PreparedImage image = make_image(8, 4, mirador::PixelFormat::kRgba8);
    const mirador::OcrRequest request;

    const auto first = backend.recognize(image.view, request, active_context());
    const auto second = backend.recognize(image.view, request, active_context());
    MIRAGE_CHECK(first.ok());
    MIRAGE_CHECK(second.ok());
    MIRAGE_CHECK(first.value() == second.value());
    MIRAGE_CHECK(backend.calls() == 2);
}

// ---- detection backend -----------------------------------------------------------

void detector_reports_configured_identity() {
    FakeDetectorBackend backend;
    const mirador::BackendInfo info = backend.info();
    MIRAGE_CHECK(info.name == "mirage-fake-detector");
    MIRAGE_CHECK(info.implementation_version == "1.0.0");
    MIRAGE_CHECK(info.accepted_formats.size() == 1);
    MIRAGE_CHECK(info.accepted_formats[0] == mirador::PixelFormat::kRgba8);
}

void detector_default_region_covers_the_whole_view() {
    FakeDetectorBackend backend;
    const PreparedImage image = make_image(6, 3, mirador::PixelFormat::kRgba8);
    const auto result = backend.detect(image.view, mirador::DetectionRequest{}, active_context());

    MIRAGE_CHECK(result.ok());
    MIRAGE_CHECK(backend.calls() == 1);
    if (result.ok()) {
        const std::vector<mirador::DetectionRegion> &regions = result.value();
        MIRAGE_CHECK(regions.size() == 1);
        if (regions.size() == 1) {
            const mirador::RectF full_view{0.0F, 0.0F, 6.0F, 3.0F};
            MIRAGE_CHECK(regions[0].bounds == full_view);
            MIRAGE_CHECK(regions[0].class_id == 0);
            MIRAGE_CHECK(regions[0].label == "button");
            MIRAGE_CHECK(regions[0].confidence == 0.8F);
        }
    }
}

void detector_invalid_identity_reports_backend_unavailable_without_side_effects() {
    FakeDetectorConfig formatless;
    formatless.info.accepted_formats.clear();
    FakeDetectorBackend backend(formatless);
    const PreparedImage image = make_image(6, 3, mirador::PixelFormat::kRgba8);
    const auto result = backend.detect(image.view, mirador::DetectionRequest{}, active_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kBackendUnavailable));
    MIRAGE_CHECK(backend.calls() == 0);
}

void detector_cancelled_context_reports_cancelled_without_side_effects() {
    FakeDetectorBackend backend;
    const PreparedImage image = make_image(6, 3, mirador::PixelFormat::kRgba8);
    const auto result =
        backend.detect(image.view, mirador::DetectionRequest{}, cancelled_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kCancelled));
    MIRAGE_CHECK(backend.calls() == 0);
}

void detector_expired_deadline_reports_timeout_without_side_effects() {
    FakeDetectorBackend backend;
    const PreparedImage image = make_image(6, 3, mirador::PixelFormat::kRgba8);
    const auto result =
        backend.detect(image.view, mirador::DetectionRequest{}, expired_deadline_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kTimeout));
    MIRAGE_CHECK(backend.calls() == 0);
}

void detector_unaccepted_format_reports_unsupported_without_side_effects() {
    FakeDetectorBackend backend; // accepts kRgba8 only
    const PreparedImage image = make_image(6, 3, mirador::PixelFormat::kRgb8);
    const auto result = backend.detect(image.view, mirador::DetectionRequest{}, active_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kUnsupportedFormat));
    MIRAGE_CHECK(backend.calls() == 0);
}

void detector_budget_exceeded_fails_without_truncating() {
    FakeDetectorConfig config;
    config.max_regions = 2;
    config.regions.push_back(detection_region(0.0F, 0.0F, 1.0F, 1.0F, 0, "one", 0.9F));
    config.regions.push_back(detection_region(1.0F, 0.0F, 1.0F, 1.0F, 1, "two", 0.8F));
    config.regions.push_back(detection_region(2.0F, 0.0F, 1.0F, 1.0F, 2, "three", 0.7F));
    FakeDetectorBackend backend(config);
    const PreparedImage image = make_image(6, 3, mirador::PixelFormat::kRgba8);
    const auto result = backend.detect(image.view, mirador::DetectionRequest{}, active_context());
    MIRAGE_CHECK(reports(result, mirador::ErrorCode::kBudgetExceeded));
    MIRAGE_CHECK(backend.calls() == 1);

    // The exact boundary succeeds.
    FakeDetectorConfig exact = config;
    exact.max_regions = 3;
    FakeDetectorBackend exact_backend(exact);
    const auto exact_result =
        exact_backend.detect(image.view, mirador::DetectionRequest{}, active_context());
    MIRAGE_CHECK(exact_result.ok());
    if (exact_result.ok()) {
        MIRAGE_CHECK(exact_result.value().size() == 3);
    }
}

void detector_min_confidence_filters_regions() {
    FakeDetectorConfig config;
    config.regions.push_back(detection_region(0.0F, 0.0F, 1.0F, 1.0F, 0, "kept-high", 0.95F));
    config.regions.push_back(detection_region(1.0F, 0.0F, 1.0F, 1.0F, 1, "kept-edge", 0.5F));
    config.regions.push_back(detection_region(2.0F, 0.0F, 1.0F, 1.0F, 2, "dropped", 0.4F));
    FakeDetectorBackend backend(config);

    mirador::DetectionRequest request;
    request.min_confidence = 0.5F;
    const PreparedImage image = make_image(6, 3, mirador::PixelFormat::kRgba8);
    const auto result = backend.detect(image.view, request, active_context());

    MIRAGE_CHECK(result.ok());
    if (result.ok()) {
        const std::vector<mirador::DetectionRegion> &regions = result.value();
        MIRAGE_CHECK(regions.size() == 2);
        if (regions.size() == 2) {
            MIRAGE_CHECK(regions[0].label == "kept-high");
            MIRAGE_CHECK(regions[1].label == "kept-edge");
            MIRAGE_CHECK(regions[1].confidence == 0.5F);
        }
    }
}

void detector_equal_inputs_produce_equal_outputs() {
    FakeDetectorConfig config;
    config.regions.push_back(detection_region(0.0F, 0.0F, 2.0F, 2.0F, 3, "stable", 0.75F));
    FakeDetectorBackend backend(config);
    const PreparedImage image = make_image(8, 4, mirador::PixelFormat::kRgba8);
    const mirador::DetectionRequest request;

    const auto first = backend.detect(image.view, request, active_context());
    const auto second = backend.detect(image.view, request, active_context());
    MIRAGE_CHECK(first.ok());
    MIRAGE_CHECK(second.ok());
    MIRAGE_CHECK(first.value() == second.value());
    MIRAGE_CHECK(backend.calls() == 2);
}

// ---- Mirage-side identity translation ----------------------------------------------

void validate_backend_identity_accepts_a_known_format() {
    const VisualBackendIdentity identity{"mirage-ocr", "1.0.0", "", "", {"bgra8"}};
    MIRAGE_CHECK(mirage::integration::validate_backend_identity(identity));
}

void validate_backend_identity_rejects_an_unknown_format() {
    const VisualBackendIdentity identity{"mirage-ocr", "1.0.0", "", "", {"yuv420"}};
    MIRAGE_CHECK(!mirage::integration::validate_backend_identity(identity));
}

} // namespace

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[fake_visual_backend_test] scenario: %s\n", name);
    scenario();
}

int main() {
    run_scenario("ocr_reports_configured_identity", ocr_reports_configured_identity);
    run_scenario("ocr_default_region_covers_the_whole_view",
                 ocr_default_region_covers_the_whole_view);
    run_scenario("ocr_invalid_identity_reports_backend_unavailable_without_side_effects",
                 ocr_invalid_identity_reports_backend_unavailable_without_side_effects);
    run_scenario("ocr_cancelled_context_reports_cancelled_without_side_effects",
                 ocr_cancelled_context_reports_cancelled_without_side_effects);
    run_scenario("ocr_expired_deadline_reports_timeout_without_side_effects",
                 ocr_expired_deadline_reports_timeout_without_side_effects);
    run_scenario("ocr_cancelled_beats_expired_deadline", ocr_cancelled_beats_expired_deadline);
    run_scenario("ocr_unaccepted_format_reports_unsupported_without_side_effects",
                 ocr_unaccepted_format_reports_unsupported_without_side_effects);
    run_scenario("ocr_budget_exceeded_fails_without_truncating",
                 ocr_budget_exceeded_fails_without_truncating);
    run_scenario("ocr_min_confidence_filters_regions", ocr_min_confidence_filters_regions);
    run_scenario("ocr_equal_inputs_produce_equal_outputs", ocr_equal_inputs_produce_equal_outputs);
    run_scenario("detector_reports_configured_identity", detector_reports_configured_identity);
    run_scenario("detector_default_region_covers_the_whole_view",
                 detector_default_region_covers_the_whole_view);
    run_scenario("detector_invalid_identity_reports_backend_unavailable_without_side_effects",
                 detector_invalid_identity_reports_backend_unavailable_without_side_effects);
    run_scenario("detector_cancelled_context_reports_cancelled_without_side_effects",
                 detector_cancelled_context_reports_cancelled_without_side_effects);
    run_scenario("detector_expired_deadline_reports_timeout_without_side_effects",
                 detector_expired_deadline_reports_timeout_without_side_effects);
    run_scenario("detector_unaccepted_format_reports_unsupported_without_side_effects",
                 detector_unaccepted_format_reports_unsupported_without_side_effects);
    run_scenario("detector_budget_exceeded_fails_without_truncating",
                 detector_budget_exceeded_fails_without_truncating);
    run_scenario("detector_min_confidence_filters_regions",
                 detector_min_confidence_filters_regions);
    run_scenario("detector_equal_inputs_produce_equal_outputs",
                 detector_equal_inputs_produce_equal_outputs);
    run_scenario("validate_backend_identity_accepts_a_known_format",
                 validate_backend_identity_accepts_a_known_format);
    run_scenario("validate_backend_identity_rejects_an_unknown_format",
                 validate_backend_identity_rejects_an_unknown_format);
    return mirage::testing::finish("fake_visual_backend_test");
}
