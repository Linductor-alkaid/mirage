#include "../support/provider_tool_name.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <algorithm>
#include <kairo/executor.hpp>
#include <limits>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <poll.h>
#include <stdexcept>
#include <vector>

// Xlib's None macro must follow the pinned C++ headers.
#include "../support/x11_pointer_diagnostics.hpp"
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using Json = mira::JsonValue;
namespace {
class Fixture {
  public:
    mirage::testing::XvfbDisplay server{mirage::testing::find_xvfb()};
    mirage::platform::linux_backend::LinuxDesktopEnvironment env{
        {}, {.enabled = true, .display = server.display_name()}};
    Display *client = XOpenDisplay(server.display_name().c_str());
    Window window = 0, other = 0;
    std::vector<XEvent> events;
    d::PointerActionScope scope;
    Fixture() {
        MIRAGE_CHECK(client != nullptr && env.input() &&
                     env.input()->bounded_pointer_actions_supported());
        if (!client)
            throw std::runtime_error("private X client unavailable");
        const auto root = DefaultRootWindow(client);
        window = XCreateSimpleWindow(client, root, 40, 40, 360, 240, 0, 0, 0xffffff);
        other = XCreateSimpleWindow(client, root, 440, 40, 100, 100, 0, 0, 0xffffff);
        XSelectInput(client, window, ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
        XMapWindow(client, window);
        XMapWindow(client, other);
        XSetInputFocus(client, window, RevertToParent, CurrentTime);
        XSync(client, False);
        // Backend is opened before the first client creates the UTF-8 title atom.
        const auto utf8 = XInternAtom(client, "UTF8_STRING", False);
        const auto name = XInternAtom(client, "_NET_WM_NAME", False);
        const std::string modern = "Mirage · Agent";
        XChangeProperty(client, window, name, utf8, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(modern.data()),
                        static_cast<int>(modern.size()));
        XSync(client, False);
        MIRAGE_CHECK(env.window()->front_window({}).window.title == modern);
        XDeleteProperty(client, window, name);
        const char classic[] = "Mirage \xb7 Agent";
        XStoreName(client, window, classic);
        XSync(client, False);
        MIRAGE_CHECK(env.window()->front_window({}).window.title == modern);
        XStoreName(client, window, "Before");
        XSync(client, False);
        const auto front = env.window()->front_window({});
        MIRAGE_CHECK(front.ok && front.found);
        scope = {front.window.id, front.window.geometry};
    }
    ~Fixture() { XCloseDisplay(client); }
    unsigned int mask() {
        Window root = 0, child = 0;
        int rx = 0, ry = 0, wx = 0, wy = 0;
        unsigned int result = 0;
        MIRAGE_CHECK(XQueryPointer(client, DefaultRootWindow(client), &root, &child, &rx, &ry, &wx,
                                   &wy, &result));
        return result;
    }
    void drain() {
        XSync(client, False);
        while (XPending(client)) {
            MIRAGE_CHECK(events.size() < 128);
            XEvent event{};
            XNextEvent(client, &event);
            if (event.type == ButtonPress || event.type == ButtonRelease ||
                event.type == MotionNotify)
                events.push_back(event);
        }
    }
    int count(int type, unsigned int button = 0) {
        drain();
        int result = 0;
        for (const auto &event : events)
            result += event.type == type && (!button || event.xbutton.button == button);
        return result;
    }
    void reset() {
        drain();
        events.clear();
    }
};

void normal_and_budgets() {
    Fixture f;
    auto *input = f.env.input();
    const auto scrolled = input->scroll_at(f.scope, {90, 100}, -2, 3, {}, {});
    MIRAGE_CHECK(scrolled.ok && scrolled.input_released && scrolled.side_effect_may_have_occurred);
    MIRAGE_CHECK(f.count(ButtonPress, 5) == 3 && f.count(ButtonRelease, 5) == 3);
    MIRAGE_CHECK(f.count(ButtonPress, 6) == 2 && f.count(ButtonRelease, 6) == 2);
    MIRAGE_CHECK(f.count(ButtonPress) == 5 && f.count(ButtonRelease) == 5);
    MIRAGE_CHECK(!(f.mask() & (Button1Mask | Button4Mask | Button5Mask)));
    f.reset();
    MIRAGE_CHECK(input->scroll_at(f.scope, {90, 100}, 1, -1, {}, {}).ok);
    MIRAGE_CHECK(f.count(ButtonPress, 4) == 1 && f.count(ButtonPress, 7) == 1);
    f.reset();
    for (const auto *button : {"left", "middle", "right"}) {
        f.reset();
        const auto code = std::string_view(button) == "left"     ? 1u
                          : std::string_view(button) == "middle" ? 2u
                                                                 : 3u;
        const auto held = code == 1 ? Button1Mask : code == 2 ? Button2Mask : Button3Mask;
        const auto dragged_button =
            input->drag_with_button(f.scope, {90, 100}, {300, 180}, button, {}, {});
        MIRAGE_CHECK(dragged_button.ok && dragged_button.input_released);
        MIRAGE_CHECK(f.count(ButtonPress, code) == 1 && f.count(ButtonRelease, code) == 1);
        bool motion = false;
        for (const auto &event : f.events)
            motion |= event.type == MotionNotify && (event.xmotion.state & held);
        MIRAGE_CHECK(motion && !(f.mask() & held));
        f.reset();
        // Cancel after the selected button's actual down; cleanup must use that button.
        mirage::desktop::CancelToken cancel([&] { return (f.mask() & held) != 0; });
        const auto stopped =
            input->drag_with_button(f.scope, {90, 100}, {300, 180}, button, {}, cancel);
        MIRAGE_CHECK(!stopped.ok && stopped.cancelled && stopped.input_released);
        MIRAGE_CHECK(f.count(ButtonPress, code) == 1 && f.count(ButtonRelease, code) == 1);
        MIRAGE_CHECK(!(f.mask() & held));
    }
    f.reset();
    MIRAGE_CHECK(!input->drag_with_button(f.scope, {90, 100}, {300, 180}, "invalid", {}, {}).ok);
    MIRAGE_CHECK(f.count(ButtonPress) == 0);
    const auto dragged = input->drag_to(f.scope, {90, 100}, {300, 180}, {}, {});
    MIRAGE_CHECK(dragged.ok && dragged.input_released && dragged.side_effect_may_have_occurred);
    MIRAGE_CHECK(f.count(ButtonPress, 1) == 1 && f.count(ButtonRelease, 1) == 1);
    const auto position = input->pointer_position({});
    MIRAGE_CHECK(position.ok && position.position.x == 300 && position.position.y == 180);
    MIRAGE_CHECK(!(f.mask() & Button1Mask));
    int held_motion = 0;
    for (const auto &event : f.events)
        if (event.type == MotionNotify && (event.xmotion.state & Button1Mask))
            ++held_motion;
    MIRAGE_CHECK(held_motion > 0 && held_motion <= 16);
    f.reset();
    for (const auto steps : {0, 17, std::numeric_limits<int>::min()}) {
        const auto failed = input->scroll_at(f.scope, {90, 100}, steps, 0, {}, {});
        MIRAGE_CHECK(!failed.ok && failed.error.code == "invalid_argument" &&
                     !failed.side_effect_may_have_occurred);
    }
    MIRAGE_CHECK(!input->scroll_at(f.scope, {90, 100}, 16, 1, {}, {}).ok);
    MIRAGE_CHECK(!input->drag_to(f.scope, {90, 100}, {90, 100}, {}, {}).ok);
    MIRAGE_CHECK(!input->drag_to(f.scope, {90, 100}, {400, 100}, {}, {}).ok);
    d::InputLimits invalid;
    invalid.timeout = std::chrono::milliseconds{1001};
    MIRAGE_CHECK(!input->scroll_at(f.scope, {90, 100}, 1, 0, invalid, {}).ok);
    MIRAGE_CHECK(f.count(ButtonPress) == 0 && f.count(ButtonRelease) == 0);
    MIRAGE_CHECK(!d::valid_drag_path({std::numeric_limits<int>::min(), 0},
                                     {std::numeric_limits<int>::max(), 0}));
    MIRAGE_CHECK(d::valid_drag_path({-2000, -100}, {0, -100}));
    MIRAGE_CHECK(!d::valid_drag_path({0, 0}, {2048, 1}));
}

void preflight_guards() {
    Fixture f;
    auto *input = f.env.input();
    d::CancelToken stopped;
    stopped.request_cancel();
    auto result = input->drag_to(f.scope, {90, 100}, {300, 180}, {}, stopped);
    MIRAGE_CHECK(result.cancelled && !result.side_effect_may_have_occurred);
    XTestFakeButtonEvent(f.client, 1, True, CurrentTime);
    XSync(f.client, False);
    f.reset();
    result = input->scroll_at(f.scope, {90, 100}, 0, 2, {}, {});
    MIRAGE_CHECK(result.error.code == "input_busy" && !result.side_effect_may_have_occurred);
    MIRAGE_CHECK(f.mask() & Button1Mask); // user's press remains owned by user
    MIRAGE_CHECK(f.count(ButtonRelease) == 0);
    XTestFakeButtonEvent(f.client, 1, False, CurrentTime);
    const auto ctrl = XKeysymToKeycode(f.client, XK_Control_L);
    XTestFakeKeyEvent(f.client, ctrl, True, CurrentTime);
    XSync(f.client, False);
    result = input->scroll_at(f.scope, {90, 100}, 0, 2, {}, {});
    MIRAGE_CHECK(result.error.code == "input_busy" && !result.side_effect_may_have_occurred);
    XTestFakeKeyEvent(f.client, ctrl, False, CurrentTime);
    XSetInputFocus(f.client, f.other, RevertToParent, CurrentTime);
    XSync(f.client, False);
    result = input->drag_to(f.scope, {90, 100}, {300, 180}, {}, {});
    MIRAGE_CHECK(result.error.code == "focus_changed" && !result.side_effect_may_have_occurred);
    XSetInputFocus(f.client, f.window, RevertToParent, CurrentTime);
    XMoveWindow(f.client, f.window, 41, 40);
    XSync(f.client, False);
    result = input->drag_to(f.scope, {90, 100}, {300, 180}, {}, {});
    MIRAGE_CHECK(result.error.code == "focus_changed" && !result.side_effect_may_have_occurred);
    XMoveWindow(f.client, f.window, 40, 40);
    // Overlay covers the start point without changing keyboard focus.
    XMoveResizeWindow(f.client, f.other, 70, 80, 80, 80);
    XRaiseWindow(f.client, f.other);
    XSync(f.client, False);
    f.reset();
    result = input->drag_to(f.scope, {90, 100}, {300, 180}, {}, {});
    MIRAGE_CHECK(!result.ok && result.error.code == "target_obscured" && result.input_released);
    MIRAGE_CHECK(f.count(ButtonPress) == 0 && !(f.mask() & Button1Mask));
}

void running_guards() {
    for (const auto *mode : {"cancel", "exception", "timeout", "focus", "move", "takeover"}) {
        Fixture f;
        const std::string scenario = mode;
        d::CancelToken stop;
        auto owner = std::make_shared<d::ActionCoordinator>();
        bool injected = false;
        d::CancelToken probe{[&] {
            if (!injected && (f.mask() & Button1Mask)) {
                injected = true;
                if (scenario == "cancel")
                    stop.request_cancel();
                if (scenario == "exception")
                    throw std::runtime_error("probe fault");
                if (scenario == "timeout")
                    (void)::poll(nullptr, 0, 550); // bounded test-only deadline fault
                if (scenario == "focus")
                    XSetInputFocus(f.client, f.other, RevertToParent, CurrentTime);
                if (scenario == "move")
                    XMoveWindow(f.client, f.window, 45, 40);
                if (scenario == "takeover")
                    owner->takeover();
                XSync(f.client, False);
            }
            return stop.cancelled();
        }};
        d::InputLimits limits;
        if (scenario == "timeout")
            limits.timeout = std::chrono::milliseconds{500};
        d::InputOutcome result;
        {
            auto lease = owner->try_begin(probe);
            MIRAGE_CHECK(static_cast<bool>(lease));
            result = f.env.input()->drag_to(f.scope, {90, 100}, {300, 180}, limits, probe);
            MIRAGE_CHECK(!result.ok && result.input_released &&
                         result.side_effect_may_have_occurred);
            MIRAGE_CHECK(injected && f.count(ButtonPress, 1) == 1 &&
                         f.count(ButtonRelease, 1) == 1);
            MIRAGE_CHECK(!(f.mask() & Button1Mask));
            if (scenario == "timeout")
                MIRAGE_CHECK(result.error.code == "timeout");
            else if (scenario == "focus" || scenario == "move")
                MIRAGE_CHECK(result.error.code == "focus_changed");
            else
                MIRAGE_CHECK(result.cancelled);
        }
        MIRAGE_CHECK(owner->input_released() && owner->resume());
    }
    // Cancel between wheel pulses: no extra pulse and no held wheel button.
    Fixture f;
    bool injected = false;
    d::CancelToken stop;
    d::CancelToken probe{[&] {
        if (!injected && f.count(ButtonRelease, 5) >= 1) {
            injected = true;
            stop.request_cancel();
        }
        return stop.cancelled();
    }};
    const auto result = f.env.input()->scroll_at(f.scope, {90, 100}, 0, 16, {}, probe);
    MIRAGE_CHECK(result.cancelled && result.input_released && injected);
    MIRAGE_CHECK(f.count(ButtonPress, 5) == 1 && f.count(ButtonRelease, 5) == 1);
    MIRAGE_CHECK(!(f.mask() & Button5Mask));
}

void drag_helper_guards(bool helpers) {
    for (const auto *mode :
         {"helper", "existing", "ordinary", "input-only", "foreign-above", "foreign-below",
          "two-helpers", "root-budget", "reparent", "frame-sibling", "descendant-budget"}) {
        Fixture f;
        Display *foreign = XOpenDisplay(f.server.display_name().c_str());
        if (!foreign)
            throw std::runtime_error("foreign X client unavailable");
        struct Close {
            Display *display;
            ~Close() { XCloseDisplay(display); }
        } close{foreign};
        const std::string_view scenario{mode};
        XSetWindowAttributes attributes{};
        attributes.override_redirect = scenario != "ordinary";
        const auto helper =
            XCreateWindow(f.client, DefaultRootWindow(f.client), 0, 0, 400, 300, 0, CopyFromParent,
                          scenario == "input-only" ? InputOnly : InputOutput, CopyFromParent,
                          CWOverrideRedirect, &attributes);
        const auto second =
            XCreateWindow(f.client, DefaultRootWindow(f.client), 0, 0, 400, 300, 0, CopyFromParent,
                          InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
        attributes.override_redirect = True;
        const auto alien =
            XCreateWindow(foreign, DefaultRootWindow(foreign), 0, 0, 400, 300, 0, CopyFromParent,
                          InputOutput, CopyFromParent, CWOverrideRedirect, &attributes);
        // Spoofed public properties cannot change server connection ownership.
        XStoreName(foreign, alien, "GTK drag icon");
        if (scenario == "existing") {
            XMoveWindow(f.client, helper, 500, 300);
            XMapWindow(f.client, helper);
        }
        if (scenario == "root-budget")
            for (int n = 0; n < 129; ++n)
                (void)XCreateSimpleWindow(f.client, DefaultRootWindow(f.client), 500, 300, 1, 1, 0,
                                          0, 0);
        Window sibling = None;
        if (scenario == "reparent" || scenario == "frame-sibling" ||
            scenario == "descendant-budget") {
            const auto frame =
                XCreateSimpleWindow(foreign, DefaultRootWindow(foreign), 30, 30, 400, 300, 0, 0, 0);
            XMapWindow(foreign, frame);
            XReparentWindow(foreign, f.window, frame, 10, 10);
            XMapWindow(foreign, f.window);
            if (scenario == "frame-sibling")
                sibling = XCreateSimpleWindow(foreign, frame, 0, 0, 400, 300, 0, 0, 0);
            if (scenario == "descendant-budget")
                for (int n = 0; n < 129; ++n)
                    (void)XCreateSimpleWindow(foreign, frame, 0, 0, 1, 1, 0, 0, 0);
            XSync(foreign, False);
            XSetInputFocus(f.client, f.window, RevertToParent, CurrentTime);
        }
        XSync(f.client, False);
        XSync(foreign, False);
        mirage::testing::X11PointerDiagnostics diagnostics(f.client, f.window);
        bool injected = false;
        d::CancelToken probe{[&] {
            if (!injected && (f.mask() & Button1Mask)) {
                injected = true;
                XMoveWindow(f.client, helper, 0, 0);
                XMapRaised(f.client, helper);
                if (scenario == "two-helpers")
                    XMapRaised(f.client, second);
                XSync(f.client, False);
                if (sibling != None) {
                    XMapRaised(foreign, sibling);
                    XSync(foreign, False);
                }
                if (scenario.starts_with("foreign-")) {
                    XMapWindow(foreign, alien);
                    XWindowChanges changes{};
                    changes.sibling = helper;
                    changes.stack_mode = scenario == "foreign-below" ? Below : Above;
                    XConfigureWindow(foreign, alien, CWSibling | CWStackMode, &changes);
                    XSync(foreign, False);
                }
            }
            return false;
        }};
        const auto result = f.env.input()->drag_to(f.scope, {90, 100}, {300, 180}, {}, probe);
        MIRAGE_CHECK(injected && result.input_released && !(f.mask() & Button1Mask));
        const bool accepted = helpers && (scenario == "helper" || scenario == "reparent");
        if (result.ok != accepted)
            std::fprintf(stderr, "helper mode=%s expected=%d error=%s\n", mode, accepted,
                         result.error.code.c_str());
        MIRAGE_CHECK(result.ok == accepted);
        MIRAGE_CHECK(accepted || result.error.code == "target_obscured");
        MIRAGE_CHECK(f.count(ButtonPress, 1) == 1 && f.count(ButtonRelease, 1) == 1);
        diagnostics.sample("released");
        for (int n = 0; n < 4; ++n)
            diagnostics.sample("capacity-probe");
        const auto diagnostic = diagnostics.report();
        MIRAGE_CHECK(diagnostic.find("truncated")->as_boolean().value_or(false));
        const auto &samples = *diagnostic.find("samples")->as_array();
        MIRAGE_CHECK(samples.size() == 4);
        MIRAGE_CHECK(!samples[0].find("held")->as_boolean().value_or(true));
        if (helpers) {
            const bool root_budget = scenario == "root-budget";
            MIRAGE_CHECK(diagnostic.find("prepared")->as_boolean().value_or(false) != root_budget);
            if (!root_budget) {
                MIRAGE_CHECK(samples[0].find("top_source")->as_boolean().value_or(false) ==
                             (scenario != "foreign-above"));
                MIRAGE_CHECK(samples[0].find("top_new")->as_boolean().value_or(false) ==
                             (scenario != "existing"));
                MIRAGE_CHECK(samples[0].find("top_class")->as_integer().value_or(0) ==
                             (scenario == "input-only" ? InputOnly : InputOutput));
            } else
                MIRAGE_CHECK(samples[0].find("roots")->as_integer().value_or(0) > 128);
        }
    }
}

class Script final : public mira::IModelProvider {
  public:
    mira::ModelProfile configured;
    std::string operation;
    int requests = 0, images = 0;
    explicit Script(mira::ModelProfile profile, std::string name)
        : configured(std::move(profile)), operation(std::move(name)) {}
    const mira::ModelProfile &profile() const override { return configured; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        Json marked, action;
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                images += std::holds_alternative<mira::ImagePart>(part);
                if (const auto *result = std::get_if<mira::ToolResultPart>(&part);
                    result && !result->failed) {
                    if (result->result.find("marks"))
                        marked = result->result;
                    if (result->result.find("action_id"))
                        action = result->result;
                }
            }
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = configured.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (requests < 3) {
            const auto name = requests == 0   ? std::string{"desktop.vision.mark"}
                              : requests == 1 ? operation
                                              : std::string{"desktop.action.verify"};
            const auto spec =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &tool) {
                    return tool.wire_name == mirage::testing::fixture_tool_name(name);
                });
            MIRAGE_CHECK(spec != request.tools.end());
            if (spec == request.tools.end())
                throw std::runtime_error("pointer tool unavailable");
            Json::Object arguments;
            if (requests == 1) {
                MIRAGE_CHECK(marked.find("marks") && marked.find("marks")->as_array()->size() == 2);
                if (!marked.find("marks") || marked.find("marks")->as_array()->size() != 2)
                    throw std::runtime_error("expected two real geometric targets");
                arguments = {{"visual_scope", *marked.find("visual_scope")},
                             {"environment_epoch", *marked.find("environment_epoch")},
                             {"window_id", *marked.find("window_id")},
                             {"mark", std::int64_t{1}},
                             {"verify_kind", "window_title_equals"},
                             {"expected_title", "Done"}};
                if (operation == "desktop.vision.drag")
                    arguments.emplace_back("destination_mark", std::int64_t{2});
                else {
                    arguments.emplace_back("horizontal_steps", std::int64_t{-1});
                    arguments.emplace_back("vertical_steps", std::int64_t{1});
                }
            } else if (requests == 2) {
                MIRAGE_CHECK(action.find("action_id"));
                if (!action.find("action_id"))
                    throw std::runtime_error("missing pointer action evidence");
                arguments.emplace_back("action_id", *action.find("action_id"));
            }
            mira::ToolCallOutput call;
            call.tool_id = spec->tool_id;
            call.provider_name = spec->wire_name;
            call.provider_call_id.value = "pointer-" + std::to_string(requests);
            call.arguments = Json{std::move(arguments)};
            call.arguments_digest = mira::digest_string(mira::to_json_string(call.arguments));
            response.output.emplace_back(std::move(call));
        } else {
            mira::MessageOutput answer;
            answer.content.emplace_back(mira::OutputTextPart{"done", {}});
            response.output.emplace_back(std::move(answer));
        }
        ++requests;
        return response;
    }
};

void composed_loop(kairo::Executor &executor) {
    for (const auto *operation : {"desktop.vision.scroll", "desktop.vision.drag"}) {
        for (const bool changed : {true, false}) {
            Fixture f;
            const auto gc = XCreateGC(f.client, f.window, 0, nullptr);
            XSetForeground(f.client, gc, 0);
            XFillRectangle(f.client, f.window, gc, 30, 40, 60, 60);
            XFillRectangle(f.client, f.window, gc, 240, 120, 60, 60);
            XFreeGC(f.client, gc);
            XStoreName(f.client, f.window, "Before");
            XSync(f.client, False);
            f.reset();
            d::VisualReferenceRegistry visual;
            i::VisualObservationPipelineConfig pipeline_config;
            pipeline_config.source_id =
                std::string{operation} + (changed ? "-changed" : "-unchanged");
            pipeline_config.geometry_marks = true;
            pipeline_config.roi_capture = true;
            i::VisualObservationPipeline pipeline(executor, *f.env.screen(), visual,
                                                  pipeline_config);
            std::string failure;
            MIRAGE_CHECK(pipeline.start(failure));
            i::DesktopToolContext tools;
            tools.environment = std::shared_ptr<d::DesktopEnvironment>(&f.env, [](auto *) {});
            tools.actions = std::make_shared<d::ActionCoordinator>();
            tools.capture_scope = f.scope.window_id;
            tools.gate = [](const auto &, const auto &, const auto &probe) { return !probe(); };
            tools.mark = [&](const auto &crop, const auto &cancel, const auto &end) {
                return pipeline.refresh_marked(crop, cancel, end);
            };
            tools.with_visual_reference = [&](const auto &scope, const auto &ref,
                                              const auto &action) {
                return visual.with_reference(scope, ref, action);
            };
            tools.with_visual_references = [&](const auto &scope, const auto &from, const auto &to,
                                               const auto &action) {
                return visual.with_references(scope, from, to, action);
            };
            bool painted = false;
            d::CancelToken service{[&] {
                f.drain();
                int releases = 0;
                for (const auto &event : f.events)
                    releases += event.type == ButtonRelease;
                if (changed && !painted &&
                    releases >= (std::string_view{operation}.ends_with("drag") ? 1 : 2)) {
                    painted = true;
                    XStoreName(f.client, f.window, "Done");
                    XClearWindow(f.client, f.window);
                    XSync(f.client, False);
                }
                return false;
            }};
            i::ModelLayerConfig config;
            config.enabled = config.supports_images = true;
            config.model_selector = "pointer-fixture";
            config.max_output_tokens = 512;
            std::shared_ptr<Script> script;
            i::ModelProviderOverride provider([&](const auto &profile) {
                script = std::make_shared<Script>(profile, operation);
                return script;
            });
            i::ModelLayer model(executor, config, &provider, tools);
            auto ctx = mira::make_control_context();
            ctx.task = mira::TaskId::generate();
            ctx.session = mira::SessionId::generate();
            ctx.task_epoch = 1;
            ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
            const auto completion =
                model.complete_harness_turn("", "move and verify", ctx, "", true, {}, {}, service);
            if (completion.ok != changed)
                std::fprintf(stderr, "composed %s changed=%d: %s\n", operation, changed,
                             completion.error.c_str());
            MIRAGE_CHECK(completion.ok == changed && completion.failed != changed);
            MIRAGE_CHECK(changed || completion.reply_text.empty());
            MIRAGE_CHECK(script->requests == 4 && script->images == 1 &&
                         completion.tool_calls == 3);
            MIRAGE_CHECK(!completion.supplied_input_dropped && !completion.supply_degradations);
            MIRAGE_CHECK(f.count(ButtonPress) ==
                         (std::string_view{operation}.ends_with("drag") ? 1 : 2));
            MIRAGE_CHECK(f.count(ButtonRelease) == f.count(ButtonPress) &&
                         !(f.mask() & Button1Mask));
            MIRAGE_CHECK(tools.actions->input_released());
            model.shutdown();
            pipeline.stop();
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    if (argc > 2 || (argc == 2 && std::string_view{argv[1]} != "--strict"))
        return 2;
#ifdef MIRAGE_TEST_X11_DRAG_HELPERS
    const bool helpers = argc == 1;
#else
    const bool helpers = false;
#endif
    kairo::Executor executor;
    MIRAGE_CHECK(executor.initialize({}).ok);
    try {
        executor
            .submit_auto([&] {
                normal_and_budgets();
                preflight_guards();
                running_guards();
                drag_helper_guards(helpers);
                composed_loop(executor);
            })
            .get();
        // Consumed exception and external-owner shutdown; no detached fixture task.
        bool exception_seen = false;
        try {
            executor.submit_auto([] { throw std::runtime_error("finite fixture fault"); }).get();
        } catch (const std::runtime_error &) {
            exception_seen = true;
        }
        MIRAGE_CHECK(exception_seen);
        executor.shutdown(true);
        bool rejected = false;
        try {
            executor.submit_auto([] { return 1; }).get();
        } catch (const std::exception &) {
            rejected = true;
        }
        MIRAGE_CHECK(rejected);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "pointer fixture: %s\n", error.what());
        executor.shutdown(true);
        return 1;
    }
    return mirage::testing::finish("x11_pointer_actions");
}
