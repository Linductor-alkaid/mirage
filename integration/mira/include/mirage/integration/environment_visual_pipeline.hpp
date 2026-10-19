#pragma once

#include <chrono>
#include <optional>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/observation_assembler.hpp>
#include <mirage/desktop/screen_provider.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>

namespace mirage::integration {

/// Result of one visual refresh driven through an EnvironmentVisualPipeline
/// (plan item `M3-05`). The two stages are reported independently so a
/// caller asking for both the pinned `screen` component and perception
/// evidence keeps what succeeded instead of losing the captured frame to an
/// analysis failure — required components still fail the whole request at
/// the binding, optional ones degrade per component.
struct VisualMark {
    std::uint32_t number = 0;
    std::string ref;
    desktop::PointerState anchor;
    bool selectable = false; ///< Whole region visible; no interactive semantics.
};

struct EnvironmentVisualRefresh {
    /// Capture stage (ScreenProvider capture of the display or explicit ROI).
    std::optional<bool> presentation_excluded = std::nullopt;
    bool captured = false;
    bool capture_cancelled = false;
    desktop::ProviderError capture_error; ///< meaningful when !captured && !capture_cancelled
    /// Identifier of the captured display as reported by the screen
    /// provider; meaningful only when captured.
    std::string display_id;
    /// The captured frame; meaningful only when captured.
    desktop::ImageFrame frame;
    desktop::WindowGeometry display_geometry, image_geometry;
    /// DEC-063: frame contains exactly this global ROI when set; otherwise
    /// the entire display. Publication retains the physical display identity.
    std::optional<desktop::WindowGeometry> capture_roi;
    bool display_primary = false;
    desktop::ImageFrame marked_frame;
    std::vector<VisualMark> marks;
    std::uint64_t visual_generation = 0, frame_sequence = 0;

    /// Analysis stage (session analysis + registry publication); only run
    /// when the caller asks for perception evidence. `analyzed` implies a
    /// fresh generation was published into the registry.
    bool analyzed = false;
    bool analysis_cancelled = false;
    /// Publication outcome; meaningful only when analyzed.
    desktop::VisualPublishOutcome publish;
    desktop::ProviderError
        analysis_error; ///< meaningful when analysis ran && !analyzed && !analysis_cancelled
};

/// DEC-075: read-only OCR in the supplied image's pixel space. These are not
/// global desktop coordinates or executable references.
struct ImageTextRegion {
    std::string text;
    double confidence = 0, x = 0, y = 0, width = 0, height = 0;
};
struct ImageOcrOutcome {
    bool ok = false, cancelled = false;
    desktop::ProviderError error;
    std::vector<ImageTextRegion> regions;
    std::string model_revision;
    std::uint64_t backend_calls = 0;
    /// DEC-076: count before the native adapter's 128-region prefix.
    /// Zero means the producer supplied its entire regions vector.
    std::uint64_t total_region_count = 0;
};

/// Pinned-free visual observation surface the MiraEnvironmentBinding drives
/// when an observe request asks for the `screen` component or perception
/// evidence (plan item `M3-05`, DEC-016 "能力如实上报" extension). It
/// abstracts the integration/mirador pipeline — capture from the screen
/// provider, per-source session analysis on its Executor blocking worker,
/// publication into the visual reference registry — without exposing pinned
/// mirador or executor types through the binding header (DEC-003).
///
/// Implementations must keep the analysis off the caller's thread (the
/// session's serialized blocking worker, DEC-016 decision 4) and bound every
/// wait: a `deadline` on the steady clock bounds the analysis, cooperative
/// cancellation rides the token, and a refresh whose deadline is absent
/// still settles within an implementation-defined generous bound.
class EnvironmentVisualPipeline {
  public:
    virtual ~EnvironmentVisualPipeline() = default;

    /// True while the pipeline's session is started and able to serve
    /// refreshes; drives the binding's honest capability reporting.
    [[nodiscard]] virtual bool running() const = 0;

    /// The registry refreshes publish into. The same registry must back the
    /// harness-side action path so `@vN` references resolve against the
    /// generation the observations delivered.
    [[nodiscard]] virtual desktop::VisualReferenceRegistry &registry() = 0;

    /// One refresh cycle: capture the configured display, and — when
    /// `analyze` is set — analyze the captured frame and publish the fused
    /// snapshot into the registry. With `analyze` unset the visual surface
    /// stays dark beyond the capture itself (DEC-016: an observe request
    /// without the visual component triggers no analysis).
    virtual EnvironmentVisualRefresh
    refresh(bool analyze, const desktop::CancelToken &cancel,
            const std::optional<std::chrono::steady_clock::time_point> &deadline) = 0;
    /// DEC-107: owner-selected exclusion is scoped to acquisition, before analysis.
    /// Custom pipelines must implement this boundary when an owner is supplied.
    virtual EnvironmentVisualRefresh
    refresh_presented(bool analyze, const desktop::CancelToken &cancel,
                      const std::optional<std::chrono::steady_clock::time_point> &deadline,
                      const desktop::CapturePresentation &presentation) {
        if (!presentation)
            return refresh(analyze, cancel, deadline);
        EnvironmentVisualRefresh failed;
        failed.capture_error = {"unsupported_capability",
                                "capture presentation boundary unavailable"};
        return failed;
    }
};

} // namespace mirage::integration
