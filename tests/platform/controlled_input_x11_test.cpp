#include "../../integration/mira/src/controlled_input_session.hpp"
#include "../../integration/mira/src/controlled_input_tools.hpp"
#include "../support/provider_tool_name.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <kairo/executor.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/desktop/input_session.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <poll.h>
#include <stdexcept>

// Xlib macros must follow the pinned C++ headers.
#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using J = mira::JsonValue;
namespace {
struct Fixture {
    mirage::testing::XvfbDisplay server{mirage::testing::find_xvfb()};
    d::CancelToken cancel;
    mirage::platform::linux_backend::LinuxDesktopEnvironment environment{
        {}, {.enabled = true, .display = server.display_name()}};
    Display *client = XOpenDisplay(server.display_name().c_str());
    Window window = 0;
    d::PointerActionScope scope;
    Fixture() {
        if (!client || !environment.input())
            throw std::runtime_error("private X11 fixture unavailable");
        window = XCreateSimpleWindow(client, DefaultRootWindow(client), 40, 40, 360, 240, 0, 0,
                                     0xffffff);
        XStoreName(client, window, "Controlled input fixture");
        XSelectInput(client, window,
                     KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask |
                         PointerMotionMask);
        XMapWindow(client, window);
        XSetInputFocus(client, window, RevertToParent, CurrentTime);
        XWarpPointer(client, None, window, 0, 0, 0, 0, 100, 100);
        XSync(client, False);
        const auto front = environment.window()->front_window({});
        MIRAGE_CHECK(front.ok && front.found && environment.input()->controlled_input_supported());
        scope = {front.window.id, front.window.geometry};
    }
    ~Fixture() {
        if (client)
            XCloseDisplay(client);
    }
    bool shift() {
        std::array<char, 32> keys{};
        XQueryKeymap(client, keys.data());
        const auto code = XKeysymToKeycode(client, XK_Shift_L);
        return (static_cast<unsigned char>(keys[code / 8]) & (1u << (code % 8))) != 0;
    }
    unsigned buttons() {
        Window root = 0, child = 0;
        int x = 0, y = 0, wx = 0, wy = 0;
        unsigned mask = 0;
        MIRAGE_CHECK(XQueryPointer(client, DefaultRootWindow(client), &root, &child, &x, &y, &wx,
                                   &wy, &mask));
        return mask;
    }
    d::PointerPoint pointer() {
        const auto outcome = environment.input()->pointer_position({});
        MIRAGE_CHECK(outcome.ok);
        return {outcome.position.x, outcome.position.y};
    }
};
void tool_contract(kairo::Executor &executor) {
    std::ifstream file(std::filesystem::path{__FILE__}.parent_path().parent_path() /
                       "integration/data/controlled-input-travel-v1.1.json");
    const auto parsed = mira::parse_json(std::string{std::istreambuf_iterator<char>{file}, {}});
    if (!parsed)
        throw std::runtime_error("controlled input travel golden unavailable");
    const auto &golden = parsed.value();
    Fixture f;
    i::DesktopToolContext desktop;
    desktop.environment = std::shared_ptr<d::DesktopEnvironment>(&f.environment, [](auto *) {});
    desktop.actions = std::make_shared<d::ActionCoordinator>();
    desktop.gate = [](const auto &, const auto &, const auto &stop) { return !stop(); };
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.task_epoch = 1;
    i::detail::ControlledInputTools tools(executor, desktop, ctx, {});
    mira::BuiltinToolRegistry registry;
    tools.register_tools(registry, {});
    const auto specs = registry.exposed_tools();
    const auto call = [&](const std::string &name, const J &args) {
        const auto spec = std::find_if(specs.begin(), specs.end(), [&](const auto &entry) {
            return entry.wire_name == "desktop.input_session." + name;
        });
        if (spec == specs.end())
            throw std::runtime_error("controlled input tool not registered");
        mira::ToolProposal proposal;
        proposal.tool_id = spec->tool_id;
        proposal.wire_name = spec->wire_name;
        proposal.tool_version = spec->version;
        proposal.arguments = args;
        proposal.arguments_digest = mira::digest_string(mira::to_json_string(args));
        proposal.operation_id = mira::OperationId::generate();
        proposal.has_side_effects = spec->has_side_effects;
        const auto result = registry.execute(proposal, ctx);
        if (!result)
            throw std::runtime_error(result.error().safe_message);
        return result.value();
    };
    for (const auto &spec : specs) {
        if (spec.wire_name != "desktop.input_session.advance")
            continue;
        const auto &version = *golden.find("advance_version")->as_array();
        MIRAGE_CHECK(spec.version.major == version[0].as_integer());
        MIRAGE_CHECK(spec.version.minor == version[1].as_integer());
        MIRAGE_CHECK(spec.version.patch == version[2].as_integer());
        const auto *properties = spec.parameters_schema.root.find("properties");
        for (const char *coordinate : {"x", "y"}) {
            MIRAGE_CHECK(properties->find(coordinate)->find("minimum")->as_integer() ==
                         golden.find("coordinate_min")->as_integer());
            MIRAGE_CHECK(properties->find(coordinate)->find("maximum")->as_integer() ==
                         golden.find("coordinate_max")->as_integer());
        }
        MIRAGE_CHECK(!properties->find("duration") && !properties->find("path_points"));
    }
    auto state = call("begin", J{J::Object{{"window_id", f.scope.window_id},
                                           {"environment_epoch",
                                            static_cast<std::int64_t>(desktop.actions->epoch())}}});
    MIRAGE_CHECK(!state.failed);
    const auto args_for = [&](const J &command) {
        auto args = command;
        args.set("session_id", *state.result.find("session_id"));
        args.set("state_version", *state.result.find("state_version"));
        args.set("sequence", std::int64_t{1});
        return args;
    };
    for (const auto &row : *golden.find("invalid_arguments")->as_array()) {
        const auto invalid = call("advance", args_for(row));
        MIRAGE_CHECK(invalid.failed);
        MIRAGE_CHECK(f.pointer().x == 140 && f.pointer().y == 140);
        MIRAGE_CHECK(tools.snapshot().find("command_sequence")->as_integer() == 0);
    }
    const auto moved = call("advance", args_for(*golden.find("example")));
    MIRAGE_CHECK(!moved.failed && f.pointer().x == 340 && f.pointer().y == 190);
    MIRAGE_CHECK(moved.result.find("schema") && *moved.result.find("schema")->as_string() ==
                                                    *golden.find("receipt_schema")->as_string());
    MIRAGE_CHECK(
        !call("release", J{J::Object{{"session_id", *state.result.find("session_id")}}}).failed);
    MIRAGE_CHECK(tools.finish().code.empty());
}
void travel(kairo::Executor &executor) {
    // Actual server events demonstrate held delivery to the receiving window;
    // no desktop input is sent to the user's display.
    for (int mode = 0; mode < 5; ++mode) {
        Fixture f;
        auto owner = std::make_shared<d::ActionCoordinator>();
        d::CancelToken stop;
        using Session = i::detail::ControlledInputSession;
        Session session(executor, owner, stop,
                        [&] { return f.environment.input()->open_input_session(f.scope, {}); }, {});
        auto state = session.begin(owner->epoch());
        d::InputSessionCommand command;
        command.kind = d::InputSessionCommand::Kind::key_down;
        command.key_name = "shift";
        state = session.advance(1, state.state_version, command);
        command = {};
        command.kind = d::InputSessionCommand::Kind::button_down;
        command.button = "left";
        state = session.advance(2, state.state_version, command);
        MIRAGE_CHECK(f.shift() && (f.buttons() & Button1Mask));
        XSync(f.client, False);
        while (XPending(f.client)) {
            XEvent event;
            XNextEvent(f.client, &event);
        }
        command = {};
        command.kind = d::InputSessionCommand::Kind::smooth_move;
        command.x = mode == 2 ? 450 : 340; // outside the bound [40,400) window
        command.y = 140;
        command.duration = std::chrono::milliseconds{600};
        bool interrupted = false;
        if (mode == 1 || mode == 3 || mode == 4) {
            // Same-worker probe avoids fixture cross-thread Xlib calls. At least
            // one segment reached the server before cancellation/focus change.
            stop = d::CancelToken([&] {
                const auto position = f.pointer();
                if (!interrupted && position.x >= 180) {
                    interrupted = true;
                    if (mode == 3) {
                        XSetInputFocus(f.client, DefaultRootWindow(f.client), RevertToParent,
                                       CurrentTime);
                        XSync(f.client, False);
                    }
                    if (mode == 4)
                        owner->invalidate_authority();
                }
                return interrupted && mode == 1;
            });
        }
        const auto started = std::chrono::steady_clock::now();
        state = session.advance(3, state.state_version, command, stop);
        const auto position = f.pointer();
        if (mode == 0) {
            MIRAGE_CHECK(position.x == 340 && position.y == 140);
            MIRAGE_CHECK(std::chrono::steady_clock::now() - started >=
                         std::chrono::milliseconds{599});
            MIRAGE_CHECK(state.outcome == d::InputSessionOutcome::holding &&
                         state.held_keys == std::vector<std::string>{"shift"} &&
                         state.held_buttons == std::vector<std::string>{"left"});
            MIRAGE_CHECK(f.environment.screen()->capture_window(f.scope.window_id, {}, {}).ok);
            MIRAGE_CHECK(f.shift() && (f.buttons() & Button1Mask));
            const auto stale = session.advance(3, state.state_version, command);
            MIRAGE_CHECK(stale.error.code == "stale_input_command");
            MIRAGE_CHECK(session.release().release_confirmed);
        } else {
            MIRAGE_CHECK(state.release_confirmed && !state.error.code.empty());
            if (mode == 2) {
                MIRAGE_CHECK(position.x == 140 && position.y == 140);
                MIRAGE_CHECK(state.error.code == "target_changed");
            } else {
                MIRAGE_CHECK(interrupted && position.x >= 180 && position.x < 340);
                MIRAGE_CHECK(state.cancelled == (mode == 1 || mode == 4));
                MIRAGE_CHECK(state.error.code == (mode == 3 ? "target_changed" : "cancelled"));
            }
        }
        MIRAGE_CHECK(!f.shift() && !(f.buttons() & Button1Mask) && owner->input_released());
        int motion = 0, button_up = 0, key_up = 0, last_x = 140;
        XSync(f.client, False);
        while (XPending(f.client)) {
            XEvent event;
            XNextEvent(f.client, &event);
            if (event.type == MotionNotify) {
                MIRAGE_CHECK(event.xmotion.x_root >= last_x && event.xmotion.x_root <= 340);
                MIRAGE_CHECK(event.xmotion.y_root == 140);
                MIRAGE_CHECK(event.xmotion.state & ShiftMask);
                MIRAGE_CHECK(event.xmotion.state & Button1Mask);
                last_x = event.xmotion.x_root;
                ++motion;
            }
            button_up += event.type == ButtonRelease;
            key_up += event.type == KeyRelease;
        }
        if (mode == 2)
            MIRAGE_CHECK(motion == 0);
        else
            MIRAGE_CHECK(motion > 1);
        MIRAGE_CHECK(button_up == 1);
        // After focus changes, X11 may route release through pointer focus;
        // server key state above is authoritative and release is never replayed.
        MIRAGE_CHECK(mode == 3 ? key_up <= 1 : key_up == 1);
        session.stop();
    }
}
void platform(kairo::Executor &executor) {
    Fixture f;
    auto backend = f.environment.input()->open_input_session(f.scope, {});
    MIRAGE_CHECK(bool(backend));
    if (!backend)
        throw std::runtime_error("controlled X11 input unavailable");
    d::InputSessionCommand command;
    command.kind = d::InputSessionCommand::Kind::key_down;
    command.key_name = "shift";
    MIRAGE_CHECK(backend->apply(command, {}).ok && f.shift());
    const auto capture = f.environment.screen()->capture_window(f.scope.window_id, {}, {});
    MIRAGE_CHECK(capture.ok && f.shift() && backend->check().code.empty());
    command.kind = d::InputSessionCommand::Kind::key_up;
    MIRAGE_CHECK(backend->apply(command, {}).ok && !f.shift());
    command.kind = d::InputSessionCommand::Kind::key_tap;
    command.dwell = std::chrono::milliseconds{1150};
    const auto started = std::chrono::steady_clock::now();
    MIRAGE_CHECK(backend->apply(command, {}).ok && !f.shift());
    MIRAGE_CHECK(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds{1149});
    command = {};
    command.kind = d::InputSessionCommand::Kind::button_down;
    command.button = "left";
    MIRAGE_CHECK(backend->apply(command, {}).ok && (f.buttons() & Button1Mask));
    MIRAGE_CHECK(f.environment.screen()->capture_window(f.scope.window_id, {}, {}).ok);
    MIRAGE_CHECK(backend->release().input_released && !(f.buttons() & Button1Mask));
    // Existing physical/global state is never acquired and never released.
    const auto shift = XKeysymToKeycode(f.client, XK_Shift_L);
    XTestFakeKeyEvent(f.client, shift, True, CurrentTime);
    XSync(f.client, False);
    backend = f.environment.input()->open_input_session(f.scope, {});
    MIRAGE_CHECK(bool(backend));
    command = {};
    command.kind = d::InputSessionCommand::Kind::key_down;
    command.key_name = "shift";
    const auto refused = backend->apply(command, {});
    MIRAGE_CHECK(!refused.ok && refused.error.code == "physical_input_busy");
    MIRAGE_CHECK(backend->release().input_released && f.shift());
    XTestFakeKeyEvent(f.client, shift, False, CurrentTime);
    XSync(f.client, False);
    // Timeout inside a complete press still releases through the owning host.
    backend = f.environment.input()->open_input_session(f.scope, {});
    d::CancelToken cancel;
    auto cancellation =
        executor.submit_delayed(25, [cancel]() mutable { cancel.request_cancel(); });
    command.kind = d::InputSessionCommand::Kind::key_tap;
    command.dwell = std::chrono::milliseconds{1000};
    const auto stopped = backend->apply(command, cancel);
    cancellation.future.get();
    MIRAGE_CHECK(!stopped.ok && stopped.cancelled);
    MIRAGE_CHECK(backend->release().input_released && !f.shift());
    // A non-monotonic external probe cannot erase an observed cancellation.
    backend = f.environment.input()->open_input_session(f.scope, {});
    bool noticed = false;
    d::CancelToken once([&] {
        if (!noticed && f.shift()) {
            noticed = true;
            return true;
        }
        return false;
    });
    const auto transient = backend->apply(command, once);
    MIRAGE_CHECK(noticed && transient.cancelled && !transient.ok);
    MIRAGE_CHECK(backend->release().input_released && !f.shift());
    // A focus change stops a hold but cleanup retains ownership of its key.
    backend = f.environment.input()->open_input_session(f.scope, {});
    command.kind = d::InputSessionCommand::Kind::key_down;
    command.dwell = {};
    MIRAGE_CHECK(backend->apply(command, {}).ok && f.shift());
    XSetInputFocus(f.client, DefaultRootWindow(f.client), RevertToParent, CurrentTime);
    XSync(f.client, False);
    MIRAGE_CHECK(backend->check().code == "target_changed");
    MIRAGE_CHECK(backend->release().input_released && !f.shift());
}
class Script final : public mira::IModelProvider {
    mira::ModelProfile profile_;
    Fixture &fixture_;
    std::string mode_;

  public:
    int requests = 0;
    std::function<void()> pause;
    explicit Script(mira::ModelProfile profile, Fixture &fixture, std::string mode)
        : profile_(std::move(profile)), fixture_(fixture), mode_(std::move(mode)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        J front, state;
        bool host_state = false, has_image = false;
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                has_image |= std::holds_alternative<mira::ImagePart>(part);
                if (const auto *text = std::get_if<mira::TextPart>(&part);
                    text && text->text.starts_with("Host controlled-input state"))
                    host_state = true;
                if (const auto *result = std::get_if<mira::ToolResultPart>(&part);
                    result && !result->failed) {
                    if (result->result.find("environment_epoch") && result->result.find("id"))
                        front = result->result;
                    if (result->result.find("session_id"))
                        state = result->result;
                }
            }
        const auto actual_step = requests++;
        const bool moving = mode_ == "move" || mode_ == "drag";
        const auto step = actual_step - (moving && actual_step >= 4 ? 1 : 0);
        if (step == 4 || (step == 5 && mode_ == "recover"))
            MIRAGE_CHECK(has_image);
        if ((step == 3 || step == 4) && mode_ != "long") {
            MIRAGE_CHECK(
                (mode_ == "drag" ? (fixture_.buttons() & Button1Mask) != 0 : fixture_.shift()) &&
                host_state);
        }
        if (moving && actual_step == 4) {
            MIRAGE_CHECK(fixture_.pointer().x == 340 && fixture_.pointer().y == 190);
            MIRAGE_CHECK(state.find("outcome") && *state.find("outcome")->as_string() == "holding");
        }
        if ((mode_ == "paused" || mode_ == "policy" || mode_ == "recover") && step == 4) {
            pause();
            MIRAGE_CHECK(!fixture_.shift()); // Released while provider has not returned.
            if (mode_ == "paused") {
                mira::Error cancelled;
                cancelled.code = mira::ErrorCode::Cancelled;
                cancelled.safe_message = "fixture provider cancellation";
                return cancelled;
            }
        }
        if (mode_ == "protocol" && step >= 4)
            return mira::make_model_error(mira::ModelDomainCode::ProtocolViolation,
                                          "invalid Messages SSE lifecycle or budget");
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        std::string name;
        J::Object args;
        if (step == 0)
            name = "desktop.window.front";
        else if (step == 1) {
            MIRAGE_CHECK(front.find("id") && front.find("environment_epoch"));
            name = "desktop.input_session.begin";
            args = {{"window_id", *front.find("id")},
                    {"environment_epoch", *front.find("environment_epoch")}};
        } else if (step == 2) {
            MIRAGE_CHECK(state.find("state_version") && state.find("session_id"));
            name = "desktop.input_session.advance";
            args = {{"session_id", *state.find("session_id")},
                    {"state_version", *state.find("state_version")},
                    {"sequence", std::int64_t{1}},
                    {"command", mode_ == "long"   ? "key_tap"
                                : mode_ == "drag" ? "button_down"
                                                  : "key_down"},
                    {mode_ == "drag" ? "button" : "key", mode_ == "drag" ? "left" : "shift"}};
            if (mode_ == "long")
                args.emplace_back("press", "long");
        } else if (moving && actual_step == 3) {
            name = "desktop.input_session.advance";
            args = {{"session_id", *state.find("session_id")},
                    {"state_version", *state.find("state_version")},
                    {"sequence", std::int64_t{2}},
                    {"command", "smooth_move"},
                    {"x", std::int64_t{340}},
                    {"y", std::int64_t{190}}};
        } else if (step == 3) {
            name = "desktop.screen.capture";
            args = {{"source", "window"}, {"source_id", fixture_.scope.window_id}};
        } else if (step == 4 && mode_ == "recover") {
            name = "desktop.screen.capture";
            args = {{"source", "window"}, {"source_id", fixture_.scope.window_id}};
        } else if (step == 4 && mode_ != "unclosed") {
            name = "desktop.input_session.release";
            args = {{"session_id", *state.find("session_id")}};
        } else {
            mira::MessageOutput answer;
            answer.content.emplace_back(mira::OutputTextPart{"fixture complete", {}});
            response.output.emplace_back(std::move(answer));
            return response;
        }
        const auto tool =
            std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &spec) {
                return spec.wire_name == mirage::testing::fixture_tool_name(name);
            });
        if (tool == request.tools.end())
            throw std::runtime_error("controlled input tool missing");
        mira::ToolCallOutput call;
        call.tool_id = tool->tool_id;
        call.provider_name = tool->wire_name;
        call.provider_call_id.value = "hold-" + std::to_string(step);
        call.arguments = J{std::move(args)};
        call.arguments_digest = mira::digest_string(mira::to_json_string(call.arguments));
        response.output.emplace_back(std::move(call));
        return response;
    }
};
void loop(kairo::Executor &executor) {
    for (const std::string mode : {"normal", "long", "unclosed", "protocol", "paused", "policy",
                                   "recover", "move", "drag"}) {
        Fixture f;
        i::DesktopToolContext desktop;
        desktop.environment = std::shared_ptr<d::DesktopEnvironment>(&f.environment, [](auto *) {});
        desktop.actions = std::make_shared<d::ActionCoordinator>();
        desktop.capture_scope = f.scope.window_id;
        desktop.gate = [](const auto &, const auto &, const auto &stop) { return !stop(); };
        i::ModelLayerConfig config;
        config.enabled = config.supports_images = true;
        config.model_selector = "controlled-fixture";
        config.max_output_tokens = 512;
        std::shared_ptr<Script> script;
        i::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<Script>(profile, f, mode);
            return script;
        });
        i::ModelLayer model(executor, config, &provider, desktop);
        d::CancelToken parent;
        script->pause = [&, parent]() mutable {
            if (mode == "policy")
                desktop.actions->invalidate_authority();
            else if (mode == "recover") {
                XSetInputFocus(f.client, DefaultRootWindow(f.client), RevertToParent, CurrentTime);
                XSync(f.client, False);
            } else
                parent.request_cancel();
            (void)::poll(nullptr, 0, 80); // Synthetic provider stall on the managed caller.
            if (mode == "recover") {
                MIRAGE_CHECK(!f.shift());
                XSetInputFocus(f.client, f.window, RevertToParent, CurrentTime);
                XSync(f.client, False);
            }
        };
        auto context = mira::make_control_context();
        context.task = mira::TaskId::generate();
        context.session = mira::SessionId::generate();
        context.task_epoch = 1;
        context.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{20};
        const auto completion =
            model.complete_harness_turn("", "hold and observe", context, "", true, {}, {}, parent);
        const bool expected = mode == "normal" || mode == "long" || mode == "recover" ||
                              mode == "move" || mode == "drag";
        if (completion.ok != expected)
            std::fprintf(stderr, "hold mode=%s error=%s\n", mode.c_str(), completion.error.c_str());
        MIRAGE_CHECK(completion.ok == expected);
        MIRAGE_CHECK(!f.shift() && !(f.buttons() & Button1Mask) &&
                     desktop.actions->input_released());
        if (mode == "unclosed")
            MIRAGE_CHECK(completion.error.find("input_session_unclosed") != std::string::npos);
        if (mode == "protocol")
            MIRAGE_CHECK(completion.error.find("invalid Messages SSE lifecycle") !=
                         std::string::npos);
        int down = 0, up = 0;
        XSync(f.client, False);
        while (XPending(f.client)) {
            XEvent event{};
            XNextEvent(f.client, &event);
            down += event.type == KeyPress;
            up += event.type == KeyRelease;
        }
        MIRAGE_CHECK(down == (mode == "drag" ? 0 : 1) && up == down); // No replay.
        model.shutdown();
    }
}
} // namespace
int main() {
    kairo::Executor executor;
    MIRAGE_CHECK(executor.initialize({}).ok);
    try {
        executor
            .submit_auto([&] {
                platform(executor);
                travel(executor);
                tool_contract(executor);
                loop(executor);
            })
            .get();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "controlled X11: %s\n", error.what());
        executor.shutdown(true);
        return 1;
    }
    executor.shutdown(true);
    return mirage::testing::finish("controlled_input_x11");
}
