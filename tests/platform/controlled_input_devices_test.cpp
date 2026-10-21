#include "../../integration/mira/src/controlled_input_session.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <array>
#include <atomic>
#include <kairo/executor.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <set>
#include <stdexcept>

// Xlib macros follow the pinned C++ headers.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace d = mirage::desktop;
using Session = mirage::integration::detail::ControlledInputSession;
namespace {
std::atomic<int> version_fault{0}, lost_source{0};
}
extern "C" int __real_XIQueryVersion(Display *, int *, int *);
extern "C" int __wrap_XIQueryVersion(Display *display, int *major, int *minor) {
    const auto fault = version_fault.load();
    if (fault == 2)
        return BadRequest;
    const auto status = __real_XIQueryVersion(display, major, minor);
    if (fault == 1 && status == Success) {
        *major = 2;
        *minor = 0;
    }
    return status;
}
extern "C" Bool __real_XGetEventData(Display *, XGenericEventCookie *);
extern "C" Bool __wrap_XGetEventData(Display *display, XGenericEventCookie *cookie) {
    const auto result = __real_XGetEventData(display, cookie);
    if (result && cookie->evtype == XI_HierarchyChanged && cookie->data && lost_source.load()) {
        auto *event = static_cast<XIHierarchyEvent *>(cookie->data);
        const auto source = lost_source.exchange(0);
        for (int n = 0; n < event->num_info; ++n)
            if (event->info[n].deviceid == source) {
                event->info[n].enabled = False;
                event->info[n].flags |= XIDeviceDisabled;
            }
    }
    return result;
}
namespace {
struct Fixture {
    mirage::testing::XvfbDisplay server{mirage::testing::find_xvfb()};
    mirage::platform::linux_backend::LinuxDesktopEnvironment environment{
        {}, {.enabled = true, .display = server.display_name()}};
    Display *client = XOpenDisplay(server.display_name().c_str());
    Window window = 0;
    XDevice *keyboard = nullptr, *mouse = nullptr;
    int keyboard_id = 0, xtest_keyboard_id = 0;
    d::PointerActionScope scope;
    std::set<unsigned> external_keys, external_buttons;
    Fixture() {
        if (!client)
            throw std::runtime_error("private X11 device fixture unavailable");
        int major = 2, minor = 1;
        if (XIQueryVersion(client, &major, &minor) != Success)
            throw std::runtime_error("XI 2.1 fixture unavailable");
        int count = 0;
        auto *devices = XIQueryDevice(client, XIAllDevices, &count);
        int mouse_id = 0;
        for (int n = 0; devices && n < count; ++n) {
            const std::string name = devices[n].name ? devices[n].name : "";
            if (name == "Xvfb keyboard")
                keyboard_id = devices[n].deviceid;
            if (name == "Xvfb mouse")
                mouse_id = devices[n].deviceid;
            if (name == "Virtual core XTEST keyboard")
                xtest_keyboard_id = devices[n].deviceid;
        }
        if (devices)
            XIFreeDeviceInfo(devices);
        if (!keyboard_id || !mouse_id || !(keyboard = XOpenDevice(client, keyboard_id)) ||
            !(mouse = XOpenDevice(client, mouse_id)))
            throw std::runtime_error("Xvfb slave devices unavailable");
        window = XCreateSimpleWindow(client, DefaultRootWindow(client), 40, 40, 360, 240, 0, 0,
                                     0xffffff);
        XSelectInput(client, window,
                     KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
        XMapWindow(client, window);
        XSetInputFocus(client, window, RevertToParent, CurrentTime);
        XWarpPointer(client, None, window, 0, 0, 0, 0, 100, 100);
        XSync(client, False);
        const auto front = environment.window()->front_window({});
        if (!front.ok || !front.found)
            throw std::runtime_error("Xvfb target unavailable");
        scope = {front.window.id, front.window.geometry};
    }
    ~Fixture() {
        if (!client)
            return;
        XUngrabKeyboard(client, CurrentTime);
        XUngrabPointer(client, CurrentTime);
        // Controlled fixture cleanup only. Product cleanup must never release
        // these unowned slave inputs or erase an uncertain ownership fence.
        for (auto code : external_keys)
            XTestFakeDeviceKeyEvent(client, keyboard, code, False, nullptr, 0, CurrentTime);
        for (auto button : external_buttons)
            XTestFakeDeviceButtonEvent(client, mouse, button, False, nullptr, 0, CurrentTime);
        XTestFakeKeyEvent(client, XKeysymToKeycode(client, XK_Shift_L), False, CurrentTime);
        XTestFakeButtonEvent(client, 1, False, CurrentTime);
        XSync(client, False);
        if (keyboard)
            XCloseDevice(client, keyboard);
        if (mouse)
            XCloseDevice(client, mouse);
        XCloseDisplay(client);
    }
    void key(unsigned symbol, bool down) {
        const auto code = XKeysymToKeycode(client, symbol);
        external_keys.insert(code);
        MIRAGE_CHECK(
            XTestFakeDeviceKeyEvent(client, keyboard, code, down, nullptr, 0, CurrentTime));
    }
    void button(unsigned code, bool down) {
        external_buttons.insert(code);
        MIRAGE_CHECK(
            XTestFakeDeviceButtonEvent(client, mouse, code, down, nullptr, 0, CurrentTime));
    }
    bool pressed(unsigned symbol) {
        std::array<char, 32> keys{};
        XQueryKeymap(client, keys.data());
        const auto code = XKeysymToKeycode(client, symbol);
        return (static_cast<unsigned char>(keys[code / 8]) & (1u << (code % 8))) != 0;
    }
    unsigned buttons() {
        Window root, child;
        int x, y, wx, wy;
        unsigned mask = 0;
        MIRAGE_CHECK(XQueryPointer(client, DefaultRootWindow(client), &root, &child, &x, &y, &wx,
                                   &wy, &mask));
        return mask;
    }
};
void await_terminal(kairo::Executor &executor, Session &session) {
    for (int n = 0; n < 200; ++n) {
        const auto state = session.snapshot();
        if (state.state == d::InputSessionState::released ||
            state.state == d::InputSessionState::fenced)
            return;
        auto wait = executor.submit_delayed(5, [] {});
        wait.future.get();
    }
}
void competition(kairo::Executor &executor, bool button, bool same, bool grab) {
    Fixture f;
    auto owner = std::make_shared<d::ActionCoordinator>();
    Session session(executor, owner, {},
                    [&] { return f.environment.input()->open_input_session(f.scope, {}); }, {});
    auto state = session.begin(owner->epoch());
    MIRAGE_CHECK(state.state == d::InputSessionState::ready);
    if (grab) {
        if (button)
            MIRAGE_CHECK(XGrabPointer(f.client, f.window, False,
                                      ButtonPressMask | ButtonReleaseMask, GrabModeAsync,
                                      GrabModeAsync, None, None, CurrentTime) == GrabSuccess);
        else
            MIRAGE_CHECK(XGrabKeyboard(f.client, f.window, False, GrabModeAsync, GrabModeAsync,
                                       CurrentTime) == GrabSuccess);
        XSync(f.client, False);
    }
    d::InputSessionCommand command;
    command.kind =
        button ? d::InputSessionCommand::Kind::button_down : d::InputSessionCommand::Kind::key_down;
    if (button)
        command.button = "left";
    else
        command.key_name = "shift";
    state = session.advance(1, state.state_version, command);
    MIRAGE_CHECK(state.outcome == d::InputSessionOutcome::holding);
    // A non-XTEST slave source sends real XI raw events on this private server.
    // The same-key state was already down, so QueryKeymap alone cannot catch it.
    if (button)
        f.button(same ? 1 : 3, true);
    else
        f.key(same ? XK_Shift_L : XK_Control_L, true);
    XSync(f.client, False);
    await_terminal(executor, session);
    state = session.snapshot();
    if (same) {
        MIRAGE_CHECK(state.state == d::InputSessionState::fenced && !state.release_confirmed);
        MIRAGE_CHECK(state.error.code == "input_release_failed");
        MIRAGE_CHECK(!owner->input_released() && !owner->resume());
        MIRAGE_CHECK(button ? state.held_buttons == std::vector<std::string>{"left"}
                            : state.held_keys == std::vector<std::string>{"shift"});
        MIRAGE_CHECK(button ? (f.buttons() & Button1Mask) != 0 : f.pressed(XK_Shift_L));
    } else {
        MIRAGE_CHECK(state.state == d::InputSessionState::released && state.release_confirmed);
        // A state query can see a newly held unowned input just before its raw
        // event is drained. Both paths must stop input and preserve that input.
        MIRAGE_CHECK(state.cancelled && (state.error.code == "physical_takeover" ||
                                         state.error.code == "physical_input_busy"));
        MIRAGE_CHECK(button ? !(f.buttons() & Button1Mask) : !f.pressed(XK_Shift_L));
        MIRAGE_CHECK(button ? (f.buttons() & Button3Mask) != 0 : f.pressed(XK_Control_L));
        MIRAGE_CHECK(owner->input_released() && !owner->try_begin({}));
    }
    session.stop();
}
void topology(kairo::Executor &executor, bool held, bool lose_route = false) {
    Fixture f;
    auto owner = std::make_shared<d::ActionCoordinator>();
    Session session(executor, owner, {},
                    [&] { return f.environment.input()->open_input_session(f.scope, {}); }, {});
    auto state = session.begin(owner->epoch());
    if (held) {
        d::InputSessionCommand command;
        command.kind = d::InputSessionCommand::Kind::key_down;
        command.key_name = "shift";
        state = session.advance(1, state.state_version, command);
        MIRAGE_CHECK(state.outcome == d::InputSessionOutcome::holding);
    }
    XIAnyHierarchyChangeInfo change{};
    char name[] = "Mirage topology fixture";
    change.add = {XIAddMaster, name, True, True};
    // Xvfb protects its XTEST devices from disabling/removal. Fault-inject the
    // boundary metadata for this one event; the server's real Shift stays down
    // so an improper cleanup up would be observable below.
    if (lose_route)
        lost_source = f.xtest_keyboard_id;
    MIRAGE_CHECK(XIChangeHierarchy(f.client, &change, 1) == Success);
    XSync(f.client, False);
    await_terminal(executor, session);
    state = session.snapshot();
    MIRAGE_CHECK(lost_source == 0);
    MIRAGE_CHECK(state.release_confirmed == !lose_route);
    MIRAGE_CHECK(state.error.code ==
                 (lose_route ? "input_release_failed" : "input_device_changed"));
    MIRAGE_CHECK(f.pressed(XK_Shift_L) == lose_route && owner->input_released() == !lose_route);
    if (lose_route)
        MIRAGE_CHECK(!owner->resume() && state.held_keys == std::vector<std::string>{"shift"});
    // A terminal session cannot silently adopt the new device graph or replay.
    d::InputSessionCommand late;
    late.kind = d::InputSessionCommand::Kind::key_down;
    late.key_name = "shift";
    MIRAGE_CHECK(session.advance(2, state.state_version, late).state == state.state);
    MIRAGE_CHECK(f.pressed(XK_Shift_L) == lose_route);
    session.stop();
}
void version_admission() {
    Fixture f;
    for (const int fault : {1, 2}) {
        version_fault = fault;
        mirage::platform::linux_backend::LinuxDesktopEnvironment environment{
            {}, {.enabled = true, .display = f.server.display_name()}};
        MIRAGE_CHECK(environment.input() && !environment.input()->controlled_input_supported());
        MIRAGE_CHECK(environment.input()->complete_actions_supported());
        MIRAGE_CHECK(!environment.input()->open_input_session(f.scope, {}));
        MIRAGE_CHECK(!f.pressed(XK_Shift_L) && !(f.buttons() & Button1Mask));
        version_fault = 0;
    }
    MIRAGE_CHECK(f.environment.input()->controlled_input_supported());
}
void event_pressure(kairo::Executor &executor) {
    Fixture f;
    auto owner = std::make_shared<d::ActionCoordinator>();
    Session session(executor, owner, {},
                    [&] { return f.environment.input()->open_input_session(f.scope, {}); }, {});
    auto state = session.begin(owner->epoch());
    d::InputSessionCommand command;
    command.kind = d::InputSessionCommand::Kind::key_down;
    command.key_name = "shift";
    state = session.advance(1, state.state_version, command);
    bool injected = false;
    d::CancelToken burst([&] {
        if (!injected) {
            injected = true;
            // One synchronous callback on the owning worker makes the backlog
            // deterministic. It has no private thread or input event queue.
            for (int n = 0; n < 160; ++n) {
                f.key(XK_Control_L, true);
                f.key(XK_Control_L, false);
            }
            XSync(f.client, False);
        }
        return false;
    });
    command.kind = d::InputSessionCommand::Kind::key_up;
    state = session.advance(2, state.state_version, command, burst);
    MIRAGE_CHECK(injected && state.state == d::InputSessionState::fenced);
    MIRAGE_CHECK(!state.release_confirmed && state.error.code == "input_release_failed");
    MIRAGE_CHECK(f.pressed(XK_Shift_L) && !owner->resume());
    session.stop();
}
} // namespace
int main() {
    kairo::Executor executor;
    MIRAGE_CHECK(executor.initialize({}).ok);
    try {
        executor
            .submit_auto([&] {
                for (const bool button : {false, true})
                    for (const bool same : {false, true})
                        for (const bool grab : {false, true})
                            competition(executor, button, same, grab);
                topology(executor, false);
                topology(executor, true);
                topology(executor, true, true);
                event_pressure(executor);
                version_admission();
            })
            .get();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "controlled input devices: %s\n", error.what());
        executor.shutdown(true);
        return 1;
    }
    executor.shutdown(true);
    return mirage::testing::finish("controlled_input_devices");
}
