#include "portal_input.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <mirage/desktop/input_session.hpp>
#include <thread>
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
    if (limits.timeout.count() <= 0 || limits.timeout > std::chrono::seconds(5))
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
    const auto end = std::chrono::steady_clock::now() + limits.timeout;
    const auto stopped = [&] {
        return cancel.cancelled() || s.stopped() || std::chrono::steady_clock::now() >= end;
    };
    if (stopped())
        return failed(cancel.cancelled() ? "cancelled" : "input_timeout");
    std::array<int, 5> held{};
    std::size_t count = 0;
    bool held_button = false, effects = false, released = true, ok = true;
    auto notify = [&](PortalInputEvent event) {
        effects = true;
        try {
            return s.notify(event);
        } catch (...) {
            return false;
        }
    };
    auto key = [&](int symbol, bool pressed) {
        return notify({PortalInputEvent::Kind::Key, symbol, pressed ? 1u : 0u});
    };
    // DEC-110 (M7-26): record the same-origin pointer position after every
    // accepted motion so a later glide has a trusted start.
    auto note_pointer = [&](double x, double y) {
        s.pointer_known = true;
        s.pointer_x = x;
        s.pointer_y = y;
    };
    // Timed travel on the frozen minimum-jerk schedule. Every notify is a
    // synchronous Portal call (no client-side buffering to flush); a delivery
    // more than 250 ms behind its schedule stops the travel instead of
    // replaying a backlog, mirroring the X11 path.
    auto travel = [&](double from_x, double from_y, double to_x, double to_y) {
        if (from_x == to_x && from_y == to_y)
            return true;
        const desktop::PointerPoint from{static_cast<std::int32_t>(std::llround(from_x)),
                                         static_cast<std::int32_t>(std::llround(from_y))};
        const desktop::PointerPoint to{static_cast<std::int32_t>(std::llround(to_x)),
                                       static_cast<std::int32_t>(std::llround(to_y))};
        std::optional<desktop::PointerMotionVariation> variation;
        if (action.kind == Kind::Move)
            variation = desktop::make_pointer_motion_variation(
                action.target_width > 0 ? static_cast<std::int32_t>(double(action.target_width) *
                                                                    s.logical_width / s.frame_width)
                                        : 0);
        const auto glide = variation ? desktop::varied_pointer_duration(from, to, *variation)
                                     : std::chrono::milliseconds{action.glide_ms};
        if (glide > limits.timeout)
            return false;
        const auto schedule = desktop::build_smooth_move_schedule(from, to, glide, 0, variation);
        const auto started = std::chrono::steady_clock::now();
        const auto kMaxLag = std::chrono::milliseconds{250};
        for (const auto &sample : schedule) {
            if (cancel.cancelled() || s.stopped())
                return false;
            const auto scheduled = started + sample.at;
            std::this_thread::sleep_until(scheduled);
            const auto now = std::chrono::steady_clock::now();
            if (stopped() || now > scheduled + kMaxLag || now >= end)
                return false;
            // The schedule is built in logical coordinates (from/to above are
            // the same space the plain Motion dispatch sends), so samples go
            // to the compositor as-is — no further frame-to-logical scaling.
            const double px = variation
                                  ? std::clamp(double(sample.x), 0.0, double(s.logical_width) - 1.0)
                                  : double(sample.x);
            const double py =
                variation ? std::clamp(double(sample.y), 0.0, double(s.logical_height) - 1.0)
                          : double(sample.y);
            if (!notify({PortalInputEvent::Kind::Motion, 0, 0, px, py}))
                return false;
            note_pointer(px, py);
        }
        // Land on the exact logical target; the schedule rounds to pixels.
        if (to_x != std::llround(to_x) || to_y != std::llround(to_y)) {
            if (!notify({PortalInputEvent::Kind::Motion, 0, 0, to_x, to_y}))
                return false;
            note_pointer(to_x, to_y);
        }
        return true;
    };
    auto release = [&] {
        if (held_button) {
            released = notify({PortalInputEvent::Kind::Button, button, 0u}) && released;
            held_button = false;
        }
        while (count)
            released = key(held[--count], false) && released;
    };
    try {
        if (action.kind == Kind::Click || action.kind == Kind::Scroll ||
            action.kind == Kind::Drag || action.kind == Kind::Move) {
            const double target_x = action.x * s.logical_width / s.frame_width;
            const double target_y = action.y * s.logical_height / s.frame_height;
            if (action.kind == Kind::Move || (action.kind == Kind::Click && action.glide_ms != 0)) {
                ok = travel(s.pointer_origin_x, s.pointer_origin_y, target_x, target_y);
            } else {
                ok = notify({PortalInputEvent::Kind::Motion, 0, 0, target_x, target_y});
                if (ok)
                    note_pointer(target_x, target_y);
            }
            if (ok && !stopped() && action.kind == Kind::Click) {
                for (std::uint32_t index = 0;
                     index < action.click_count && ok && released && !stopped(); ++index) {
                    held_button = true;
                    ok = notify({PortalInputEvent::Kind::Button, button, 1u});
                    release(); // Each click completes even on cancellation/failed down.
                }
            } else if (ok && !stopped() && action.kind == Kind::Drag) {
                held_button = true;
                ok = notify({PortalInputEvent::Kind::Button, button, 1u});
                for (int step = 1; step <= 16 && ok && !stopped(); ++step) {
                    const double x = action.x + (action.destination_x - action.x) * step / 16;
                    const double y = action.y + (action.destination_y - action.y) * step / 16;
                    ok = notify({PortalInputEvent::Kind::Motion, 0, 0,
                                 x * s.logical_width / s.frame_width,
                                 y * s.logical_height / s.frame_height});
                    if (ok)
                        note_pointer(x * s.logical_width / s.frame_width,
                                     y * s.logical_height / s.frame_height);
                }
            } else if (ok && !stopped() && action.kind == Kind::Scroll) {
                for (unsigned axis = 0; axis < 2 && ok && !stopped(); ++axis) {
                    const int steps = axis ? action.horizontal_steps : action.vertical_steps;
                    if (steps)
                        ok = notify({PortalInputEvent::Kind::Axis, steps, axis});
                }
            }
        } else {
            for (const int symbol : keys) {
                if (stopped()) {
                    ok = false;
                    break;
                }
                held[count++] = symbol;
                if (!key(symbol, true)) {
                    ok = false;
                    break;
                }
                if (action.kind == Kind::Text) {
                    const bool up = key(symbol, false);
                    --count;
                    if (!up) {
                        released = false;
                        ok = false;
                        break;
                    }
                }
            }
        }
    } catch (...) {
        ok = false;
    }
    release(); // Cancellation/deadline must never suppress releases.
    const bool stop = stopped();
    if (!released || !ok || stop) {
        return failed(!released            ? "input_release_failed"
                      : cancel.cancelled() ? "cancelled"
                      : stop               ? "input_timeout"
                                           : "portal_input_failed",
                      effects, released);
    }
    return {true, false, {}, effects, true};
}
} // namespace mirage::platform::linux_backend
