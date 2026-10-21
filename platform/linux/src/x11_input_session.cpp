#include "x11_input_session.hpp"
#include "input_pacing.hpp"
#include <X11/XKBlib.h>
#include <X11/Xatom.h>
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
Window window_property(Display *display, Window window, const char *name) {
    Atom actual = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    const auto property = XInternAtom(display, name, True);
    if (!property)
        return 0;
    const auto status = XGetWindowProperty(display, window, property, 0, 1, False, XA_WINDOW,
                                           &actual, &format, &count, &remaining, &data);
    const auto result =
        status == Success && actual == XA_WINDOW && format == 32 && count == 1 && !remaining && data
            ? *reinterpret_cast<unsigned long *>(data)
            : 0;
    if (data)
        XFree(data);
    return result;
}
bool text_property(Display *display, Window window, const char *name, const char *expected) {
    const auto property = XInternAtom(display, name, True);
    if (!property)
        return false;
    Atom actual = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *data = nullptr;
    const auto status = XGetWindowProperty(display, window, property, 0, 16, False, AnyPropertyType,
                                           &actual, &format, &count, &remaining, &data);
    const bool matches = status == Success && format == 8 && !remaining && data &&
                         std::string_view{reinterpret_cast<char *>(data), count} == expected;
    if (data)
        XFree(data);
    return matches;
}
Window switcher_manager(Display *display) {
    const auto root = DefaultRootWindow(display);
    const auto wm = window_property(display, root, "_NET_SUPPORTING_WM_CHECK");
    return wm && window_property(display, wm, "_NET_SUPPORTING_WM_CHECK") == wm &&
                   text_property(display, wm, "_NET_WM_NAME", "Openbox") &&
                   text_property(display, root, "_OB_VERSION", "3.6.1")
               ? wm
               : 0;
}
class Session final : public InputSessionBackend {
    Display *display_ = nullptr;
    PointerActionScope scope_;
    Window window_ = 0;
    Window switcher_manager_ = 0;
    int xi_opcode_ = 0;
    std::set<int> injected_sources_;
    struct DeviceRoute {
        int use = 0, attachment = 0;
    };
    std::map<int, DeviceRoute> device_route_;
    std::map<std::string, unsigned> keys_, buttons_;
    std::vector<std::pair<bool, std::string>> order_;
    std::set<unsigned> conflicted_keys_, conflicted_buttons_;
    bool effects_ = false, lost_ = false, device_changed_ = false, route_lost_ = false;
    std::uint64_t seed_ = 0;
    std::uint32_t commands_ = 0;
    void fence_owned() {
        for (const auto &[name, code] : keys_) {
            (void)name;
            conflicted_keys_.insert(code);
        }
        for (const auto &[name, code] : buttons_) {
            (void)name;
            conflicted_buttons_.insert(code);
        }
    }
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
    bool contains(PointerPoint point) const {
        const auto &g = scope_.geometry;
        return point.x >= g.x && point.y >= g.y &&
               std::int64_t{point.x} < std::int64_t{g.x} + g.width &&
               std::int64_t{point.y} < std::int64_t{g.y} + g.height && point.x >= 0 &&
               point.y >= 0 && point.x < DisplayWidth(display_, DefaultScreen(display_)) &&
               point.y < DisplayHeight(display_, DefaultScreen(display_));
    }
    bool pointer(unsigned &mask, bool require_target, PointerPoint *position = nullptr) const {
        Window root = 0, child = 0;
        int x = 0, y = 0, wx = 0, wy = 0;
        if (!XQueryPointer(display_, DefaultRootWindow(display_), &root, &child, &x, &y, &wx, &wy,
                           &mask))
            return false;
        if (position)
            *position = {x, y};
        return !require_target || (within(child) && contains({x, y}));
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
    bool open(const std::string &name, const PointerActionScope &scope, const CancelToken &cancel,
              bool window_switcher) {
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
        if (window_switcher && !(switcher_manager_ = switcher_manager(display_)))
            return false;
        int event = 0, error = 0, major = 2, minor = 1;
        if (!XQueryExtension(display_, "XInputExtension", &xi_opcode_, &event, &error) ||
            XIQueryVersion(display_, &major, &minor) != Success || major < 2 ||
            (major == 2 && minor < 1))
            return false;
        // Subscribe before discovering sources: a change during admission must
        // be observed by the first guard, never silently accepted as a new route.
        unsigned char raw_mask[XIMaskLen(XI_LASTEVENT)]{}, device_mask[XIMaskLen(XI_LASTEVENT)]{};
        for (const int type : {XI_RawKeyPress, XI_RawKeyRelease, XI_RawButtonPress,
                               XI_RawButtonRelease, XI_RawMotion})
            XISetMask(raw_mask, type);
        XISetMask(device_mask, XI_HierarchyChanged);
        XISetMask(device_mask, XI_DeviceChanged);
        XIEventMask selections[]{
            {XIAllMasterDevices, static_cast<int>(sizeof(raw_mask)), raw_mask},
            {XIAllDevices, static_cast<int>(sizeof(device_mask)), device_mask}};
        if (XISelectEvents(display_, DefaultRootWindow(display_), selections, 2) != Success)
            return false;
        // Resolve the same client pointer that core XTEST / QueryKeymap use.
        unsigned pointer_mask = 0;
        int client_pointer = 0;
        if (!pointer(pointer_mask, false) || !XIGetClientPointer(display_, None, &client_pointer))
            return false;
        int count = 0;
        XIDeviceInfo *devices = XIQueryDevice(display_, XIAllDevices, &count);
        const std::unique_ptr<XIDeviceInfo, decltype(&XIFreeDeviceInfo)> device_owner{
            devices, XIFreeDeviceInfo};
        if (!devices || count <= 0 || count > 256)
            return false;
        const auto find_device = [&](int device_id) -> const XIDeviceInfo * {
            for (int n = 0; n < count; ++n)
                if (devices[n].deviceid == device_id)
                    return &devices[n];
            return nullptr;
        };
        const auto *master_pointer = find_device(client_pointer);
        const auto *master_keyboard =
            master_pointer ? find_device(master_pointer->attachment) : nullptr;
        if (!master_pointer || !master_keyboard || !master_pointer->enabled ||
            !master_keyboard->enabled || master_pointer->use != XIMasterPointer ||
            master_keyboard->use != XIMasterKeyboard ||
            master_keyboard->attachment != client_pointer)
            return false;
        for (const auto *device : {master_pointer, master_keyboard})
            device_route_.emplace(device->deviceid, DeviceRoute{device->use, device->attachment});
        for (int n = 0; n < count; ++n) {
            const std::string_view device_name = devices[n].name ? devices[n].name : "";
            const bool pointer_source = device_name == "Virtual core XTEST pointer";
            const bool keyboard_source = device_name == "Virtual core XTEST keyboard";
            if (pointer_source || keyboard_source) {
                const auto &device = devices[n];
                if (!device.enabled ||
                    device.use != (pointer_source ? XISlavePointer : XISlaveKeyboard) ||
                    device.attachment !=
                        (pointer_source ? client_pointer : master_keyboard->deviceid))
                    return false;
                injected_sources_.insert(devices[n].deviceid);
                device_route_.emplace(device.deviceid, DeviceRoute{device.use, device.attachment});
            }
        }
        if (injected_sources_.size() != 2 || device_route_.size() != 4)
            return false;
        XSelectInput(display_, window_, StructureNotifyMask);
        XSync(display_, False);
        seed_ = make_pointer_motion_variation(0).seed;
        return true;
    }
    InputCapabilitySet capabilities() const override {
        return InputCapabilitySet{}
            .with(InputCapability::software_injection)
            .with(InputCapability::smooth_move)
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
                fence_owned();
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
                fence_owned();
                return {"physical_input_unknown", "Raw input provenance is unavailable"};
            }
            if (event.xcookie.evtype == XI_HierarchyChanged) {
                const auto *hierarchy = static_cast<const XIHierarchyEvent *>(event.xcookie.data);
                device_changed_ = true;
                if (hierarchy->num_info <= 0 || hierarchy->num_info > 256 || !hierarchy->info)
                    route_lost_ = true;
                else
                    for (int i = 0; i < hierarchy->num_info; ++i) {
                        const auto &info = hierarchy->info[i];
                        const auto route = device_route_.find(info.deviceid);
                        if (route != device_route_.end() &&
                            (!info.enabled || info.use != route->second.use ||
                             info.attachment != route->second.attachment ||
                             (info.flags & (XIMasterRemoved | XISlaveRemoved | XISlaveDetached |
                                            XIDeviceDisabled))))
                            route_lost_ = true;
                    }
                XFreeEventData(display_, &event.xcookie);
                continue; // Drain physical events too, before deciding which inputs to release.
            }
            if (event.xcookie.evtype == XI_DeviceChanged) {
                const auto *changed = static_cast<const XIDeviceChangedEvent *>(event.xcookie.data);
                if (changed->reason != XISlaveSwitch) {
                    device_changed_ = true;
                    route_lost_ |= device_route_.contains(changed->deviceid) ||
                                   injected_sources_.contains(changed->sourceid);
                }
                XFreeEventData(display_, &event.xcookie);
                continue;
            }
            const auto *raw = static_cast<const XIRawEvent *>(event.xcookie.data);
            if (raw->sourceid <= 1 ||
                (injected_sources_.contains(raw->sourceid) &&
                 raw->deviceid != device_route_.at(raw->sourceid).attachment)) {
                fence_owned();
                XFreeEventData(display_, &event.xcookie);
                return {"physical_input_unknown",
                        "Raw input source does not match the admitted route"};
            }
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
        if (route_lost_) {
            fence_owned();
            return {"input_device_lost", "The admitted input route is no longer reliable"};
        }
        if (physical_seen)
            return {"physical_takeover", "Physical input interrupted controlled input"};
        if (device_changed_)
            return {"input_device_changed", "Device hierarchy changed; readmit explicitly"};
        if (switcher_manager_ && switcher_manager(display_) != switcher_manager_)
            return {"target_changed", "Window-switcher manager identity changed"};
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
        const auto parsed_key = parse_session_key_name(command.key_name);
        const bool alt = parsed_key && parsed_key->modifier == SessionKeyName::Modifier::alt;
        const bool shift = parsed_key && parsed_key->modifier == SessionKeyName::Modifier::shift;
        const bool tab = command.key_name == "tab";
        const bool holding_alt = std::any_of(keys_.begin(), keys_.end(), [](const auto &item) {
            const auto parsed = parse_session_key_name(item.first);
            return parsed && parsed->modifier == SessionKeyName::Modifier::alt;
        });
        if (switcher_manager_) {
            if (!(key && ((alt && command.kind == K::key_down && keys_.empty()) ||
                          (shift && holding_alt && (command.kind == K::key_down || up)) ||
                          (tab && holding_alt && command.kind == K::key_tap))))
                return fail({"invalid_input_context",
                             "Switcher accepts only owned Alt/Shift/Tab; release to commit"});
        } else if ((tab && holding_alt) || (alt && keys_.contains("tab"))) {
            return fail(
                {"invalid_input_context", "Use the explicit window-switcher context for Alt-Tab"});
        }
        if (command.kind == K::smooth_move) {
            unsigned mask = 0;
            PointerPoint origin;
            if (!pointer(mask, true, &origin) || !contains({command.x, command.y}))
                return fail({"target_changed", "Pointer travel must stay inside the bound window"});
            const auto path = build_smooth_move_schedule(origin, {command.x, command.y},
                                                         command.duration, command.path_points);
            // Validate the whole path before dispatch. Held travel deliberately uses the
            // monotonic minimum-jerk baseline: no overshoot beyond the bound target.
            if (std::any_of(path.begin(), path.end(),
                            [&](const auto &sample) { return !contains({sample.x, sample.y}); }))
                return fail({"target_changed", "Pointer path leaves the bound window"});
            ++commands_;
            const auto started = std::chrono::steady_clock::now();
            for (const auto &sample : path) {
                ProviderError interrupted;
                const bool waited = pace_input_until(started + sample.at, [&] {
                    if (stopped())
                        return false;
                    interrupted = check();
                    if (interrupted.code.empty() && !pointer(mask, true))
                        interrupted = {"target_changed", "Pointer left the bound window"};
                    return interrupted.code.empty();
                });
                if (!waited)
                    return fail(!interrupted.code.empty()
                                    ? interrupted
                                    : ProviderError{cancelled ? "cancelled" : "input_motion_late",
                                                    "Pointer travel interrupted; no replay"});
                effects_ = true;
                if (!XTestFakeMotionEvent(display_, DefaultScreen(display_), sample.x, sample.y,
                                          CurrentTime))
                    return fail({"input_dispatch_failed", "Pointer motion was not accepted"});
                // Deliver every segment now; a buffered trajectory would become a teleport.
                XSync(display_, False);
                PointerPoint actual;
                if (!pointer(mask, true, &actual) || actual.x != sample.x || actual.y != sample.y)
                    return fail({"input_state_changed", "Pointer motion readback changed"});
            }
            if (const auto error = check(); !error.code.empty())
                return fail(error);
            return {true, false, effects_, {}};
        }
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
                       const desktop::CancelToken &cancel, bool window_switcher) {
#ifdef MIRAGE_LINUX_XINPUT2
    auto session = std::make_unique<Session>();
    if (session->open(name, scope, cancel, window_switcher))
        return session;
#else
    (void)name;
    (void)scope;
    (void)cancel;
    (void)window_switcher;
#endif
    return {};
}
bool x11_window_switcher_available(const std::string &name) {
#ifdef MIRAGE_LINUX_XINPUT2
    Display *display = XOpenDisplay(name.c_str());
    if (!display)
        return false;
    const bool available = switcher_manager(display) != 0;
    XCloseDisplay(display);
    return available;
#else
    (void)name;
    return false;
#endif
}
} // namespace mirage::platform::linux_backend
