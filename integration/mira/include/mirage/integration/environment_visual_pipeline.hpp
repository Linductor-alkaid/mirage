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
struct EnvironmentVisualRefresh {
    /// Capture stage (ScreenProvider capture of the configured display).
    bool captured = false;
    bool capture_cancelled = false;
    desktop::ProviderError capture_error; ///< meaningful when !captured && !capture_cancelled
    /// Identifier of the captured display as reported by the screen
    /// provider; meaningful only when captured.
    std::string display_id;
    /// The captured frame; meaningful only when captured.
    desktop::ImageFrame frame;

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
};

} // namespace mirage::integration
