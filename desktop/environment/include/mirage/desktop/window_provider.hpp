#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// One window known to the environment (design doc section 5). `id` is an
/// opaque backend identifier, stable while the window lives.
struct WindowInfo {
    std::string id;
    std::string title;
    WindowGeometry geometry;
    bool focused = false;
};

/// Budget for one window enumeration (RULE-07). A result that would exceed
/// `max_windows` entries is refused — never silently truncated.
struct WindowListLimits {
    std::size_t max_windows = 256;
};

struct WindowListOutcome {
    bool ok = false;
    std::vector<WindowInfo> windows;
    ProviderError error; ///< meaningful only when ok is false
};

struct WindowQueryOutcome {
    bool ok = false;
    bool found = false;
    WindowInfo window;   ///< meaningful only when ok and found
    ProviderError error; ///< meaningful only when ok is false
};

/// Outcome of one window management action (e.g. activation).
struct WindowActionOutcome {
    bool ok = false;
    bool cancelled = false;
    ProviderError error; ///< meaningful only when ok is false
};

/// Window enumeration, focus and activation (design doc section 5).
///
/// Activation is a real side effect: callers judge the `window.activate`
/// capability through the runtime permission gate (RULE-05, DEC-010) before
/// invoking. The provider stays permission-agnostic. Methods are synchronous
/// and bounded by their limits; callers decide the execution context.
class WindowProvider {
  public:
    virtual ~WindowProvider() = default;

    /// Enumerates top-level windows within the budget; refuses over-budget
    /// results with a stable ProviderError instead of truncating.
    virtual WindowListOutcome list_windows(const WindowListLimits &limits,
                                           const CancelToken &cancel) = 0;

    /// Same enumeration under default limits and without cancellation.
    WindowListOutcome list_windows() { return list_windows(WindowListLimits{}, CancelToken{}); }

    /// Reports the currently focused window; found=false when no window has
    /// focus (not an error).
    virtual WindowQueryOutcome front_window(const CancelToken &cancel) = 0;

    WindowQueryOutcome front_window() { return front_window(CancelToken{}); }

    /// Brings the window with the given id to the front and focuses it.
    /// Unknown ids fail with "not_found" before any side effect.
    virtual WindowActionOutcome activate(const std::string &window_id,
                                         const CancelToken &cancel) = 0;

    WindowActionOutcome activate(const std::string &window_id) {
        return activate(window_id, CancelToken{});
    }
};

} // namespace mirage::desktop
