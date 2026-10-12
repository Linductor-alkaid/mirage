#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../support/fake_desktop_environment.hpp"
#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <kairo/comm/mailbox.hpp>
#include <kairo/executor.hpp>
#include <mira/model_digest.hpp>
#include <mirage/integration/model_layer.hpp>
#include <source_location>
#ifdef MIRAGE_POST_ACTION_X11
#include "../support/xvfb_display.hpp"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <poll.h>
#endif

namespace i = mirage::integration;
using Json = mira::JsonValue;
namespace {
std::atomic_int checks{0};
void check(bool ok, const std::source_location where = std::source_location::current()) {
    ++checks;
    if (!ok)
        throw std::runtime_error("post-action check at " + std::to_string(where.line()));
}
std::string field(const Json &json, const char *key) {
    const auto *v = json.find(key);
    return v && v->as_string() ? *v->as_string() : "";
}
struct Script : mira::IModelProvider {
    mira::ModelProfile profile_;
    std::vector<std::pair<std::string, Json>> steps;
    std::function<void(std::size_t)> on_step;
    std::size_t step = 0;
    explicit Script(const mira::ModelProfile &profile) : profile_(profile) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        if (on_step)
            on_step(step);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (step < steps.size()) {
            auto [name, args] = steps[step];
            const auto tool = std::find_if(request.tools.begin(), request.tools.end(),
                                           [&](const auto &t) { return t.wire_name == name; });
            check(tool != request.tools.end());
            Json latest, action;
            for (const auto &item : request.input)
                for (const auto &part : item.content)
                    if (const auto *result = std::get_if<mira::ToolResultPart>(&part); result) {
                        if (!result->failed && result->result.find("observation_scope") &&
                            (!name.starts_with("desktop.application.") ||
                             result->result.find("applications")))
                            latest = result->result;
                        const auto rejected = mira::parse_json(result->safe_error_summary);
                        const auto &receipt =
                            result->failed && rejected ? rejected.value() : result->result;
                        if (receipt.find("action_id"))
                            action = receipt;
                    }
            auto fields = *args.as_object();
            if (i::detail::DesktopActionVerification::mutation(name)) {
                fields.emplace_back("observation_scope", *latest.find("observation_scope"));
                fields.emplace_back("environment_epoch", *latest.find("environment_epoch"));
            }
            if (name == "desktop.action.verify" && !args.find("action_id")) {
                check(action.find("action_id") != nullptr);
                fields.emplace_back("action_id", *action.find("action_id"));
            }
            mira::ToolCallOutput call;
            call.tool_id = tool->tool_id;
            call.provider_name = name;
            call.provider_call_id.value = "step-" + std::to_string(step);
            call.arguments = Json{std::move(fields)};
            call.arguments_digest = mira::digest_string(mira::to_json_string(call.arguments));
            response.output.emplace_back(std::move(call));
        } else {
            mira::MessageOutput message;
            message.content.emplace_back(mira::OutputTextPart{"TASK SUCCESS!", {}});
            response.output.emplace_back(std::move(message));
        }
        ++step;
        return response;
    }
};
mira::OperationContext context() {
    auto c = mira::make_control_context();
    c.task = mira::TaskId::generate();
    c.session = mira::SessionId::generate();
    c.task_epoch = 3;
    c.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    return c;
}
i::ModelLayerConfig config() {
    i::ModelLayerConfig c;
    c.enabled = true;
    c.model_selector = "fixture";
    c.max_output_tokens = 512;
    return c;
}
void scenario(kairo::Executor &executor, const std::string &mode) {
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->windows = {{"w1", "Before", {0, 0, 100, 100}, true},
                    {"w2", "Other", {200, 0, 100, 100}, false}};
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [&](const auto &, const auto &, const auto &) { return mode != "denied"; };
    mirage::desktop::CancelToken cancel;
    std::uint64_t ocr_calls = 0;
    bool visual_done = false;
    if (mode.starts_with("visual_")) {
        tools.gate = [&](const auto &cap, const auto &, const auto &) {
            return cap != "screen.capture" || mode != "visual_denied";
        };
        tools.ocr = [&](const auto &, const auto &) {
            i::DesktopOcrResult read;
            read.capture_begin = std::chrono::steady_clock::now();
            read.ok = true;
            read.backend_calls = ++ocr_calls;
            read.snapshot.scope_ref = "@vs" + std::to_string(ocr_calls);
            read.display_id = "d1";
            read.display_geometry = {0, 0, 400, 200};
            read.model_revision = "fixture";
            read.snapshot.regions = {{"@v1",
                                      mirage::desktop::VisualRegionSource::kOcr,
                                      {10, 10, 50, 20},
                                      visual_done ? "Task saved" : "Before",
                                      {},
                                      0.98}};
            if (mode == "visual_duplicate" && visual_done) {
                read.snapshot.regions.push_back(read.snapshot.regions[0]);
                read.snapshot.regions[1].ref = "@v2";
            }
            read.capture_end = std::chrono::steady_clock::now();
            return read;
        };
    }
    std::shared_ptr<Script> script;
    i::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile);
        Json::Object args{{"window_id", "w1"}, {"text", "sensitive-payload"}};
        if (mode.starts_with("visual_")) {
            args.emplace_back("verify_kind", "visual_text_exists");
            args.emplace_back("expected_text", "Task saved");
        } else if (mode != "missing") {
            args.emplace_back("verify_kind", "window_title_equals");
            args.emplace_back("expected_title", "After");
        }
        script->steps = {{"desktop.window.front", Json{Json::Object{}}},
                         {"desktop.input.type_text", Json{std::move(args)}}};
        if (mode == "rejected_then_corrected")
            script->steps = {
                {"desktop.window.list", Json{Json::Object{}}},
                {"desktop.window.activate",
                 Json::Object{{"window_id", "w2"},
                              {"verify_kind", "window_focused"},
                              {"expected_title", "private-invalid-title"}}},
                {"desktop.action.verify", Json{Json::Object{}}},
                {"desktop.window.list", Json{Json::Object{}}},
                {"desktop.window.activate", Json::Object{{"window_id", "w2"},
                                                         {"verify_kind", "window_focused"},
                                                         {"expected_title", ""},
                                                         {"expected_role", ""},
                                                         {"expected_name", ""},
                                                         {"expected_text", ""}}}};
        if (mode == "delayed" || mode == "visual_good")
            script->steps.emplace_back("desktop.action.verify", Json{Json::Object{}});
        if (mode == "foreign")
            script->steps.emplace_back("desktop.action.verify",
                                       Json{Json::Object{{"action_id", "foreign"}}});
        if (mode == "retry") {
            script->steps.emplace_back("desktop.window.front", Json{Json::Object{}});
            script->steps.emplace_back("desktop.input.type_text",
                                       Json{Json::Object{{"window_id", "w1"}, {"text", "again"}}});
        }
        script->on_step = [&](std::size_t step) {
            if (step == 2) {
                visual_done = mode != "visual_wrong";
                if (mode == "good" || mode == "delayed" || mode == "foreign")
                    env->windows[0].title = "After";
                if (mode == "focus") {
                    env->windows[0].focused = false;
                    env->windows[1].focused = true;
                }
                if (mode == "epoch")
                    tools.actions->takeover();
                if (mode == "cancel")
                    cancel.request_cancel();
            }
        };
        return script;
    });
    i::ModelLayer layer(executor, config(), &provider, tools);
    const auto ctx = context();
    const auto result = executor
                            .submit_auto([&] {
                                return layer.complete_harness_turn("", "verify business", ctx, "",
                                                                   true, {}, {}, cancel);
                            })
                            .get();
    const bool passed = mode == "good" || mode == "delayed" || mode == "visual_good" ||
                        mode == "rejected_then_corrected";
    if (result.ok != passed) {
        std::cerr << mode << ": " << result.error << '\n';
        for (const auto &part : result.parts)
            if (part.name.starts_with("desktop."))
                std::cerr << part.name << ": " << part.output << '\n';
    }
    check(result.ok == passed);
    check(passed || result.failed || result.cancelled);
    check(passed || result.reply_text.find("TASK SUCCESS") == std::string::npos);
    check(env->input_log.size() ==
          (mode == "denied" || mode == "rejected_then_corrected" ? 0u : 1u));
    if (mode == "rejected_then_corrected") {
        check(env->windows[1].focused);
        const auto failed =
            std::find_if(result.parts.begin(), result.parts.end(), [](const auto &p) {
                return p.name == "desktop.window.activate" && p.status == "failed";
            });
        check(failed != result.parts.end());
        check(failed->input.find("private-invalid-title") == std::string::npos);
        check(failed->input.find("condition_fields") != std::string::npos);
        check(failed->output.find("expected_title") != std::string::npos);
        const auto evidence = mirage::conversation::desktop_evidence(*failed);
        check(evidence && evidence->status == "rejected");
    }
    for (const auto &part : result.parts)
        if (part.name == "desktop.input.type_text") {
            check(part.input.find("sensitive-payload") == std::string::npos);
            check(part.input.find("After") == std::string::npos);
            check(part.input.find("Task saved") == std::string::npos);
            if (passed) {
                auto view = mirage::conversation::desktop_evidence(part);
                check(view && view->status == "verified" && !view->evidence_id.empty());
            }
        }
    layer.shutdown();
}

void application_scenario(kairo::Executor &executor, const std::string &mode) {
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->applications = {{"browser.desktop", "Fixture Browser", false}};
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [&](const auto &cap, const auto &resource, const auto &) {
        check(cap == "application.launch");
        check(resource == "browser.desktop");
        if (mode == "epoch") {
            tools.actions->takeover();
            check(tools.actions->resume());
        }
        if (mode == "removed_during_gate")
            env->applications.clear();
        if (mode == "gate_delayed") {
            auto delay = executor.submit_delayed(6100, [] {});
            delay.future.get();
        }
        return mode != "denied";
    };
    mirage::desktop::CancelToken cancel;
    int lists = 0;
    env->on_application_list = [&] {
        ++lists;
        if (lists == 2 && mode == "lookup_throw")
            throw std::runtime_error("lookup fixture");
        if (lists == 3 && mode == "lookup_cancel")
            cancel.request_cancel();
        if (lists == 3 && mode == "lookup_epoch") {
            tools.actions->takeover();
            check(tools.actions->resume());
        }
        if (lists == 2 && mode == "duplicate")
            env->applications.push_back(env->applications.front());
    };
    env->application_uri_uncertain = mode == "uncertain";
    if (mode == "running")
        env->application_instances["browser.desktop"] = "existing";
    env->on_application_uri = [&] {
        if (mode == "cancel_after")
            cancel.request_cancel();
        if (mode == "not_running")
            env->application_instances.clear();
        if (mode == "throw_after")
            throw std::runtime_error("fixture");
    };
    std::shared_ptr<Script> script;
    const std::string uri = "https://example.invalid/private-query?fixture=123";
    i::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile);
        const auto action =
            mode == "launch" ? "desktop.application.launch" : "desktop.application.open_uri";
        Json::Object args{
            {"application_id", mode == "unknown" ? "unknown.desktop" : "browser.desktop"}};
        if (mode != "launch")
            args.emplace_back("uri", mode == "invalid" ? "file:///tmp/fixture" : uri);
        script->steps = {{"desktop.application.list", Json::Object{}},
                         {action, Json{std::move(args)}}};
        if (mode == "interleaved")
            script->steps.insert(script->steps.begin() + 1,
                                 {"desktop.window.list", Json::Object{}});
        if (mode == "good" || mode == "running" || mode == "launch" || mode == "interleaved" ||
            mode == "delayed_candidate" || mode == "gate_delayed")
            script->steps.emplace_back("desktop.action.verify", Json::Object{});
        if (mode == "uncertain") {
            script->steps.emplace_back("desktop.application.list", Json::Object{});
            script->steps.emplace_back(
                "desktop.application.open_uri",
                Json::Object{{"application_id", "browser.desktop"}, {"uri", uri}});
        }
        script->on_step = [&](std::size_t step) {
            if (step == 1 && mode == "delayed_candidate") {
                auto delay = executor.submit_delayed(6100, [] {});
                delay.future.get();
            }
            if (step == 1 && mode == "removed")
                env->applications.clear();
            if (step == 1 && mode == "cancel_before")
                cancel.request_cancel();
            if (step == 1 && mode == "stale") {
                auto lease = tools.actions->try_begin({});
                check(static_cast<bool>(lease));
            }
        };
        return script;
    });
    i::ModelLayer layer(executor, config(), &provider, tools);
    auto ctx = context();
    ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    auto result = executor
                      .submit_auto([&] {
                          return layer.complete_harness_turn("", "open the fixture page", ctx, "",
                                                             true, {}, {}, cancel);
                      })
                      .get();
    const bool success = mode == "good" || mode == "running" || mode == "launch" ||
                         mode == "interleaved" || mode == "delayed_candidate" ||
                         mode == "gate_delayed";
    if (result.ok != success)
        std::cerr << "application " << mode << ": " << result.error << '\n';
    check(result.ok == success);
    check(success || result.failed || result.cancelled);
    const bool sent = mode == "good" || mode == "running" || mode == "interleaved" ||
                      mode == "not_running" || mode == "uncertain" || mode == "throw_after" ||
                      mode == "cancel_after" || mode == "delayed_candidate" ||
                      mode == "gate_delayed";
    check(env->application_uri_log.size() == (sent ? 1u : 0u));
    if (success)
        check(lists == 3);
    if (mode == "removed" || mode == "removed_during_gate" || mode == "lookup_throw" ||
        mode == "duplicate")
        check(result.error.find("未产生该次操作的副作用") != std::string::npos);
    check(env->input_log.empty());
    if (sent)
        check(env->application_uri_log.front().second == uri);
    for (const auto &part : result.parts) {
        check(part.input.find(uri) == std::string::npos);
        check(part.output.find(uri) == std::string::npos);
        if (success && (part.name == "desktop.application.launch" ||
                        part.name == "desktop.application.open_uri")) {
            auto view = mirage::conversation::desktop_evidence(part);
            check(view && view->status == "verified" && view->window_id.empty() &&
                  view->application_id == "browser.desktop");
            check(part.output.find("application_running_only") != std::string::npos);
        }
    }
    layer.shutdown();
}

void contracts(kairo::Executor &executor) {
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->windows = {{"w1", "Target", {0, 0, 100, 100}, true}};
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    auto ctx = context();
    i::detail::DesktopActionVerification verifier(tools, ctx, {});
    Json args{Json::Object{{"window_id", "w1"},
                           {"verify_kind", "semantic_node_exists"},
                           {"expected_role", "label"},
                           {"expected_name", "Done"}}};
    mira::BuiltinToolHandler handler = [](const auto &, const auto &) -> mira::Result<Json> {
        return Json{
            Json::Object{{"status", "dispatched"}, {"input_released", true}, {"verified", false}}};
    };
    auto first = verifier.execute("desktop.input.key", args, ctx, handler);
    check(static_cast<bool>(first));
    check(!verifier.settle());
    mira::BuiltinToolRegistry registry;
    verifier.register_tool(registry, {});
    const auto spec = registry.exposed_tools().front();
    const auto execute = [&](Json arguments, mira::OperationContext owner) {
        mira::ToolProposal call;
        call.tool_id = spec.tool_id;
        call.tool_version = spec.version;
        call.wire_name = spec.wire_name;
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "verify";
        call.arguments = std::move(arguments);
        call.arguments_digest = mira::canonical_json_digest(call.arguments);
        return registry.execute(call, owner);
    };
    mirage::desktop::SemanticNode node;
    node.ref = "@e1";
    node.name = "Done";
    node.role = "label";
    env->snapshots["w1"] = {"App", "Target", {node}};
    auto verified =
        execute(Json{Json::Object{{"action_id", field(first.value(), "action_id")}}}, ctx);
    check(verified && !verified.value().failed &&
          field(verified.value().result, "status") == "verified");
    check(verifier.settle());
    env->snapshots["w1"].nodes.push_back(node);
    check(!verifier.settle());
    auto foreign = ctx;
    foreign.session = mira::SessionId::generate();
    check(execute(Json{Json::Object{{"action_id", field(first.value(), "action_id")}}}, foreign)
              .value()
              .failed);
    check(execute(Json{Json::Object{{"action_id", field(first.value(), "action_id")},
                                    {"expected_name", "different"}}},
                  ctx)
              .value()
              .failed);
    i::detail::DesktopActionVerification uncertain(tools, ctx, {});
    int dispatches = 0;
    auto failure = [&](const auto &, const auto &) -> mira::Result<Json> {
        ++dispatches;
        mira::Error e;
        e.code = mira::ErrorCode::PlatformError;
        e.safe_message = R"({"side_effect_may_have_occurred":true,"input_released":true})";
        return e;
    };
    check(!uncertain.execute("desktop.input.key", args, ctx, failure));
    check(!uncertain.execute("desktop.input.key", args, ctx, failure) && dispatches == 1);
    check(!uncertain.settle());
    // A browser-like semantic tree can exceed the former 512-node cap.
    mira::BuiltinToolRegistry tree_registry;
    i::DesktopAtomToolset::register_harness_tools(tree_registry, tools, false, {}, {});
    const auto trees = tree_registry.exposed_tools();
    const auto tree_spec = std::find_if(trees.begin(), trees.end(), [](const auto &t) {
        return t.wire_name == "desktop.accessibility.semantic_snapshot";
    });
    check(tree_spec != trees.end());
    const auto tree_read = [&] {
        mira::ToolProposal p;
        p.operation_id = mira::OperationId::generate();
        p.tool_id = tree_spec->tool_id;
        p.tool_version = tree_spec->version;
        p.wire_name = tree_spec->wire_name;
        p.arguments = Json{Json::Object{{"window_id", "w1"}}};
        p.arguments_digest = mira::canonical_json_digest(p.arguments);
        return tree_registry.execute(p, ctx);
    };
    env->snapshots["w1"].nodes.assign(513, node);
    const auto larger_tree = tree_read();
    check(larger_tree && !larger_tree.value().failed);
    check(*larger_tree.value().result.find("node_count")->as_integer() == 513);
    env->snapshots["w1"].nodes.assign(4097, node);
    check(tree_read().value().failed);
    env->snapshots["w1"].nodes = {node, node};
    // DEC-068: reproduce the user session's missing OCR validation, then repair.
    i::detail::DesktopActionVerification recoverable(tools, ctx, {});
    int recovered_dispatches = 0;
    auto counted = [&](const auto &a, const auto &c) -> mira::Result<Json> {
        ++recovered_dispatches;
        return handler(a, c);
    };
    Json unavailable{Json::Object{
        {"window_id", "w1"}, {"verify_kind", "visual_text_exists"}, {"expected_text", "Done"}}};
    const auto rejection = recoverable.execute("desktop.input.key", unavailable, ctx, counted);
    check(!rejection && recovered_dispatches == 0 && !recoverable.settle());
    check(rejection.error().safe_message.size() <= 512);
    check(static_cast<bool>(mira::parse_json(rejection.error().safe_message)));
    mira::ModelRequest repaired_history;
    repaired_history.task_id = ctx.task;
    repaired_history.task_epoch = ctx.task_epoch;
    mira::ModelInputItem rejected_item;
    mira::ToolCallPart rejected_call;
    rejected_call.provider_call_id.value = "preflight-rejected";
    rejected_call.wire_name = "desktop.input.key";
    rejected_item.content.emplace_back(rejected_call);
    mira::ToolResultPart rejected_result;
    rejected_result.provider_call_id = rejected_call.provider_call_id;
    rejected_result.failed = true;
    rejected_result.safe_error_summary = rejection.error().safe_message;
    rejected_item.content.emplace_back(rejected_result);
    repaired_history.input.push_back(rejected_item);
    check(static_cast<bool>(recoverable.validate_request(repaired_history, ctx)));
    // Even our own action cannot recover from a truncated canonical receipt.
    i::detail::DesktopActionVerification truncated(tools, ctx, {});
    const auto truncated_rejection =
        truncated.execute("desktop.input.key", unavailable, ctx, counted);
    check(!truncated_rejection);
    auto truncated_history = repaired_history;
    auto &truncated_result =
        std::get<mira::ToolResultPart>(truncated_history.input.front().content.back());
    truncated_result.safe_error_summary = truncated_rejection.error().safe_message.substr(0, 100);
    check(static_cast<bool>(truncated.validate_request(truncated_history, ctx)));
    Json focused{Json::Object{{"window_id", "w1"}, {"verify_kind", "window_focused"}}};
    check(!truncated.execute("desktop.input.key", focused, ctx, counted));
    check(static_cast<bool>(recoverable.execute("desktop.input.key", focused, ctx, counted)));
    check(recovered_dispatches == 1 && recoverable.settle());
    // A successful earlier activation cannot hide a final rejected mutation.
    check(!recoverable.execute("desktop.input.key", unavailable, ctx, counted));
    check(!recoverable.settle());
    check(static_cast<bool>(recoverable.execute("desktop.input.key", focused, ctx, counted)));
    check(recoverable.settle());
    // Failed full-screen mark -> successful bounded ROI is a recoverable read-only path.
    i::detail::DesktopActionVerification vision_retry(tools, ctx, {});
    auto vision_history = repaired_history;
    auto &vision_item = vision_history.input.front();
    auto &vision_call = std::get<mira::ToolCallPart>(vision_item.content.front());
    vision_call.wire_name = "desktop.vision.mark";
    check(static_cast<bool>(vision_retry.validate_request(vision_history, ctx)));
    check(vision_retry.ocr_failed() && !vision_retry.settle());
    auto fresh_call = vision_call;
    fresh_call.provider_call_id.value = "fresh-roi";
    vision_item.content.emplace_back(fresh_call);
    mira::ToolResultPart fresh_vision;
    fresh_vision.provider_call_id = fresh_call.provider_call_id;
    fresh_vision.result = Json{Json::Object{{"visual_scope", "fresh-roi-scope"}}};
    vision_item.content.emplace_back(fresh_vision);
    check(static_cast<bool>(vision_retry.validate_request(vision_history, ctx)));
    check(!vision_retry.ocr_failed() && vision_retry.settle());
    // A guessed receipt or plain Provider error cannot prove absence of effects.
    i::detail::DesktopActionVerification unknown(tools, ctx, {});
    check(static_cast<bool>(unknown.validate_request(repaired_history, ctx)));
    check(!unknown.execute("desktop.input.key", focused, ctx, counted));
    i::detail::DesktopActionVerification opaque(tools, ctx, {});
    const auto plain_error = [](const auto &, const auto &) -> mira::Result<Json> {
        mira::Error e;
        e.code = mira::ErrorCode::PlatformError;
        e.safe_message = "unknown provider failure";
        return e;
    };
    check(!opaque.execute("desktop.input.key", focused, ctx, plain_error));
    check(!opaque.execute("desktop.input.key", focused, ctx, counted));
    // Field diagnostics remain valid through pinned ToolExecutor's 512-byte cap.
    for (const auto &invalid :
         {Json{Json::Object{
              {"window_id", "w1"}, {"verify_kind", "window_focused"}, {"expected_text", 7}}},
          Json{Json::Object{
              {"window_id", "w1"}, {"verify_kind", "window_focused"}, {"expected_text", "unused"}}},
          Json{Json::Object{{"window_id", "w1"},
                            {"verify_kind", "visual_text_exists"},
                            {"expected_text", ""}}}}) {
        i::detail::DesktopActionVerification fields(tools, ctx, {});
        const auto before = recovered_dispatches;
        const auto result = fields.execute("desktop.input.key", invalid, ctx, counted);
        check(!result && recovered_dispatches == before);
        check(result.error().safe_message.size() <= 512);
        const auto receipt = mira::parse_json(result.error().safe_message);
        check(receipt && field(receipt.value(), "field") == "expected_text");
        check(static_cast<bool>(fields.execute("desktop.input.key", focused, ctx, counted)));
    }
    i::detail::DesktopActionVerification escaped(tools, ctx, {});
    const auto escaped_error = [](const auto &, const auto &) -> mira::Result<Json> {
        mira::Error e;
        e.code = mira::ErrorCode::PlatformError;
        e.safe_message.assign(1024, '\x01');
        return e;
    };
    const auto escaped_result = escaped.execute("desktop.input.key", focused, ctx, escaped_error);
    check(!escaped_result && escaped_result.error().safe_message.size() <= 512);
    check(static_cast<bool>(mira::parse_json(escaped_result.error().safe_message)));
    check(!escaped.execute("desktop.input.key", focused, ctx, counted));
    // The new state remains bounded and task admission uses the external Executor.
    auto work = executor.submit_auto([&] { return verifier.settle(); });
    check(!work.get());
    mira::ModelRequest request;
    request.task_id = ctx.task;
    request.task_epoch = ctx.task_epoch;
    mira::ModelInputItem history;
    mira::ToolCallPart call;
    call.provider_call_id.value = "failed-outside-projection";
    call.wire_name = "desktop.input.key";
    history.content.emplace_back(call);
    mira::ToolResultPart failed;
    failed.provider_call_id = call.provider_call_id;
    failed.failed = true;
    history.content.emplace_back(failed);
    request.input.push_back(history);
    env->snapshots["w1"].nodes.pop_back();
    check(verifier.settle());
    check(static_cast<bool>(verifier.validate_request(request, ctx)));
    check(!verifier.settle());
}

#ifdef MIRAGE_POST_ACTION_X11
void real_tasks(kairo::Executor &executor) {
    check(XInitThreads() != 0);
    mirage::testing::XvfbDisplay xvfb(mirage::testing::find_xvfb());
    Display *display = XOpenDisplay(xvfb.display_name().c_str());
    check(display != nullptr);
    const auto root = DefaultRootWindow(display);
    const Window window = XCreateSimpleWindow(display, root, 50, 50, 300, 150, 0, 0, 0xffffff);
    XSelectInput(display, window, KeyPressMask | KeyReleaseMask | ExposureMask | FocusChangeMask);
    const auto set_title = [&](const std::string &title) {
        XStoreName(display, window, title.c_str());
        XChangeProperty(display, window, XInternAtom(display, "_NET_WM_NAME", False),
                        XInternAtom(display, "UTF8_STRING", False), 8, PropModeReplace,
                        reinterpret_cast<const unsigned char *>(title.data()),
                        static_cast<int>(title.size()));
        XSync(display, False);
    };
    set_title("Editor");
    XMapWindow(display, window);
    XSync(display, False);
    auto env = std::make_shared<mirage::platform::linux_backend::LinuxDesktopEnvironment>(
        std::vector<std::filesystem::path>{},
        mirage::platform::linux_backend::X11Options{true, xvfb.display_name()});
    const auto windows = env->window()->list_windows();
    check(windows.ok);
    const auto it = std::find_if(windows.windows.begin(), windows.windows.end(),
                                 [](const auto &w) { return w.title == "Editor"; });
    check(it != windows.windows.end());
    const auto id = it->id;
    std::string text;
    const auto file = std::filesystem::temp_directory_path() /
                      ("mirage-post-action-" + std::to_string(getpid()) + ".txt");
    for (int task = 0; task < 4; ++task) {
        if (task == 3) {
            XSetInputFocus(display, root, RevertToParent, CurrentTime);
            XSync(display, False);
        }
        kairo::comm::LatestMailbox<bool> ready;
        check(ready.try_publish(task == 0 || task == 3));
        const std::string expected = task == 0 ? "Editor" : task == 1 ? "Text:probe" : "Saved";
        i::DesktopToolContext tools;
        tools.environment = env;
        tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
        tools.gate = [](const auto &, const auto &, const auto &) { return true; };
        i::ModelProviderOverride provider([&](const auto &profile) {
            auto script = std::make_shared<Script>(profile);
            const auto observe = task == 0 ? "desktop.window.list" : "desktop.window.front";
            Json::Object args{{"window_id", id},
                              {"verify_kind", "window_title_equals"},
                              {"expected_title", expected}};
            if (task == 1)
                args.emplace_back("text", "probe");
            if (task == 2)
                args.emplace_back("key", "enter");
            script->steps = {{observe, Json{Json::Object{}}},
                             {task == 0   ? "desktop.window.activate"
                              : task == 1 ? "desktop.input.type_text"
                                          : "desktop.input.key",
                              Json{std::move(args)}},
                             {"desktop.action.verify", Json{Json::Object{}}}};
            if (task == 3) {
                script->steps = {
                    {"desktop.window.list", Json{Json::Object{}}},
                    {"desktop.window.activate",
                     Json{Json::Object{{"window_id", id},
                                       {"verify_kind", "window_focused"},
                                       {"expected_title", "unused-condition"}}}},
                    {"desktop.action.verify", Json{Json::Object{}}},
                    {"desktop.window.list", Json{Json::Object{}}},
                    {"desktop.window.activate", Json{Json::Object{{"window_id", id},
                                                                  {"verify_kind", "window_focused"},
                                                                  {"expected_title", ""},
                                                                  {"expected_role", ""},
                                                                  {"expected_name", ""},
                                                                  {"expected_text", ""}}}},
                    {"desktop.action.verify", Json{Json::Object{}}}};
            }
            script->on_step = [&](std::size_t step) {
                if (step == 2) {
                    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds{2};
                    bool value = false;
                    while (std::chrono::steady_clock::now() < end &&
                           (!ready.try_load(value) || !value))
                        (void)::poll(nullptr, 0, 1);
                    check(value);
                }
            };
            return script;
        });
        i::ModelLayer layer(executor, config(), &provider, tools);
        const auto ctx = context();
        auto work = executor.submit_auto(
            [&] { return layer.complete_harness_turn("", "local X11 business task", ctx); });
        while (work.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) {
            if (!XPending(display)) {
                (void)::poll(nullptr, 0, 1);
                continue;
            }
            XEvent event{};
            XNextEvent(display, &event);
            if (event.type == KeyPress) {
                char buffer[16]{};
                KeySym key{};
                const int n = XLookupString(&event.xkey, buffer, sizeof(buffer), &key, nullptr);
                if (key == XK_Return) {
                    std::ofstream out(file);
                    out << text;
                    out.close();
                    set_title("Saved");
                    check(ready.try_publish(true));
                } else if (n > 0) {
                    text.append(buffer, static_cast<std::size_t>(n));
                    set_title("Text:" + text);
                    if (text == "probe")
                        check(ready.try_publish(true));
                }
            }
        }
        const auto result = work.get();
        if (!result.ok)
            std::cerr << result.error << '\n';
        check(result.ok);
        const auto front = env->window()->front_window();
        check(front.ok && front.found && front.window.id == id && front.window.title == expected);
        check(task != 1 || text == "probe");
        if (task == 2) {
            std::ifstream input(file);
            std::string value;
            input >> value;
            check(value == "probe");
        }
        char keys[32]{};
        XQueryKeymap(display, keys);
        check(std::all_of(std::begin(keys), std::end(keys), [](char b) { return b == 0; }));
        layer.shutdown();
    }
    std::filesystem::remove(file);
    env.reset();
    XDestroyWindow(display, window);
    XCloseDisplay(display);
    std::cout << "4 real X11 tasks passed: activate, text echo, submit/persist, rejected "
                 "activation recovery\n";
}
#endif
} // namespace
int main() {
    try {
        kairo::Executor executor;
        kairo::ExecutorConfig options;
        options.min_threads = options.max_threads = 3;
        check(static_cast<bool>(executor.initialize(options)));
        for (const auto &mode :
             {"good", "delayed", "missing", "false_success", "focus", "epoch", "cancel", "denied",
              "foreign", "retry", "visual_good", "visual_wrong", "visual_duplicate",
              "visual_denied", "rejected_then_corrected"})
            scenario(executor, mode);
        for (const auto &mode : {"good",          "running",       "interleaved",
                                 "launch",        "denied",        "invalid",
                                 "unknown",       "stale",         "epoch",
                                 "cancel_before", "cancel_after",  "uncertain",
                                 "throw_after",   "not_running",   "delayed_candidate",
                                 "gate_delayed",  "removed",       "removed_during_gate",
                                 "lookup_throw",  "lookup_cancel", "lookup_epoch",
                                 "duplicate"})
            application_scenario(executor, mode);
        contracts(executor);
#ifdef MIRAGE_POST_ACTION_X11
        real_tasks(executor);
#endif
        executor.shutdown(true);
        std::cout << checks.load() << " post-action checks passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
