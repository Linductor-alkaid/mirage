#include <mirage/integration/fake_visual_backend.hpp>

#include <mirador/backend_info.hpp>
#include <mirador/status.hpp>

#include <algorithm>
#include <optional>
#include <utility>

namespace mirage::integration {
namespace {

/// SPI obligations that precede any produced output, shared by both fakes
/// (DEC-016 decision 5). Returns nullopt when the call may proceed;
/// otherwise the Status the call must return.
template <typename Formats>
std::optional<mirador::Status> gate(const mirador::BackendInfo &info, const Formats &formats,
                                    const mirador::ImageView &prepared_image,
                                    const mirador::ExecutionContext &context) {
    if (!mirador::validate(info).ok()) {
        // A malformed capability query means the backend cannot be used at
        // all (mirador BackendInfo contract).
        return mirador::Status{mirador::ErrorCode::kBackendUnavailable,
                               "fake backend identity fails validation"};
    }
    if (mirador::is_cancelled(context)) {
        // Cancellation is observed before any side effect: the call counter
        // stays flat and no result is produced.
        return mirador::Status{mirador::ErrorCode::kCancelled, "cancelled before execution"};
    }
    if (mirador::deadline_reached(context)) {
        return mirador::Status{mirador::ErrorCode::kTimeout, "deadline reached before execution"};
    }
    const bool format_accepted = std::any_of(
        formats.begin(), formats.end(),
        [fmt = prepared_image.format](mirador::PixelFormat accepted) { return accepted == fmt; });
    if (!format_accepted) {
        return mirador::Status{mirador::ErrorCode::kUnsupportedFormat,
                               "prepared view format is not in the accepted set"};
    }
    return std::nullopt;
}

} // namespace

FakeOcrBackend::FakeOcrBackend(FakeOcrConfig config) : config_(std::move(config)) {}

mirador::BackendInfo FakeOcrBackend::info() const { return config_.info; }

mirador::Result<std::vector<mirador::TextRegion>>
FakeOcrBackend::recognize(const mirador::ImageView &prepared_image,
                          const mirador::OcrRequest &request,
                          const mirador::ExecutionContext &context) {
    if (const std::optional<mirador::Status> rejection =
            gate(config_.info, config_.info.accepted_formats, prepared_image, context)) {
        return *rejection;
    }
    calls_.fetch_add(1, std::memory_order_relaxed);

    std::vector<mirador::TextRegion> regions;
    if (config_.regions.empty()) {
        // Identity default: one full-view region derived from the view
        // geometry only.
        mirador::TextRegion region;
        region.bounds = mirador::RectF{0.0F, 0.0F, static_cast<float>(prepared_image.width),
                                       static_cast<float>(prepared_image.height)};
        region.utf8_text = "mirage-fake";
        region.confidence = 0.9F;
        regions.push_back(std::move(region));
    } else {
        regions = config_.regions;
    }

    if (regions.size() > config_.max_regions) {
        // Budgets are errors, never silent truncation (RULE-07).
        return mirador::Status{mirador::ErrorCode::kBudgetExceeded,
                               "configured output exceeds the region budget"};
    }

    std::vector<mirador::TextRegion> accepted;
    accepted.reserve(regions.size());
    for (mirador::TextRegion &region : regions) {
        if (region.confidence >= request.min_confidence) {
            accepted.push_back(std::move(region));
        }
    }
    return accepted;
}

FakeDetectorBackend::FakeDetectorBackend(FakeDetectorConfig config) : config_(std::move(config)) {}

mirador::BackendInfo FakeDetectorBackend::info() const { return config_.info; }

mirador::Result<std::vector<mirador::DetectionRegion>>
FakeDetectorBackend::detect(const mirador::ImageView &prepared_image,
                            const mirador::DetectionRequest &request,
                            const mirador::ExecutionContext &context) {
    if (const std::optional<mirador::Status> rejection =
            gate(config_.info, config_.info.accepted_formats, prepared_image, context)) {
        return *rejection;
    }
    calls_.fetch_add(1, std::memory_order_relaxed);

    std::vector<mirador::DetectionRegion> regions;
    if (config_.regions.empty()) {
        mirador::DetectionRegion region;
        region.bounds = mirador::RectF{0.0F, 0.0F, static_cast<float>(prepared_image.width),
                                       static_cast<float>(prepared_image.height)};
        region.class_id = 0;
        region.label = "button";
        region.confidence = 0.8F;
        regions.push_back(std::move(region));
    } else {
        regions = config_.regions;
    }

    if (regions.size() > config_.max_regions) {
        return mirador::Status{mirador::ErrorCode::kBudgetExceeded,
                               "configured output exceeds the region budget"};
    }

    std::vector<mirador::DetectionRegion> accepted;
    accepted.reserve(regions.size());
    for (mirador::DetectionRegion &region : regions) {
        if (region.confidence >= request.min_confidence) {
            accepted.push_back(std::move(region));
        }
    }
    return accepted;
}

} // namespace mirage::integration
