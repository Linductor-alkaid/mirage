#pragma once

#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/provider_error.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

namespace mirage::desktop {

/// Which observation components the caller needs (design doc section 6:
/// on-demand observation — a task over terminals and files needs no GUI
/// perception, so only the requested components are captured). Requested
/// components are mandatory at the binding level: an environment that
/// cannot deliver one reports the failure instead of returning a silently
/// incomplete observation. `visual_snapshot_ref` stays empty until the M3
/// Mirador integration lands (DEC-005).
struct ObservationComponents {
    /// Focused window's application name (when known), title, geometry and
    /// focus flag.
    bool active_window = false;
    /// Accessibility snapshot of the focused window (also fills
    /// focused_element and, from the accessibility application root,
    /// active_application).
    bool semantic_snapshot = false;
    /// Global pointer position.
    bool pointer_state = false;
    /// Backend environment summary derived from the available providers.
    bool environment_state = false;
};

/// Budgets for one assembly (RULE-07).
struct ObservationAssemblyLimits {
    SemanticSnapshotLimits snapshot_limits{};
};

/// Per-component capture result. `captured` is false only when the
/// component was not requested; a requested component either was captured
/// or its stable error is reported and the whole assembly reports ok=false.
struct ObservationComponentResult {
    bool requested = false;
    bool captured = false;
    ProviderError error; ///< meaningful when requested && !captured
};

/// Result of one assembly. On ok=false the observation still carries every
/// component that did capture — incompleteness is explicit through ok, the
/// per-component results and `error` (the first failed component in capture
/// order), never silent.
struct ObservationAssemblyOutcome {
    bool ok = false;
    bool cancelled = false;
    DesktopObservation observation; ///< components captured so far
    ObservationComponentResult active_window;
    ObservationComponentResult semantic_snapshot;
    ObservationComponentResult pointer_state;
    ObservationComponentResult environment_state;
    ProviderError error; ///< meaningful only when ok is false
};

/// Assembles one DesktopObservation from the environment's providers on
/// demand (design doc section 6). The assembler owns no state: every call
/// captures fresh values through the provider accessors, so callers always
/// see current desktop state (snapshot refresh policy v1 is behavior-result
/// driven; a new observation takes a new snapshot).
///
/// Provider-mapped components fail closed when the environment lacks the
/// provider (accessor null, e.g. the X11/AT-SPI2 frontend was not enabled):
/// the stable error "unsupported_platform" is reported for that component
/// instead of guessing. Capture order is active_window, semantic_snapshot,
/// pointer_state, environment_state; a failed component does not stop the
/// remaining captures. Cancellation is observed before each component and
/// inside the providers; a cancelled assembly stops immediately.
class ObservationAssembler {
  public:
    /// Does not take ownership; `environment` must outlive the assembler.
    explicit ObservationAssembler(DesktopEnvironment &environment);

    ObservationAssemblyOutcome assemble(const ObservationComponents &components,
                                        const ObservationAssemblyLimits &limits,
                                        const CancelToken &cancel);

    ObservationAssemblyOutcome assemble(const ObservationComponents &components) {
        return assemble(components, ObservationAssemblyLimits{}, CancelToken{});
    }

  private:
    DesktopEnvironment &environment_;
};

} // namespace mirage::desktop
