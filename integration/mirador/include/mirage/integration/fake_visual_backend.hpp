#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

#include <mirador/backend_info.hpp>
#include <mirador/detector_backend.hpp>
#include <mirador/execution_context.hpp>
#include <mirador/ocr_backend.hpp>
#include <mirador/pixel_format.hpp>
#include <mirador/result.hpp>

namespace mirage::integration {

/// Deterministic, model-free OCR backend standing in for real weights
/// (DEC-016 decision 5). Identity semantics: output derives only from the
/// configuration and the prepared view's geometry — never from pixel
/// content — so equal inputs produce equal results (mirador cache
/// correctness) and headless runs are reproducible.
///
/// The backend exercises every SPI obligation the pinned mirador contract
/// declares, including the negative paths: an identity that fails
/// `mirador::validate` reports kBackendUnavailable, a prepared view outside
/// `info.accepted_formats` reports kUnsupportedFormat, an already-cancelled
/// or past-deadline context reports kCancelled / kTimeout before any side
/// effect (the call counter does not move), `request.min_confidence`
/// filters the output, and a result count above `max_regions` fails with
/// kBudgetExceeded instead of truncating.
struct FakeOcrConfig {
    /// Capability identity; must pass `mirador::validate`. `thread_safe`
    /// stays false: one call at a time, matching the session confinement.
    mirador::BackendInfo info{
        "mirage-fake-ocr", "1.0.0", "", "", {mirador::PixelFormat::kRgba8}, false};
    /// Regions returned per call, in prepared-image pixel space. When empty,
    /// one full-view region ("mirage-fake", confidence 0.9) is produced.
    std::vector<mirador::TextRegion> regions;
    /// Output budget (RULE-07): producing more regions than this fails the
    /// call with kBudgetExceeded.
    std::size_t max_regions = 64;
};

class FakeOcrBackend final : public mirador::OcrBackend {
  public:
    explicit FakeOcrBackend(FakeOcrConfig config = {});

    [[nodiscard]] mirador::BackendInfo info() const override;
    [[nodiscard]] mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &prepared_image, const mirador::OcrRequest &request,
              const mirador::ExecutionContext &context) override;

    /// Number of recognize() calls that actually executed (cancel/deadline
    /// and validation rejections never count). Cache hits are proven by this
    /// counter staying flat once the real pipeline feeds the backend.
    [[nodiscard]] std::size_t calls() const noexcept { return calls_; }

  private:
    FakeOcrConfig config_;
    std::atomic<std::size_t> calls_{0};
};

/// Deterministic, model-free detection backend; same contract and negative
/// semantics as FakeOcrBackend (DEC-016 decision 5).
struct FakeDetectorConfig {
    mirador::BackendInfo info{"mirage-fake-detector",         "1.0.0", "", "",
                              {mirador::PixelFormat::kRgba8}, false};
    /// Boxes returned per call, in prepared-image pixel space. When empty,
    /// one full-view box (label "button", confidence 0.8) is produced.
    std::vector<mirador::DetectionRegion> regions;
    std::size_t max_regions = 64;
};

class FakeDetectorBackend final : public mirador::DetectorBackend {
  public:
    explicit FakeDetectorBackend(FakeDetectorConfig config = {});

    [[nodiscard]] mirador::BackendInfo info() const override;
    [[nodiscard]] mirador::Result<std::vector<mirador::DetectionRegion>>
    detect(const mirador::ImageView &prepared_image, const mirador::DetectionRequest &request,
           const mirador::ExecutionContext &context) override;

    [[nodiscard]] std::size_t calls() const noexcept { return calls_; }

  private:
    FakeDetectorConfig config_;
    std::atomic<std::size_t> calls_{0};
};

} // namespace mirage::integration
