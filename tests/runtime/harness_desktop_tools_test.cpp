#include "../support/fake_desktop_environment.hpp"
#include "../support/ipc_io.hpp"
#include "../support/provider_tool_name.hpp"

// <X11/Xlib.h> (pulled in under MIRAGE_HARNESS_X11 below) defines `Status`
// as a macro, so the frame-read discriminator cannot be spelled after those
// headers; park the classification helper ahead of them instead.
namespace {
bool frame_message(const mirage::testing::FrameRead &read) {
    return read.status == mirage::testing::FrameRead::Status::Message;
}
} // namespace

#include <kairo/executor.hpp>
#include <mira/model_budget.hpp>
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
#include <optional>
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
    /// DEC-085 epoch-reset probe: coordinator whose environment epoch the
    /// fixture advances (takeover + resume, a fresh observation) immediately
    /// before serving the model turns listed here — i.e. between tool
    /// executions, where no observation wrapper can see the change.
    mirage::desktop::ActionCoordinator *coordinator = nullptr;
    std::vector<std::size_t> advance_epoch_before_turn;
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
        if (coordinator != nullptr &&
            std::find(advance_epoch_before_turn.begin(), advance_epoch_before_turn.end(), step) !=
                advance_epoch_before_turn.end()) {
            coordinator->takeover();
            (void)coordinator->resume();
        }
        if (step < actions.size()) {
            const auto &[name, arguments] = actions[step];
            const auto tool =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &entry) {
                    return entry.wire_name == mirage::testing::fixture_tool_name(name);
                });
            if (tool == request.tools.end()) {
                mira::Error error;
                error.code = mira::ErrorCode::UnsupportedCapability;
                error.safe_message = "fixture tool missing: " + name;
                return error;
            }
            mira::ToolCallOutput call;
            call.provider_call_id.value = "call-" + std::to_string(step + 1);
            call.tool_id = tool->tool_id;
            call.provider_name = tool->wire_name;
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

bool request_text_contains(const mira::ModelRequest &request, const std::string &needle) {
    for (const auto &item : request.input)
        for (const auto &part : item.content)
            if (const auto *text = std::get_if<mira::TextPart>(&part);
                text && text->text.find(needle) != std::string::npos)
                return true;
    return false;
}

// DEC-085 failure-guard fixture: counts semantic_snapshot dispatches at the
// provider boundary and injects one deterministic failure kind, so the
// session-scoped blocking can be observed as "handler stopped running" rather
// than inferred from tool receipts alone.
class CountingAccessibility final : public mirage::desktop::AccessibilityProvider {
  public:
    explicit CountingAccessibility(mirage::desktop::AccessibilityProvider *inner) : inner_(inner) {}
    bool scoped_references_supported() const override {
        return inner_->scoped_references_supported();
    }
    mirage::desktop::SnapshotOutcome
    semantic_snapshot(const std::string &window_id,
                      const mirage::desktop::SemanticSnapshotLimits &limits,
                      const mirage::desktop::CancelToken &cancel) override {
        ++calls;
        mirage::desktop::SnapshotOutcome outcome;
        if (failure == "permission_denied")
            outcome.error = {"permission_denied", "fixture denied snapshot"};
        else if (failure == "unsupported_window")
            outcome.error = {"unsupported_window", "fixture has no accessibility tree"};
        else
            return inner_->semantic_snapshot(window_id, limits, cancel);
        return outcome;
    }
    mirage::desktop::ElementActionOutcome
    activate_element(const mirage::desktop::ElementTarget &target,
                     const mirage::desktop::CancelToken &cancel) override {
        return inner_->activate_element(target, cancel);
    }
    mirage::desktop::ElementActionOutcome
    set_text(const mirage::desktop::ElementTarget &target, const std::string &text,
             const mirage::desktop::InputLimits &limits,
             const mirage::desktop::CancelToken &cancel) override {
        return inner_->set_text(target, text, limits, cancel);
    }
    int calls = 0;
    std::string failure; ///< "", "permission_denied" or "unsupported_window"

  private:
    mirage::desktop::AccessibilityProvider *inner_;
};

// Wraps the fake environment so only the accessibility provider is replaced;
// every other provider stays the shared fake.
class SnapshotCountingEnvironment final : public mirage::desktop::DesktopEnvironment {
  public:
    explicit SnapshotCountingEnvironment(std::shared_ptr<Environment> inner)
        : inner_(std::move(inner)), accessibility_(inner_->accessibility()) {}
    mirage::desktop::EnvironmentInfo info() const override { return inner_->info(); }
    mirage::desktop::FilesystemProvider *filesystem() override { return &inner_->filesystem_; }
    mirage::desktop::ProcessProvider *process() override { return &inner_->process_; }
    mirage::desktop::ApplicationProvider *application() override { return &inner_->application_; }
    mirage::desktop::WindowProvider *window() override { return &inner_->window_; }
    mirage::desktop::AccessibilityProvider *accessibility() override { return &accessibility_; }
    mirage::desktop::ScreenProvider *screen() override { return &inner_->screen_; }
    mirage::desktop::InputProvider *input() override { return &inner_->input_; }
    mirage::desktop::ClipboardProvider *clipboard() override { return &inner_->clipboard_; }
    mirage::desktop::NotificationProvider *notification() override {
        return &inner_->notification_;
    }
    /// DEC-085 declared capture capability, independent of the provider face
    /// (the fake screen always answers, so the default derivation is Full).
    mirage::desktop::ScreenCapability screen_capability() override { return screen_capability_; }
    mirage::desktop::ScreenCapability screen_capability_ = mirage::desktop::ScreenCapability::Full;
    std::shared_ptr<Environment> inner_;
    CountingAccessibility accessibility_;
};

// DEC-085 SSE-recovery fixture: the first inference fails with the pinned
// stream-lifecycle protocol violation, later ones answer with plain text.
// The observed stream flag per call distinguishes the layer's non-streaming
// retry from any other recovery path.
class SseScript final : public mira::IModelProvider {
  public:
    explicit SseScript(const mira::ModelProfile &profile) : profile_(profile) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    std::vector<bool> stream_flags;
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &options) override {
        stream_flags.push_back(options.stream);
        if (stream_flags.size() == 1) {
            mira::Error error;
            error.code = mira::ErrorCode::PlatformError;
            error.domain = "mira.model";
            error.safe_message = "provider stream broke: invalid Messages SSE lifecycle";
            return error;
        }
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        mira::MessageOutput message;
        mira::OutputTextPart text;
        text.text = "recovered answer";
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }

  private:
    mira::ModelProfile profile_;
};

// DEC-085 portal-timeout fixture: delegates the four mandatory capture
// entry points to the fake screen but fails native desktop capture with a
// configurable provider error, so the classification of an
// authorization-dialog timeout can be observed through the real
// desktop.screen.capture path (provider failure -> PlatformError with the
// "portal_timeout: ..." safe message).
class CaptureFailingScreen final : public mirage::desktop::ScreenProvider {
  public:
    explicit CaptureFailingScreen(mirage::desktop::ScreenProvider *inner) : inner_(inner) {}
    // Portal-style native observation: only this class of screen routes
    // source=desktop captures through capture_desktop(), where portal
    // authorization timeouts actually surface.
    bool desktop_observation_only() const override { return true; }
    mirage::desktop::DisplayListOutcome
    list_displays(const mirage::desktop::DisplayListLimits &limits,
                  const mirage::desktop::CancelToken &cancel) override {
        return inner_->list_displays(limits, cancel);
    }
    mirage::desktop::CaptureOutcome
    capture_display(const std::string &display_id, const mirage::desktop::CaptureLimits &limits,
                    const mirage::desktop::CancelToken &cancel) override {
        return inner_->capture_display(display_id, limits, cancel);
    }
    mirage::desktop::CaptureOutcome
    capture_window(const std::string &window_id, const mirage::desktop::CaptureLimits &limits,
                   const mirage::desktop::CancelToken &cancel) override {
        return inner_->capture_window(window_id, limits, cancel);
    }
    mirage::desktop::CaptureOutcome
    capture_roi(const mirage::desktop::WindowGeometry &roi,
                const mirage::desktop::CaptureLimits &limits,
                const mirage::desktop::CancelToken &cancel) override {
        return inner_->capture_roi(roi, limits, cancel);
    }
    mirage::desktop::CaptureOutcome capture_desktop(const mirage::desktop::CaptureLimits &,
                                                    const mirage::desktop::CancelToken &) override {
        ++calls;
        mirage::desktop::CaptureOutcome outcome;
        outcome.error = {error_code, error_message};
        return outcome;
    }
    mirage::desktop::ScreenProvider *inner_;
    std::string error_code = "portal_timeout";
    std::string error_message = "Screenshot authorization cancelled or timed out; request closed";
    int calls = 0;
};

// Wraps the fake environment so only the screen provider is replaced and the
// declared capture capability is pinned to PortalOnly: the degraded class
// where the portal-timeout branch is observable (a Full capability already
// classified any PlatformError as conditional before DEC-085).
class CaptureFailingEnvironment final : public mirage::desktop::DesktopEnvironment {
  public:
    explicit CaptureFailingEnvironment(std::shared_ptr<Environment> inner)
        : inner_(std::move(inner)), screen_(inner_->screen()) {}
    mirage::desktop::EnvironmentInfo info() const override { return inner_->info(); }
    mirage::desktop::FilesystemProvider *filesystem() override { return &inner_->filesystem_; }
    mirage::desktop::ProcessProvider *process() override { return &inner_->process_; }
    mirage::desktop::ApplicationProvider *application() override { return &inner_->application_; }
    mirage::desktop::WindowProvider *window() override { return &inner_->window_; }
    mirage::desktop::AccessibilityProvider *accessibility() override {
        return &inner_->accessibility_;
    }
    mirage::desktop::ScreenProvider *screen() override { return &screen_; }
    mirage::desktop::InputProvider *input() override { return &inner_->input_; }
    mirage::desktop::ClipboardProvider *clipboard() override { return &inner_->clipboard_; }
    mirage::desktop::NotificationProvider *notification() override {
        return &inner_->notification_;
    }
    mirage::desktop::ScreenCapability screen_capability() override { return capability_; }
    std::shared_ptr<Environment> inner_;
    CaptureFailingScreen screen_;
    mirage::desktop::ScreenCapability capability_ = mirage::desktop::ScreenCapability::PortalOnly;
};

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
        MIRAGE_CHECK(tool.wire_name != "desktop_process_execute");
        MIRAGE_CHECK(tool.wire_name != "desktop_clipboard_write_text");
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

// DEC-085 session resilience: the repeated-failure guard (block after 3
// deterministic failures, transient never blocks), the failure digest on a
// failed turn, and graceful budget degradation (bounded notices, final-turn
// tool closure) driven through the same harness + scripted-provider pattern.
void resilience_scenario(const std::string &scenario) {
    kairo::Executor owner;
    kairo::ExecutorConfig executor_config;
    executor_config.min_threads = executor_config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
    auto desktop = std::make_shared<SnapshotCountingEnvironment>(environment());
    std::shared_ptr<Script> script;
    const bool budget = scenario == "budget_final_turn" || scenario == "budget_counts";
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile, true);
        // The scripted provider answers with text once its table is
        // exhausted, so each table covers exactly the model turns the
        // scenario needs: blocking runs answer after their 5 proposals, the
        // failure-summary run keeps proposing across the whole max_turns=16
        // budget, and the budget runs cover all 16 declared turns.
        const std::size_t proposals = budget ? 16 : (scenario == "failure_summary" ? 16 : 5);
        if (budget)
            script->actions_override = std::vector<std::pair<std::string, std::string>>(
                proposals,
                scenario == "budget_counts"
                    ? std::pair<std::string, std::string>{"desktop.accessibility.semantic_snapshot",
                                                          R"({"window_id":"w3"})"}
                    : std::pair<std::string, std::string>{"desktop.window.front", "{}"});
        else
            script->actions_override = std::vector<std::pair<std::string, std::string>>(
                proposals, {"desktop.accessibility.semantic_snapshot", R"({"window_id":"w3"})"});
        return script;
    });
    if (scenario == "transient_not_blocked")
        desktop->accessibility_.failure = "permission_denied";
    else if (!budget || scenario == "budget_counts")
        desktop->accessibility_.failure = "unsupported_window";
    integration::DesktopToolContext tools;
    tools.environment = desktop;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    // DEC-085 final budget model: turn/recovery/continuation counts are
    // declared bounds (16/2/2), so the default 512 per-call config spends
    // 512 x 19 inside the profile envelope (21 x the thinking floor) and the
    // budget notices ride turns 15 and 16 of that declared envelope.
    auto config = model_config();
    integration::ModelLayer layer(owner, config, &provider, tools);
    auto context = mira::make_control_context();
    context.session = mira::SessionId::generate();
    context.deadline = std::chrono::steady_clock::now() + 30s;
    mirage::desktop::CancelToken cancel;
    const auto result = owner
                            .submit_auto([&] {
                                return layer.complete_harness_turn({}, "resilience probe", context,
                                                                   "", false, {}, {}, cancel);
                            })
                            .get();
    MIRAGE_CHECK(script && !script->requests.empty());
    if (scenario == "block_permanent") {
        // Three deterministic failures block the tool; calls 4 and 5 are
        // refused by the decorator and never reach the provider again.
        MIRAGE_CHECK(result.ok);
        MIRAGE_CHECK(result.tool_calls == 5);
        MIRAGE_CHECK(desktop->accessibility_.calls == 3);
        MIRAGE_CHECK(script->requests.size() == 6);
        MIRAGE_CHECK(failed_result(*script, "call-1", "unsupported_window"));
        MIRAGE_CHECK(!failed_result(*script, "call-1", "permanent_failure"));
        MIRAGE_CHECK(!failed_result(*script, "call-3", "permanent_failure"));
        MIRAGE_CHECK(failed_result(*script, "call-4", "permanent_failure"));
        MIRAGE_CHECK(failed_result(*script, "call-5", "permanent_failure"));
    } else if (scenario == "transient_not_blocked") {
        // PermissionDenied failures stay recoverable: every call reaches the
        // provider and no blocked receipt ever appears.
        MIRAGE_CHECK(result.ok);
        MIRAGE_CHECK(result.tool_calls == 5);
        MIRAGE_CHECK(desktop->accessibility_.calls == 5);
        MIRAGE_CHECK(script->requests.size() == 6);
        for (int call = 1; call <= 5; ++call)
            MIRAGE_CHECK(
                !failed_result(*script, "call-" + std::to_string(call), "permanent_failure"));
        MIRAGE_CHECK(failed_result(*script, "call-5", "permission_denied"));
    } else if (scenario == "failure_summary") {
        // The model never answers, the turn budget drains on refusals, and
        // the failed completion carries the user-facing failure digest.
        MIRAGE_CHECK(!result.ok && result.failed && !result.cancelled);
        MIRAGE_CHECK(result.model_steps == 16);
        MIRAGE_CHECK(script->requests.size() == 16);
        MIRAGE_CHECK(desktop->accessibility_.calls == 3);
        MIRAGE_CHECK(result.error.find("工具失败摘要") != std::string::npos);
        MIRAGE_CHECK(result.error.find("连续失败 3 次") != std::string::npos);
        MIRAGE_CHECK(result.error.find("已阻止后续调用") != std::string::npos);
        MIRAGE_CHECK(result.error.find("unsupported_window") != std::string::npos);
    } else if (scenario == "budget_counts") {
        // The whole declared turn budget spent on a blocked tool: the failed
        // completion names the spent budget AND the blocked tool.
        std::fprintf(stderr, "budget_counts error: %s\n", result.error.c_str());
        MIRAGE_CHECK(script->requests.size() == 16);
        MIRAGE_CHECK(!result.ok && result.failed && !result.cancelled);
        MIRAGE_CHECK(result.model_steps == 16);
        MIRAGE_CHECK(desktop->accessibility_.calls == 3);
        MIRAGE_CHECK(result.error.find("16 个模型步骤") != std::string::npos);
        MIRAGE_CHECK(result.error.find("16 次工具调用") != std::string::npos);
        MIRAGE_CHECK(result.error.find("工具失败摘要") != std::string::npos);
        MIRAGE_CHECK(result.error.find("已阻止后续调用") != std::string::npos);
        MIRAGE_CHECK(result.error.find("unsupported_window") != std::string::npos);
    } else if (budget) {
        // max_turns=16: no notice on turns 1-14, the "one turn remains"
        // notice on turn 15, the FINAL notice on turn 16 whose tool proposal
        // is refused, then the loop settles without an answer.
        MIRAGE_CHECK(script->requests.size() == 16);
        MIRAGE_CHECK(!result.ok && result.failed);
        if (script->requests.size() == 16) {
            MIRAGE_CHECK(!request_text_contains(script->requests[13], "SYSTEM BUDGET NOTICE"));
            MIRAGE_CHECK(
                request_text_contains(script->requests[14], "only one model turn remains"));
            MIRAGE_CHECK(request_text_contains(
                script->requests[15], "FINAL model turn of this task's budget (16 turns)"));
        }
        bool refusal_seen = false;
        std::size_t front_complete = 0;
        for (const auto &part : result.parts)
            if (part.kind == "tool" && part.name == "desktop.window.front") {
                if (part.status == "complete")
                    ++front_complete;
                if (part.output.find("final_turn_tools_closed") != std::string::npos)
                    refusal_seen = true;
            }
        MIRAGE_CHECK(front_complete == 15);
        MIRAGE_CHECK(refusal_seen);
        // A turn without tool failures must not grow a failure digest.
        MIRAGE_CHECK(result.error.find("工具失败摘要") == std::string::npos);
    }
    layer.shutdown();
    owner.shutdown(true);
}

// DEC-085 epoch semantics for the repeated-failure guard: two failures per
// environment epoch never block, the third failure within one epoch does, and
// an established block survives a further epoch change.
void epoch_reset_scenario() {
    kairo::Executor owner;
    kairo::ExecutorConfig executor_config;
    executor_config.min_threads = executor_config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
    auto desktop = std::make_shared<SnapshotCountingEnvironment>(environment());
    desktop->accessibility_.failure = "unsupported_window";
    auto actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    std::shared_ptr<Script> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile, true);
        // Six snapshot proposals against the treeless window; the fixture
        // advances the coordinator epoch immediately before model turns 3
        // and 6 (0-based 2 and 5), simulating a fresh observation between
        // retries.
        script->actions_override = std::vector<std::pair<std::string, std::string>>(
            6, {"desktop.accessibility.semantic_snapshot", R"({"window_id":"w3"})"});
        script->coordinator = actions.get();
        script->advance_epoch_before_turn = {2, 5};
        return script;
    });
    integration::DesktopToolContext tools;
    tools.environment = desktop;
    tools.actions = actions;
    integration::ModelLayer layer(owner, model_config(), &provider, tools);
    auto context = mira::make_control_context();
    context.session = mira::SessionId::generate();
    context.deadline = std::chrono::steady_clock::now() + 30s;
    mirage::desktop::CancelToken cancel;
    const auto result = owner
                            .submit_auto([&] {
                                return layer.complete_harness_turn({}, "epoch reset probe", context,
                                                                   "", false, {}, {}, cancel);
                            })
                            .get();
    MIRAGE_CHECK(result.ok);
    MIRAGE_CHECK(script && script->requests.size() == 7);
    // The handler stopped after the block: five dispatches only, and the
    // sixth proposal drew the canonical blocked receipt.
    MIRAGE_CHECK(desktop->accessibility_.calls == 5);
    MIRAGE_CHECK(result.tool_calls == 6);
    // Failures 1-2 belong to the first epoch; the fixture advanced the epoch
    // before turn 3, so failures 3-4 started a fresh per-epoch budget and
    // still reached the handler.
    MIRAGE_CHECK(failed_result(*script, "call-1", "unsupported_window"));
    MIRAGE_CHECK(failed_result(*script, "call-2", "unsupported_window"));
    MIRAGE_CHECK(failed_result(*script, "call-4", "unsupported_window"));
    MIRAGE_CHECK(!failed_result(*script, "call-4", "permanent_failure"));
    MIRAGE_CHECK(failed_result(*script, "call-5", "unsupported_window"));
    // The third failure within the second epoch blocked the tool; the
    // call-6 receipt is the canonical blocked form quoting all five
    // failures, proving the block survived the epoch advance before turn 6.
    MIRAGE_CHECK(failed_result(*script, "call-6", "permanent_failure"));
    MIRAGE_CHECK(failed_result(*script, "call-6", "is blocked for this session after 5"));
    // The reported per-epoch count is the current epoch's three failures.
    MIRAGE_CHECK(result.tool_failures.size() == 1);
    if (result.tool_failures.size() == 1)
        MIRAGE_CHECK(result.tool_failures.front().count == 3);
    layer.shutdown();
    owner.shutdown(true);
}

// DEC-085 structured tool failures: with a non-Full declared capture
// capability, an UnsupportedCapability failure classifies as
// platform_limitation and the settled completion carries exactly one
// ToolFailure with the user-facing message and the current-epoch count.
void tool_failure_record_scenario() {
    kairo::Executor owner;
    kairo::ExecutorConfig executor_config;
    executor_config.min_threads = executor_config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
    auto desktop = std::make_shared<SnapshotCountingEnvironment>(environment());
    desktop->screen_capability_ = mirage::desktop::ScreenCapability::PortalOnly;
    desktop->accessibility_.failure = "unsupported_window";
    std::shared_ptr<Script> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile, true);
        script->actions_override = std::vector<std::pair<std::string, std::string>>(
            3, {"desktop.accessibility.semantic_snapshot", R"({"window_id":"w3"})"});
        return script;
    });
    integration::DesktopToolContext tools;
    tools.environment = desktop;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    integration::ModelLayer layer(owner, model_config(), &provider, tools);
    auto context = mira::make_control_context();
    context.session = mira::SessionId::generate();
    context.deadline = std::chrono::steady_clock::now() + 30s;
    mirage::desktop::CancelToken cancel;
    const auto result = owner
                            .submit_auto([&] {
                                return layer.complete_harness_turn(
                                    {}, "failure record probe", context, "", false, {}, {}, cancel);
                            })
                            .get();
    MIRAGE_CHECK(result.ok);
    MIRAGE_CHECK(script && script->requests.size() == 4);
    MIRAGE_CHECK(desktop->accessibility_.calls == 3);
    MIRAGE_CHECK(result.tool_failures.size() == 1);
    if (result.tool_failures.size() == 1) {
        const auto &failure = result.tool_failures.front();
        MIRAGE_CHECK(failure.tool == "desktop.accessibility.semantic_snapshot");
        // UnsupportedCapability under a non-Full capability is a platform
        // limitation, not a tool defect.
        MIRAGE_CHECK(failure.code == "platform_limitation");
        MIRAGE_CHECK(failure.message.starts_with("平台限制："));
        MIRAGE_CHECK(failure.message.find("unsupported_window") != std::string::npos &&
                     failure.message.find("fixture has no accessibility tree") !=
                         std::string::npos);
        MIRAGE_CHECK(failure.count == 3);
    }
    layer.shutdown();
    owner.shutdown(true);
}

// DEC-085 defect probe: the tracker resets per-epoch counters lazily (at the
// next record), so a tool that only failed in an older epoch can be reported
// with count == 0 — a value the session.tool_error wire decoder rejects
// (failure_count must be positive). Expected: either no stale entry or a
// positive count.
void tool_failure_stale_count_probe() {
    kairo::Executor owner;
    kairo::ExecutorConfig executor_config;
    executor_config.min_threads = executor_config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
    auto desktop = std::make_shared<SnapshotCountingEnvironment>(environment());
    desktop->accessibility_.failure = "unsupported_window";
    auto actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    std::shared_ptr<Script> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile, true);
        // Turn 1: the snapshot tool fails once, then a window enumeration
        // failure registers a second tool; turn 2: after the fixture advanced
        // the epoch, only the snapshot tool fails again — its record resets
        // the enumeration tool's per-epoch counter to zero.
        script->actions_override = {
            {"desktop.accessibility.semantic_snapshot", R"({"window_id":"w3"})"},
            {"desktop.window.list", "{}"},
            {"desktop.accessibility.semantic_snapshot", R"({"window_id":"w3"})"}};
        script->coordinator = actions.get();
        script->advance_epoch_before_turn = {2};
        return script;
    });
    desktop->inner_->failures.window_list_error = true;
    integration::DesktopToolContext tools;
    tools.environment = desktop;
    tools.actions = actions;
    integration::ModelLayer layer(owner, model_config(), &provider, tools);
    auto context = mira::make_control_context();
    context.session = mira::SessionId::generate();
    context.deadline = std::chrono::steady_clock::now() + 30s;
    mirage::desktop::CancelToken cancel;
    const auto first = owner
                           .submit_auto([&] {
                               return layer.complete_harness_turn(
                                   {}, "stale probe turn one", context, "", false, {}, {}, cancel);
                           })
                           .get();
    MIRAGE_CHECK(first.ok && script && script->requests.size() == 4);
    MIRAGE_CHECK(first.tool_failures.size() == 2);
    const auto second = owner
                            .submit_auto([&] {
                                return layer.complete_harness_turn(
                                    {}, "stale probe turn two", context, "", false, {}, {}, cancel);
                            })
                            .get();
    MIRAGE_CHECK(second.ok);
    for (const auto &failure : second.tool_failures) {
        std::fprintf(stderr, "stale probe: tool=%s code=%s count=%u\n", failure.tool.c_str(),
                     failure.code.c_str(), failure.count);
        MIRAGE_CHECK(failure.count >= 1);
    }
    layer.shutdown();
    owner.shutdown(true);
}

// DEC-085 portal-timeout classification: a desktop capture failure whose
// safe message starts with "portal_timeout" is a conditional failure — the
// user can still approve a later authorization request — not a platform
// limitation. The distinction is observable under the non-Full declared
// capability (PortalOnly), where any other PlatformError stays
// platform_limitation. Three same-epoch failures still block the tool for
// the session, and the blocked receipt quotes the portal timeout.
void portal_timeout_scenario() {
    for (const auto *error_code : {"portal_timeout", "io_error"}) {
        const bool portal = std::string{error_code} == "portal_timeout";
        kairo::Executor owner;
        kairo::ExecutorConfig executor_config;
        executor_config.min_threads = executor_config.max_threads = 2;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
        auto desktop = std::make_shared<CaptureFailingEnvironment>(environment());
        desktop->screen_.error_code = error_code;
        // The portal variant carries the production timeout sentence; the
        // control is an ordinary provider failure with no special prefix.
        desktop->screen_.error_message =
            portal ? "Screenshot authorization cancelled or timed out; request closed"
                   : "injected capture failure";
        std::shared_ptr<Script> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<Script>(profile, true);
            script->actions_override = {{"desktop.screen.capture", R"({"source":"desktop"})"}};
            return script;
        });
        integration::DesktopToolContext tools;
        tools.environment = desktop;
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        tools.gate = [](const auto &, const auto &, const auto &) { return true; };
        auto config = model_config();
        config.supports_images = true;
        integration::ModelLayer layer(owner, config, &provider, tools);
        auto context = mira::make_control_context();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 30s;
        mirage::desktop::CancelToken cancel;
        // Three turns in one environment epoch each propose one capture; the
        // provider fails identically every time, so each attempt is recorded
        // and the third record blocks the tool.
        integration::DialogCompletion third;
        for (int turn = 1; turn <= 3; ++turn) {
            third = owner
                        .submit_auto([&] {
                            return layer.complete_harness_turn({}, "portal probe", context, "",
                                                               false, {}, {}, cancel);
                        })
                        .get();
            MIRAGE_CHECK(third.ok);
            MIRAGE_CHECK(desktop->screen_.calls == turn);
        }
        MIRAGE_CHECK(script && script->requests.size() == 6u);
        MIRAGE_CHECK(failed_result(*script, "call-1", error_code));
        MIRAGE_CHECK(third.tool_failures.size() == 1u);
        if (third.tool_failures.size() == 1u) {
            const auto &failure = third.tool_failures.front();
            MIRAGE_CHECK(failure.tool == "desktop.screen.capture");
            // The fix under test: a portal authorization timeout is
            // conditional_failure even with a degraded capability.
            MIRAGE_CHECK(failure.code == (portal ? "conditional_failure" : "platform_limitation"));
            MIRAGE_CHECK(failure.count == 3u);
            if (portal) {
                MIRAGE_CHECK(failure.message.starts_with("暂不可用：portal_timeout:"));
                MIRAGE_CHECK(failure.message.find("Screenshot authorization cancelled or timed "
                                                  "out; request closed") != std::string::npos);
            } else {
                MIRAGE_CHECK(failure.message.starts_with("平台限制：io_error:"));
            }
        }
        // The fourth proposal never reaches the handler: the session-scoped
        // block answers with the canonical receipt quoting all three
        // failures, and the classifier stays at the conditional class.
        const auto blocked =
            owner
                .submit_auto([&] {
                    return layer.complete_harness_turn({}, "portal probe blocked", context, "",
                                                       false, {}, {}, cancel);
                })
                .get();
        MIRAGE_CHECK(blocked.ok);
        MIRAGE_CHECK(desktop->screen_.calls == 3);
        MIRAGE_CHECK(script->requests.size() == 8u);
        MIRAGE_CHECK(failed_result(*script, "call-1", "permanent_failure"));
        MIRAGE_CHECK(failed_result(*script, "call-1", "is blocked for this session after 3"));
        MIRAGE_CHECK(blocked.tool_failures.size() == 1u);
        if (blocked.tool_failures.size() == 1u)
            MIRAGE_CHECK(blocked.tool_failures.front().count == 3u);
        layer.shutdown();
        owner.shutdown(true);
    }
}

// DEC-085 capability honesty at registration time: with screen_capability()
// == Unavailable the capture-dependent tools stay unregistered and the goal
// declares the missing screen provider; PortalOnly keeps capture exposed and
// declares the portal authorization facts; Full exposes capture with no
// capability caveat.
void capability_registration_scenario() {
    const auto closed_set_of = [](const mira::ModelRequest &request) {
        for (const auto &item : request.input)
            for (const auto &part : item.content)
                if (const auto *text = std::get_if<mira::TextPart>(&part)) {
                    const auto marker =
                        text->text.find("Available tools for THIS turn (closed set):");
                    if (marker != std::string::npos) {
                        const auto end = text->text.find(". Never call a name absent", marker);
                        return text->text.substr(
                            marker, end == std::string::npos ? std::string::npos : end - marker);
                    }
                }
        return std::string{};
    };
    for (const auto capability :
         {mirage::desktop::ScreenCapability::Unavailable,
          mirage::desktop::ScreenCapability::PortalOnly, mirage::desktop::ScreenCapability::Full}) {
        kairo::Executor owner;
        kairo::ExecutorConfig executor_config;
        executor_config.min_threads = executor_config.max_threads = 2;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
        auto desktop = std::make_shared<SnapshotCountingEnvironment>(environment());
        desktop->screen_capability_ = capability;
        std::shared_ptr<Script> script;
        integration::ModelProviderOverride provider(
            [&](const auto &profile) { return script = std::make_shared<Script>(profile, true); });
        integration::DesktopToolContext tools;
        tools.environment = desktop;
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        tools.gate = [](const auto &, const auto &, const auto &) { return true; };
        auto config = model_config();
        config.supports_images = true; // would build the capture turn when usable
        integration::ModelLayer layer(owner, config, &provider, tools);
        auto context = mira::make_control_context();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 30s;
        mirage::desktop::CancelToken cancel;
        const auto result =
            owner
                .submit_auto([&] {
                    return layer.complete_harness_turn({}, "capability facts probe", context, "",
                                                       false, {}, {}, cancel);
                })
                .get();
        MIRAGE_CHECK(result.ok);
        MIRAGE_CHECK(script && !script->requests.empty());
        if (!script || script->requests.empty()) {
            layer.shutdown();
            owner.shutdown(true);
            return;
        }
        const auto &request = script->requests.front();
        const auto closed_set = closed_set_of(request);
        MIRAGE_CHECK(!closed_set.empty());
        if (capability == mirage::desktop::ScreenCapability::Unavailable) {
            MIRAGE_CHECK(request_text_contains(request, "no screen provider is available"));
            MIRAGE_CHECK(closed_set.find("desktop_screen_capture") == std::string::npos);
            MIRAGE_CHECK(closed_set.find("desktop_screen_list_displays") == std::string::npos);
            MIRAGE_CHECK(closed_set.find("desktop_vision_ocr") == std::string::npos);
            MIRAGE_CHECK(closed_set.find("desktop_window_list") != std::string::npos);
        } else if (capability == mirage::desktop::ScreenCapability::PortalOnly) {
            MIRAGE_CHECK(request_text_contains(request, "portal_only"));
            MIRAGE_CHECK(request_text_contains(
                request, "denial is recoverable once the user grants access"));
            MIRAGE_CHECK(request_text_contains(request, "Do not retry a denied capture"));
            MIRAGE_CHECK(closed_set.find("desktop_screen_capture") != std::string::npos);
        } else {
            MIRAGE_CHECK(closed_set.find("desktop_screen_capture") != std::string::npos);
            MIRAGE_CHECK(!request_text_contains(request, "no screen provider is available"));
            MIRAGE_CHECK(!request_text_contains(request, "portal_only"));
        }
        layer.shutdown();
        owner.shutdown(true);
    }
}

// DEC-085: a stream-lifecycle protocol violation on pure dialog retries once
// without streaming (bounded, side-effect free); without a preview sink the
// same violation surfaces after the single non-streaming attempt.
void dialog_sse_scenario(bool preview) {
    kairo::Executor owner;
    kairo::ExecutorConfig executor_config;
    executor_config.min_threads = executor_config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
    std::shared_ptr<SseScript> script;
    integration::ModelProviderOverride provider(
        [&](const auto &profile) { return script = std::make_shared<SseScript>(profile); });
    integration::ModelLayer layer(owner, model_config(), &provider, {});
    auto context = mira::make_control_context();
    context.deadline = std::chrono::steady_clock::now() + 10s;
    std::string preview_text;
    const auto result =
        owner
            .submit_auto([&] {
                return layer.complete_dialog_turn(
                    {}, "stream probe", context, "", true,
                    preview ? integration::DialogPreviewSink(
                                  [&preview_text](const std::string &, const std::string &text,
                                                  bool) { preview_text += text; })
                            : integration::DialogPreviewSink{});
            })
            .get();
    MIRAGE_CHECK(script && !script->stream_flags.empty());
    if (preview) {
        MIRAGE_CHECK(result.ok && result.reply_text == "recovered answer");
        MIRAGE_CHECK(script->stream_flags.size() == 2);
        MIRAGE_CHECK(script->stream_flags[0] && !script->stream_flags[1]);
    } else {
        MIRAGE_CHECK(!result.ok && result.failed);
        MIRAGE_CHECK(result.error.find("invalid Messages SSE lifecycle") != std::string::npos);
        MIRAGE_CHECK(script->stream_flags.size() == 1);
        MIRAGE_CHECK(!script->stream_flags[0]);
    }
    layer.shutdown();
    owner.shutdown(true);
}

// DEC-085: the same violation on a harness turn retries exactly once with
// streaming disabled, before any tool executed (no dispatched side effects).
void harness_sse_scenario() {
    kairo::Executor owner;
    kairo::ExecutorConfig executor_config;
    executor_config.min_threads = executor_config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(owner.initialize(executor_config)));
    auto desktop = environment();
    std::shared_ptr<SseScript> script;
    integration::ModelProviderOverride provider(
        [&](const auto &profile) { return script = std::make_shared<SseScript>(profile); });
    integration::DesktopToolContext tools;
    tools.environment = desktop;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    integration::ModelLayer layer(owner, model_config(), &provider, tools);
    auto context = mira::make_control_context();
    context.session = mira::SessionId::generate();
    context.deadline = std::chrono::steady_clock::now() + 10s;
    mirage::desktop::CancelToken cancel;
    const auto result = owner
                            .submit_auto([&] {
                                return layer.complete_harness_turn(
                                    {}, "stream probe", context, "", true,
                                    integration::DialogPreviewSink(
                                        [](const std::string &, const std::string &, bool) {}),
                                    {}, cancel);
                            })
                            .get();
    MIRAGE_CHECK(script != nullptr);
    // Exactly one streaming attempt, then one non-streaming retry that
    // settles the turn.
    MIRAGE_CHECK(script->stream_flags.size() == 2);
    std::fprintf(stderr,
                 "sse harness: stream attempts=%zu ok=%d failed=%d cancelled=%d "
                 "error=%s\n",
                 script->stream_flags.size(), result.ok, result.failed, result.cancelled,
                 result.error.c_str());
    if (script->stream_flags.size() == 2) {
        MIRAGE_CHECK(script->stream_flags[0]);
        MIRAGE_CHECK(!script->stream_flags[1]);
    }
    MIRAGE_CHECK(result.ok && result.reply_text.find("recovered answer") != std::string::npos);
    layer.shutdown();
    owner.shutdown(true);
}

void reasoning_scenario() {
    for (const auto *reasoning : {"", "none", "adaptive", "low"}) {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<Script> script;
        std::uint64_t profile_limit = 0;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            // DEC-085 final budget model: the Messages profile limit scales
            // with the 32768 thinking floor, independent of the reasoning
            // choice, and funds the declared whole-run envelope.
            profile_limit = profile.capabilities.limits.max_output_tokens;
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
        // 22 * 32768 = 720896: the profile limit is the router's per-call
        // admission bound and funds the declared whole-run envelope (16 turns
        // + 2 recoveries + 3 continuations + 1) at the unchanged 32768
        // per-call wire budget — the budget is no longer halved.
        MIRAGE_CHECK(profile_limit == 720896u);
        for (const auto &request : script->requests) {
            MIRAGE_CHECK(request.generation.thinking == (std::string{reasoning} == "none"
                                                             ? mira::ThinkingMode::Disabled
                                                             : mira::ThinkingMode::Adaptive));
            MIRAGE_CHECK(!request.generation.reasoning_effort);
            MIRAGE_CHECK(request.generation.max_output_tokens ==
                         (std::string{reasoning} == "none" ? 512u : 32768u));
        }
        layer.shutdown();
        owner.shutdown(true);
    }
}

// DEC-085 final output-budget model (DEC-087 alignment): an adaptive-thinking
// call floors its per-request output budget at 32768 even when the configured
// value is a larger-but-under-floor 8192 (the old floor), the wire budget is
// the full floor (no profile/2 halving; the fixture declares no context
// window, so the per-request preflight does not shrink), and the 720896
// profile envelope funds the declared whole-run spend: 32768 x (16 turns +
// 2 recoveries + 3 continuations + 1).
void thinking_budget_scenario() {
    // (a) floor domination: config 8192 + adaptive -> 32768 on the wire
    // (the old floor returned 8192 here) and the 720896 profile envelope.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<Script> script;
        std::uint64_t profile_limit = 0;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            profile_limit = profile.capabilities.limits.max_output_tokens;
            script = std::make_shared<Script>(profile, true);
            return script;
        });
        auto config = model_config();
        config.max_output_tokens = 8192;
        config.dialect = "anthropic.messages.v1";
        config.model_selector = "MiniMax-M3";
        integration::DesktopToolContext tools;
        tools.environment = environment();
        integration::ModelLayer layer(owner, config, &provider, tools);
        auto context = mira::make_control_context();
        context.task = mira::TaskId::generate();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 10s;
        const auto result =
            owner
                .submit_auto([&] {
                    return layer.complete_harness_turn({}, "floor probe", context, "adaptive");
                })
                .get();
        MIRAGE_CHECK(result.ok);
        MIRAGE_CHECK(profile_limit == 720896u);
        MIRAGE_CHECK(script && script->requests.size() == 2u);
        for (const auto &request : script->requests) {
            MIRAGE_CHECK(request.generation.thinking == mira::ThinkingMode::Adaptive);
            MIRAGE_CHECK(request.generation.max_output_tokens == 32768u);
        }
        layer.shutdown();
        owner.shutdown(true);
    }
    // (b) envelope turn count: with the default 512 config and adaptive
    // thinking the loop offers 16 model turns — 15 accepted read-only
    // proposals, the penultimate request carries the one-turn warning, and
    // the 16th carries the FINAL notice naming 16 turns; the script then
    // answers with text, so the run settles Answered inside the budget.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<Script> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<Script>(profile, true);
            script->actions_override = std::vector<std::pair<std::string, std::string>>(
                15, {"desktop.window.front", "{}"});
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
        context.deadline = std::chrono::steady_clock::now() + 30s;
        const auto result = owner
                                .submit_auto([&] {
                                    return layer.complete_harness_turn({}, "turn budget probe",
                                                                       context, "adaptive", false);
                                })
                                .get();
        MIRAGE_CHECK(result.ok);
        MIRAGE_CHECK(script && script->requests.size() == 16u);
        MIRAGE_CHECK(result.tool_calls == 15u);
        if (script->requests.size() == 16u) {
            // Assembling request N marks turns_used = N: request 15 (index
            // 14) warns one turn remains, request 16 (index 15) is final.
            MIRAGE_CHECK(request_text_contains(script->requests[14], "only one model turn "
                                                                     "remains"));
            MIRAGE_CHECK(request_text_contains(script->requests[15],
                                               "FINAL model turn of this task's budget (16 "
                                               "turns)"));
            MIRAGE_CHECK(!request_text_contains(script->requests[14],
                                                "FINAL model turn of this task's budget"));
            MIRAGE_CHECK(!request_text_contains(script->requests[13], "SYSTEM BUDGET NOTICE"));
        }
        layer.shutdown();
        owner.shutdown(true);
    }
}

// DEC-085 upstream output continuation (mira PR#93, pin 1befa45): a tolerated
// max_tokens cutoff (status Incomplete, incomplete_reason MaxOutputTokens)
// settles by bounded continuation instead of recovery. The harness replays
// the partial protocol-allowed text as an assistant turn and asks the model
// to continue from the cut; a reply cut off mid tool call is discarded
// unexecuted; the declared continuation budget (3, the ZCode-aligned depth)
// exhausts into an explicit output-limit failure. None of it may spend the
// recovery budget.
class ContinuationScript final : public mira::IModelProvider {
  public:
    explicit ContinuationScript(const mira::ModelProfile &profile) : profile_(profile) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    std::vector<mira::ModelRequest> requests;
    // Per request: a truncation carrying this partial text, a truncation
    // carrying a cut-off tool call instead, or a terminal answer.
    std::vector<std::string> partials;
    std::vector<bool> cut_tool_calls;
    std::string final_answer;
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        requests.push_back(request);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.requested_model = profile_.model_selector;
        const auto step = requests.size() - 1;
        if (step < partials.size() || step < cut_tool_calls.size()) {
            response.status = mira::ModelCompletionStatus::Incomplete;
            response.incomplete_reason = mira::IncompleteReason::MaxOutputTokens;
            if (step < cut_tool_calls.size() && cut_tool_calls[step]) {
                // The cut call must reference a declared tool identity: the
                // gateway validates tool names/IDs before the loop ever sees
                // the Incomplete response (the upstream fixture binds the
                // same call to its registered echo tool).
                const auto wire = mirage::testing::fixture_tool_name("desktop.window.front");
                const auto tool =
                    std::find_if(request.tools.begin(), request.tools.end(),
                                 [&](const auto &entry) { return entry.wire_name == wire; });
                MIRAGE_CHECK(tool != request.tools.end());
                mira::ToolCallOutput cut;
                cut.provider_call_id.value = "cut-call-" + std::to_string(step + 1);
                cut.tool_id = tool != request.tools.end() ? tool->tool_id : mira::ToolId{};
                cut.provider_name = tool != request.tools.end() ? tool->wire_name : wire;
                // The wire-level truncation is expressed by the Incomplete
                // status; the arguments value itself stays valid JSON (the
                // pinned parser rejects malformed argument bytes outright).
                cut.arguments = mira::parse_json(R"({"window_id":"w1"})").value();
                cut.arguments_digest = mira::digest_string(mira::to_json_string(cut.arguments));
                response.output.emplace_back(std::move(cut));
            } else {
                mira::MessageOutput message;
                message.role = mira::ModelRole::Assistant;
                mira::OutputTextPart text;
                text.text = step < partials.size() ? partials[step] : "";
                message.content.emplace_back(std::move(text));
                response.output.emplace_back(std::move(message));
            }
            return response;
        }
        response.status = mira::ModelCompletionStatus::Completed;
        mira::MessageOutput message;
        mira::OutputTextPart text;
        text.text = final_answer;
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }

  private:
    mira::ModelProfile profile_;
};

std::string assistant_partial(const mira::ModelRequest &request, std::size_t index) {
    std::size_t seen = 0;
    for (const auto &item : request.input)
        if (item.role == mira::ModelRole::Assistant)
            for (const auto &part : item.content) {
                const auto *text = std::get_if<mira::TextPart>(&part);
                if (text && seen++ == index)
                    return text->text;
            }
    return "";
}

void output_continuation_scenario() {
    // (a) partial text is kept as the assistant turn and continued: two
    // tolerated cutoffs, the third response answers. Requests 2 and 3 carry
    // the exact continuation feedback plus every earlier partial, and no
    // recovery is spent (the request count stays at 3 and no recovery
    // feedback text appears).
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<ContinuationScript> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<ContinuationScript>(profile);
            script->partials = {"The window list shows", " two windows open"};
            script->final_answer = "Two windows are open.";
            return script;
        });
        integration::DesktopToolContext tools;
        tools.environment = environment();
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        integration::ModelLayer layer(owner, model_config(), &provider, tools);
        auto context = mira::make_control_context();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 30s;
        mirage::desktop::CancelToken cancel;
        const auto result =
            owner
                .submit_auto([&] {
                    return layer.complete_harness_turn({}, "continuation probe", context, "", false,
                                                       {}, {}, cancel);
                })
                .get();
        MIRAGE_CHECK(result.ok && !result.failed && !result.cancelled);
        MIRAGE_CHECK(result.reply_text == "Two windows are open.");
        MIRAGE_CHECK(script && script->requests.size() == 3);
        MIRAGE_CHECK(result.model_steps == 3);
        MIRAGE_CHECK(result.tool_calls == 0);
        if (script->requests.size() == 3) {
            // The second request replays the first partial as an assistant
            // TextPart and carries the anchored continuation feedback (the
            // pin 9e7b729 anchor: continue from the last 50 chars).
            MIRAGE_CHECK(assistant_partial(script->requests[1], 0) == "The window list shows");
            MIRAGE_CHECK(request_text_contains(script->requests[1],
                                               "Continue from: \"The window list shows\""));
            // The third request replays both partials in order plus the
            // feedback anchored at the second cut; no truncation-recovery
            // wording ever appears.
            MIRAGE_CHECK(assistant_partial(script->requests[2], 0) == "The window list shows");
            MIRAGE_CHECK(assistant_partial(script->requests[2], 1) == " two windows open");
            MIRAGE_CHECK(
                request_text_contains(script->requests[2], "Continue from: \" two windows open\""));
            for (const auto &request : script->requests)
                MIRAGE_CHECK(!request_text_contains(request, "concise plain text"));
        }
        layer.shutdown();
        owner.shutdown(true);
    }
    // (b) a reply cut off mid tool call is discarded unexecuted: the tool
    // never runs, the discard is stated on the next request, and the final
    // answer settles the turn.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<ContinuationScript> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<ContinuationScript>(profile);
            script->cut_tool_calls = {true};
            script->final_answer = "Cut call discarded.";
            return script;
        });
        integration::DesktopToolContext tools;
        tools.environment = environment();
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        integration::ModelLayer layer(owner, model_config(), &provider, tools);
        auto context = mira::make_control_context();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 30s;
        mirage::desktop::CancelToken cancel;
        const auto result = owner
                                .submit_auto([&] {
                                    return layer.complete_harness_turn(
                                        {}, "cut call probe", context, "", false, {}, {}, cancel);
                                })
                                .get();
        MIRAGE_CHECK(result.ok);
        MIRAGE_CHECK(result.tool_calls == 0);
        MIRAGE_CHECK(script && script->requests.size() == 2);
        MIRAGE_CHECK(result.model_steps == 2);
        if (script->requests.size() == 2) {
            MIRAGE_CHECK(request_text_contains(script->requests[1], "discarded and NOT executed"));
            MIRAGE_CHECK(request_text_contains(script->requests[1], "output-token limit"));
        }
        layer.shutdown();
        owner.shutdown(true);
    }
    // (c) the declared continuation budget is 3 (the ZCode-aligned depth):
    // three tolerated cutoffs are each continued, and the fourth response
    // settles the run with every partial replayed.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<ContinuationScript> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<ContinuationScript>(profile);
            script->partials = {"first cut", "second cut", "third cut"};
            script->final_answer = "Continued after three cutoffs.";
            return script;
        });
        integration::DesktopToolContext tools;
        tools.environment = environment();
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        integration::ModelLayer layer(owner, model_config(), &provider, tools);
        auto context = mira::make_control_context();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 30s;
        mirage::desktop::CancelToken cancel;
        const auto result =
            owner
                .submit_auto([&] {
                    return layer.complete_harness_turn({}, "continuation budget probe", context, "",
                                                       false, {}, {}, cancel);
                })
                .get();
        MIRAGE_CHECK(result.ok && !result.failed && !result.cancelled);
        MIRAGE_CHECK(result.reply_text == "Continued after three cutoffs.");
        // Four requests: three continuations, then the terminal answer.
        MIRAGE_CHECK(script && script->requests.size() == 4);
        MIRAGE_CHECK(result.model_steps == 4);
        MIRAGE_CHECK(result.tool_calls == 0);
        if (script->requests.size() == 4) {
            MIRAGE_CHECK(assistant_partial(script->requests[3], 0) == "first cut");
            MIRAGE_CHECK(assistant_partial(script->requests[3], 1) == "second cut");
            MIRAGE_CHECK(assistant_partial(script->requests[3], 2) == "third cut");
        }
        layer.shutdown();
        owner.shutdown(true);
    }
    // (d) a fourth cutoff exhausts the budget: the run settles with the
    // explicit output-limit summary instead of looping.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<ContinuationScript> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            script = std::make_shared<ContinuationScript>(profile);
            script->partials = {"first cut", "second cut", "third cut", "fourth cut"};
            return script;
        });
        integration::DesktopToolContext tools;
        tools.environment = environment();
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        integration::ModelLayer layer(owner, model_config(), &provider, tools);
        auto context = mira::make_control_context();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 30s;
        mirage::desktop::CancelToken cancel;
        const auto result =
            owner
                .submit_auto([&] {
                    return layer.complete_harness_turn({}, "continuation exhaustion probe", context,
                                                       "", false, {}, {}, cancel);
                })
                .get();
        MIRAGE_CHECK(!result.ok && result.failed && !result.cancelled);
        MIRAGE_CHECK(result.error.find("output-limit continuation budget exhausted") !=
                     std::string::npos);
        // Four requests: the initial call plus three continuations; the
        // fourth cutoff arrives on the last continuation's response and the
        // exhausted settle makes no further model call (loop semantics:
        // a continuation is only issued while budget remains).
        MIRAGE_CHECK(script && script->requests.size() == 4);
        MIRAGE_CHECK(result.tool_calls == 0);
        layer.shutdown();
        owner.shutdown(true);
    }
}

// DEC-087 preflight fixture: a minimal dialog provider that records every
// wire request and can attach a provider-reported usage to every response —
// the anchor the decorator's usage estimate builds on.
class PreflightScript final : public mira::IModelProvider {
  public:
    explicit PreflightScript(const mira::ModelProfile &profile) : profile_(profile) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    std::vector<mira::ModelRequest> requests;
    // When set, every response carries this provider-reported input size.
    std::optional<std::uint64_t> reported_input;
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        requests.push_back(request);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (reported_input) {
            response.usage.quality = mira::UsageQuality::ProviderReported;
            response.usage.input_tokens = *reported_input;
            response.usage.output_tokens = 16;
        }
        mira::MessageOutput message;
        mira::OutputTextPart text;
        text.text = "preflight probe answer";
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }

  private:
    mira::ModelProfile profile_;
};

// DEC-087 per-request output-budget preflight (ZCode model-token-limits
// semantics) on the pure dialog path: one request per turn, a fixed system
// persona plus the user text, no tools — the pinned estimator's result is
// exactly computable from the recorded wire request, whose bytes the
// decorator never touches. The declared window shrinks the wire budget to
// window − estimate − 1000, the provider-reported usage anchors the next
// request's usage estimate, and a non-positive margin keeps the baseline.
// model_config() declares max_output_tokens = 512; these probes re-declare
// 8192 so the shrink is observable against the small fixture input.
void preflight_budget_scenario() {
    // (a) a declared window shrinks the first wire call; no window leaves
    // the baseline verbatim.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        const std::string probe_text = "preflight shrink probe";
        std::shared_ptr<PreflightScript> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            return script = std::make_shared<PreflightScript>(profile);
        });
        auto shrunk_config = model_config();
        shrunk_config.max_output_tokens = 8192;
        shrunk_config.context_window_tokens = 6000;
        integration::ModelLayer layer(owner, shrunk_config, &provider, {});
        auto context = mira::make_control_context();
        context.deadline = std::chrono::steady_clock::now() + 10s;
        const auto result =
            owner.submit_auto([&] { return layer.complete_dialog_turn({}, probe_text, context); })
                .get();
        MIRAGE_CHECK(result.ok && result.reply_text == "preflight probe answer");
        MIRAGE_CHECK(script && script->requests.size() == 1);
        if (script && script->requests.size() == 1) {
            const auto estimate = mira::estimate_input_tokens(script->requests.front());
            // Premise: the fixture input leaves a positive margin below the
            // declared window and that margin sits under the baseline, so
            // the shrink is observable at all.
            MIRAGE_CHECK(6000u > estimate + 1000u && 6000u - estimate - 1000u < 8192u);
            MIRAGE_CHECK(script->requests.front().generation.max_output_tokens ==
                         6000u - estimate - 1000u);
        }
        layer.shutdown();
        // Control: the same fixture without a declared window keeps the
        // baseline untouched (no shrink without a window).
        std::shared_ptr<PreflightScript> control_script;
        integration::ModelProviderOverride control_provider([&](const auto &profile) {
            return control_script = std::make_shared<PreflightScript>(profile);
        });
        auto plain_config = model_config();
        plain_config.max_output_tokens = 8192;
        integration::ModelLayer plain_layer(owner, plain_config, &control_provider, {});
        const auto plain_result =
            owner
                .submit_auto(
                    [&] { return plain_layer.complete_dialog_turn({}, probe_text, context); })
                .get();
        MIRAGE_CHECK(plain_result.ok);
        MIRAGE_CHECK(control_script && control_script->requests.size() == 1 &&
                     control_script->requests.front().generation.max_output_tokens == 8192u);
        plain_layer.shutdown();
        owner.shutdown(true);
    }
    // (b) the provider-reported usage anchors the next request: the second
    // byte-identical turn is charged the reported provider input (7000)
    // plus zero growth, so its wire budget lands on window − 7000 − 1000 =
    // 4000, while the first turn (no anchor yet) keeps the baseline.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        std::shared_ptr<PreflightScript> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            return script = std::make_shared<PreflightScript>(profile);
        });
        auto config = model_config();
        config.max_output_tokens = 8192;
        config.context_window_tokens = 12000;
        integration::ModelLayer layer(owner, config, &provider, {});
        auto context = mira::make_control_context();
        context.session = mira::SessionId::generate();
        context.deadline = std::chrono::steady_clock::now() + 10s;
        script->reported_input = 7000;
        const auto first = owner
                               .submit_auto([&] {
                                   return layer.complete_dialog_turn({}, "anchor probe", context);
                               })
                               .get();
        MIRAGE_CHECK(first.ok);
        script->reported_input.reset(); // the second response reports no usage
        const auto second = owner
                                .submit_auto([&] {
                                    return layer.complete_dialog_turn({}, "anchor probe", context);
                                })
                                .get();
        MIRAGE_CHECK(second.ok);
        MIRAGE_CHECK(script && script->requests.size() == 2);
        if (script && script->requests.size() == 2) {
            const auto first_estimate = mira::estimate_input_tokens(script->requests[0]);
            // Premise: without an anchor the first call keeps the baseline.
            MIRAGE_CHECK(12000u - first_estimate - 1000u >= 8192u);
            MIRAGE_CHECK(script->requests[0].generation.max_output_tokens == 8192u);
            MIRAGE_CHECK(script->requests[1].generation.max_output_tokens == 4000u);
        }
        layer.shutdown();
        owner.shutdown(true);
    }
    // (c) a non-positive margin keeps the baseline: with the window exactly
    // at estimate + 1000 the preflight has nothing to shave and must not
    // drive the wire budget to zero; one spare token above that shrinks to
    // exactly 1.
    {
        kairo::Executor owner;
        MIRAGE_CHECK(static_cast<bool>(owner.initialize({})));
        auto context = mira::make_control_context();
        context.deadline = std::chrono::steady_clock::now() + 10s;
        // Measure the fixture input size on an anchor-free layer first.
        std::shared_ptr<PreflightScript> script;
        integration::ModelProviderOverride provider([&](const auto &profile) {
            return script = std::make_shared<PreflightScript>(profile);
        });
        integration::ModelLayer measure_layer(owner, model_config(), &provider, {});
        const auto probe =
            owner
                .submit_auto(
                    [&] { return measure_layer.complete_dialog_turn({}, "margin probe", context); })
                .get();
        MIRAGE_CHECK(probe.ok && script && script->requests.size() == 1);
        MIRAGE_CHECK(script->requests.front().generation.max_output_tokens == 512u);
        const auto estimate = mira::estimate_input_tokens(script->requests.front());
        MIRAGE_CHECK(estimate > 0);
        measure_layer.shutdown();
        // usable == 0: the baseline is kept verbatim.
        auto zero_config = model_config();
        zero_config.max_output_tokens = 8192;
        zero_config.context_window_tokens = estimate + 1000u;
        std::shared_ptr<PreflightScript> zero_script;
        integration::ModelProviderOverride zero_provider([&](const auto &profile) {
            return zero_script = std::make_shared<PreflightScript>(profile);
        });
        integration::ModelLayer zero_layer(owner, zero_config, &zero_provider, {});
        const auto zero =
            owner
                .submit_auto(
                    [&] { return zero_layer.complete_dialog_turn({}, "margin probe", context); })
                .get();
        MIRAGE_CHECK(zero.ok);
        MIRAGE_CHECK(zero_script && zero_script->requests.size() == 1 &&
                     zero_script->requests.front().generation.max_output_tokens == 8192u);
        zero_layer.shutdown();
        // usable == 1: the shrink bottoms out at the single remaining token.
        auto one_config = model_config();
        one_config.max_output_tokens = 8192;
        one_config.context_window_tokens = estimate + 1001u;
        std::shared_ptr<PreflightScript> one_script;
        integration::ModelProviderOverride one_provider([&](const auto &profile) {
            return one_script = std::make_shared<PreflightScript>(profile);
        });
        integration::ModelLayer one_layer(owner, one_config, &one_provider, {});
        const auto one = owner
                             .submit_auto([&] {
                                 return one_layer.complete_dialog_turn({}, "margin probe", context);
                             })
                             .get();
        MIRAGE_CHECK(one.ok);
        MIRAGE_CHECK(one_script && one_script->requests.size() == 1 &&
                     one_script->requests.front().generation.max_output_tokens == 1u);
        one_layer.shutdown();
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
// DEC-085: the service projects a settled harness turn's classified tool
// failures onto `session.tool_error` events, delivered after the
// authoritative `session.chat_updated` settlement on a subscribed
// connection.
void service_tool_error_scenario() {
    mirage::testing::TempDir temp;
    auto desktop = std::make_shared<SnapshotCountingEnvironment>(environment());
    desktop->accessibility_.failure = "unsupported_window";
    mirage::runtime::ServiceConfig config;
    config.socket_path = temp.root() / "tool-error.sock";
    config.persist_recovery_state = config.persist_session_state = config.persist_settings = false;
    config.model = model_config();
    config.model_provider_override =
        std::make_shared<integration::ModelProviderOverride>([](const auto &profile) {
            auto script = std::make_shared<Script>(profile, true);
            // Three deterministic failures block the tool; the scripted
            // provider then answers, so the turn settles ok with one
            // classified failure entry.
            script->actions_override = std::vector<std::pair<std::string, std::string>>(
                3, {"desktop.accessibility.semantic_snapshot", R"({"window_id":"w3"})"});
            return script;
        });
    mirage::runtime::RuntimeService service(config);
    MIRAGE_CHECK(service.start(std::make_shared<integration::MiraEnvironmentBinding>(desktop)).ok);

    // Raw event connection: ipc::IpcClient decodes every inbound frame as a
    // response and would misread event frames, so the subscriber uses the
    // poll-driven stream helpers (same discipline as event_subscription_test).
    ipc::IpcStream stream;
    {
        std::string diagnostic;
        stream = ipc::connect_stream(config.socket_path, 5s, diagnostic);
    }
    MIRAGE_CHECK(stream.valid());
    std::string buffer;
    std::vector<ipc::Event> events;
    const auto next_event = [&](std::chrono::milliseconds budget) -> std::optional<ipc::Event> {
        const auto read = mirage::testing::read_frame(stream, buffer, budget);
        if (!frame_message(read))
            return std::nullopt;
        const auto decoded = ipc::decode_event(read.message);
        if (!decoded.ok)
            return std::nullopt;
        events.push_back(decoded.event);
        return decoded.event;
    };
    {
        const auto frame = ipc::make_frame(ipc::encode_request(1, ipc::SubscribeEventsRequest{}));
        MIRAGE_CHECK(mirage::testing::write_all(stream, frame, 5s));
        // Consume the subscribe acknowledgement (a response frame) and the
        // seed host.status event before driving the chat turn.
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        bool seeded = false;
        while (!seeded && std::chrono::steady_clock::now() < deadline) {
            const auto event = next_event(std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()));
            seeded = event.has_value();
        }
        MIRAGE_CHECK(seeded);
    }

    ipc::IpcClient client(config.socket_path);
    const auto sessions = client.call(ipc::ListSessionsRequest{}, 5s);
    MIRAGE_CHECK(sessions.ok);
    const auto session = std::get<ipc::SessionList>(sessions.payload).sessions.front().id;
    MIRAGE_CHECK(client.call(ipc::SessionChatRequest{session, "tool error probe", true}, 5s).ok);
    std::optional<ipc::Event> settlement;
    std::vector<ipc::Event> tool_error_events;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (std::chrono::steady_clock::now() < deadline &&
           (!settlement || tool_error_events.empty())) {
        const auto event = next_event(std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()));
        if (!event)
            break;
        if (const auto *turn = std::get_if<ipc::ChatTurnUpdatedEvent>(&event->payload);
            turn && turn->session_id == session)
            settlement = event;
        if (const auto *error = std::get_if<ipc::ToolErrorEvent>(&event->payload);
            error && error->session_id == session)
            tool_error_events.push_back(*event);
    }
    MIRAGE_CHECK(settlement.has_value());
    MIRAGE_CHECK(tool_error_events.size() == 1);
    if (settlement) {
        const auto &turn = std::get<ipc::ChatTurnUpdatedEvent>(settlement->payload);
        MIRAGE_CHECK(turn.status == "ok");
        // The classified event follows the authoritative turn settlement.
        MIRAGE_CHECK(settlement->seq < (tool_error_events.empty() ? settlement->seq + 1
                                                                  : tool_error_events.front().seq));
    }
    if (tool_error_events.size() == 1) {
        const auto &error = std::get<ipc::ToolErrorEvent>(tool_error_events.front().payload);
        std::fprintf(stderr, "tool_error event: session=%s tool=%s code=%s count=%u message=%s\n",
                     error.session_id.c_str(), error.tool_name.c_str(), error.error_code.c_str(),
                     error.failure_count, error.message.c_str());
        MIRAGE_CHECK(error.tool_name == "desktop.accessibility.semantic_snapshot");
        MIRAGE_CHECK(error.error_code == "permanent_failure");
        MIRAGE_CHECK(error.message.starts_with("工具不可用："));
        MIRAGE_CHECK(error.message.find("unsupported_window") != std::string::npos);
        MIRAGE_CHECK(error.failure_count == 3);
    }
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

} // namespace

int main() {
#ifdef MIRAGE_HARNESS_X11
    MIRAGE_CHECK(XInitThreads() != 0);
#endif
    registration_scenario();
    reasoning_scenario();
    thinking_budget_scenario();
    output_continuation_scenario();
    preflight_budget_scenario();
    for (const auto *scenario : {"block_permanent", "transient_not_blocked", "failure_summary",
                                 "budget_final_turn", "budget_counts"})
        resilience_scenario(scenario);
    epoch_reset_scenario();
    tool_failure_record_scenario();
    tool_failure_stale_count_probe();
    portal_timeout_scenario();
    capability_registration_scenario();
    dialog_sse_scenario(true);
    dialog_sse_scenario(false);
    harness_sse_scenario();
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
    service_tool_error_scenario();
#ifdef MIRAGE_HARNESS_X11
    x11_scenario();
#endif
    return mirage::testing::finish("harness_desktop_tools_test");
}
