#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Stable key-name vocabulary for inject_key, shared by all backends:
/// printable keys are their literal character ("a", "1", "?"), everything
/// else uses lowercase long names ("enter", "tab", "escape", "backspace",
/// "delete", "home", "end", "left", "right", "up", "down", "f1".."f12",
/// with modifier prefixes "ctrl+", "alt+", "shift+", "meta+" applied in
/// this order, e.g. "ctrl+shift+t"). Unknown names are rejected before any
/// injection.
struct KeySym {
    std::string name;
};

/// Mouse buttons in a stable vocabulary ("left", "middle", "right",
/// "back", "forward").
using MouseButton = std::string;

/// Budget for one input action (RULE-07). Injection itself is fast; the
/// timeout bounds platform round-trips that would otherwise hang the caller.
struct InputLimits {
    std::chrono::milliseconds timeout{1000};
    /// Cap for type_text payloads; longer text is refused before any
    /// injection, never truncated. Must be positive.
    std::size_t max_text_bytes = 64u << 10;
};

struct InputOutcome {
    bool ok = false;
    bool cancelled = false;
    ProviderError error; ///< meaningful only when ok is false
};

/// Keyboard and pointer injection (design doc section 5).
///
/// Input is the fallback path, not the preferred one: actions that can be
/// expressed semantically (Accessibility activation, application launch) go
/// through their own providers first (design doc section 9). Every method is
/// a real side effect: callers judge the `input.inject` capability through
/// the runtime permission gate (RULE-05, DEC-010) before invoking. The
/// provider stays permission-agnostic. Coordinates are in global desktop
/// coordinates, consistent with WindowGeometry. Methods are synchronous and
/// bounded by their limits; callers decide the execution context.
class InputProvider {
  public:
    virtual ~InputProvider() = default;

    /// Presses or releases a key per the KeySym vocabulary. Combination
    /// names ("ctrl+c") inject the full chord.
    virtual InputOutcome inject_key(const KeySym &key, bool pressed, const InputLimits &limits,
                                    const CancelToken &cancel) = 0;

    /// Injects text as if typed; encoding is UTF-8, other encodings are
    /// rejected before any injection.
    virtual InputOutcome type_text(const std::string &text, const InputLimits &limits,
                                   const CancelToken &cancel) = 0;

    /// Moves the pointer to global coordinates.
    virtual InputOutcome pointer_move(std::int32_t x, std::int32_t y, const InputLimits &limits,
                                      const CancelToken &cancel) = 0;

    /// Presses or releases a mouse button at the current pointer position.
    virtual InputOutcome pointer_button(const MouseButton &button, bool pressed,
                                        const InputLimits &limits, const CancelToken &cancel) = 0;

    /// Convenience overloads without cancellation and under default limits.
    InputOutcome inject_key(const KeySym &key, bool pressed, const InputLimits &limits) {
        return inject_key(key, pressed, limits, CancelToken{});
    }

    InputOutcome inject_key(const KeySym &key, bool pressed) {
        return inject_key(key, pressed, InputLimits{}, CancelToken{});
    }

    InputOutcome type_text(const std::string &text) {
        return type_text(text, InputLimits{}, CancelToken{});
    }

    InputOutcome pointer_move(std::int32_t x, std::int32_t y) {
        return pointer_move(x, y, InputLimits{}, CancelToken{});
    }

    InputOutcome pointer_button(const MouseButton &button, bool pressed) {
        return pointer_button(button, pressed, InputLimits{}, CancelToken{});
    }
};

/// Returns true when `name` follows the KeySym vocabulary documented above.
/// Shared by backends and callers so validation is identical everywhere;
/// implementations must reject invalid names with "invalid_argument" before
/// any injection.
bool is_valid_key_name(const std::string &name);

/// Returns true when `text` is well-formed UTF-8 (type_text contract).
bool is_valid_utf8(const std::string &text);

} // namespace mirage::desktop
