#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// DEC-110 / M7-28: a partial observation, never an inference from an engine
/// name. Unknown values carry a reason; known values carry their actual source
/// and sample time. Engine layout metadata is not the active keyboard layout.
struct InputStateField {
    std::optional<std::string> value;
    std::string source;
    std::int64_t captured_at_ms = 0;
    std::string reason = "not_observable";
};

struct InputMethodState {
    std::string backend;
    InputStateField engine;
    InputStateField activation;
    InputStateField conversion_mode;
    InputStateField layout;
    InputStateField focus_context;
    InputStateField focused_window;
    InputStateField preedit_active;
    InputStateField candidates_visible;
};

struct InputMethodLimits {
    /// Cooperative total deadline, including discovery and platform round trips.
    std::chrono::milliseconds timeout{1000};
};

struct InputMethodOutcome {
    bool ok = false;
    bool cancelled = false;
    /// Separate projections when multiple frameworks run. Their presence does
    /// not establish which framework handles the target application.
    std::vector<InputMethodState> input_methods;
    ProviderError error;
};

/// Read-only state of keyboard input methods, independent of global input
/// injection (also usable on native Wayland). Synchronous on the caller's
/// Executor task. No polling owner, settings changes or context creation.
class InputMethodProvider {
  public:
    virtual ~InputMethodProvider() = default;
    virtual InputMethodOutcome observe(const InputMethodLimits &, const CancelToken &) = 0;
};

} // namespace mirage::desktop
