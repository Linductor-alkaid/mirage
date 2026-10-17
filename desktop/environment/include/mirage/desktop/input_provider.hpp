#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/desktop_observation.hpp>
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
/// Common complete actions support these buttons across target routes.
inline bool valid_action_button(const MouseButton &button) {
    return button == "left" || button == "middle" || button == "right";
}

/// Bound one uninterrupted pointer action; longer repetitions require fresh intent.
inline constexpr std::uint32_t max_click_count = 32;
constexpr bool valid_click_count(std::uint32_t count) {
    return count >= 1 && count <= max_click_count;
}

/// Budget for one input action (RULE-07). Injection itself is fast; the
/// timeout is cooperative: backends check it between platform steps; it does
/// not forcibly interrupt a blocked platform round-trip or suppress cleanup.
struct InputLimits {
    std::chrono::milliseconds timeout{1000};
    /// Cap for type_text payloads; longer text is refused before any
    /// injection, never truncated. Must be positive.
    std::size_t max_text_bytes = 64u << 10;
};

struct InputOutcome {
    bool ok = false;
    bool cancelled = false;
    ProviderError error;                        ///< meaningful only when ok is false
    bool side_effect_may_have_occurred = false; ///< no automatic retry when true
    bool input_released = true;                 ///< false fences further mutations and shutdown
};

/// Outcome of one pointer position query. `position` is meaningful only
/// when ok.
struct PointerQueryOutcome {
    bool ok = false;
    bool cancelled = false;
    PointerState position;
    ProviderError error; ///< meaningful only when ok is false
};

/// DEC-064: resolved, window-bound pointer action. A scope is execution
/// evidence, never a model-supplied coordinate or authorization.
struct PointerActionScope {
    std::string window_id;
    WindowGeometry geometry;
};
struct PointerPoint {
    std::int32_t x = 0, y = 0;
};

/// Common, overflow-safe preflight for bounded complete pointer paths.
bool valid_pointer_scope(const PointerActionScope &, PointerPoint, const InputLimits &);
bool valid_scroll_steps(std::int32_t horizontal, std::int32_t vertical);
bool valid_drag_path(PointerPoint from, PointerPoint to);

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
    /// Complete discrete actions require explicit backend opt-in. The backend
    /// preflights physical input, owns its presses and reports cleanup failure.
    /// This includes type_text. Cancellation/deadline never suppress release;
    /// backend exceptions must also settle owned presses before propagation.
    virtual bool complete_actions_supported() const { return false; }
    /// Separate opt-in: existing complete clicks do not imply scroll/drag.
    virtual bool bounded_pointer_actions_supported() const { return false; }
    virtual InputOutcome scroll_at(const PointerActionScope &, PointerPoint,
                                   std::int32_t horizontal, std::int32_t vertical,
                                   const InputLimits &, const CancelToken &);
    /// Left-button complete action, 16 linear segments, no held-button API.
    virtual InputOutcome drag_to(const PointerActionScope &, PointerPoint from, PointerPoint to,
                                 const InputLimits &, const CancelToken &);
    virtual InputOutcome key_chord(const KeySym &key, const InputLimits &limits,
                                   const CancelToken &cancel);
    virtual InputOutcome click_at(std::int32_t x, std::int32_t y, const MouseButton &button,
                                  const InputLimits &limits, const CancelToken &cancel);
    /// DEC-100: complete drag using a selected button; left-only legacy providers
    /// reject other buttons before effects.
    virtual InputOutcome drag_with_button(const PointerActionScope &, PointerPoint from,
                                          PointerPoint to, const MouseButton &button,
                                          const InputLimits &, const CancelToken &);
    /// DEC-099: one complete click sequence (count 1..max_click_count), same point and
    /// ownership throughout. No observations or model calls between presses.
    /// Default forwards count 1 only; unsupported repetitions fail before effects.
    virtual InputOutcome click_sequence_at(std::int32_t x, std::int32_t y,
                                           const MouseButton &button, std::uint32_t count,
                                           const InputLimits &limits, const CancelToken &cancel);

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

    /// Queries the current pointer position in global desktop coordinates
    /// (feeds the DesktopObservation pointer_state component, design doc
    /// section 6). Read-only: no side effect, no injection.
    virtual PointerQueryOutcome pointer_position(const CancelToken &cancel) = 0;

    PointerQueryOutcome pointer_position() { return pointer_position(CancelToken{}); }

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

/// Parsed KeySym name: modifier flags plus the plain base key. Backends use
/// it to build platform key events, so chord semantics cannot diverge
/// between validation and injection.
struct KeyChord {
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;
    std::string base; ///< plain key name, e.g. "enter" or "a"
};

/// Splits a KeySym name into modifiers and base key per the vocabulary
/// above; nullopt when the name is invalid.
std::optional<KeyChord> parse_key_chord(const std::string &name);

/// Returns true when `name` follows the KeySym vocabulary documented above.
/// Shared by backends and callers so validation is identical everywhere;
/// implementations must reject invalid names with "invalid_argument" before
/// any injection.
bool is_valid_key_name(const std::string &name);

/// Returns true when `text` is well-formed UTF-8 (type_text contract).
bool is_valid_utf8(const std::string &text);

} // namespace mirage::desktop
