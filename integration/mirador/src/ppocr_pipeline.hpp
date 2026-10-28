#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <mirage/integration/desktop_perception.hpp>
#include <mirage/integration/environment_visual_pipeline.hpp>

namespace kairo {
class Executor;
}

namespace mirage::integration {
// DEC-057: explicit verified resources; no model discovery/download or new owner.
class PpOcrPipeline final : public EnvironmentVisualPipeline {
  public:
    static bool supported();
    static std::unique_ptr<PpOcrPipeline>
    create(kairo::Executor &, desktop::ScreenProvider &, const std::filesystem::path &manifest,
           std::string &error, int experimental_recognition_refresh_ms = 0,
           bool experimental_object_tracking = false, bool experimental_async_recognition = false);
    ~PpOcrPipeline() override;
    bool running() const override;
    desktop::VisualReferenceRegistry &registry() override;
    EnvironmentVisualRefresh
    refresh(bool analyze, const desktop::CancelToken &,
            const std::optional<std::chrono::steady_clock::time_point> &deadline) override;
    EnvironmentVisualRefresh
    refresh_presented(bool analyze, const desktop::CancelToken &cancel,
                      const std::optional<std::chrono::steady_clock::time_point> &deadline,
                      const desktop::CapturePresentation &presentation) override;
    EnvironmentVisualRefresh mark(const std::optional<desktop::WindowGeometry> &,
                                  const desktop::CancelToken &,
                                  const std::optional<std::chrono::steady_clock::time_point> &,
                                  const desktop::CapturePresentation &presentation = {});
    /// DEC-063: owner-selected range with global OCR coordinates.
    EnvironmentVisualRefresh
    refresh_region(const desktop::WindowGeometry &, const desktop::CancelToken &,
                   const std::optional<std::chrono::steady_clock::time_point> &,
                   const desktop::CapturePresentation &presentation = {});
    ImageOcrOutcome read_image_ocr(const desktop::ImageFrame &, const desktop::CancelToken &,
                                   const std::optional<std::chrono::steady_clock::time_point> &,
                                   const std::string &source_key = {});
    ImageContinuousState
    query_image_state(const std::string &source_key, const desktop::CancelToken &cancel,
                      const std::optional<std::chrono::steady_clock::time_point> &deadline);
    /// DEC-125: captured_at and sampling_generation are supplied together.
    /// Nonzero generation selects real recognition on the original capture time;
    /// worker tracking must be disabled (the capture owner owns templates).
    /// DEC-126: work.roi selects raw unconfirmed capabilities, without fusion/coverage.
    PerceptionSample
    observe_image(const desktop::ImageFrame &, const std::string &,
                  const std::optional<desktop::WindowGeometry> &, const desktop::CancelToken &,
                  const std::optional<std::chrono::steady_clock::time_point> &,
                  std::optional<std::chrono::steady_clock::time_point> captured_at = {},
                  std::uint64_t sampling_generation = 0,
                  const PerceptionRecognitionWork &work = {});
    void stop();
    const std::string &model_revision() const;
    std::uint64_t backend_calls() const;
    std::uint64_t detector_calls() const;

  private:
    EnvironmentVisualRefresh
    refresh_impl(bool, bool, const std::optional<desktop::WindowGeometry> &,
                 const desktop::CancelToken &,
                 const std::optional<std::chrono::steady_clock::time_point> &,
                 const desktop::CapturePresentation &presentation = {});
    struct Impl;
    explicit PpOcrPipeline(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::integration
