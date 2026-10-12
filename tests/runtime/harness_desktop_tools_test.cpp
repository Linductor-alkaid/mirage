#include "../support/fake_desktop_environment.hpp"
#include "../support/ipc_io.hpp"

#include <kairo/executor.hpp>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <mirage/runtime/runtime_service.hpp>

#ifdef MIRAGE_HARNESS_X11
#include "../support/xvfb_display.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std::chrono_literals;
namespace integration = mirage::integration;
namespace ipc = mirage::runtime::ipc;

namespace {
using Environment = mirage::testing::FakeDesktopEnvironment;

std::shared_ptr<Environment> environment() {
    auto desktop = std::make_shared<Environment>();
    desktop->windows = {{"w1", "First", {0, 0, 200, 100}, true},
                        {"w2", "Target", {200, 0, 200, 100}, false}};
    mirage::desktop::SemanticNode button;
    button.ref = "@e1";
    button.role = "button";
    button.name = "Run";
    desktop->snapshots["w2"] = {"fixture", "Target", {button}};
    desktop->actionable_refs = {"@e1"};
    return desktop;
}

class Script final : public mira::IModelProvider {
  public:
    Script(const mira::ModelProfile &profile, bool read_only)
        : profile_(profile), read_only_(read_only) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    std::vector<mira::ModelRequest> requests;
    std::string mutation_fault;
    std::vector<std::pair<std::string, std::string>> actions_override;
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        requests.push_back(request);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.requested_model = profile_.model_selector;
        response.status = mira::ModelCompletionStatus::Completed;
        auto actions =
            read_only_
                ? std::vector<std::pair<std::string, std::string>>{{"desktop.window.front", "{}"}}
                : std::vector<std::pair<std::string, std::string>>{
                      {"desktop.window.list", "{}"},
                      {"desktop.window.activate", R"({"window_id":"w2"})"},
                      {"desktop.window.front", "{}"},
                      {"desktop.input.type_text", R"({"window_id":"w2","text":"probe"})"},
                      {"desktop.accessibility.semantic_snapshot", R"({"window_id":"w2"})"},
                      {"desktop.input.click", R"({"window_id":"w2","ref":"@e1"})"},
                      {"desktop.window.front", "{}"},
                      {"desktop.input.key", R"({"window_id":"w2","key":"ctrl+a"})"},
                      {"desktop.window.front", "{}"}};
        if (!actions_override.empty())
            actions = actions_override;
        const auto step = step_++;
        if (step < actions.size()) {
            const auto &[name, arguments] = actions[step];
            const auto tool =
                std::find_if(request.tools.begin(), request.tools.end(),
                             [&](const auto &entry) { return entry.wire_name == name; });
            if (tool == request.tools.end()) {
                mira::Error error;
                error.code = mira::ErrorCode::UnsupportedCapability;
                error.safe_message = "fixture tool missing: " + name;
                return error;
            }
            mira::ToolCallOutput call;
            call.provider_call_id.value = "call-" + std::to_string(step + 1);
            call.tool_id = tool->tool_id;
            call.provider_name = name;
            call.arguments = mira::parse_json(arguments).value();
            if (tool->has_side_effects && name != "wait") {
                mira::JsonValue latest;
                for (const auto &item : request.input)
                    for (const auto &part : item.content)
                        if (const auto *result = std::get_if<mira::ToolResultPart>(&part);
                            result && !result->failed)
                            if (result->result.find("observation_scope"))
                                latest = result->result;
                auto object = *call.arguments.as_object();
                if (!name.starts_with("desktop.application."))
                    object.emplace_back("verify_kind", "window_focused");
                if (const auto *scope = latest.find("observation_scope"))
                    object.emplace_back("observation_scope", *scope);
                if (const auto *epoch = latest.find("environment_epoch"))
                    object.emplace_back("environment_epoch", *epoch);
                if (mutation_fault == "stale_scope")
                    for (auto &[field, value] : object)
                        if (field == "observation_scope")
                            value = mira::JsonValue{std::string{"wrong-scope"}};
                if (mutation_fault == "stale_epoch")
                    for (auto &[field, value] : object)
                        if (field == "environment_epoch")
                            value = mira::JsonValue{value.as_integer().value() + 1};
                if (mutation_fault == "missing_scope")
                    std::erase_if(object, [](const auto &field) {
                        return field.first == "observation_scope";
                    });
                call.arguments = mira::JsonValue{std::move(object)};
            }
            call.arguments_digest = mira::digest_string(mira::to_json_string(call.arguments));
            response.output.emplace_back(std::move(call));
        } else {
            step_ = 0;
            mira::MessageOutput message;
            mira::OutputTextPart text;
            text.text = "observed; not business verified";
            message.content.emplace_back(std::move(text));
            response.output.emplace_back(std::move(message));
        }
        return response;
    }

  private:
    mira::ModelProfile profile_;
    bool read_only_;
    std::size_t step_ = 0;
};

integration::ModelLayerConfig model_config() {
    integration::ModelLayerConfig config;
    config.enabled = true;
    config.model_selector = "fixture";
    config.endpoint_origin = "https://fixture.example";
    config.request_deadline = 3s;
    config.max_output_tokens = 512;
    return config;
}

bool failed_result(const Script &script, const std::string &call_id, const std::string &reason) {
    for (const auto &request : script.requests)
        for (const auto &item : request.input)
            for (const auto &part : item.content)
                if (const auto *result = std::get_if<mira::ToolResultPart>(&part);
                    result && result->provider_call_id.value == call_id && result->failed &&
                    result->safe_error_summary.find(reason) != std::string::npos)
                    return true;
    return false;
}

void layer_scenario(const std::string &scenario) {
    kairo::Executor owner;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(owner.initialize(config)));
    auto desktop = environment();
    mirage::desktop::CancelToken cancel;
    const bool read_only = scenario == "read_only";
    std::shared_ptr<Script> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile, read_only);
        script->mutation_fault = scenario;

        return script;
    });
    integration::DesktopToolContext tools;
    auto context = mira::make_control_context();
    tools.environment = desktop;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    std::vector<std::string> capabilities;
    tools.gate = [&](const std::string &capability, const std::string &resource,
                     const integration::AtomCancelProbe &probe) {
        capabilities.push_back(capability);
        MIRAGE_CHECK(resource == "w2" || (scenario == "focus_changed" && resource == "w1"));
        if (capability == "input.inject") {
            if (scenario == "epoch_during_gate") {
                tools.actions->takeover();
                (void)tools.actions->resume();
            }
            if (scenario == "backend_scope")
                (void)desktop->accessibility()->semantic_snapshot("w2");
            if (scenario == "focus_changed") {
                desktop->windows[0].focused = true;
                desktop->windows[1].focused = false;
            }
            if (scenario == "cancelled")
                cancel.request_cancel();
            if (scenario == "expired") {
                // Expire at this gate, rather than relying on a 20 ms race
                // with Executor admission and sanitizer instrumentation.
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                    *context.deadline - std::chrono::steady_clock::now());
                const auto wait = std::clamp(remaining.count() + 10, 10L, 1010L);
                (void)::poll(nullptr, 0, static_cast<int>(wait));
            }
            if (scenario == "throwing")
                throw std::runtime_error("private provider detail");
            if (scenario == "denied")
                return false;
        }
        return !probe();
    };
    if (scenario == "normal") {
        tools.authorize = [gate = tools.gate](const auto &capability, const auto &resource,
                                              const auto &probe, const auto &task_id,
                                              const auto &operation_id) {
            MIRAGE_CHECK(!task_id.empty() && !operation_id.empty());
            return gate(capability, resource, probe);
        };
        tools.gate = [](const auto &, const auto &, const auto &) { return false; };
    }
    integration::ModelLayer layer(owner, model_config(), &provider, tools);
    context.session = mira::SessionId::generate();
    context.cancellation_requested = [cancel] { return cancel.cancelled(); };
    std::vector<std::string> tool_states;
    auto future = owner.submit_auto([&] {
        context.deadline = std::chrono::steady_clock::now() + (scenario == "expired" ? 1s : 3s);
        return layer.complete_harness_turn(
            {}, "use the desktop", context, "", !read_only, {},
            [&](const auto &parts) {
                for (const auto &part : parts)
                    if (part.kind == "tool")
                        tool_states.push_back(part.status);
            },
            cancel);
    });
    const auto result = future.get();
    if (!result.ok)
        std::fprintf(stderr, "scenario %s: %s\n", scenario.c_str(), result.error.c_str());
    const bool success = scenario == "normal" || read_only || scenario == "backend_scope";
    MIRAGE_CHECK(result.ok == success);
    // A stale backend reference rejects the click before dispatch. The later
    // fresh observation permits the separately verified keyboard action.
    MIRAGE_CHECK(desktop->input_log.size() == (success && !read_only ? 3u : 0u));
    MIRAGE_CHECK(script && !script->requests.empty());
    if (!script || script->requests.empty()) {
        layer.shutdown();
        owner.shutdown(true);
        return;
    }
    if (scenario == "expired")
        MIRAGE_CHECK(std::find(capabilities.begin(), capabilities.end(), "input.inject") !=
                     capabilities.end());
    MIRAGE_CHECK(script->requests.front().tools.size() == (read_only ? 4u : 12u));
    for (const auto &tool : script->requests.front().tools) {
        MIRAGE_CHECK(tool.wire_name != "desktop.process.execute");
        MIRAGE_CHECK(tool.wire_name != "desktop.clipboard.write_text");
        if (read_only)
            MIRAGE_CHECK(!tool.has_side_effects && tool.wire_name != "wait");
    }
    if (read_only)
        MIRAGE_CHECK(capabilities.empty());
    if (scenario == "stale_scope" || scenario == "stale_epoch")
        MIRAGE_CHECK(failed_result(*script, "call-2", "stale_observation"));
    if (scenario == "backend_scope") {
        MIRAGE_CHECK(desktop->activated_refs.empty());
        MIRAGE_CHECK(failed_result(*script, "call-6", "stale_observation"));
    }
    if (scenario == "epoch_during_gate")
        MIRAGE_CHECK(failed_result(*script, "call-4", "stale_observation"));
    if (scenario == "denied")
        MIRAGE_CHECK(failed_result(*script, "call-4", "permission denied"));
    if (scenario == "focus_changed")
        MIRAGE_CHECK(failed_result(*script, "call-4", "focused window changed"));
    if (scenario == "throwing")
        MIRAGE_CHECK(failed_result(*script, "call-4", "tool handler failed"));
    if (scenario == "cancelled" || scenario == "expired")
        MIRAGE_CHECK(result.cancelled || result.failed);
    MIRAGE_CHECK(std::find(tool_states.begin(), tool_states.end(), "running") != tool_states.end());
    if (success) {
        MIRAGE_CHECK(std::find(tool_states.begin(), tool_states.end(), "complete") !=
                     tool_states.end());
        MIRAGE_CHECK(result.tool_calls == (read_only ? 1u : 9u));
    }
    if (scenario == "normal") {
        for (const auto &part : result.parts)
            if (part.name == "desktop.input.type_text") {
                MIRAGE_CHECK(part.input.find("probe") == std::string::npos);
                MIRAGE_CHECK(part.input.find("text_bytes") != std::string::npos);
                MIRAGE_CHECK(part.output.find("dispatched") != std::string::npos);
                MIRAGE_CHECK(part.output.find("condition_matched") != std::string::npos);
            }
        const auto second = owner
                                .submit_auto([&] {
                                    return layer.complete_harness_turn({}, "second turn", context,
                                                                       "", true, {}, {}, cancel);
                                })
                                .get();
        MIRAGE_CHECK(second.ok && second.tool_calls == 9 && desktop->input_log.size() == 6);
        MIRAGE_CHECK(script->requests[0].tools.front().tool_id !=
                     script->requests[10].tools.front().tool_id);
    }
    layer.shutdown();
    owner.shutdown(true);
}

void reasoning_scenario() {
    for (const auto *reasoning : {"none", "adaptive", "low"}) {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<Script> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<Script>(profile, true);
            return script;
        });
        auto config = model_config();
        config.dialect = "anthropic.messages.v1";
        config.model_selector = "MiniMax-M3";
        integration::DesktopToolContext tools;
        tools.environment = environment();
        integration::ModelLayer layer(owner, config, &provider, tools);
        auto context = mira::make_control_context();
        context.task = mira::TaskId::generate();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 3s;
        const auto result = owner
                                .submit_auto([&] {
                                    return layer.complete_harness_turn({}, "observe only", context,
                                                                       reasoning, false);
                                })
                                .get();
        const bool valid = std::string{reasoning} != "low";
        MIRAGE_CHECK(result.ok == valid && result.failed == !valid);
        MIRAGE_CHECK(script && script->requests.size() == (valid ? 2u : 0u));
        for (const auto &request : script->requests) {
            MIRAGE_CHECK(request.generation.thinking == (std::string{reasoning} == "none"
                                                             ? mira::ThinkingMode::Disabled
                                                             : mira::ThinkingMode::Adaptive));
            MIRAGE_CHECK(!request.generation.reasoning_effort);
            MIRAGE_CHECK(request.generation.max_output_tokens ==
                         (std::string{reasoning} == "none" ? 512u : 8192u));
        }
        layer.shutdown();
        owner.shutdown(true);
    }
}

void registration_scenario() {
    auto desktop = environment();
    mira::BuiltinToolRegistry empty;
    integration::DesktopAtomToolset::register_harness_tools(empty, {}, true, {}, {});
    MIRAGE_CHECK(empty.size() == 0);
    integration::DesktopToolContext tools;
    tools.environment = desktop;
    mira::BuiltinToolRegistry no_gate;
    integration::DesktopAtomToolset::register_harness_tools(no_gate, tools, true, {}, {});
    MIRAGE_CHECK(no_gate.size() == 4);
    tools.gate = [](const auto &, const auto &, const auto &) { return true; };
    mira::BuiltinToolRegistry no_owner;
    integration::DesktopAtomToolset::register_harness_tools(no_owner, tools, true, {}, {});
    MIRAGE_CHECK(no_owner.size() == 4);
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    desktop->application_uri_supported = false;
    mira::BuiltinToolRegistry unsupported_uri;
    integration::DesktopAtomToolset::register_harness_tools(unsupported_uri, tools, true, {}, {});
    const auto exposed = unsupported_uri.exposed_tools();
    MIRAGE_CHECK(std::none_of(exposed.begin(), exposed.end(), [](const auto &tool) {
        return tool.wire_name == "desktop.application.open_uri";
    }));
    MIRAGE_CHECK(std::any_of(exposed.begin(), exposed.end(), [](const auto &tool) {
        return tool.wire_name == "desktop.application.launch";
    }));
    class InputProviderOnly final : public mirage::desktop::DesktopEnvironment {
      public:
        explicit InputProviderOnly(std::shared_ptr<Environment> env) : env_(std::move(env)) {}
        mirage::desktop::EnvironmentInfo info() const override { return {"partial", "fixture"}; }
        mirage::desktop::InputProvider *input() override { return env_->input(); }

      private:
        std::shared_ptr<Environment> env_;
    };
    tools.environment = std::make_shared<InputProviderOnly>(desktop);
    tools.gate = [](const auto &, const auto &, const auto &) { return true; };
    mira::BuiltinToolRegistry partial;
    integration::DesktopAtomToolset::register_harness_tools(partial, tools, true, {}, {});
    MIRAGE_CHECK(partial.size() == 0);
}

#ifdef MIRAGE_HARNESS_X11
void x11_scenario() {
    mirage::testing::XvfbDisplay server(mirage::testing::find_xvfb());
    Display *display = XOpenDisplay(server.display_name().c_str());
    MIRAGE_CHECK(display != nullptr);
    if (!display)
        return;
    const Window target =
        XCreateSimpleWindow(display, DefaultRootWindow(display), 10, 10, 300, 150, 0, 0, 0);
    XStoreName(display, target, "M7 private target");
    XSelectInput(display, target,
                 KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask);
    XMapWindow(display, target);
    XSync(display, False);
    using namespace mirage::platform::linux_backend;
    auto desktop = std::make_shared<LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{}, X11Options{true, server.display_name()});
    MIRAGE_CHECK(desktop->window() && desktop->input());
    const auto windows = desktop->window()->list_windows();
    const auto found =
        std::find_if(windows.windows.begin(), windows.windows.end(),
                     [](const auto &window) { return window.title == "M7 private target"; });
    MIRAGE_CHECK(windows.ok && found != windows.windows.end());
    if (found != windows.windows.end()) {
        const auto id = found->id;
        kairo::Executor owner;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 2;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize(config)));
        integration::ModelProviderOverride provider([&](const auto &profile) {
            auto script = std::make_shared<Script>(profile, false);
            script->actions_override = {
                {"desktop.window.list", "{}"},
                {"desktop.window.activate", "{\"window_id\":\"" + id + "\"}"},
                {"desktop.window.front", "{}"},
                {"desktop.input.type_text", "{\"window_id\":\"" + id + "\",\"text\":\"probe\"}"},
                {"desktop.window.front", "{}"}};
            return script;
        });
        integration::DesktopToolContext tools;
        tools.environment = desktop;
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        tools.gate = [](const auto &, const auto &, const auto &) { return true; };
        integration::ModelLayer layer(owner, model_config(), &provider, tools);
        auto context = mira::make_control_context();
        context.deadline = std::chrono::steady_clock::now() + 3s;
        const auto result =
            owner
                .submit_auto(
                    [&] { return layer.complete_harness_turn({}, "private X11 fixture", context); })
                .get();
        if (!result.ok)
            std::fprintf(stderr, "X11 harness: %s\n", result.error.c_str());
        MIRAGE_CHECK(result.ok && result.tool_calls == 5);
        const auto front = desktop->window()->front_window();
        MIRAGE_CHECK(front.ok && front.found && front.window.id == id);
        std::string typed;
        int releases = 0;
        const auto deadline = std::chrono::steady_clock::now() + 1s;
        while ((typed.size() < 5 || releases < 5) && std::chrono::steady_clock::now() < deadline) {
            if (!XPending(display)) {
                (void)::poll(nullptr, 0, 1);
                continue;
            }
            XEvent event{};
            XNextEvent(display, &event);
            if (event.type == KeyPress) {
                char buffer[8]{};
                const int count =
                    XLookupString(&event.xkey, buffer, sizeof(buffer), nullptr, nullptr);
                if (count > 0)
                    typed.append(buffer, static_cast<std::size_t>(count));
            } else if (event.type == KeyRelease) {
                ++releases;
            }
        }
        MIRAGE_CHECK(typed == "probe" && releases == 5);
        // Real complete actions, cancellation after a modifier down, timeout,
        // newline, atomic Unicode refusal and preservation of ambient input.
        auto *input = desktop->input();
        mirage::desktop::InputLimits limits;
        const auto keys_released = [&] {
            char physical[32]{};
            XQueryKeymap(display, physical);
            return std::all_of(std::begin(physical), std::end(physical),
                               [](char value) { return value == 0; });
        };
        MIRAGE_CHECK(input->key_chord({"ctrl+shift+t"}, limits, {}).ok);
        MIRAGE_CHECK(keys_released());
        int probes = 0;
        const auto cancelled_key = input->key_chord(
            {"ctrl+shift+t"}, limits, mirage::desktop::CancelToken{[&] { return ++probes >= 3; }});
        MIRAGE_CHECK(!cancelled_key.ok && cancelled_key.cancelled &&
                     cancelled_key.side_effect_may_have_occurred && cancelled_key.input_released);
        MIRAGE_CHECK(keys_released());
        probes = 0;
        limits.timeout = 1ms;
        const auto timed_key =
            input->key_chord({"ctrl+shift+t"}, limits, mirage::desktop::CancelToken{[&] {
                                 if (++probes == 3)
                                     (void)::poll(nullptr, 0, 5);
                                 return false;
                             }});
        MIRAGE_CHECK(!timed_key.ok && timed_key.error.code == "timeout" &&
                     timed_key.side_effect_may_have_occurred && timed_key.input_released);
        MIRAGE_CHECK(keys_released());
        limits.timeout = 1s;
        MIRAGE_CHECK(input->inject_key({"ctrl+a"}, true, limits, {}).ok);
        char held_before[32]{}, held_after[32]{};
        XQueryKeymap(display, held_before);
        const auto busy = input->key_chord({"ctrl+shift+t"}, limits, {});
        XQueryKeymap(display, held_after);
        MIRAGE_CHECK(!busy.ok && busy.error.code == "input_busy" &&
                     !busy.side_effect_may_have_occurred);
        MIRAGE_CHECK(
            std::equal(std::begin(held_before), std::end(held_before), std::begin(held_after)));
        MIRAGE_CHECK(input->inject_key({"ctrl+a"}, false, limits, {}).ok);
        MIRAGE_CHECK(keys_released());
        while (XPending(display)) {
            XEvent event{};
            XNextEvent(display, &event);
        }
        const auto unicode = input->type_text("ASCII中", limits, {});
        MIRAGE_CHECK(!unicode.ok && unicode.error.code == "unsupported_key" &&
                     !unicode.side_effect_may_have_occurred);
        XSync(display, False);
        MIRAGE_CHECK(XPending(display) == 0);
        MIRAGE_CHECK(input->type_text("a\nb", limits, {}).ok);
        XSync(display, False);
        std::string multiline;
        while (XPending(display)) {
            XEvent event{};
            XNextEvent(display, &event);
            if (event.type == KeyPress) {
                char buffer[8]{};
                const int count =
                    XLookupString(&event.xkey, buffer, sizeof(buffer), nullptr, nullptr);
                if (count > 0)
                    multiline.append(buffer, static_cast<std::size_t>(count));
            }
        }
        MIRAGE_CHECK(multiline == "a\rb" && keys_released());
        probes = 0;
        const auto cancelled_click = input->click_at(
            30, 30, "left", limits, mirage::desktop::CancelToken{[&] { return ++probes >= 3; }});
        MIRAGE_CHECK(!cancelled_click.ok && cancelled_click.cancelled &&
                     cancelled_click.side_effect_may_have_occurred &&
                     cancelled_click.input_released);
        Window root{}, child{};
        int rx{}, ry{}, wx{}, wy{};
        unsigned int mask{};
        MIRAGE_CHECK(XQueryPointer(display, DefaultRootWindow(display), &root, &child, &rx, &ry,
                                   &wx, &wy, &mask) != 0);
        MIRAGE_CHECK((mask & Button1Mask) == 0);
        layer.shutdown();
        owner.shutdown(true);
    }
    XDestroyWindow(display, target);
    XCloseDisplay(display);
}
#endif

void service_scenario(bool replacement, bool deny_input, bool uri_handoff = false) {
    mirage::testing::TempDir temp;
    auto desktop = environment();
    if (uri_handoff) {
        desktop->applications = {{"browser.desktop", "Fixture Browser", true}};
        desktop->application_instances["browser.desktop"] = "existing";
        desktop->on_application_uri = [desktop] {
            desktop->windows.push_back({"w3", "Fixture browser page", {0, 100, 200, 100}, false});
        };
    }
    auto binding = std::make_shared<integration::MiraEnvironmentBinding>(desktop);
    mirage::runtime::ServiceConfig config;
    config.socket_path = temp.root() / "service.sock";
    config.persist_recovery_state = false;
    config.persist_session_state = false;
    config.persist_settings = false;
    config.model = model_config();
    if (replacement)
        config.model.enabled = false;
    if (deny_input)
        config.permission_policy.rules[static_cast<std::size_t>(
            uri_handoff ? mirage::runtime::permission::Capability::ApplicationLaunch
                        : mirage::runtime::permission::Capability::InputInject)] =
            mirage::runtime::permission::Rule::Deny;
    if (uri_handoff && !deny_input)
        config.permission_policy.rules[static_cast<std::size_t>(
            mirage::runtime::permission::Capability::ApplicationLaunch)] =
            mirage::runtime::permission::Rule::Allow;
    config.model_provider_override = std::make_shared<
        integration::ModelProviderOverride>([uri_handoff](const auto &profile) {
        auto script = std::make_shared<Script>(profile, false);
        if (uri_handoff)
            script->actions_override = {
                {"desktop.application.list", "{}"},
                {"desktop.application.open_uri",
                 R"({"application_id":"browser.desktop","uri":"https://example.invalid/private-query"})"},
                {"desktop.window.list", "{}"},
                {"desktop.window.activate", R"({"window_id":"w3"})"},
                {"desktop.window.front", "{}"}};
        return script;
    });
    mirage::runtime::RuntimeService service(config);
    MIRAGE_CHECK(service.start(binding).ok);
    ipc::IpcClient client(config.socket_path);
    if (replacement) {
        mirage::runtime::persistence::LocalSettings settings;
        settings.model = mirage::runtime::persistence::ModelSettings{
            true, "openai.responses.v1", "fixture", "https://fixture.example", "/v1", "fixture", "",
            0};
        MIRAGE_CHECK(
            client
                .call(ipc::SetModelRequest{mirage::runtime::persistence::encode_settings(settings)},
                      2s)
                .ok);
    }
    const auto sessions = client.call(ipc::ListSessionsRequest{}, 2s);
    MIRAGE_CHECK(sessions.ok);
    const auto session = std::get<ipc::SessionList>(sessions.payload).sessions.front().id;
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "desktop fixture", true}, 2s).ok);
    bool settled = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!settled && std::chrono::steady_clock::now() < deadline) {
        const auto history = client.call(ipc::ChatHistoryRequest{session, 10}, 1s);
        if (const auto *view = std::get_if<ipc::DialogHistory>(&history.payload);
            history.ok && view && !view->turns.empty() && view->turns.back().status != "pending") {
            const auto &turn = view->turns.back();
            if (turn.status != "ok")
                std::fprintf(stderr, "service turn: %s\n", turn.error.c_str());
            MIRAGE_CHECK(turn.status == (deny_input ? "failed" : "ok"));
            MIRAGE_CHECK(std::count_if(turn.parts.begin(), turn.parts.end(), [](const auto &part) {
                             return part.kind == "tool" && part.status == "complete";
                         }) == (uri_handoff ? (deny_input ? 3 : 5) : (deny_input ? 6 : 9)));
            if (uri_handoff)
                for (const auto &part : turn.parts) {
                    MIRAGE_CHECK(part.input.find("https://example.invalid/private-query") ==
                                 std::string::npos);
                    if (!deny_input && part.name == "desktop.application.open_uri") {
                        const auto evidence = mirage::conversation::desktop_evidence(part);
                        MIRAGE_CHECK(evidence && evidence->application_id == "browser.desktop" &&
                                     evidence->status == "verified");
                    }
                }
            settled = true;
        }
        if (!settled)
            (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(settled);
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
    MIRAGE_CHECK(desktop->input_log.size() == (deny_input || uri_handoff ? 0u : 3u));
    if (uri_handoff) {
        MIRAGE_CHECK(desktop->application_uri_log.size() == (deny_input ? 0u : 1u));
        desktop->on_application_uri = {}; // Release fixture callback's shared owner.
    }
}

// A running provider only sees the pinned-free token. IPC cancellation and
// service shutdown must reach it while the ConversationLoop is inside it.
class WaitingInput final : public mirage::desktop::DesktopEnvironment,
                           private mirage::desktop::InputProvider {
  public:
    explicit WaitingInput(std::string fault = {}) : fault_(std::move(fault)) {}
    std::atomic_bool started{false};
    std::atomic_bool observed_cancel{false};
    std::atomic_bool held{false};
    mirage::desktop::EnvironmentInfo info() const override { return {"waiting", "fixture"}; }
    mirage::desktop::WindowProvider *window() override { return desktop_->window(); }
    mirage::desktop::AccessibilityProvider *accessibility() override {
        return desktop_->accessibility();
    }
    mirage::desktop::ApplicationProvider *application() override { return desktop_->application(); }
    mirage::desktop::InputProvider *input() override { return this; }
    bool complete_actions_supported() const override { return true; }

  private:
    std::string fault_;
    std::shared_ptr<Environment> desktop_ = environment();
    mirage::desktop::InputOutcome type_text(const std::string &,
                                            const mirage::desktop::InputLimits &limits,
                                            const mirage::desktop::CancelToken &cancel) override {
        held.store(true);
        started.store(true);
        if (!fault_.empty()) {
            // The throwing fixture cleans its own presses before propagating;
            // the caller still cannot prove cleanup from an exception alone.
            held.store(false);
            if (fault_ == "exception")
                throw std::runtime_error("provider fixture");
            return {fault_ == "inconsistent",
                    false,
                    {"input_release_failed", "fixture cleanup failed"},
                    true,
                    false};
        }
        const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
        while (!cancel.cancelled() && std::chrono::steady_clock::now() < deadline)
            (void)::poll(nullptr, 0, 1);
        observed_cancel.store(cancel.cancelled());
        held.store(false);
        return {false, cancel.cancelled(), {"cancelled", "fixture input stopped"}};
    }
    mirage::desktop::InputOutcome inject_key(const mirage::desktop::KeySym &key, bool pressed,
                                             const mirage::desktop::InputLimits &limits,
                                             const mirage::desktop::CancelToken &cancel) override {
        return desktop_->input()->inject_key(key, pressed, limits, cancel);
    }
    mirage::desktop::InputOutcome
    pointer_move(std::int32_t x, std::int32_t y, const mirage::desktop::InputLimits &limits,
                 const mirage::desktop::CancelToken &cancel) override {
        return desktop_->input()->pointer_move(x, y, limits, cancel);
    }
    mirage::desktop::InputOutcome
    pointer_button(const mirage::desktop::MouseButton &button, bool pressed,
                   const mirage::desktop::InputLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override {
        return desktop_->input()->pointer_button(button, pressed, limits, cancel);
    }
    mirage::desktop::PointerQueryOutcome
    pointer_position(const mirage::desktop::CancelToken &cancel) override {
        return desktop_->input()->pointer_position(cancel);
    }
};

void service_cancel_scenario(bool shutdown, bool takeover = false) {
    mirage::testing::TempDir temp;
    auto desktop = std::make_shared<WaitingInput>();
    mirage::runtime::ServiceConfig config;
    config.socket_path = temp.root() / "cancel.sock";
    config.persist_recovery_state = config.persist_session_state = config.persist_settings = false;
    config.model = model_config();
    config.model_provider_override = std::make_shared<integration::ModelProviderOverride>(
        [](const auto &profile) { return std::make_shared<Script>(profile, false); });
    mirage::runtime::RuntimeService service(config);
    MIRAGE_CHECK(service.start(std::make_shared<integration::MiraEnvironmentBinding>(desktop)).ok);
    ipc::IpcClient client(config.socket_path);
    const auto sessions = client.call(ipc::ListSessionsRequest{}, 2s);
    MIRAGE_CHECK(sessions.ok);
    const auto session = std::get<ipc::SessionList>(sessions.payload).sessions.front().id;
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "cancel provider", true}, 2s).ok);
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!desktop->started.load() && std::chrono::steady_clock::now() < deadline)
        (void)::poll(nullptr, 0, 1);
    MIRAGE_CHECK(desktop->started.load());
    if (!shutdown) {
        if (takeover) {
            service.request_desktop_takeover();
        } else
            MIRAGE_CHECK(client.call(ipc::CancelChatRequest{session}, 2s).ok);
        const auto stopped = std::chrono::steady_clock::now() + 1s;
        while (!desktop->observed_cancel.load() && std::chrono::steady_clock::now() < stopped)
            (void)::poll(nullptr, 0, 1);
    }
    if (takeover) {
        bool resumed = false;
        const auto released = std::chrono::steady_clock::now() + 1s;
        while (!resumed && std::chrono::steady_clock::now() < released) {
            resumed = service.resume_desktop();
            if (!resumed)
                (void)::poll(nullptr, 0, 1);
        }
        MIRAGE_CHECK(resumed);
    }
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
    MIRAGE_CHECK(desktop->observed_cancel.load() && !desktop->held.load());
}

void service_release_failure(const std::string &fault) {
    mirage::testing::TempDir temp;
    auto desktop = std::make_shared<WaitingInput>(fault);
    mirage::runtime::ServiceConfig config;
    config.socket_path = temp.root() / "release.sock";
    config.persist_recovery_state = config.persist_session_state = config.persist_settings = false;
    config.model = model_config();
    config.model_provider_override = std::make_shared<integration::ModelProviderOverride>(
        [](const auto &profile) { return std::make_shared<Script>(profile, false); });
    mirage::runtime::RuntimeService service(config);
    MIRAGE_CHECK(service.start(std::make_shared<integration::MiraEnvironmentBinding>(desktop)).ok);
    ipc::IpcClient client(config.socket_path);
    const auto sessions = client.call(ipc::ListSessionsRequest{}, 2s);
    MIRAGE_CHECK(sessions.ok);
    const auto session = std::get<ipc::SessionList>(sessions.payload).sessions.front().id;
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "cleanup fixture", true}, 2s).ok);
    bool settled = false, uncertain = false, fenced = false;
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!settled && std::chrono::steady_clock::now() < deadline) {
        const auto history = client.call(ipc::ChatHistoryRequest{session, 10}, 1s);
        if (const auto *view = std::get_if<ipc::DialogHistory>(&history.payload);
            history.ok && view && !view->turns.empty() && view->turns.back().status != "pending") {
            for (const auto &part : view->turns.back().parts) {
                if (part.output.find("uncertain") != std::string::npos &&
                    part.output.find("\"input_released\":false") != std::string::npos)
                    uncertain = true;
                if (part.output.find("input_release_failed") != std::string::npos)
                    fenced = true;
            }
            settled = true;
        }
        if (!settled)
            (void)::poll(nullptr, 0, 1);
    }
    MIRAGE_CHECK(settled && uncertain && fenced);
    MIRAGE_CHECK(!service.resume_desktop());
    service.request_shutdown();
    const auto report = service.run();
    MIRAGE_CHECK(!report.clean && report.diagnostic == "desktop input release did not complete");
}
} // namespace

int main() {
#ifdef MIRAGE_HARNESS_X11
    MIRAGE_CHECK(XInitThreads() != 0);
#endif
    registration_scenario();
    reasoning_scenario();
    for (const auto *scenario :
         {"normal", "read_only", "denied", "focus_changed", "cancelled", "expired", "throwing",
          "stale_scope", "stale_epoch", "missing_scope", "epoch_during_gate", "backend_scope"})
        layer_scenario(scenario);
    service_scenario(false, false);
    service_scenario(true, false);
    service_scenario(false, true);
    service_scenario(false, false, true);
    service_scenario(false, true, true);
    service_cancel_scenario(false);
    service_cancel_scenario(true);
    service_cancel_scenario(false, true);
    service_release_failure("failed");
    service_release_failure("exception");
    service_release_failure("inconsistent");
#ifdef MIRAGE_HARNESS_X11
    x11_scenario();
#endif
    return mirage::testing::finish("harness_desktop_tools_test");
}
