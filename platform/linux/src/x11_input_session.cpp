#include "x11_input_session.hpp"
#include "input_pacing.hpp"
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>
#include <mirage/desktop/input_cadence.hpp>
#ifdef MIRAGE_LINUX_XINPUT2
#include <X11/extensions/XInput2.h>
#endif
#include <algorithm>
#include <array>
#include <charconv>
#include <map>
#include <set>

namespace mirage::platform::linux_backend {
#ifdef MIRAGE_LINUX_XINPUT2
namespace {
using namespace desktop;
class Session final : public InputSessionBackend {
    Display *display_ = nullptr;
    PointerActionScope scope_;
    Window window_ = 0;
    int xi_opcode_ = 0;
    std::set<int> injected_sources_;
    std::map<std::string, unsigned> keys_, buttons_;
    std::vector<std::pair<bool, std::string>> order_;
    std::set<unsigned> conflicted_keys_, conflicted_buttons_;
    bool effects_ = false, lost_ = false;
    std::uint64_t seed_ = 0;
    std::uint32_t commands_ = 0;
    bool within(Window current) const {
        for (int depth = 0; depth < 32 && current && current != PointerRoot; ++depth) {
            if (current == window_)
                return true;
            Window root = 0, parent = 0, *children = nullptr;
            unsigned count = 0;
            const auto ok = XQueryTree(display_, current, &root, &parent, &children, &count);
            if (children)
                XFree(children);
            if (!ok || current == parent)
                break;
            current = parent;
        }
        return false;
    }
    unsigned keycode(const std::string &name) const {
        const auto parsed = parse_session_key_name(name);
        if (!parsed)
            return 0;
        ::KeySym symbol = NoSymbol;
        if (parsed->modifier != SessionKeyName::Modifier::none) {
            const bool right = parsed->side == SessionKeyName::Side::right;
            switch (parsed->modifier) {
            case SessionKeyName::Modifier::ctrl:
                symbol = right ? XK_Control_R : XK_Control_L;
                break;
            case SessionKeyName::Modifier::alt:
                symbol = right ? XK_Alt_R : XK_Alt_L;
                break;
            case SessionKeyName::Modifier::shift:
                symbol = right ? XK_Shift_R : XK_Shift_L;
                break;
            case SessionKeyName::Modifier::meta:
                symbol = right ? XK_Super_R : XK_Super_L;
                break;
            default:
                break;
            }
        } else {
            static const std::map<std::string, const char *> named{{"enter", "Return"},
                                                                   {"tab", "Tab"},
                                                                   {"escape", "Escape"},
                                                                   {"space", "space"},
                                                                   {"backspace", "BackSpace"},
                                                                   {"delete", "Delete"},
                                                                   {"insert", "Insert"},
                                                                   {"home", "Home"},
                                                                   {"end", "End"},
                                                                   {"pageup", "Prior"},
                                                                   {"pagedown", "Next"},
                                                                   {"left", "Left"},
                                                                   {"right", "Right"},
                                                                   {"up", "Up"},
                                                                   {"down", "Down"}};
            auto spelling = parsed->base;
            if (const auto it = named.find(spelling); it != named.end())
                spelling = it->second;
            else if (spelling.size() >= 2 && spelling[0] == 'f')
                spelling[0] = 'F';
            symbol = XStringToKeysym(spelling.c_str());
        }
        if (symbol == NoSymbol)
            return 0;
        const auto code = XKeysymToKeycode(display_, symbol);
        // No implicit modifier synthesis: a session composes modifiers explicitly.
        if (code && parsed->modifier == SessionKeyName::Modifier::none &&
            XkbKeycodeToKeysym(display_, code, 0, 0) != symbol)
            return 0;
        return code;
    }
    unsigned button_code(const std::string &name) const {
        return name == "left" ? 1u : name == "middle" ? 2u : name == "right" ? 3u : 0u;
    }
    bool pressed(unsigned code) const {
        std::array<char, 32> state{};
        XQueryKeymap(display_, state.data());
        return (static_cast<unsigned char>(state[code / 8]) & (1u << (code % 8))) != 0;
    }
    bool pointer(unsigned &mask, bool require_target) const {
        Window root = 0, child = 0;
        int x = 0, y = 0, wx = 0, wy = 0;
        if (!XQueryPointer(display_, DefaultRootWindow(display_), &root, &child, &x, &y, &wx, &wy,
                           &mask))
            return false;
        const auto &g = scope_.geometry;
        return !require_target || (within(child) && x >= g.x && y >= g.y &&
                                   std::int64_t{x} < std::int64_t{g.x} + g.width &&
                                   std::int64_t{y} < std::int64_t{g.y} + g.height);
    }
    bool transition(unsigned code, bool key, bool down) {
        effects_ = true;
        const auto accepted = key ? XTestFakeKeyEvent(display_, code, down, CurrentTime)
                                  : XTestFakeButtonEvent(display_, code, down, CurrentTime);
        XSync(display_, False);
        if (!accepted)
            return false;
        if (key)
            return pressed(code) == down;
        unsigned mask = 0;
        return pointer(mask, false) && bool(mask & (Button1Mask << (code - 1))) == down;
    }

  public:
    ~Session() override {
        if (display_) {
            (void)release();
            XCloseDisplay(display_);
        }
    }
    bool open(const std::string &name, const PointerActionScope &scope, const CancelToken &cancel) {
        if (cancel.cancelled() || scope.geometry.width <= 0 || scope.geometry.height <= 0)
            return false;
        unsigned long id = 0;
        const auto parsed = std::from_chars(scope.window_id.data(),
                                            scope.window_id.data() + scope.window_id.size(), id);
        if (parsed.ec != std::errc{} ||
            parsed.ptr != scope.window_id.data() + scope.window_id.size() || !id)
            return false;
        display_ = XOpenDisplay(name.c_str());
        if (!display_)
            return false;
        window_ = id;
        scope_ = scope;
        int event = 0, error = 0, major = 2, minor = 0;
        if (!XQueryExtension(display_, "XInputExtension", &xi_opcode_, &event, &error) ||
            XIQueryVersion(display_, &major, &minor) != Success)
            return false;
        int count = 0;
        XIDeviceInfo *devices = XIQueryDevice(display_, XIAllDevices, &count);
        if (!devices || count > 256) {
            if (devices)
                XIFreeDeviceInfo(devices);
            return false;
        }
        for (int n = 0; n < count; ++n) {
            const std::string device_name = devices[n].name ? devices[n].name : "";
            if (device_name == "Virtual core XTEST pointer" ||
                device_name == "Virtual core XTEST keyboard")
                injected_sources_.insert(devices[n].deviceid);
        }
        XIFreeDeviceInfo(devices);
        if (injected_sources_.size() != 2)
            return false;
        unsigned char mask[XIMaskLen(XI_LASTEVENT)]{};
        for (const int type : {XI_RawKeyPress, XI_RawKeyRelease, XI_RawButtonPress,
                               XI_RawButtonRelease, XI_RawMotion})
            XISetMask(mask, type);
        XIEventMask selection{XIAllMasterDevices, static_cast<int>(sizeof(mask)), mask};
        if (XISelectEvents(display_, DefaultRootWindow(display_), &selection, 1) != Success)
            return false;
        XSelectInput(display_, window_, StructureNotifyMask);
        XSync(display_, False);
        seed_ = make_pointer_motion_variation(0).seed;
        return true;
    }
    InputCapabilitySet capabilities() const override {
        return InputCapabilitySet{}
            .with(InputCapability::software_injection)
            .with(InputCapability::timed_press)
            .with(InputCapability::independent_modifiers)
            .with(InputCapability::cross_observation_hold)
            .with(InputCapability::hold_capture);
    }
    ProviderError check() override {
        XSync(display_, False);
        bool physical_seen = false, target_changed = false;
        for (unsigned n = 0; XPending(display_); ++n) {
            if (n >= 256) {
                for (const auto &[name, code] : keys_) {
                    (void)name;
                    conflicted_keys_.insert(code);
                }
                for (const auto &[name, code] : buttons_) {
                    (void)name;
                    conflicted_buttons_.insert(code);
                }
                return {"physical_input_unknown", "Input event budget exceeded"};
            }
            XEvent event{};
            XNextEvent(display_, &event);
            if (event.type == DestroyNotify || event.type == UnmapNotify ||
                event.type == ConfigureNotify)
                target_changed = true;
            if (event.type != GenericEvent || event.xcookie.extension != xi_opcode_)
                continue;
            if (!XGetEventData(display_, &event.xcookie)) {
                for (const auto &[name, code] : keys_) {
                    (void)name;
                    conflicted_keys_.insert(code);
                }
                for (const auto &[name, code] : buttons_) {
                    (void)name;
                    conflicted_buttons_.insert(code);
                }
                return {"physical_input_unknown", "Raw input provenance is unavailable"};
            }
            const auto *raw = static_cast<const XIRawEvent *>(event.xcookie.data);
            const bool physical = !injected_sources_.contains(raw->sourceid);
            if (physical) {
                for (const auto &[name, code] : keys_) {
                    (void)name;
                    if ((raw->evtype == XI_RawKeyPress || raw->evtype == XI_RawKeyRelease) &&
                        raw->detail == static_cast<int>(code))
                        conflicted_keys_.insert(code);
                }
                for (const auto &[name, code] : buttons_) {
                    (void)name;
                    if ((raw->evtype == XI_RawButtonPress || raw->evtype == XI_RawButtonRelease) &&
                        raw->detail == static_cast<int>(code))
                        conflicted_buttons_.insert(code);
                }
            }
            XFreeEventData(display_, &event.xcookie);
            physical_seen |= physical;
        }
        if (physical_seen)
            return {"physical_takeover", "Physical input interrupted controlled input"};
        if (target_changed)
            return {"target_changed", "Controlled input target changed"};
        std::array<char, 32> state{};
        XQueryKeymap(display_, state.data());
        for (unsigned code = 1; code < 256; ++code)
            if ((static_cast<unsigned char>(state[code / 8]) & (1u << (code % 8))) &&
                std::none_of(keys_.begin(), keys_.end(),
                             [&](const auto &item) { return item.second == code; }))
                return {"physical_input_busy", "An unowned key is pressed"};
        unsigned pointer_mask = 0;
        if (!pointer(pointer_mask, false))
            return {"input_state_changed", "Pointer state is unavailable"};
        unsigned owned_mask = 0;
        for (const auto &[name, code] : buttons_) {
            (void)name;
            owned_mask |= Button1Mask << (code - 1);
        }
        if ((pointer_mask & (Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)) &
            ~owned_mask)
            return {"physical_input_busy", "An unowned button is pressed"};
        Window focus = 0;
        int revert = 0;
        XGetInputFocus(display_, &focus, &revert);
        XWindowAttributes attributes{};
        Window child = 0;
        int x = 0, y = 0;
        const auto &g = scope_.geometry;
        if (!within(focus) || !XGetWindowAttributes(display_, window_, &attributes) ||
            attributes.map_state != IsViewable || attributes.override_redirect ||
            !XTranslateCoordinates(display_, window_, DefaultRootWindow(display_), 0, 0, &x, &y,
                                   &child) ||
            x != g.x || y != g.y || attributes.width != g.width || attributes.height != g.height)
            return {"target_changed", "Controlled input focus or geometry changed"};
        for (const auto &[name, code] : keys_) {
            (void)name;
            if (!pressed(code)) {
                lost_ = true;
                return {"input_state_changed", "Owned key state changed externally"};
            }
        }
        if (!buttons_.empty()) {
            unsigned mask = 0;
            if (!pointer(mask, true))
                return {"target_changed", "Held pointer left the controlled target"};
            for (const auto &[name, code] : buttons_) {
                (void)name;
                if (!(mask & (Button1Mask << (code - 1)))) {
                    lost_ = true;
                    return {"input_state_changed", "Owned button state changed externally"};
                }
            }
        }
        return {};
    }
    InputSessionDelivery apply(const InputSessionCommand &command,
                               const CancelToken &cancel) override {
        bool cancelled = false;
        const auto stopped = [&] {
            cancelled = cancelled || cancel.cancelled();
            return cancelled;
        };
        const auto fail = [&](ProviderError error) {
            return InputSessionDelivery{false, stopped(), effects_, std::move(error)};
        };
        if (stopped())
            return fail({"cancelled", "Input command cancelled"});
        if (const auto error = check(); !error.code.empty())
            return fail(error);
        if (const auto invalid = validate_input_session_command(
                command, {capabilities(),
                          static_cast<std::uint32_t>(keys_.size() + buttons_.size()), commands_}))
            return fail(*invalid);
        using K = InputSessionCommand::Kind;
        const bool key =
            command.kind == K::key_down || command.kind == K::key_up || command.kind == K::key_tap;
        const bool tap = command.kind == K::key_tap || command.kind == K::button_tap;
        const bool up = command.kind == K::key_up || command.kind == K::button_up;
        if (command.kind == K::smooth_move)
            return fail({"unsupported_platform", "Held pointer travel is not enabled"});
        auto &owned = key ? keys_ : buttons_;
        const auto &name = key ? command.key_name : command.button;
        const auto code = key ? keycode(name) : button_code(name);
        if (!code)
            return fail({"unsupported_key", "Input has no unmodified physical mapping"});
        if (stopped())
            return fail({"cancelled", "Input command cancelled before dispatch"});
        ++commands_;
        if (up) {
            const auto found = owned.find(name);
            if (found == owned.end())
                return fail({"invalid_input_transition", "Input is not owned"});
            if (!transition(code, key, false))
                return fail({"input_release_failed", "Input release could not be confirmed"});
            owned.erase(found);
            std::erase(order_, std::pair{key, name});
        } else {
            if (keys_.size() + buttons_.size() >= input_session_budget::max_held_inputs)
                return fail({"result_too_large", "Held input budget exhausted"});
            if (std::any_of(owned.begin(), owned.end(),
                            [&](const auto &item) { return item.second == code; }))
                return fail({"invalid_input_transition", "Physical input is already owned"});
            unsigned mask = 0;
            if ((key && pressed(code)) ||
                (!key &&
                 (!pointer(mask, true) || (mask & (Button1Mask | Button2Mask | Button3Mask)))))
                return fail({"physical_input_busy",
                             "Input is already pressed or pointer target is invalid"});
            if (stopped())
                return fail({"cancelled", "Input command cancelled before press"});
            owned.emplace(name, code); // Claim before the call, including ambiguous delivery.
            order_.emplace_back(key, name);
            if (!transition(code, key, true))
                return fail({"input_dispatch_failed", "Input press could not be confirmed"});
            if (tap) {
                const auto dwell =
                    command.dwell.count()
                        ? command.dwell
                        : input_cadence_delay(seed_, InputCadencePhase::key_dwell, code);
                ProviderError interrupted;
                const bool waited = pace_input_for(dwell, [&] {
                    if (stopped())
                        return false;
                    interrupted = check();
                    return interrupted.code.empty();
                });
                if (!waited)
                    return fail(interrupted.code.empty()
                                    ? ProviderError{"cancelled", "Input press interrupted"}
                                    : interrupted);
                if (!transition(code, key, false))
                    return fail({"input_release_failed", "Input release could not be confirmed"});
                owned.erase(name);
                std::erase(order_, std::pair{key, name});
            }
        }
        return {true, false, effects_, {}};
    }
    InputOutcome release() override {
        // Drain pending physical events before cleanup, even after task cancellation.
        (void)check();
        bool ok = !lost_ && conflicted_keys_.empty() && conflicted_buttons_.empty();
        for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
            const auto &owned = it->first ? keys_ : buttons_;
            const auto found = owned.find(it->second);
            if (found == owned.end())
                continue;
            const auto &conflicts = it->first ? conflicted_keys_ : conflicted_buttons_;
            if (!conflicts.contains(found->second))
                ok = transition(found->second, it->first, false) && ok;
        }
        order_.clear();
        keys_.clear();
        buttons_.clear();
        return {
            ok, false,
            ok ? ProviderError{}
               : ProviderError{"input_release_failed", "Input ownership or release is uncertain"},
            effects_, ok};
    }
};
} // namespace
#endif
std::unique_ptr<desktop::InputSessionBackend>
open_x11_input_session(const std::string &name, const desktop::PointerActionScope &scope,
                       const desktop::CancelToken &cancel) {
#ifdef MIRAGE_LINUX_XINPUT2
    auto session = std::make_unique<Session>();
    if (session->open(name, scope, cancel))
        return session;
#else
    (void)name;
    (void)scope;
    (void)cancel;
#endif
    return {};
}
} // namespace mirage::platform::linux_backend
