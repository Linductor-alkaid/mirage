#include "../../integration/mira/src/controlled_input_tools.hpp"
#include "../support/provider_tool_name.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <kairo/executor.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <poll.h>
#include <set>
#include <stdexcept>
#include <sys/wait.h>

// Xlib macros follow pinned C++ headers.
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using J = mira::JsonValue;
namespace {
template <class Ready> void await(Ready ready) {
    for (int n = 0; n < 400; ++n) {
        if (ready())
            return;
        (void)::poll(nullptr, 0, 5);
    }
    throw std::runtime_error("private switcher fixture timed out");
}
struct Manager {
    pid_t pid = -1;
    explicit Manager(const std::string &display) {
        pid = ::fork();
        if (pid < 0)
            throw std::runtime_error("Openbox fork failed");
        if (!pid) {
            ::setenv("DISPLAY", display.c_str(), 1);
            ::execl(MIRAGE_OPENBOX_EXECUTABLE, "openbox", "--sm-disable", "--config-file",
                    MIRAGE_OPENBOX_CONFIG, static_cast<char *>(nullptr));
            _exit(127);
        }
    }
    ~Manager() {
        if (pid <= 0)
            return;
        ::kill(pid, SIGTERM);
        for (int n = 0; n < 200; ++n) {
            if (::waitpid(pid, nullptr, WNOHANG) == pid)
                return;
            (void)::poll(nullptr, 0, 5);
        }
        ::kill(pid, SIGKILL);
        ::waitpid(pid, nullptr, 0);
    }
};
Window property(Display *display, const char *name) {
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char *bytes = nullptr;
    XGetWindowProperty(display, DefaultRootWindow(display), XInternAtom(display, name, False), 0, 1,
                       False, XA_WINDOW, &type, &format, &count, &remaining, &bytes);
    const auto value = bytes && type == XA_WINDOW && format == 32 && count == 1
                           ? *reinterpret_cast<unsigned long *>(bytes)
                           : 0;
    if (bytes)
        XFree(bytes);
    return value;
}
struct Fixture {
    mirage::testing::XvfbDisplay server{mirage::testing::find_xvfb()};
    Manager manager{server.display_name()};
    Display *client = XOpenDisplay(server.display_name().c_str());
    std::array<Window, 3> windows{};
    std::shared_ptr<mirage::platform::linux_backend::LinuxDesktopEnvironment> environment;
    d::PointerActionScope scope;
    std::string display_id;
    Fixture() {
        if (!client)
            throw std::runtime_error("Xvfb unavailable");
        await([&] { return property(client, "_NET_SUPPORTING_WM_CHECK") != 0; });
        for (int n = 0; n < 3; ++n) {
            windows[n] = XCreateSimpleWindow(client, DefaultRootWindow(client), 40 + n * 100,
                                             40 + n * 60, 240, 160, 0, 0, 0xffffff);
            XStoreName(client, windows[n], ("Mirage switcher " + std::to_string(n)).c_str());
            XClassHint hint{const_cast<char *>("mirage-fixture"),
                            const_cast<char *>("MirageFixture")};
            XSetClassHint(client, windows[n], &hint);
            XMapWindow(client, windows[n]);
            XSync(client, False);
            (void)::poll(nullptr, 0, 100); // Openbox finishes startup and manages each MapRequest.
        }
        environment = std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{},
            mirage::platform::linux_backend::X11Options{.enabled = true,
                                                        .display = server.display_name()});
        const auto front = environment->window()->front_window({});
        if (!front.ok || !front.found || !environment->input()->window_switcher_supported())
            throw std::runtime_error("Openbox 3.6.1 switcher profile unavailable");
        scope = {front.window.id, front.window.geometry};
        const auto displays = environment->screen()->list_displays({}, {});
        if (!displays.ok || displays.displays.empty())
            throw std::runtime_error("private display unavailable");
        display_id = displays.displays.front().id;
    }
    ~Fixture() {
        if (client)
            XCloseDisplay(client);
    }
    bool pressed(unsigned symbol) {
        std::array<char, 32> state{};
        XQueryKeymap(client, state.data());
        const auto code = XKeysymToKeycode(client, symbol);
        return (static_cast<unsigned char>(state[code / 8]) & (1u << (code % 8))) != 0;
    }
    Window active() { return property(client, "_NET_ACTIVE_WINDOW"); }
    bool clean() { return !pressed(XK_Alt_L) && !pressed(XK_Shift_L) && !pressed(XK_Tab); }
};
struct Tools {
    Fixture f;
    d::CancelToken cancel;
    i::DesktopToolContext desktop;
    mira::OperationContext ctx = mira::make_control_context();
    std::unique_ptr<i::detail::ControlledInputTools> tools;
    mira::BuiltinToolRegistry registry;
    std::vector<std::string> gates;
    std::string denied;
    explicit Tools(kairo::Executor &executor, bool scoped = false) {
        desktop.environment = f.environment;
        desktop.actions = std::make_shared<d::ActionCoordinator>();
        if (scoped)
            desktop.capture_scope = f.scope.window_id;
        desktop.gate = [&](const auto &capability, const auto &resource, const auto &stop) {
            gates.push_back(capability + ":" + resource);
            return !stop() && capability != denied;
        };
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        tools = std::make_unique<i::detail::ControlledInputTools>(executor, desktop, ctx, cancel);
        tools->register_tools(registry, {});
    }
    mira::ToolExecutionRecord call(const std::string &operation, const J &args,
                                   const std::string &family = "desktop.window_switch.") {
        const auto specs = registry.exposed_tools();
        const auto found = std::find_if(specs.begin(), specs.end(), [&](const auto &spec) {
            return spec.wire_name == family + operation;
        });
        if (found == specs.end())
            throw std::runtime_error("switcher tool missing");
        mira::ToolProposal proposal;
        proposal.tool_id = found->tool_id;
        proposal.wire_name = found->wire_name;
        proposal.tool_version = found->version;
        proposal.arguments = args;
        proposal.arguments_digest = mira::digest_string(mira::to_json_string(args));
        proposal.operation_id = mira::OperationId::generate();
        proposal.has_side_effects = found->has_side_effects;
        auto result = registry.execute(proposal, ctx);
        if (!result && result.error().code == mira::ErrorCode::Cancelled) {
            mira::ToolExecutionRecord cancelled;
            cancelled.failed = true;
            cancelled.safe_error_summary = result.error().safe_message;
            return cancelled;
        }
        if (!result)
            throw std::runtime_error(result.error().safe_message);
        return result.value();
    }
    J begin() {
        const auto result =
            call("begin", J{J::Object{{"window_id", f.scope.window_id},
                                      {"environment_epoch",
                                       static_cast<std::int64_t>(desktop.actions->epoch())}}});
        if (result.failed)
            throw std::runtime_error(result.safe_error_summary);
        return result.result;
    }
    J args(const J &state, const char *direction = "forward") {
        return J{J::Object{{"session_id", *state.find("session_id")},
                           {"state_version", *state.find("state_version")},
                           {"sequence", *state.find("command_sequence")->as_integer() + 1},
                           {"direction", direction}}};
    }
    J release(const J &state) {
        const auto result =
            call("release", J{J::Object{{"session_id", *state.find("session_id")}}});
        MIRAGE_CHECK(!result.failed && f.clean());
        return result.result;
    }
};
void contract(kairo::Executor &executor, const J &golden) {
    Tools t(executor);
    for (const auto &spec : t.registry.exposed_tools()) {
        if (!spec.wire_name.starts_with("desktop.window_switch."))
            continue;
        const auto name = spec.wire_name.substr(std::string{"desktop.window_switch."}.size());
        const auto &version = *golden.find("version")->as_array();
        MIRAGE_CHECK(spec.version.major == version[0].as_integer());
        MIRAGE_CHECK(spec.version.minor == version[1].as_integer());
        MIRAGE_CHECK(spec.version.patch == version[2].as_integer());
        std::set<std::string> fields, required, expected;
        for (const auto &[key, value] :
             *spec.parameters_schema.root.find("properties")->as_object()) {
            (void)value;
            fields.insert(key);
        }
        for (const auto &field : *spec.parameters_schema.root.find("required")->as_array())
            required.insert(*field.as_string());
        for (const auto &field : *golden.find("tools")->find(name)->as_array())
            expected.insert(*field.as_string());
        MIRAGE_CHECK(fields == expected && required == expected);
    }
    auto state = t.begin();
    MIRAGE_CHECK(*state.find("schema")->as_string() == *golden.find("schema")->as_string());
    MIRAGE_CHECK(mira::to_json_string(*state.find("held_keys")) ==
                 mira::to_json_string(*golden.find("held_keys")));
    MIRAGE_CHECK(t.f.pressed(XK_Alt_L) && !t.desktop.actions->try_begin({}));
    for (const auto &capability : *golden.find("permissions")->as_array())
        MIRAGE_CHECK(std::find(t.gates.begin(), t.gates.end(),
                               *capability.as_string() + ":" +
                                   *golden.find("permission_resource")->as_string()) !=
                     t.gates.end());
    for (const auto &invalid : *golden.find("invalid_advance")->as_array()) {
        auto args = t.args(state);
        for (const auto &[key, value] : *invalid.as_object())
            args.set(key, value);
        MIRAGE_CHECK(t.call("advance", args).failed && t.f.pressed(XK_Alt_L));
        MIRAGE_CHECK(t.tools->snapshot().find("command_sequence")->as_integer() == 1);
    }
    MIRAGE_CHECK(t.call("release", J{J::Object{{"session_id", *state.find("session_id")}}},
                        "desktop.input_session.")
                     .failed);
    MIRAGE_CHECK(t.f.pressed(XK_Alt_L));
    state = t.release(state);
    MIRAGE_CHECK(state.find("release_confirmed")->as_boolean().value_or(false));
    MIRAGE_CHECK(t.tools->finish().code.empty());
    const auto workers = executor.get_blocking_io_worker_list().size();
    const auto fixed = t.call(
        "begin",
        J{J::Object{{"window_id", t.f.scope.window_id},
                    {"environment_epoch", static_cast<std::int64_t>(t.desktop.actions->epoch())}}},
        "desktop.input_session.");
    MIRAGE_CHECK(!fixed.failed && *fixed.result.find("schema")->as_string() == "input_session_v1");
    MIRAGE_CHECK(!t.call("release", J{J::Object{{"session_id", *fixed.result.find("session_id")}}},
                         "desktop.input_session.")
                      .failed);
    MIRAGE_CHECK(executor.get_blocking_io_worker_list().size() == workers && t.f.clean());
}
void choose(kairo::Executor &executor) {
    Tools t(executor);
    const auto before = t.f.environment->screen()->capture_display(t.f.display_id, {}, {});
    auto state = t.begin();
    for (const char *direction : {"forward", "forward", "backward"}) {
        const auto args = t.args(state, direction);
        const auto moved = t.call("advance", args);
        if (moved.failed)
            throw std::runtime_error(moved.safe_error_summary);
        state = moved.result;
        MIRAGE_CHECK(t.f.active() == t.f.windows[2] && t.f.pressed(XK_Alt_L));
        MIRAGE_CHECK(!t.f.pressed(XK_Shift_L) && !t.f.pressed(XK_Tab));
        MIRAGE_CHECK(*state.find("outcome")->as_string() == "holding");
        MIRAGE_CHECK(t.call("advance", args).failed); // No stale replay.
    }
    (void)::poll(nullptr, 0, 30); // Let the real WM finish drawing its popup.
    const auto popup = t.f.environment->screen()->capture_display(t.f.display_id, {}, {});
    MIRAGE_CHECK(before.ok && popup.ok && before.frame.pixels != popup.frame.pixels);
    state = t.call("observe", J{J::Object{{"session_id", *state.find("session_id")}}}).result;
    MIRAGE_CHECK(t.f.pressed(XK_Alt_L) && t.f.active() == t.f.windows[2]);
    state = t.release(state);
    await([&] { return t.f.active() == t.f.windows[1]; });
    MIRAGE_CHECK(!state.find("verified")->as_boolean().value_or(true));
    MIRAGE_CHECK(t.tools->finish().code.empty());
}
void endings(kairo::Executor &executor) {
    for (const std::string mode :
         {"cancel", "takeover", "authority", "permission", "focus", "wm"}) {
        Tools t(executor);
        auto state = t.begin();
        const auto stepped = t.call("advance", t.args(state));
        MIRAGE_CHECK(!stepped.failed);
        state = stepped.result;
        if (mode == "cancel")
            t.cancel.request_cancel();
        else if (mode == "takeover")
            t.desktop.actions->takeover();
        else if (mode == "authority")
            t.desktop.actions->invalidate_authority();
        else if (mode == "permission") {
            t.denied = "window.activate";
            MIRAGE_CHECK(t.call("advance", t.args(state)).failed);
        } else if (mode == "focus") {
            XSetInputFocus(t.f.client, t.f.windows[0], RevertToParent, CurrentTime);
            XSync(t.f.client, False);
        } else {
            XDeleteProperty(t.f.client, DefaultRootWindow(t.f.client),
                            XInternAtom(t.f.client, "_NET_SUPPORTING_WM_CHECK", False));
            XSync(t.f.client, False);
        }
        await([&] {
            return t.tools->snapshot().find("release_confirmed")->as_boolean().value_or(false);
        });
        MIRAGE_CHECK(t.f.clean() && t.desktop.actions->input_released());
        const auto final = t.tools->snapshot();
        MIRAGE_CHECK(!final.find("error_code")->as_string()->empty());
        const auto late = t.call("advance", t.args(state));
        MIRAGE_CHECK(late.failed ||
                     late.result.find("release_confirmed")->as_boolean().value_or(false));
        MIRAGE_CHECK(t.f.clean());
    }
}
void scope_and_budget(kairo::Executor &executor) {
    {
        Tools t(executor, true);
        for (const auto &spec : t.registry.exposed_tools())
            MIRAGE_CHECK(!spec.wire_name.starts_with("desktop.window_switch."));
        MIRAGE_CHECK(t.f.clean());
    }
    {
        Tools t(executor);
        t.denied = "input.inject";
        const auto refused =
            t.call("begin", J{J::Object{{"window_id", t.f.scope.window_id},
                                        {"environment_epoch",
                                         static_cast<std::int64_t>(t.desktop.actions->epoch())}}});
        MIRAGE_CHECK(refused.failed && t.f.clean() && t.desktop.actions->input_released());
    }
    {
        Tools t(executor);
        const unsigned char version[] = "9.9.9";
        XChangeProperty(t.f.client, DefaultRootWindow(t.f.client),
                        XInternAtom(t.f.client, "_OB_VERSION", False), XA_STRING, 8,
                        PropModeReplace, version, 5);
        XSync(t.f.client, False);
        const auto refused =
            t.call("begin", J{J::Object{{"window_id", t.f.scope.window_id},
                                        {"environment_epoch",
                                         static_cast<std::int64_t>(t.desktop.actions->epoch())}}});
        MIRAGE_CHECK(refused.failed && t.f.clean()); // Factory rechecks the live profile.
        MIRAGE_CHECK(t.desktop.actions->input_released());
    }
    {
        Tools t(executor);
        auto state = t.begin();
        for (int n = 0; n < 61; ++n) {
            const auto step = t.call("advance", t.args(state));
            if (step.failed)
                throw std::runtime_error(step.safe_error_summary);
            state = step.result;
        }
        MIRAGE_CHECK(state.find("commands_remaining")->as_integer() == 2);
        MIRAGE_CHECK(t.call("advance", t.args(state, "backward")).failed);
        MIRAGE_CHECK(t.f.clean() &&
                     t.tools->snapshot().find("command_sequence")->as_integer() == 62);
    }
    {
        Tools t(executor);
        auto backend = t.f.environment->input()->open_input_session(t.f.scope, {});
        d::InputSessionCommand command;
        command.kind = d::InputSessionCommand::Kind::key_down;
        command.key_name = "alt";
        MIRAGE_CHECK(backend && backend->apply(command, {}).ok);
        command.kind = d::InputSessionCommand::Kind::key_tap;
        command.key_name = "tab";
        MIRAGE_CHECK(backend->apply(command, {}).error.code == "invalid_input_context");
        MIRAGE_CHECK(backend->release().input_released && t.f.clean());
    }
}
class Script final : public mira::IModelProvider {
    mira::ModelProfile profile_;
    Fixture &fixture_;
    int step_ = 0;

  public:
    Script(mira::ModelProfile profile, Fixture &fixture)
        : profile_(std::move(profile)), fixture_(fixture) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        J front, state, host;
        bool host_present = false, image = false;
        const std::string prefix = "Host controlled-input state (not business success): ";
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                image |= std::holds_alternative<mira::ImagePart>(part);
                if (const auto *text = std::get_if<mira::TextPart>(&part);
                    text && text->text.starts_with(prefix)) {
                    const auto parsed = mira::parse_json(text->text.substr(prefix.size()));
                    if (parsed) {
                        host = parsed.value();
                        host_present = true;
                    }
                }
                if (const auto *result = std::get_if<mira::ToolResultPart>(&part);
                    result && !result->failed) {
                    if (result->result.find("id") && result->result.find("environment_epoch"))
                        front = result->result;
                    if (result->result.find("session_id"))
                        state = result->result;
                }
            }
        if (host_present)
            state = host;
        const auto step = step_++;
        if (step >= 3 && step <= 7)
            MIRAGE_CHECK(host_present && fixture_.pressed(XK_Alt_L));
        if (step == 4 || step == 6)
            MIRAGE_CHECK(image);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        std::string name;
        J::Object args;
        if (step == 0 || step == 8) {
            if (step == 8)
                await([&] { return fixture_.active() == fixture_.windows[1]; });
            name = "desktop.window.front";
        } else if (step == 1) {
            name = "desktop.window_switch.begin";
            args = {{"window_id", *front.find("id")},
                    {"environment_epoch", *front.find("environment_epoch")}};
        } else if (step == 2 || step == 4 || step == 6) {
            name = "desktop.window_switch.advance";
            args = {{"session_id", *state.find("session_id")},
                    {"state_version", *state.find("state_version")},
                    {"sequence", *state.find("command_sequence")->as_integer() + 1},
                    {"direction", step == 6 ? "backward" : "forward"}};
        } else if (step == 3 || step == 5) {
            name = "desktop.screen.capture";
            args = {{"source", "display"}, {"source_id", fixture_.display_id}};
        } else if (step == 7) {
            name = "desktop.window_switch.release";
            args = {{"session_id", *state.find("session_id")}};
        } else {
            MIRAGE_CHECK(fixture_.clean() && front.find("id") &&
                         *front.find("id")->as_string() == std::to_string(fixture_.windows[1]));
            mira::MessageOutput answer;
            answer.content.emplace_back(mira::OutputTextPart{"verified fixture selection", {}});
            response.output.emplace_back(std::move(answer));
            return response;
        }
        const auto tool =
            std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &spec) {
                return spec.wire_name == mirage::testing::fixture_tool_name(name);
            });
        if (tool == request.tools.end())
            throw std::runtime_error("model switcher tool missing");
        mira::ToolCallOutput call;
        call.tool_id = tool->tool_id;
        call.provider_name = tool->wire_name;
        call.provider_call_id.value = "switch-" + std::to_string(step);
        call.arguments = J{std::move(args)};
        call.arguments_digest = mira::digest_string(mira::to_json_string(call.arguments));
        response.output.emplace_back(std::move(call));
        return response;
    }
};
void model_loop(kairo::Executor &executor) {
    Fixture f;
    i::DesktopToolContext desktop;
    desktop.environment = f.environment;
    desktop.actions = std::make_shared<d::ActionCoordinator>();
    desktop.gate = [](const auto &, const auto &, const auto &cancelled) { return !cancelled(); };
    i::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = "switcher-fixture";
    config.max_output_tokens = 512;
    i::ModelProviderOverride provider(
        [&](const auto &profile) { return std::make_shared<Script>(profile, f); });
    i::ModelLayer model(executor, config, &provider, desktop);
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.task_epoch = 1;
    ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
    const auto result = model.complete_harness_turn("", "choose and verify window", ctx, "", true);
    if (!result.ok)
        std::fprintf(stderr, "model switcher: %s\n", result.error.c_str());
    MIRAGE_CHECK(result.ok && f.clean() && desktop.actions->input_released());
    model.shutdown();
}
} // namespace
int main() {
    XInitThreads();
    kairo::Executor executor;
    MIRAGE_CHECK(executor.initialize({}).ok);
    try {
        std::ifstream file(MIRAGE_SWITCHER_GOLDEN);
        const auto golden = mira::parse_json(std::string{std::istreambuf_iterator<char>{file}, {}});
        if (!golden)
            throw std::runtime_error("switcher golden unavailable");
        executor
            .submit_auto([&] {
                contract(executor, golden.value());
                choose(executor);
                endings(executor);
                scope_and_budget(executor);
                model_loop(executor);
            })
            .get();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "window switcher: %s\n", error.what());
        executor.shutdown(true);
        return 1;
    }
    executor.shutdown(true);
    return mirage::testing::finish("controlled_window_switch");
}
