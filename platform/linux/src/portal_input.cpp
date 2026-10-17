#include "portal_input.hpp"
#include <algorithm>
#include <array>
#include <cmath>
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
    } else if (action.kind != Kind::Click && action.kind != Kind::Scroll) {
        return failed("invalid_argument");
    }
    int button = 0;
    if (action.kind == Kind::Click) {
        if (action.value == "left")
            button = 272;
        else if (action.value == "right")
            button = 273;
        else if (action.value == "middle")
            button = 274;
        else
            return failed("invalid_argument");
    }
    if (action.kind == Kind::Scroll && (std::abs(std::int64_t{action.vertical_steps}) > 20 ||
                                        std::abs(std::int64_t{action.horizontal_steps}) > 20 ||
                                        (!action.vertical_steps && !action.horizontal_steps)))
        return failed("invalid_argument");
    if (action.kind == Kind::Click || action.kind == Kind::Scroll) {
        if (!std::isfinite(action.x) || !std::isfinite(action.y) || action.x < 0 || action.y < 0 ||
            action.image_width != s.frame_width || action.image_height != s.frame_height ||
            action.x >= s.frame_width || action.y >= s.frame_height || s.logical_width <= 0 ||
            s.logical_height <= 0)
            return failed("invalid_argument");
    }
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
    auto release = [&] {
        if (held_button) {
            released = notify({PortalInputEvent::Kind::Button, button, 0u}) && released;
            held_button = false;
        }
        while (count)
            released = key(held[--count], false) && released;
    };
    try {
        if (action.kind == Kind::Click || action.kind == Kind::Scroll) {
            ok = notify({PortalInputEvent::Kind::Motion, 0, 0,
                         action.x * s.logical_width / s.frame_width,
                         action.y * s.logical_height / s.frame_height});
            if (ok && !stopped() && action.kind == Kind::Click) {
                for (std::uint32_t index = 0;
                     index < action.click_count && ok && released && !stopped(); ++index) {
                    held_button = true;
                    ok = notify({PortalInputEvent::Kind::Button, button, 1u});
                    release(); // Each click completes even on cancellation/failed down.
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
