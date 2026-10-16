#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/screen_provider.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>

#include <mirage/integration/environment_visual_pipeline.hpp>
#include <mirage/integration/visual_session_host.hpp>

namespace mirage::integration {

/// Configuration of one visual observation pipeline (plan item `M3-05`):
/// the Mirage-side wiring that turns "observe with perception" into
/// capture -> per-source session analysis -> registry publication
/// (design doc section 8, DEC-016 decisions 3 and 4).
struct VisualObservationPipelineConfig {
    /// Identity of the single image source; names both the perception
    /// session and its Executor blocking worker. Must be non-empty.
    std::string source_id = "mirage.desktop.screen";
    /// Session budgets and backends; null backends make the corresponding
    /// stage fail with kBackendUnavailable when requested (mirador
    /// contract), so only stages with a live backend should be enabled.
    VisualSessionConfig session;
    /// Budget of one display capture (RULE-07).
    desktop::CaptureLimits capture_limits{};
    /// Display to capture; empty captures the primary display (or the first
    /// reported one when no display is flagged primary).
    std::string display_id;
    /// Analysis stages of a perception refresh. Fusion always runs (it is
    /// the point of the pipeline); OCR / detection only make sense with a
    /// backend configured.
    bool analyze_change = true;
    bool run_ocr = false;
    bool run_detector = false;
    /// DEC-097: optional model proposals only for explicit marked observations.
    bool detector_marks = false;
    float detector_min_confidence = 0.25F;
    /// Bypass approximate OCR result reuse through the backend's public Refresh
    /// policy; product OCR enables this for text freshness (DEC-057).
    bool refresh_ocr = false;
    /// DEC-060: semantics-free closed-region candidates only for marked refreshes.
    /// Disabled by default; ordinary OCR and postconditions keep their original path.
    bool geometry_marks = false;
    /// DEC-063: capture explicit mark crops before analysis. Default preserves
    /// whole-display analysis and subsequent crop numbering.
    bool roi_capture = false;
};

/// Concrete EnvironmentVisualPipeline over one ScreenProvider and one
/// VisualSessionHost (plan item `M3-05`): each perception refresh captures
/// the configured display or explicit ROI, hands the frame to the session's serialized
/// Executor blocking worker (DEC-016 decision 4 — the analysis never runs
/// on the caller's thread), and publishes the fused kDisplay snapshot into
/// the registry. The registry is shared by design: the binding observes
/// what a refresh published, and the harness-side action path resolves
/// `@vN` references against the same generation.
///
/// The display transform (kOriented -> kDisplay) is built from the captured
/// pixel area's placement on every refresh and passed even when it is the
/// identity — the fused bounds are global desktop coordinates exactly
/// because of it (DEC-016 decision 3).
///
/// refresh() may be called from any thread; concurrent refreshes serialize
/// through the session host's latest-wins semantics (a newer analysis
/// supersedes an in-flight one, explicit settlement, RULE-10). start()/
/// stop() bracket the host's life; the host is one-shot, like its
/// VisualSessionHost.
class VisualObservationPipeline final : public EnvironmentVisualPipeline {
  public:
    /// `executor` must be initialized and outlive the pipeline (EXEC-01:
    /// the process-wide executor stays with its external owner); `screen`
    /// and `registry` must outlive the pipeline.
    VisualObservationPipeline(kairo::Executor &executor, desktop::ScreenProvider &screen,
                              desktop::VisualReferenceRegistry &registry,
                              VisualObservationPipelineConfig config = {});
    ~VisualObservationPipeline() override;
    VisualObservationPipeline(const VisualObservationPipeline &) = delete;
    VisualObservationPipeline &operator=(const VisualObservationPipeline &) = delete;

    /// Creates the perception session and starts its blocking worker. Fails
    /// closed with `error` set on any refusal; idempotent rejection after a
    /// start or stop (VisualSessionHost contract).
    [[nodiscard]] bool start(std::string &error);

    /// Stops accepting refresh analyses, cancels the in-flight one, and
    /// joins the worker. Safe from any thread except a worker callback.
    void stop();

    [[nodiscard]] bool running() const override;
    [[nodiscard]] desktop::VisualReferenceRegistry &registry() override;

    EnvironmentVisualRefresh
    refresh(bool analyze, const desktop::CancelToken &cancel,
            const std::optional<std::chrono::steady_clock::time_point> &deadline) override;

    /// Analyze an already-authorized <=8 MiB preview on the existing worker,
    /// without display enumeration or registry publication (DEC-075).
    ImageOcrOutcome read_image_ocr(const desktop::ImageFrame &, const desktop::CancelToken &,
                                   const std::optional<std::chrono::steady_clock::time_point> &);

    EnvironmentVisualRefresh
    refresh_marked(const std::optional<desktop::WindowGeometry> &crop,
                   const desktop::CancelToken &cancel,
                   const std::optional<std::chrono::steady_clock::time_point> &deadline);
    /// Owner-selected ROI observation; never falls back to a display.
    EnvironmentVisualRefresh
    refresh_region(const desktop::WindowGeometry &, bool analyze, const desktop::CancelToken &,
                   const std::optional<std::chrono::steady_clock::time_point> &deadline);

  private:
    EnvironmentVisualRefresh
    refresh_impl(bool analyze, bool marks, const std::optional<desktop::WindowGeometry> &crop,
                 const desktop::CancelToken &cancel,
                 const std::optional<std::chrono::steady_clock::time_point> &deadline);
    kairo::Executor &executor_;
    desktop::ScreenProvider &screen_;
    desktop::VisualReferenceRegistry &registry_;
    VisualObservationPipelineConfig config_;
    VisualSessionHost session_;
    /// Guards the frame sequence counter across concurrent refresh callers.
    std::mutex sequence_mutex_;
    std::uint64_t sequence_ = 0;
};

} // namespace mirage::integration
