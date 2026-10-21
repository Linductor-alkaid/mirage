#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <iterator>
#include <kairo/comm/phase_gate.hpp>
#include <kairo/executor.hpp>
#include <mira/model_digest.hpp>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/desktop/element_target_executor.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>
#include <stdexcept>

using namespace std::chrono_literals;
namespace d = mirage::desktop;

int main() {
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 2;
    MIRAGE_CHECK(static_cast<bool>(executor.initialize(config)));
    auto actions = std::make_shared<d::ActionCoordinator>();
    auto schema_env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    schema_env->windows = {{"w1", "Target", {0, 0, 200, 100}, true}};
    mirage::integration::DesktopToolContext tool_context;
    tool_context.environment = schema_env;
    tool_context.actions = actions;
    tool_context.gate = [](const auto &, const auto &, const auto &) { return true; };
    mira::BuiltinToolRegistry schema_registry;
    mirage::integration::DesktopAtomToolset::register_harness_tools(schema_registry, tool_context,
                                                                    true, {}, {});
    MIRAGE_CHECK(schema_registry.size() == 10);
    std::ifstream golden_file(MIRAGE_ACTION_SCHEMA_GOLDEN);
    const auto golden =
        mira::parse_json(std::string(std::istreambuf_iterator<char>(golden_file), {}));
    MIRAGE_CHECK(golden && golden.value().as_object());
    for (const auto &tool : schema_registry.exposed_tools()) {
        if (tool.has_side_effects) {
            const auto *expected = golden.value().find(tool.wire_name);
            MIRAGE_CHECK(expected != nullptr);
            const auto *required = tool.parameters_schema.root.find("required");
            MIRAGE_CHECK(required && expected &&
                         mira::to_json_string(*required) == mira::to_json_string(*expected));
            MIRAGE_CHECK(tool.version.major == ((tool.wire_name == "desktop.input.key" ||
                                                 tool.wire_name == "desktop.input.type_text")
                                                    ? 4
                                                    : 2));
        }
    }
    auto context = mira::make_control_context();
    context.session = mira::SessionId::generate();
    const auto execute = [&](const std::string &name, const mira::JsonValue &args,
                             const mira::OperationContext &ctx) {
        const auto tools = schema_registry.exposed_tools();
        const auto spec = std::find_if(tools.begin(), tools.end(),
                                       [&](const auto &tool) { return tool.wire_name == name; });
        mira::ToolProposal call;
        call.tool_id = spec->tool_id;
        call.tool_version = spec->version;
        call.wire_name = name;
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id = {call.operation_id.to_string()};
        call.has_side_effects = spec->has_side_effects;
        call.arguments = args;
        call.arguments_digest = mira::canonical_json_digest(args);
        return schema_registry.execute(call, ctx);
    };
    const auto front =
        execute("desktop.window.front", mira::JsonValue{mira::JsonValue::Object{}}, context);
    MIRAGE_CHECK(front && !front.value().failed);
    auto args = mira::parse_json(R"({"window_id":"w1","text":"safe"})").value();
    auto fields = *args.as_object();
    fields.emplace_back("observation_scope", *front.value().result.find("observation_scope"));
    fields.emplace_back("environment_epoch", *front.value().result.find("environment_epoch"));
    args = mira::JsonValue{fields};
    auto foreign = context;
    foreign.session = mira::SessionId::generate();
    const auto wrong_session = execute("desktop.input.type_text", args, foreign);
    MIRAGE_CHECK(wrong_session && wrong_session.value().failed &&
                 wrong_session.value().safe_error_summary.find("stale_observation") !=
                     std::string::npos);
    MIRAGE_CHECK(schema_env->input_log.empty());
    for (auto &[name, value] : fields)
        if (name == "text")
            value = mira::JsonValue{std::string(65537, 'a')};
    const auto over_budget = execute("desktop.input.type_text", mira::JsonValue{fields}, context);
    MIRAGE_CHECK(over_budget && over_budget.value().failed && schema_env->input_log.empty());
    schema_env->windows.front().geometry.x += 1;
    const auto moved = execute("desktop.input.type_text", args, context);
    MIRAGE_CHECK(moved && moved.value().failed &&
                 moved.value().safe_error_summary.find("focus_changed") != std::string::npos);
    MIRAGE_CHECK(schema_env->input_log.empty());
    schema_env->windows.front().focused = false;
    const auto no_front =
        execute("desktop.window.front", mira::JsonValue{mira::JsonValue::Object{}}, context);
    MIRAGE_CHECK(no_front && !no_front.value().failed &&
                 no_front.value().result.find("found")->as_boolean() == false);
    schema_env->windows.front().focused = true;
    const auto fresh =
        execute("desktop.window.front", mira::JsonValue{mira::JsonValue::Object{}}, context);
    MIRAGE_CHECK(fresh && !fresh.value().failed);
    fields = *args.as_object();
    for (auto &[name, value] : fields) {
        if (name == "observation_scope")
            value = *fresh.value().result.find(name);
        if (name == "environment_epoch")
            value = *fresh.value().result.find(name);
    }
    auto expiration = executor.submit_delayed(5100, [] {});
    expiration.future.get();
    const auto expired = execute("desktop.input.type_text", mira::JsonValue{fields}, context);
    MIRAGE_CHECK(expired && expired.value().failed &&
                 expired.value().safe_error_summary.find("stale_observation") != std::string::npos);
    MIRAGE_CHECK(schema_env->input_log.empty());
    d::CancelToken token;
    const auto authority = actions->authority_epoch();
    MIRAGE_CHECK(authority != 0);
    const auto epoch = actions->epoch();
    {
        auto lease = actions->try_begin(token, epoch);
        MIRAGE_CHECK(static_cast<bool>(lease));
        MIRAGE_CHECK(actions->authority_epoch() == authority);
        const auto refused = executor
                                 .submit_auto([&] {
                                     auto second = actions->try_begin({});
                                     return second.error().code;
                                 })
                                 .get();
        MIRAGE_CHECK(refused == "desktop_busy");
        // The workflow atom has the same owner, rather than a private lease.
        auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
        auto toolset = mirage::integration::DesktopAtomToolset::build(
            env.get(), [](const auto &, const auto &, const auto &) { return true; }, {}, actions);
        const auto tools = toolset->registry()->exposed_tools();
        const auto tool = std::find_if(tools.begin(), tools.end(), [](const auto &item) {
            return item.wire_name == "desktop.input.type_text";
        });
        MIRAGE_CHECK(tool != tools.end());
        mira::ToolProposal call;
        call.provider_call_id = {"workflow-busy"};
        call.tool_id = tool->tool_id;
        call.wire_name = tool->wire_name;
        call.tool_version = tool->version;
        call.operation_id = mira::OperationId::generate();
        call.has_side_effects = true;
        call.arguments = mira::parse_json(R"({"text":"never"})").value();
        call.arguments_digest = mira::canonical_json_digest(call.arguments);
        auto result = toolset->registry()->execute(call, mira::make_control_context());
        MIRAGE_CHECK(result && result.value().failed &&
                     result.value().safe_error_summary.find("desktop_busy") != std::string::npos);
        MIRAGE_CHECK(env->input_log.empty());
        actions->takeover();
        MIRAGE_CHECK(actions->authority_epoch() == 0);
        MIRAGE_CHECK(token.cancelled());
        MIRAGE_CHECK(!actions->resume());
    }
    MIRAGE_CHECK(actions->input_released() && actions->resume());
    auto stale = actions->try_begin({}, epoch);
    MIRAGE_CHECK(!stale && stale.error().code == "stale_observation");
    try {
        auto lease = actions->try_begin({});
        MIRAGE_CHECK(static_cast<bool>(lease));
        throw std::runtime_error("fixture");
    } catch (const std::runtime_error &) {
    }
    MIRAGE_CHECK(actions->input_released());
    {
        auto lease = actions->try_begin({});
        MIRAGE_CHECK(static_cast<bool>(lease));
        lease.release_failed();
    }
    MIRAGE_CHECK(!actions->input_released() && !actions->resume());
    MIRAGE_CHECK(actions->try_begin({}).error().code == "input_release_failed");

    d::VisualReferenceRegistry registry;
    d::VisualSnapshot snapshot;
    d::VisualRegionEntry region;
    region.bounds = {10, 20, 100, 40};
    snapshot.regions.push_back(region);
    const auto first = registry.publish(snapshot);
    MIRAGE_CHECK(first.ok);
    snapshot.regions.front().bounds.x = 900;
    const auto second = registry.publish(snapshot);
    MIRAGE_CHECK(second.ok && first.snapshot.scope_ref != second.snapshot.scope_ref);
    bool called = false;
    const auto old = registry.with_reference(first.snapshot.scope_ref, "@v1",
                                             [&](const auto &) { called = true; });
    MIRAGE_CHECK(old.code == "stale_observation" && !called);
    MIRAGE_CHECK(
        registry
            .with_reference(second.snapshot.scope_ref, "@v2", [&](const auto &) { called = true; })
            .code == "not_found");
    MIRAGE_CHECK(!called);
    kairo::comm::PhaseGate entered, release, publishing;
    auto reader = executor.submit_auto([&] {
        int captured_x = 0;
        auto result = registry.with_references(
            second.snapshot.scope_ref, "@v1", "@v1",
            [&](const auto &item, const auto &destination) {
                captured_x = item.bounds.x == destination.bounds.x ? item.bounds.x : -1;
                (void)entered.advance_to(1);
                (void)release.wait_for(1, 2s);
            });
        return std::pair{result.code, captured_x};
    });
    MIRAGE_CHECK(static_cast<bool>(entered.wait_for(1, 1s)));
    auto writer = executor.submit_auto([&] {
        (void)publishing.advance_to(1);
        return registry.publish(snapshot);
    });
    MIRAGE_CHECK(static_cast<bool>(publishing.wait_for(1, 1s)));
    MIRAGE_CHECK(writer.wait_for(10ms) == std::future_status::timeout);
    MIRAGE_CHECK(static_cast<bool>(release.advance_to(1)));
    const auto read = reader.get();
    MIRAGE_CHECK(read.first.empty() && read.second == 900);
    const auto published = writer.get();
    MIRAGE_CHECK(published.ok);
    MIRAGE_CHECK(
        registry.with_reference(second.snapshot.scope_ref, "@v1", [](const auto &) {}).code ==
        "stale_observation");

    mirage::testing::FakeDesktopEnvironment env;
    d::ElementTargetExecutor target(nullptr, &registry, env.input());
    d::ElementTarget addressed;
    addressed.reference.id = "@v1";
    addressed.snapshot_scope = second.snapshot.scope_ref;
    addressed.raw = {5, 5}; // A stale strong reference cannot fall through.
    MIRAGE_CHECK(target.click(addressed, nullptr).error.code == "stale_observation");
    MIRAGE_CHECK(env.input_log.empty());
    addressed.snapshot_scope = published.snapshot.scope_ref;
    MIRAGE_CHECK(target.click(addressed, nullptr).ok);
    MIRAGE_CHECK(env.input_log.size() == 3);
    d::VisualReferenceRegistry pair_registry;
    d::VisualSnapshot pair_snapshot;
    pair_snapshot.regions = {region, region};
    pair_snapshot.regions[1].bounds.x = 120;
    const auto pair_first = pair_registry.publish(pair_snapshot);
    MIRAGE_CHECK(pair_first.ok);
    called = false;
    MIRAGE_CHECK(pair_registry
                     .with_references(pair_first.snapshot.scope_ref, "@v1", "@v2",
                                      [&](const auto &from, const auto &to) {
                                          called = true;
                                          MIRAGE_CHECK(from.bounds.x == 10 && to.bounds.x == 120);
                                      })
                     .code.empty());
    MIRAGE_CHECK(called);
    pair_snapshot.regions[1].bounds.x = 190;
    const auto pair_second = pair_registry.publish(pair_snapshot);
    called = false;
    MIRAGE_CHECK(pair_registry
                     .with_references(pair_first.snapshot.scope_ref, "@v1", "@v2",
                                      [&](const auto &, const auto &) { called = true; })
                     .code == "stale_observation");
    MIRAGE_CHECK(!called);
    MIRAGE_CHECK(pair_registry
                     .with_references(pair_second.snapshot.scope_ref, "@v1", "@v3",
                                      [&](const auto &, const auto &) { called = true; })
                     .code == "not_found");
    MIRAGE_CHECK(!called);
    executor.shutdown(true);
    return mirage::testing::finish("action_contract_test");
}
