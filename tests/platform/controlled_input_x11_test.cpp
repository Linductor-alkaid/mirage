#include "../support/provider_tool_name.hpp"
#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"
#include <algorithm>
#include <array>
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
                     KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
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
};
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
        const auto step = requests++;
        if (step == 4 || (step == 5 && mode_ == "recover"))
            MIRAGE_CHECK(has_image);
        if ((step == 3 || step == 4) && mode_ != "long") {
            MIRAGE_CHECK(fixture_.shift() && host_state);
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
                    {"command", mode_ == "long" ? "key_tap" : "key_down"},
                    {"key", "shift"}};
            if (mode_ == "long")
                args.emplace_back("press", "long");
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
    for (const std::string mode :
         {"normal", "long", "unclosed", "protocol", "paused", "policy", "recover"}) {
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
        if (completion.ok != (mode == "normal" || mode == "long" || mode == "recover"))
            std::fprintf(stderr, "hold mode=%s error=%s\n", mode.c_str(), completion.error.c_str());
        MIRAGE_CHECK(completion.ok == (mode == "normal" || mode == "long" || mode == "recover"));
        MIRAGE_CHECK(!f.shift() && desktop.actions->input_released());
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
        MIRAGE_CHECK(down == 1 && up == 1); // No replay after provider or model failure.
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
