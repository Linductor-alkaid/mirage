#include "portal_input.hpp"
#include "input_pacing.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mirage/desktop/input_cadence.hpp>
#include <mirage/desktop/input_session.hpp>
#include <vector>
namespace mirage::platform::linux_backend {
// Notify calls are finite on the caller's Executor task. A failed press is
// ambiguous, so enter the release ledger BEFORE dispatch and never retry it.
desktop::InputOutcome dispatch_portal_input(PortalInputContext &s,
                                            const desktop::SharedInputAction &action,
                                            const desktop::InputLimits &limits,
                                            const desktop::CancelToken &cancel) {
    using Kind = desktop::SharedInputAction::Kind;
    const auto failed = [](const char *code, bool effects = false, bool released = true) {
        return desktop::InputOutcome{false,
                                     std::string_view(code) == "cancelled",
                                     {code, "Shared desktop input failed"},
                                     effects,
                                     released};
    };
    if ((s.devices & 3) != 3 || !s.notify || !s.stopped)
        return failed("portal_session_closed");
    if (limits.timeout.count() <= 0 ||
        limits.timeout > (action.kind == Kind::Text ? desktop::input_cadence_policy::text_timeout
                                                    : std::chrono::milliseconds{5000}))
        return failed("invalid_argument");
    if (!desktop::valid_click_count(action.click_count) ||
        (action.kind != Kind::Click && action.click_count != 1))
        return failed("invalid_argument");
    std::vector<int> keys;
    const auto append_character = [&](const std::string &text) {
        for (std::size_t i = 0; i < text.size();) {
            const auto first = static_cast<unsigned char>(text[i++]);
            unsigned cp = first;
            unsigned tail = 0;
            if (first >= 0xf0) {
                cp = first & 7;
                tail = 3;
            } else if (first >= 0xe0) {
                cp = first & 15;
                tail = 2;
            } else if (first >= 0xc0) {
                cp = first & 31;
                tail = 1;
            }
            while (tail--)
                cp = (cp << 6) | (static_cast<unsigned char>(text[i++]) & 63);
            keys.push_back(static_cast<int>(cp < 256 ? cp : (0x01000000u | cp)));
        }
    };
    if (action.kind == Kind::Key) {
        const auto chord = desktop::parse_key_chord(action.value);
        if (!chord)
            return failed("invalid_argument");
        if (chord->ctrl)
            keys.push_back(0xffe3);
        if (chord->alt)
            keys.push_back(0xffe9);
        if (chord->shift)
            keys.push_back(0xffe1);
        if (chord->meta)
            keys.push_back(0xffeb);
        static const std::pair<const char *, int> special[] = {
            {"ctrl", 0xffe3},   {"alt", 0xffe9},    {"shift", 0xffe1},  {"meta", 0xffeb},
            {"enter", 0xff0d},  {"tab", 0xff09},    {"escape", 0xff1b}, {"backspace", 0xff08},
            {"delete", 0xffff}, {"insert", 0xff63}, {"pageup", 0xff55}, {"pagedown", 0xff56},
            {"home", 0xff50},   {"end", 0xff57},    {"left", 0xff51},   {"up", 0xff52},
            {"right", 0xff53},  {"down", 0xff54},   {"space", 0x20}};
        int symbol = 0;
        for (const auto &[name, value] : special)
            if (chord->base == name)
                symbol = value;
        if (chord->base.starts_with("f") && chord->base.size() <= 3 && chord->base.size() > 1 &&
            chord->base[1] >= '0' && chord->base[1] <= '9')
            symbol = 0xffbd + std::stoi(chord->base.substr(1));
        if (symbol)
            keys.push_back(symbol);
        else
            append_character(chord->base);
        if (keys.empty() || keys.size() > 5)
            return failed("invalid_argument");
    } else if (action.kind == Kind::Text) {
        if (action.value.empty() || !limits.max_text_bytes ||
            action.value.size() > std::min(limits.max_text_bytes, std::size_t{4096}) ||
            !desktop::is_valid_utf8(action.value))
            return failed("invalid_argument");
        // Text controls would introduce undeclared actions (e.g. Enter).
        for (unsigned char ch : action.value)
            if (ch < 32 || ch == 127)
                return failed("invalid_argument");
        // Legacy NotifyKeyboardKeysym only resolves symbols in the current
        // compositor keymap. GNOME 46 silently accepts unmapped Unicode on
        // DBus while dropping it. Require a future explicit text transport
        // instead of claiming arbitrary UTF-8 was injected.
        for (unsigned char ch : action.value)
            if (ch >= 128)
                return failed("text_transport_unavailable");
        append_character(action.value);
    } else if (action.kind != Kind::Click && action.kind != Kind::Scroll &&
               action.kind != Kind::Drag && action.kind != Kind::Move) {
        return failed("invalid_argument");
    }
    int button = 0;
    if (action.kind == Kind::Click || action.kind == Kind::Drag) {
        if (action.value == "left")
            button = 272;
        else if (action.value == "right")
            button = 273;
        else if (action.value == "middle")
            button = 274;
        else
            return failed("invalid_argument");
    }
    if (action.long_press && ((action.kind != Kind::Click && action.kind != Kind::Key) ||
                              (action.kind == Kind::Click && action.click_count != 1)))
        return failed("invalid_argument");
    if (action.kind == Kind::Scroll &&
        !desktop::valid_scroll_steps(action.horizontal_steps, action.vertical_steps))
        return failed("invalid_argument");
    if (action.glide_ms != 0 &&
        (action.kind != Kind::Click ||
         desktop::validate_smooth_move_timing(std::chrono::milliseconds(action.glide_ms), 0)))
        return failed("invalid_argument");
    if ((action.kind == Kind::Move || action.glide_ms != 0) && !s.has_pointer_origin)
        // The message travels into the model's rejection receipt: tell it the
        // bounded recovery instead of leaving a bare code to guess at.
        return {false,
                false,
                {"pointer_origin_unknown",
                 "fresh same-stream cursor position is unavailable; refresh the authorized "
                 "desktop observation or use a backend with pointer read-back"},
                false,
                true};
    if (action.kind == Kind::Click || action.kind == Kind::Scroll || action.kind == Kind::Drag ||
        action.kind == Kind::Move) {
        const auto point_valid = [&](double x, double y) {
            return std::isfinite(x) && std::isfinite(y) && x >= 0 && y >= 0 && x < s.frame_width &&
                   y < s.frame_height;
        };
        if (!point_valid(action.x, action.y) || action.image_width != s.frame_width ||
            action.image_height != s.frame_height || s.logical_width <= 0 || s.logical_height <= 0)
            return failed("invalid_argument");
        if (action.kind == Kind::Drag &&
            (!point_valid(action.destination_x, action.destination_y) ||
             !desktop::valid_drag_path(
                 {static_cast<std::int32_t>(action.x), static_cast<std::int32_t>(action.y)},
                 {static_cast<std::int32_t>(action.destination_x),
                  static_cast<std::int32_t>(action.destination_y)})))
            return failed("invalid_argument");
    }
    if ((action.kind == Kind::Move || action.glide_ms != 0) &&
        (!std::isfinite(s.pointer_origin_x) || !std::isfinite(s.pointer_origin_y) ||
         s.pointer_origin_x < 0 || s.pointer_origin_y < 0 ||
         s.pointer_origin_x >= s.logical_width || s.pointer_origin_y >= s.logical_height))
        return failed("pointer_origin_unknown");
    const auto cap = action.kind == Kind::Text ? desktop::input_cadence_policy::text_timeout
                                               : desktop::input_cadence_policy::action_timeout;
    const auto end = std::chrono::steady_clock::now() + std::min(limits.timeout, cap);
    bool cancelled_during_action = false;
    const auto stopped = [&] {
        try {
            cancelled_during_action |= cancel.cancelled();
            return cancelled_during_action || s.stopped() ||
                   std::chrono::steady_clock::now() >= end;
        } catch (...) {
            return true;
        }
    };
    if (stopped())
        return failed(cancelled_during_action ? "cancelled" : "input_timeout");
    if (action.kind == Kind::Move &&
        action.x * s.logical_width / s.frame_width == s.pointer_origin_x &&
        action.y * s.logical_height / s.frame_height == s.pointer_origin_y)
        return {true, false, {}, false, true};
    using Phase = desktop::InputCadencePhase;
    std::uint64_t seed = 0;
    std::chrono::milliseconds planned{};
    std::vector<desktop::SmoothMoveSample> path;
    double target_x = 0, target_y = 0;
    bool has_travel = false;
    try {
        auto variation = action.kind == Kind::Click && !action.long_press
                             ? desktop::PointerMotionVariation{}
                             : desktop::make_pointer_motion_variation(action.target_width);
        seed = variation.seed;
        if (action.kind == Kind::Move || action.kind == Kind::Drag ||
            (action.kind == Kind::Click && action.glide_ms)) {
            const double from_x = action.kind == Kind::Drag
                                      ? action.x * s.logical_width / s.frame_width
                                      : s.pointer_origin_x;
            const double from_y = action.kind == Kind::Drag
                                      ? action.y * s.logical_height / s.frame_height
                                      : s.pointer_origin_y;
            target_x = (action.kind == Kind::Drag ? action.destination_x : action.x) *
                       s.logical_width / s.frame_width;
            target_y = (action.kind == Kind::Drag ? action.destination_y : action.y) *
                       s.logical_height / s.frame_height;
            const desktop::PointerPoint from{static_cast<std::int32_t>(std::llround(from_x)),
                                             static_cast<std::int32_t>(std::llround(from_y))};
            const desktop::PointerPoint to{static_cast<std::int32_t>(std::llround(target_x)),
                                           static_cast<std::int32_t>(std::llround(target_y))};
            variation.target_width =
                action.target_width > 0 ? static_cast<std::int32_t>(double(action.target_width) *
                                                                    s.logical_width / s.frame_width)
                                        : 0;
            variation.allow_overshoot = action.kind != Kind::Drag;
            const auto duration =
                action.kind == Kind::Click  ? std::chrono::milliseconds{action.glide_ms}
                : action.kind == Kind::Drag ? desktop::drag_cadence_duration(from, to, variation)
                                            : desktop::varied_pointer_duration(from, to, variation);
            path = desktop::build_smooth_move_schedule(
                from, to, duration, 0,
                action.kind == Kind::Click ? std::nullopt : std::optional{variation});
            has_travel = from_x != target_x || from_y != target_y;
            planned += has_travel ? duration : std::chrono::milliseconds{};
            if (action.kind == Kind::Drag)
                planned += desktop::input_cadence_delay(seed, Phase::drag_press) +
                           desktop::input_cadence_delay(seed, Phase::drag_settle);
        } else if (action.kind == Kind::Key) {
            planned = desktop::chord_cadence_duration(seed, keys.size());
        } else if (action.kind == Kind::Text) {
            for (std::size_t i = 0; i < keys.size(); ++i) {
                if ((i % 64 == 0) && stopped())
                    return failed(cancelled_during_action ? "cancelled" : "input_timeout");
                planned += desktop::input_cadence_delay(seed, Phase::key_dwell, i);
                if (i)
                    planned += desktop::input_cadence_delay(seed, Phase::key_interval, i);
                if (std::chrono::steady_clock::now() + planned >= end)
                    break; // A monotonic prefix already proves the whole plan cannot fit.
            }
        } else if (action.kind == Kind::Scroll) {
            planned = desktop::wheel_cadence_duration(seed, std::abs(action.horizontal_steps) +
                                                                std::abs(action.vertical_steps));
        }
    } catch (...) {
        return failed("input_plan_failed");
    }
    const auto long_dwell = desktop::long_press_cadence_duration(seed);
    if (action.long_press) {
        if (action.kind == Kind::Key)
            planned -= desktop::input_cadence_delay(seed, Phase::key_dwell);
        planned += long_dwell;
    }
    if (std::chrono::steady_clock::now() + planned >= end)
        return failed("input_timeout"); // Full plan admission, before any input.
    if (stopped())
        return failed(cancelled_during_action ? "cancelled" : "input_timeout");
    std::array<int, 5> held{};
    std::size_t count = 0;
    bool held_button = false, effects = false, released = true, ok = true, lagged = false;
    const auto execution_started = std::chrono::steady_clock::now();
    std::chrono::milliseconds lateness{0}, notify_max{0};
    const char *diagnostic_phase = "dispatch";
    std::size_t sample_index = 0, event_count = 0;
    auto notify = [&](PortalInputEvent event) {
        effects = true;
        ++event_count;
        const auto started = std::chrono::steady_clock::now();
        bool accepted = false;
        try {
            accepted = s.notify(event);
        } catch (...) {
        }
        notify_max = std::max(notify_max, std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now() - started));
        return accepted;
    };
    auto key = [&](int symbol, bool pressed) {
        return notify({PortalInputEvent::Kind::Key, symbol, pressed ? 1u : 0u});
    };
    auto wait = [&](Phase phase, std::size_t index = 0) {
        constexpr std::array names{"key_dwell",  "key_interval", "modifier_interval",
                                   "release",    "wheel_dwell",  "wheel_interval",
                                   "drag_press", "drag_settle"};
        diagnostic_phase = names[static_cast<std::size_t>(phase)];
        sample_index = index;
        const bool result = pace_input_until(
            std::chrono::steady_clock::now() + desktop::input_cadence_delay(seed, phase, index),
            [&] { return !stopped(); }, &lateness);
        lagged |= !result && !stopped();
        return result;
    };
    const auto wait_long = [&] {
        diagnostic_phase = "long_press";
        const bool result = pace_input_until(
            std::chrono::steady_clock::now() + long_dwell, [&] { return !stopped(); }, &lateness);
        lagged |= !result && !stopped();
        return result;
    };
    auto note_pointer = [&](double x, double y) {
        s.pointer_known = true;
        s.pointer_x = x;
        s.pointer_y = y;
    };
    auto travel = [&] {
        if (!has_travel)
            return true;
        const auto started = std::chrono::steady_clock::now();
        for (const auto &sample : path) {
            diagnostic_phase = "travel";
            ++sample_index;
            if (!pace_input_until(started + sample.at, [&] { return !stopped(); }, &lateness)) {
                lagged = !stopped();
                return false;
            }
            const double px =
                action.kind == Kind::Click
                    ? double(sample.x)
                    : std::clamp(double(sample.x), 0.0, double(s.logical_width) - 1.0);
            const double py =
                action.kind == Kind::Click
                    ? double(sample.y)
                    : std::clamp(double(sample.y), 0.0, double(s.logical_height) - 1.0);
            if (!notify({PortalInputEvent::Kind::Motion, 0, 0, px, py}))
                return false;
            note_pointer(px, py);
        }
        // Fractional logical targets also land exactly; never rescale samples twice.
        if (path.empty() || target_x != path.back().x || target_y != path.back().y) {
            if (stopped() || !notify({PortalInputEvent::Kind::Motion, 0, 0, target_x, target_y}))
                return false;
            note_pointer(target_x, target_y);
        }
        return true;
    };
    auto release = [&](bool paced = false) {
        if (held_button) {
            released = notify({PortalInputEvent::Kind::Button, button, 0u}) && released;
            held_button = false;
        }
        std::size_t index = 0;
        while (count) {
            released = key(held[--count], false) && released;
            if (count && paced && ok && released && !stopped())
                ok = wait(Phase::release_interval, index++);
        }
    };
    try {
        if (action.kind == Kind::Click || action.kind == Kind::Scroll ||
            action.kind == Kind::Drag || action.kind == Kind::Move) {
            const double start_x = action.x * s.logical_width / s.frame_width;
            const double start_y = action.y * s.logical_height / s.frame_height;
            if (action.kind == Kind::Move || (action.kind == Kind::Click && action.glide_ms)) {
                ok = travel();
            } else {
                ok = notify({PortalInputEvent::Kind::Motion, 0, 0, start_x, start_y});
                if (ok)
                    note_pointer(start_x, start_y);
            }
            if (ok && !stopped() && action.kind == Kind::Click) {
                diagnostic_phase = "click";
                for (std::uint32_t i = 0; i < action.click_count && ok && released && !stopped();
                     ++i) {
                    sample_index = i;
                    held_button = true;
                    ok = notify({PortalInputEvent::Kind::Button, button, 1u});
                    if (ok && action.long_press)
                        ok = wait_long();
                    release();
                }
            } else if (ok && !stopped() && action.kind == Kind::Drag) {
                diagnostic_phase = "drag";
                held_button = true;
                ok = notify({PortalInputEvent::Kind::Button, button, 1u});
                if (ok)
                    ok = wait(Phase::drag_press) && travel() && wait(Phase::drag_settle);
            } else if (ok && !stopped() && action.kind == Kind::Scroll) {
                diagnostic_phase = "scroll";
                std::size_t index = 0;
                for (unsigned axis = 0; axis < 2 && ok && !stopped(); ++axis) {
                    const int steps = axis ? action.horizontal_steps : action.vertical_steps;
                    for (int i = 0; i < std::abs(steps) && ok && !stopped(); ++i, ++index) {
                        if (index)
                            ok = wait(Phase::wheel_interval, index);
                        if (ok)
                            ok = notify({PortalInputEvent::Kind::Axis, steps < 0 ? -1 : 1, axis}) &&
                                 wait(Phase::wheel_dwell, index);
                    }
                }
            }
        } else {
            diagnostic_phase = action.kind == Kind::Text ? "text" : "key";
            for (std::size_t i = 0; i < keys.size(); ++i) {
                sample_index = i;
                if (stopped()) {
                    ok = false;
                    break;
                }
                held[count++] = keys[i];
                if (!key(keys[i], true)) {
                    ok = false;
                    break;
                }
                if (action.kind == Kind::Text) {
                    if (!wait(Phase::key_dwell, i)) {
                        ok = false;
                        break;
                    }
                    const bool up = key(keys[i], false);
                    --count;
                    if (!up) {
                        released = false;
                        ok = false;
                        break;
                    }
                    if (i + 1 < keys.size() && !wait(Phase::key_interval, i + 1)) {
                        ok = false;
                        break;
                    }
                } else if (i + 1 < keys.size()) {
                    if (!wait(Phase::modifier_interval, i)) {
                        ok = false;
                        break;
                    }
                } else {
                    ok = action.long_press ? wait_long() : wait(Phase::key_dwell);
                }
            }
        }
    } catch (...) {
        ok = false;
    }
    release(action.kind == Kind::Key); // Emergency cleanup never waits.
    const bool stop = stopped();
    if (!released || !ok || stop) {
        auto out = failed(!released                 ? "input_release_failed"
                          : cancelled_during_action ? "cancelled"
                          : stop                    ? "input_timeout"
                          : lagged                  ? "input_lag"
                                                    : "portal_input_failed",
                          effects, released);
        out.error.message = std::string("phase=") + (released ? diagnostic_phase : "release") +
                            " sample=" + std::to_string(sample_index) +
                            " planned_ms=" + std::to_string(planned.count()) + " elapsed_ms=" +
                            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now() - execution_started)
                                               .count()) +
                            " late_ms=" + std::to_string(lateness.count()) +
                            " notify_max_ms=" + std::to_string(notify_max.count()) +
                            " events=" + std::to_string(event_count);
        return out;
    }
    return {true, false, {}, effects, true};
}
} // namespace mirage::platform::linux_backend
