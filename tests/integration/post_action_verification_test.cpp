#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/provider_tool_name.hpp"
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
            const auto tool =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &t) {
                    return t.wire_name == mirage::testing::fixture_tool_name(name);
                });
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
            call.provider_name = tool->wire_name;
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
        } else if (mode != "missing" && mode != "execution_only_observed") {
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
        // DEC-056 revision: the model's action is cleanly rejected before any
        // dispatch, it re-observes the desktop and answers from that fresh
        // evidence — the turn settles instead of failing on the trailing
        // rejection (the maintainer-reported false failure).
        if (mode == "rejected_then_answer")
            script->steps = {
                {"desktop.window.list", Json{Json::Object{}}},
                {"desktop.window.activate",
                 Json::Object{{"window_id", "w2"},
                              {"verify_kind", "window_focused"},
                              {"expected_title", "private-invalid-title"}}},
                {"desktop.window.list", Json{Json::Object{}}}};
        if (mode == "good" || mode == "delayed" || mode == "visual_good" || mode == "foreign")
            script->steps.emplace_back("desktop.action.verify", Json{Json::Object{}});
        if (mode == "foreign")
            script->steps.emplace_back("desktop.action.verify",
                                       Json{Json::Object{{"action_id", "foreign"}}});
        if (mode == "execution_only_observed")
            script->steps.emplace_back("desktop.window.front", Json{Json::Object{}});
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
                        mode == "rejected_then_corrected" || mode == "rejected_then_answer" || mode == "foreign" ||
                        mode == "execution_only_observed";
    if (result.ok != passed) {
        std::cerr << mode << ": " << result.error << '\n';
        for (const auto &part : result.parts)
            if (part.name.starts_with("desktop."))
                std::cerr << part.name << ": " << part.output << '\n';
    }
    check(result.ok == passed);
    check(passed || result.failed || result.cancelled);
    if (result.failed) {
        // DEC-104: a model claim stays visible but never grants task success.
        check(!result.error.empty() && result.reply_text == "TASK SUCCESS!");
        const auto final =
            std::find_if(result.parts.begin(), result.parts.end(), [](const auto &p) {
                return p.id == mirage::conversation::terminal_reply_id;
            });
        check(final != result.parts.end() && final->kind == "text" &&
              final->text == result.reply_text);
    } else if (result.cancelled) {
        check(result.reply_text.empty());
    }
    check(env->input_log.size() ==
          (mode == "denied" || mode == "rejected_then_corrected" || mode == "rejected_then_answer"
               ? 0u
               : mode == "retry" ? 2u : 1u));
    if (mode == "rejected_then_answer") {
        // The refused activation never dispatched, and the fresh observation
        // after it lets the trailing clean rejection settle the turn.
        check(!env->windows[1].focused);
        const auto refused =
            std::find_if(result.parts.begin(), result.parts.end(), [](const auto &p) {
                return p.name == "desktop.window.activate" && p.status == "failed";
            });
        check(refused != result.parts.end());
        const auto evidence = mirage::conversation::desktop_evidence(*refused);
        check(evidence && evidence->status == "rejected");
        const auto listed = std::find_if(result.parts.begin(), result.parts.end(),
                                         [](const auto &p) {
                                             return p.name == "desktop.window.list" &&
                                                    p.status == "complete";
                                         });
        check(listed != result.parts.end());
    }
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
                if (mode == "execution_only_observed")
                    check(view && view->status == "not_requested" && view->evidence_id.empty());
                else
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
    env->application_uri_uncertain = mode == "uncertain" || mode == "uncertain_gone";
    if (mode == "running")
        env->application_instances["browser.desktop"] = "existing";
    env->on_application_uri = [&] {
        if (mode == "cancel_after")
            cancel.request_cancel();
        if (mode == "not_running" || mode == "uncertain_gone")
            env->application_instances.clear();
        if (mode == "throw_after") {
            // The exception must leave no machine evidence behind, so the
            // uncertain record keeps awaiting human review (the fixture's
            // intent; a confirmed running instance would settle it).
            env->application_instances.clear();
            throw std::runtime_error("fixture");
        }
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
        // DEC-086 (uncertain): the first handoff fails as an uncertain side
        // effect, but the fake keeps the application running, so the follow-up
        // verify sees running=true + instance id and the record settles as
        // machine-verified. The fence lifts: the second handoff dispatches and
        // the turn completes.
        // DEC-086 (uncertain_gone): without running evidence the same
        // uncertain record keeps awaiting human review — the second handoff
        // stays gated and the turn fails.
        if (mode == "uncertain" || mode == "uncertain_gone") {
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
                         mode == "gate_delayed" || mode == "uncertain";
    if (result.ok != success)
        std::cerr << "application " << mode << ": " << result.error << '\n';
    check(result.ok == success);
    check(success || result.failed || result.cancelled);
    const bool sent = mode == "good" || mode == "running" || mode == "interleaved" ||
                      mode == "not_running" || mode == "uncertain" || mode == "uncertain_gone" ||
                      mode == "throw_after" || mode == "cancel_after" ||
                      mode == "delayed_candidate" || mode == "gate_delayed";
    check(env->application_uri_log.size() == (mode == "uncertain" ? 2u : sent ? 1u : 0u));
    if (success)
        check(lists == (mode == "uncertain" ? 6 : 3));
    if (mode == "removed" || mode == "removed_during_gate" || mode == "lookup_throw" ||
        mode == "duplicate")
        check(result.error.find("未产生该次操作的副作用") != std::string::npos);
    check(env->input_log.empty());
    if (sent)
        check(env->application_uri_log.front().second == uri);
    if (mode == "uncertain") {
        // The uncertain dispatches' canonical receipts carry the machine
        // confirmation, not the human-review verdict (DEC-086).
        std::size_t uncertain_parts = 0;
        for (const auto &part : result.parts) {
            if (part.name == "desktop.application.open_uri" && part.status == "failed") {
                ++uncertain_parts;
                check(part.output.find("application_running_confirmed") != std::string::npos);
                check(part.output.find("\"reason\":\"effect_uncertain\"") == std::string::npos);
            }
        }
        check(uncertain_parts == 2);
    }
    for (const auto &part : result.parts) {
        check(part.input.find(uri) == std::string::npos);
        check(part.output.find(uri) == std::string::npos);
        if (success && part.status == "complete" &&
            (part.name == "desktop.application.launch" ||
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
    // Settlement reuses the valid prior proof; only an explicit recheck
    // observes the changed tree and invalidates the fixed condition.
    check(verifier.settle());
    check(static_cast<bool>(verifier.recheck(field(first.value(), "action_id"), ctx)));
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
    check(static_cast<bool>(verifier.recheck(field(first.value(), "action_id"), ctx)));
    check(verifier.settle());
    check(static_cast<bool>(verifier.validate_request(request, ctx)));
    check(!verifier.settle());
}

// DEC-086: an uncertain application mutation settles on machine evidence,
// the fence lifts for the next mutation, and validate_request re-derives its
// canonical failure per request instead of sticking to the oldest failure.
void evidence_contracts() {
    const Json args{Json::Object{{"application_id", "app.desktop"}}};
    const auto ctx = context();
    int dispatches = 0;
    const auto uncertain_failure = [&](const auto &, const auto &) -> mira::Result<Json> {
        ++dispatches;
        mira::Error e;
        e.code = mira::ErrorCode::PlatformError;
        e.safe_message = R"({"side_effect_may_have_occurred":true,"input_released":true})";
        return e;
    };
    const mira::BuiltinToolHandler ok_handler = [](const auto &,
                                                   const auto &) -> mira::Result<Json> {
        return Json{
            Json::Object{{"status", "dispatched"}, {"input_released", true}, {"verified", false}}};
    };
    // Scenario A: running evidence after the uncertain launch settles the
    // record as verified and releases the mutation fence.
    auto running_env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    running_env->applications = {{"app.desktop", "Fixture App", false}};
    running_env->application_instances["app.desktop"] = "i-app";
    i::DesktopToolContext running_tools;
    running_tools.environment = running_env;
    running_tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    i::detail::DesktopActionVerification confirmed(running_tools, ctx, {});
    const auto launch =
        confirmed.execute("desktop.application.launch", args, ctx,
                          [&](const auto &a, const auto &c) { return uncertain_failure(a, c); });
    check(!launch && dispatches == 1);
    const auto receipt = mira::parse_json(launch.error().safe_message);
    check(receipt.has_value());
    check(field(receipt.value(), "status") == "verified");
    check(field(receipt.value(), "reason") == "application_running_confirmed");
    check(!field(receipt.value(), "action_id").empty());
    const auto rechecked = confirmed.recheck(field(receipt.value(), "action_id"), ctx);
    check(rechecked && field(rechecked.value(), "status") == "verified" &&
          field(rechecked.value(), "reason") == "application_running_confirmed");
    const auto next = confirmed.execute("desktop.application.launch", args, ctx,
                                        [&](const auto &a, const auto &c) {
                                            ++dispatches;
                                            return ok_handler(a, c);
                                        });
    check(next && dispatches == 2);
    const auto *verification = next.value().find("verification");
    check(verification && field(*verification, "status") == "verified" &&
          field(*verification, "reason") == "application_running_only");
    check(confirmed.settle());
    // Scenario C: the failed receipt in canonical history stops disqualifying
    // once its record is machine-verified, and the derivation is per request.
    mira::ModelInputItem launch_item;
    mira::ToolCallPart launch_call;
    launch_call.provider_call_id.value = "uncertain-launch";
    launch_call.wire_name = "desktop.application.launch";
    launch_item.content.emplace_back(launch_call);
    mira::ToolResultPart launch_result;
    launch_result.provider_call_id = launch_call.provider_call_id;
    launch_result.failed = true;
    launch_result.safe_error_summary = launch.error().safe_message;
    launch_item.content.emplace_back(launch_result);
    mira::ModelRequest verified_history;
    verified_history.task_id = ctx.task;
    verified_history.task_epoch = ctx.task_epoch;
    verified_history.input.push_back(launch_item);
    check(static_cast<bool>(confirmed.validate_request(verified_history, ctx)));
    check(confirmed.settle());
    // A failure receipt records_ never saw still fences the turn.
    mira::ModelInputItem unknown_item = launch_item;
    auto &unknown_result = std::get<mira::ToolResultPart>(unknown_item.content.back());
    unknown_result.provider_call_id.value = "unknown-launch";
    std::get<mira::ToolCallPart>(unknown_item.content.front()).provider_call_id.value =
        "unknown-launch";
    unknown_result.safe_error_summary =
        R"({"schema":2,"status":"uncertain","verified":false,"dispatched":true,)"
        R"("side_effect_may_have_occurred":true,"input_released":true,)"
        R"("action_id":"unknown-action-id","reason":"action_failed"})";
    mira::ModelRequest unknown_history;
    unknown_history.task_id = ctx.task;
    unknown_history.task_epoch = ctx.task_epoch;
    unknown_history.input.push_back(unknown_item);
    // validate_request itself only refuses cancelled/mismatched owners; the
    // unknown canonical failure surfaces through settle() and the next
    // terminal answer.
    check(static_cast<bool>(confirmed.validate_request(unknown_history, ctx)));
    check(!confirmed.settle());
    // Re-derivation is per request: the verified history passes again after
    // the unknown failure instead of staying fenced forever.
    check(static_cast<bool>(confirmed.validate_request(verified_history, ctx)));
    check(confirmed.settle());

    // Scenario B: without running evidence the uncertain record keeps
    // awaiting human review and the fence holds.
    auto stopped_env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    stopped_env->applications = {{"app.desktop", "Fixture App", false}};
    i::DesktopToolContext stopped_tools;
    stopped_tools.environment = stopped_env;
    stopped_tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    i::detail::DesktopActionVerification unproven(stopped_tools, ctx, {});
    const auto fenced =
        unproven.execute("desktop.application.launch", args, ctx,
                         [&](const auto &a, const auto &c) { return uncertain_failure(a, c); });
    check(!fenced && dispatches == 3);
    const auto fenced_receipt = mira::parse_json(fenced.error().safe_message);
    check(fenced_receipt.has_value() && field(fenced_receipt.value(), "status") == "uncertain" &&
          field(fenced_receipt.value(), "reason") == "effect_uncertain");
    const auto still = unproven.recheck(field(fenced_receipt.value(), "action_id"), ctx);
    check(still && field(still.value(), "status") == "uncertain" &&
          field(still.value(), "reason") == "effect_uncertain");
    check(!unproven.execute("desktop.application.launch", args, ctx,
                            [&](const auto &a, const auto &c) { return ok_handler(a, c); }));
    check(dispatches == 3);
    check(!unproven.settle());
    // Its canonical failure also stays disqualifying for the model request.
    mira::ModelRequest fenced_history;
    fenced_history.task_id = ctx.task;
    fenced_history.task_epoch = ctx.task_epoch;
    mira::ModelInputItem fenced_item;
    mira::ToolCallPart fenced_call;
    fenced_call.provider_call_id.value = "fenced-launch";
    fenced_call.wire_name = "desktop.application.launch";
    fenced_item.content.emplace_back(fenced_call);
    mira::ToolResultPart fenced_result;
    fenced_result.provider_call_id = fenced_call.provider_call_id;
    fenced_result.failed = true;
    fenced_result.safe_error_summary = fenced.error().safe_message;
    fenced_item.content.emplace_back(fenced_result);
    fenced_history.input.push_back(fenced_item);
    check(static_cast<bool>(unproven.validate_request(fenced_history, ctx)));
    check(!unproven.settle());
}

// DEC-056 revision: a trailing clean no-effect rejection settles only when a
// successful desktop observation was recorded after it; any new mutation
// attempt re-arms the requirement, and rejections with dispatched or
// uncertain side effects never settle on observations.
void observation_settlement() {
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->windows = {{"w1", "Target", {0, 0, 100, 100}, true}};
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    const auto ctx = context();
    const Json click_args{
        Json::Object{{"window_id", "w1"}, {"verify_kind", "shared_frame_observed"}}};
    const auto shared_proof =
        [](const std::string &, const std::string &,
           const mira::OperationContext &) -> mira::Result<Json> {
        return Json{Json::Object{{"matched", true}, {"digest", "fixture-digest"}}};
    };
    // The provider-side shape of the reported defect: the shared click is
    // refused cleanly (no dispatch, no uncertainty), like the
    // target_pixels_changed guard rejection.
    const mira::BuiltinToolHandler clean_rejection =
        [](const auto &, const auto &) -> mira::Result<Json> {
        mira::Error e;
        e.code = mira::ErrorCode::PlatformError;
        e.safe_message = R"({"error_code":"target_pixels_changed",)"
                         R"("side_effect_may_have_occurred":false,"input_released":true})";
        return e;
    };
    i::detail::DesktopActionVerification verifier(tools, ctx, {});
    verifier.set_shared_observation(shared_proof);
    // Old semantics without a follow-up observation: no settlement, the
    // trailing record stays a no-effect rejection.
    check(!verifier.execute("desktop.session.click", click_args, ctx, clean_rejection));
    check(!verifier.settle() && verifier.no_effect_rejection());
    // One successful desktop observation after the rejection settles the turn
    // and the record stops qualifying as an unanswered rejection.
    verifier.note_observation();
    check(verifier.settle() && !verifier.no_effect_rejection());
    // Any new mutation attempt (here: another clean rejection) clears the
    // observation evidence; fresh observation is required again.
    check(!verifier.execute("desktop.session.click", click_args, ctx, clean_rejection));
    check(!verifier.settle() && verifier.no_effect_rejection());
    verifier.note_observation();
    check(verifier.settle() && !verifier.no_effect_rejection());
    // A handler refusal without dispatched effects is the same clean
    // rejection class and follows the same settlement rule.
    i::detail::DesktopActionVerification refused(tools, ctx, {});
    refused.set_shared_observation(shared_proof);
    const mira::BuiltinToolHandler handler_refusal =
        [](const auto &, const auto &) -> mira::Result<Json> {
        return Json{Json::Object{{"status", "refused"},
                                 {"verified", false},
                                 {"side_effect_may_have_occurred", false},
                                 {"input_released", true}}};
    };
    const auto refusal = refused.execute("desktop.session.click", click_args, ctx, handler_refusal);
    check(refusal && refusal.value().find("verified") &&
          refusal.value().find("verified")->as_boolean().value_or(true) == false);
    check(!refused.settle() && refused.no_effect_rejection());
    refused.note_observation();
    check(refused.settle() && !refused.no_effect_rejection());
    // Dispatched/uncertain side effects keep the turn unsettled no matter how
    // many observations follow.
    i::detail::DesktopActionVerification uncertain(tools, ctx, {});
    uncertain.set_shared_observation(shared_proof);
    const mira::BuiltinToolHandler dispatched_failure =
        [](const auto &, const auto &) -> mira::Result<Json> {
        mira::Error e;
        e.code = mira::ErrorCode::PlatformError;
        e.safe_message = R"({"error_code":"fixture",)"
                         R"("side_effect_may_have_occurred":true,"input_released":true})";
        return e;
    };
    check(!uncertain.execute("desktop.session.click", click_args, ctx, dispatched_failure));
    for (int observations = 0; observations < 3; ++observations) {
        uncertain.note_observation();
        check(!uncertain.settle() && !uncertain.no_effect_rejection());
    }
}

// 2026-10-20 revision (maintainer morning session): one failed OCR read must
// not poison an otherwise successful answer. validate_request() re-derives
// the vision failure in history order: any successful fresh desktop
// observation AFTER the failed read (another vision pass, a capture, a
// listing — also across an intermediate mutation) re-grounds the answer,
// while a failed read with no later successful observation keeps failing the
// turn with the same "桌面视觉读取失败" settlement text.
void ocr_turn_scenario(kairo::Executor &executor, const std::string &mode) {
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->windows = {{"w1", "Before", {0, 0, 100, 100}, true},
                    {"w2", "Other", {200, 0, 100, 100}, false}};
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [&](const auto &, const auto &, const auto &) { return true; };
    int ocr_calls = 0;
    const int fail_at = mode == "ocr_ok_then_fail" ? 2 : 1;
    tools.ocr = [&](const auto &, const auto &) {
        i::DesktopOcrResult read;
        read.capture_begin = std::chrono::steady_clock::now();
        ++ocr_calls;
        read.backend_calls = static_cast<std::uint64_t>(ocr_calls);
        read.display_id = "d1";
        read.display_geometry = {0, 0, 400, 200};
        read.model_revision = "fixture";
        read.snapshot.scope_ref = "@vs" + std::to_string(ocr_calls);
        read.snapshot.regions = {{"@v1",
                                  mirage::desktop::VisualRegionSource::kOcr,
                                  {10, 10, 50, 20},
                                  "Before",
                                  {},
                                  0.98}};
        read.capture_end = std::chrono::steady_clock::now();
        if (ocr_calls == fail_at) {
            // The maintainer's shape: the vision read times out.
            read.ok = false;
            read.error = {"timeout", "fixture analysis timeout"};
            read.snapshot.regions.clear();
        }
        return read;
    };
    std::shared_ptr<Script> script;
    i::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile);
        if (mode == "ocr_fail_then_list")
            script->steps = {{"desktop.vision.ocr", Json{Json::Object{}}},
                             {"desktop.window.list", Json{Json::Object{}}}};
        else if (mode == "ocr_fail_answer")
            script->steps = {{"desktop.vision.ocr", Json{Json::Object{}}}};
        else if (mode == "ocr_rejection_then_list")
            script->steps = {{"desktop.window.list", Json{Json::Object{}}},
                             {"desktop.vision.ocr", Json{Json::Object{}}},
                             {"desktop.window.activate",
                              Json::Object{{"window_id", "w2"},
                                           {"verify_kind", "window_focused"},
                                           {"expected_title", "private-invalid-title"}}},
                             {"desktop.window.list", Json{Json::Object{}}}};
        else if (mode == "list_then_ocr_fail")
            script->steps = {{"desktop.window.list", Json{Json::Object{}}},
                             {"desktop.vision.ocr", Json{Json::Object{}}}};
        else if (mode == "ocr_ok_then_fail")
            script->steps = {{"desktop.vision.ocr", Json{Json::Object{}}},
                             {"desktop.vision.ocr", Json{Json::Object{}}}};
        return script;
    });
    i::ModelLayer layer(executor, config(), &provider, tools);
    const auto ctx = context();
    mirage::desktop::CancelToken cancel;
    const auto result = executor
                            .submit_auto([&] {
                                return layer.complete_harness_turn("", "read the desktop", ctx, "",
                                                                   true, {}, {}, cancel);
                            })
                            .get();
    const bool passed = mode == "ocr_fail_then_list" || mode == "ocr_rejection_then_list";
    if (result.ok != passed)
        std::cerr << "ocr " << mode << ": " << result.error << '\n';
    check(result.ok == passed);
    check(passed || result.failed);
    if (passed) {
        // The successful observation after the failed read re-grounded the
        // answer: the poisoned-evidence settlement text stays absent.
        check(result.error.find("桌面视觉读取失败") == std::string::npos);
        // A turn with an action record (the clean rejection variant) answers
        // under the host verification preface; the model's claim rides both.
        check(result.reply_text.find("TASK SUCCESS!") != std::string::npos);
    } else {
        if (result.error.find("桌面视觉读取失败") == std::string::npos)
            std::cerr << "ocr " << mode << " settlement text: " << result.error << '\n';
        // Old semantics without a follow-up successful observation: the turn
        // still fails on the vision evidence.
        check(result.error.find("桌面视觉读取失败") != std::string::npos);
    }
    const auto ocr_part = std::find_if(result.parts.begin(), result.parts.end(),
                                       [](const auto &p) {
                                           return p.name == "desktop.vision.ocr" &&
                                                  p.status == "failed";
                                       });
    check(ocr_part != result.parts.end());
    check(ocr_calls == fail_at);
    if (mode == "ocr_fail_then_list") {
        check(std::any_of(result.parts.begin(), result.parts.end(), [](const auto &p) {
            return p.name == "desktop.window.list" && p.status == "complete";
        }));
    }
    if (mode == "ocr_rejection_then_list") {
        // The intermediate mutation is a clean pre-dispatch rejection; the
        // trailing successful observation after it settles the turn.
        check(env->input_log.empty());
        check(!env->windows[1].focused);
        const auto refused = std::find_if(result.parts.begin(), result.parts.end(),
                                          [](const auto &p) {
                                              return p.name == "desktop.window.activate" &&
                                                     p.status == "failed";
                                          });
        check(refused != result.parts.end());
        const auto evidence = mirage::conversation::desktop_evidence(*refused);
        check(evidence && evidence->status == "rejected");
        check(std::any_of(result.parts.begin(), result.parts.end(), [](const auto &p) {
            return p.name == "desktop.window.list" && p.status == "complete";
        }));
    }
    std::cout << "ocr " << mode << ": ok=" << (result.ok ? "true" : "false") << " error=["
              << result.error << "]\n";
    layer.shutdown();
}

// Unit-level ordering matrix of the same rule, directly against
// validate_request(): only a successful read-only desktop observation that
// appears AFTER the failed vision read clears ocr_failed_; failed
// observations and mutation/verify failures never clear it, and a failed
// mutation receipt keeps its own canonical fence independent of the vision
// question.
void ocr_ordered_rules() {
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->windows = {{"w1", "Target", {0, 0, 100, 100}, true}};
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    const auto ctx = context();
    struct Step {
        std::string wire;
        bool failed = false;
        std::string receipt;
    };
    const auto scanned = [&](std::vector<Step> steps) {
        mira::ModelRequest request;
        request.task_id = ctx.task;
        request.task_epoch = ctx.task_epoch;
        int index = 0;
        for (auto &step : steps) {
            mira::ModelInputItem item;
            mira::ToolCallPart call;
            call.provider_call_id.value = "ordered-" + std::to_string(index);
            call.wire_name = step.wire;
            item.content.emplace_back(call);
            mira::ToolResultPart result;
            result.provider_call_id = call.provider_call_id;
            result.failed = step.failed;
            if (step.failed)
                result.safe_error_summary = step.receipt;
            else
                result.result =
                    Json{Json::Object{{"observation_scope", "ordered-scope-" + std::to_string(index)},
                                      {"environment_epoch", Json{std::int64_t{3}}}}};
            item.content.emplace_back(result);
            request.input.push_back(item);
            ++index;
        }
        return request;
    };
    const auto ocr_fail = [&](std::vector<Step> tail) {
        tail.insert(tail.begin(), {"desktop.vision.ocr", true, "OCR capture or analysis failed"});
        return tail;
    };
    {
        // Failed read with nothing after it keeps failing the turn.
        i::detail::DesktopActionVerification nothing_after(tools, ctx, {});
        auto request = scanned(ocr_fail({}));
        check(static_cast<bool>(nothing_after.validate_request(request, ctx)));
        check(nothing_after.ocr_failed() && !nothing_after.settle());
    }
    {
        // The maintainer's turn-1 shape: a failed OCR, then a successful
        // desktop observation (a listing) grounds the answer afresh.
        i::detail::DesktopActionVerification verifier(tools, ctx, {});
        auto request = scanned(ocr_fail({{"desktop.window.list", false, ""}}));
        check(static_cast<bool>(verifier.validate_request(request, ctx)));
        check(!verifier.ocr_failed() && verifier.settle());
    }
    {
        // Order-sensitive: a successful observation BEFORE the failed read
        // does not clear it.
        i::detail::DesktopActionVerification stale_observation(tools, ctx, {});
        auto request = scanned({{"desktop.window.list", false, ""},
                                {"desktop.vision.ocr", true, "OCR capture or analysis failed"}});
        check(static_cast<bool>(stale_observation.validate_request(request, ctx)));
        check(stale_observation.ocr_failed() && !stale_observation.settle());
    }
    {
        // A failed observation after the failed read grounds nothing.
        i::detail::DesktopActionVerification failed_listing(tools, ctx, {});
        auto request = scanned(ocr_fail({{"desktop.window.list", true, "listing failed"}}));
        check(static_cast<bool>(failed_listing.validate_request(request, ctx)));
        check(failed_listing.ocr_failed() && !failed_listing.settle());
    }
    // Any successful read-only desktop.* evidence clears: another vision
    // pass, a capture, a front query, a semantic snapshot.
    for (const auto *grounding : {"desktop.vision.ocr", "desktop.vision.mark",
                                  "desktop.screen.capture", "desktop.window.front",
                                  "desktop.accessibility.semantic_snapshot"}) {
        i::detail::DesktopActionVerification fresh(tools, ctx, {});
        const auto request = scanned(ocr_fail({{grounding, false, ""}}));
        check(static_cast<bool>(fresh.validate_request(request, ctx)));
        check(!fresh.ocr_failed() && fresh.settle());
    }
    {
        // The last vision attempt decides: ok-then-fail fails, fail-then-ok
        // passes.
        i::detail::DesktopActionVerification last_fails(tools, ctx, {});
        const auto request =
            scanned({{"desktop.vision.ocr", false, ""},
                     {"desktop.vision.ocr", true, "OCR capture or analysis failed"}});
        check(static_cast<bool>(last_fails.validate_request(request, ctx)));
        check(last_fails.ocr_failed() && !last_fails.settle());
    }
    {
        i::detail::DesktopActionVerification last_passes(tools, ctx, {});
        const auto request = scanned({{"desktop.vision.mark", true, "mark failed"},
                                      {"desktop.vision.ocr", false, ""}});
        check(static_cast<bool>(last_passes.validate_request(request, ctx)));
        check(!last_passes.ocr_failed() && last_passes.settle());
    }
    {
        // A mutation in between neither clears nor re-fails the vision
        // question; only the later successful observation does.
        const mira::BuiltinToolHandler clean_rejection =
            [](const auto &, const auto &) -> mira::Result<Json> {
            mira::Error e;
            e.code = mira::ErrorCode::PlatformError;
            e.safe_message = R"({"error_code":"target_pixels_changed",)"
                             R"("side_effect_may_have_occurred":false,"input_released":true})";
            return e;
        };
        i::detail::DesktopActionVerification through_mutation(tools, ctx, {});
        const Json activate_args{
            Json::Object{{"window_id", "w1"}, {"verify_kind", "window_focused"}}};
        const auto rejected = through_mutation.execute("desktop.window.activate", activate_args,
                                                       ctx, clean_rejection);
        check(!rejected && !through_mutation.settle());
        const auto request =
            scanned(ocr_fail({{"desktop.window.activate", true, rejected.error().safe_message},
                              {"desktop.window.list", false, ""}}));
        check(static_cast<bool>(through_mutation.validate_request(request, ctx)));
        check(!through_mutation.ocr_failed());
        // The clean rejection still needs its own post-rejection observation
        // for settlement; the scan clearing the vision failure does not
        // substitute for it.
        check(!through_mutation.settle());
        through_mutation.note_observation();
        check(through_mutation.settle());
    }
    {
        // A mutation failure the records never saw stays fenced regardless of
        // the vision recovery, and it never clears the vision question.
        i::detail::DesktopActionVerification unknown_mutation(tools, ctx, {});
        const auto request =
            scanned(ocr_fail({{"desktop.window.activate",
                               true,
                               R"({"schema":2,"status":"uncertain","verified":false,)"
                               R"("dispatched":true,"side_effect_may_have_occurred":true,)"
                               R"("input_released":true,"action_id":"unknown-id",)"
                               R"("reason":"action_failed"})"},
                              {"desktop.window.list", false, ""}}));
        check(static_cast<bool>(unknown_mutation.validate_request(request, ctx)));
        check(!unknown_mutation.ocr_failed() && !unknown_mutation.settle());
    }
    {
        // A failed read-only verification request cannot create unknown
        // input effects. The later observation repairs the evidence question;
        // actual pending conditions and mutation uncertainty stay in records_.
        i::detail::DesktopActionVerification verify_failure(tools, ctx, {});
        const auto request =
            scanned({{"desktop.vision.ocr", true, "OCR capture or analysis failed"},
                     {"desktop.action.verify",
                      true,
                      R"({"schema":2,"status":"unverified","verified":false,)"
                      R"("dispatched":true,"side_effect_may_have_occurred":false,)"
                      R"("input_released":true,"action_id":"ghost-id",)"
                      R"("reason":"action_failed"})"},
                     {"desktop.window.list", false, ""}});
        check(static_cast<bool>(verify_failure.validate_request(request, ctx)));
        check(!verify_failure.ocr_failed() && verify_failure.settle());
    }
}

// 2026-10-20 re-verification: the canonical rejection receipt carries the
// shared-input recovery hint as a top-level field, so it survives the >512
// safe_error_summary downgrade that swaps the embedded provider message for
// the short pointer text.
void rejection_hint_downgrade() {
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->windows = {{"w1", "Target", {0, 0, 100, 100}, true}};
    i::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    const auto ctx = context();
    i::detail::DesktopActionVerification verifier(tools, ctx, {});
    // A provider rejection whose embedded receipt is padded far past any
    // budget: the >512 downgrade must fire deterministically while the hint
    // stays.
    const auto oversized_with_hint = [&](const auto &, const auto &) -> mira::Result<Json> {
        mira::Error e;
        e.code = mira::ErrorCode::PlatformError;
        Json::Object shared{{"error_code", "pointer_origin_unknown"},
                            {"side_effect_may_have_occurred", false},
                            {"input_released", true},
                            {"verified", false},
                            {"hint", "refresh authorized observation for cursor metadata; "
                                     "never bootstrap with a direct click"},
                            {"padding", std::string(600, 'x')}};
        e.safe_message = mira::to_json_string(Json{std::move(shared)});
        return e;
    };
    const auto rejected =
        verifier.execute("desktop.window.activate",
                         Json{Json::Object{{"window_id", "w1"}, {"verify_kind", "window_focused"}}},
                         ctx, oversized_with_hint);
    check(!rejected);
    const auto receipt = mira::parse_json(rejected.error().safe_message);
    check(receipt && receipt.value().as_object());
    const auto field_text = [&](const char *key) {
        const auto *value = receipt.value().find(key);
        return value && value->as_string() ? std::string(*value->as_string()) : std::string{};
    };
    check(field_text("error_code") == "pointer_origin_unknown");
    const auto hint = field_text("hint");
    check(hint.find("cursor metadata") != std::string::npos &&
          hint.find("never bootstrap") != std::string::npos);
    // The downgrade did fire: the generic pointer text replaced the oversized
    // embedded message, and the hint is still on top of the receipt.
    check(field_text("message").find("action_id") != std::string::npos);
    // The receipt itself stays within the pinned budget.
    check(rejected.error().safe_message.size() <= 512);
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
             {"good", "delayed", "missing", "execution_only_observed", "false_success", "focus", "epoch", "cancel", "denied",
              "foreign", "retry", "visual_good", "visual_wrong", "visual_duplicate",
              "visual_denied", "rejected_then_corrected", "rejected_then_answer"})
            scenario(executor, mode);
        for (const auto &mode : {"good",
                                 "running",
                                 "interleaved",
                                 "launch",
                                 "denied",
                                 "invalid",
                                 "unknown",
                                 "stale",
                                 "epoch",
                                 "cancel_before",
                                 "cancel_after",
                                 "uncertain",
                                 "uncertain_gone",
                                 "throw_after",
                                 "not_running",
                                 "delayed_candidate",
                                 "gate_delayed",
                                 "removed",
                                 "removed_during_gate",
                                 "lookup_throw",
                                 "lookup_cancel",
                                 "lookup_epoch",
                                 "duplicate"})
            application_scenario(executor, mode);
        contracts(executor);
        evidence_contracts();
        observation_settlement();
        for (const auto &mode : {"ocr_fail_then_list", "ocr_fail_answer",
                                 "ocr_rejection_then_list", "list_then_ocr_fail",
                                 "ocr_ok_then_fail"})
            ocr_turn_scenario(executor, mode);
        ocr_ordered_rules();
        rejection_hint_downgrade();
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
