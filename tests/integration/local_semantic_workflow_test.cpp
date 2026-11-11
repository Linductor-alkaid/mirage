#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mira/workflow_runtime.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <stdexcept>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using Json = mira::JsonValue;

namespace {
// Hooks run only on the current tool worker; the owner reads after future.get().
struct Fixture final : d::DesktopEnvironment, d::AccessibilityProvider {
    mirage::testing::FakeDesktopEnvironment fake;
    std::function<void()> on_read, on_activate;
    std::size_t reads = 0;
    bool scoped = true;
    Fixture() {
        fake.windows = {{"w1", "Editor", {0, 0, 200, 100}, true}};
        fake.snapshots["w1"] = {
            "EditorApp", "Editor", {{"@e1", "button", "Save", {}, d::kNoParent, {}}}};
        fake.actionable_refs = {"@e1"};
        on_activate = [this] {
            fake.snapshots["w1"].nodes.push_back({"@e2", "label", "Saved", {}, d::kNoParent, {}});
        };
    }
    d::EnvironmentInfo info() const override { return {"local-knowledge-fixture", "test"}; }
    d::WindowProvider *window() override { return fake.window(); }
    d::AccessibilityProvider *accessibility() override { return this; }
    bool scoped_references_supported() const override { return scoped; }
    d::SnapshotOutcome semantic_snapshot(const std::string &id,
                                         const d::SemanticSnapshotLimits &limits,
                                         const d::CancelToken &cancel) override {
        ++reads;
        if (on_read)
            on_read();
        return fake.accessibility()->semantic_snapshot(id, limits, cancel);
    }
    d::ElementActionOutcome activate_element(const d::ElementTarget &target,
                                             const d::CancelToken &cancel) override {
        const auto outcome = fake.accessibility()->activate_element(target, cancel);
        if (outcome.ok && on_activate)
            on_activate();
        return outcome;
    }
    d::ElementActionOutcome set_text(const d::ElementTarget &, const std::string &,
                                     const d::InputLimits &, const d::CancelToken &) override {
        throw std::logic_error("knowledge must not write text");
    }
};

Json knowledge() {
    return Json{Json::Object{{"knowledge_schema", 1},
                             {"application", "EditorApp"},
                             {"window_title", "Editor"},
                             {"role", "button"},
                             {"name", "Save"},
                             {"expected_role", "label"},
                             {"expected_name", "Saved"}}};
}
Json replace(Json args, const std::string &key, Json value) {
    auto object = *args.as_object();
    for (auto &[k, v] : object)
        if (k == key)
            v = value;
    return Json{std::move(object)};
}
auto context() {
    auto c = mira::make_control_context();
    c.task = mira::TaskId::generate();
    c.session = mira::SessionId::generate();
    c.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    return c;
}
auto dispatch(mira::BuiltinToolRegistry &registry, const Json &args,
              const mira::OperationContext &ctx) {
    const auto tools = registry.exposed_tools();
    const auto tool = std::find_if(tools.begin(), tools.end(), [](const auto &t) {
        return t.wire_name == "desktop.knowledge.activate";
    });
    if (tool == tools.end())
        throw std::logic_error("missing local knowledge atom");
    mira::ToolProposal p;
    p.provider_call_id = mira::ProviderToolCallId{"local-fixture"};
    p.tool_id = tool->tool_id;
    p.wire_name = tool->wire_name;
    p.tool_version = tool->version;
    p.arguments = args;
    p.arguments_digest = mira::canonical_json_digest(args);
    p.operation_id = mira::OperationId::generate();
    p.has_side_effects = true;
    return registry.execute(p, ctx);
}
void positive(kairo::Executor &executor) {
    Fixture env;
    auto actions = std::make_shared<d::ActionCoordinator>();
    int gates = 0;
    auto tools = i::DesktopAtomToolset::build(
        &env,
        [&](const auto &cap, const auto &resource, const auto &) {
            MIRAGE_CHECK(cap == "input.inject" && resource == "w1");
            ++gates;
            return true;
        },
        {}, actions);
    std::string digest;
    for (int round = 0; round < 2; ++round) {
        const auto ref = round == 0 ? "@e1" : "@e99";
        env.fake.snapshots["w1"].nodes = {{ref, "button", "Save", {}, d::kNoParent, {}}};
        env.fake.snapshots["w1"].nodes[0].geometry = {round * 40, round * 20, 30, 20};
        env.fake.actionable_refs = {ref};
        const auto result =
            executor
                .submit_auto([&] { return dispatch(*tools->registry(), knowledge(), context()); })
                .get();
        MIRAGE_CHECK(result && !result.value().failed);
        if (result && !result.value().failed) {
            const auto receipt = mira::parse_json(*result.value().result.as_string());
            MIRAGE_CHECK(receipt);
            const auto &out = receipt.value();
            MIRAGE_CHECK(out.find("verified") && out.find("verified")->as_boolean() == true);
            const auto *proof = out.find("verification");
            MIRAGE_CHECK(proof && proof->find("evidence_id") &&
                         !proof->find("evidence_id")->as_string()->empty());
            const auto current = *out.find("knowledge_digest")->as_string();
            MIRAGE_CHECK(round == 0 || digest == current);
            digest = current;
        }
        MIRAGE_CHECK(env.fake.activated_refs.back() == ref);
    }
    MIRAGE_CHECK(gates == 2 && env.reads == 4);
    MIRAGE_CHECK(actions->input_released());
}

void negative(kairo::Executor &executor, const std::string &mode) {
    Fixture env;
    auto actions = std::make_shared<d::ActionCoordinator>();
    auto args = knowledge();
    auto ctx = context();
    bool cancel = false;
    ctx.cancellation_requested = [&] { return cancel; };
    const bool effect = mode == "no_post" || mode == "duplicate_post" || mode == "post_identity" ||
                        mode == "post_page" || mode == "provider_throw" || mode == "post_throw" ||
                        mode == "cancel_after" || mode == "takeover_after" ||
                        mode == "timeout_after";
    auto &tree = env.fake.snapshots["w1"];
    if (mode == "application")
        tree.application = "Other";
    if (mode == "page")
        env.fake.windows[0].title = "Other";
    if (mode == "snapshot_page")
        tree.window_title = "Other";
    if (mode == "missing")
        tree.nodes.clear();
    if (mode == "duplicate")
        tree.nodes.push_back({"@e3", "button", "Save", {}, d::kNoParent, {}});
    if (mode == "disabled")
        tree.nodes[0].enabled = false;
    if (mode == "preexisting")
        tree.nodes.push_back({"@e2", "label", "Saved", {}, d::kNoParent, {}});
    if (mode == "budget")
        tree.nodes.resize(513);
    if (mode == "read_error")
        env.fake.failures.snapshot_error = true;
    if (mode == "read_throw")
        env.on_read = [] { throw std::runtime_error("read"); };
    if (mode == "invalid_ref")
        tree.nodes[0].ref = "persisted-coordinate";
    if (mode == "version")
        args = replace(args, "knowledge_schema", 2);
    if (mode == "overlong")
        args = replace(args, "name", std::string(1025, 'a'));
    if (mode == "empty")
        args = replace(args, "name", "");
    if (mode == "control")
        args = replace(args, "name", "Save\n");
    if (mode == "utf8")
        args = replace(args, "name", std::string(1, '\xff'));
    if (mode == "ref_argument") {
        auto object = *args.as_object();
        object.emplace_back("ref", "@e1");
        args = Json{std::move(object)};
    }
    if (mode == "cancel_before")
        cancel = true;
    if (mode == "expired")
        ctx.deadline = std::chrono::steady_clock::now();
    if (effect)
        env.on_activate = [&] {
            if (mode == "provider_throw")
                throw std::runtime_error("after effect");
            if (mode != "no_post")
                tree.nodes.push_back({"@e2", "label", "Saved", {}, d::kNoParent, {}});
            if (mode == "duplicate_post")
                tree.nodes.push_back({"@e3", "label", "Saved", {}, d::kNoParent, {}});
            if (mode == "post_identity")
                tree.application = "Other";
            if (mode == "post_page")
                tree.window_title = "Other";
            if (mode == "post_throw")
                env.on_read = [] { throw std::runtime_error("post"); };
            if (mode == "cancel_after")
                cancel = true;
            if (mode == "takeover_after")
                actions->takeover();
            if (mode == "timeout_after") {
                // Bounded simulated provider work, cooperatively observes its deadline.
                while (!ctx.cancelled_or_expired(mira::Timestamp::now())) {
                }
            }
        };
    if (mode == "timeout_after")
        ctx.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds{50};
    auto tools = i::DesktopAtomToolset::build(
        &env,
        [&](const auto &, const auto &, const auto &) {
            if (mode == "focus")
                env.fake.windows[0].focused = false;
            if (mode == "move")
                env.fake.windows[0].geometry.x++;
            if (mode == "gate_page")
                env.fake.windows[0].title = "Other";
            if (mode == "epoch")
                actions->takeover();
            if (mode == "cancel_gate")
                cancel = true;
            if (mode == "stale_scope")
                env.fake.accessibility()->semantic_snapshot("w1", {512, 0, {}}, {});
            return mode != "denied";
        },
        {}, actions);
    const auto result =
        executor.submit_auto([&] { return dispatch(*tools->registry(), args, ctx); }).get();
    if (result && !result.value().failed)
        std::fprintf(stderr, "unexpected success: %s\n", mode.c_str());
    MIRAGE_CHECK(!result || result.value().failed);
    MIRAGE_CHECK(env.fake.activated_refs.size() == (effect ? 1u : 0u));
    if (effect && result) {
        // Mira intentionally bounds the safe summary; it is not a JSON document.
        const auto &summary = result.value().safe_error_summary;
        MIRAGE_CHECK(summary.find("\"side_effect_may_have_occurred\":true") != std::string::npos);
        MIRAGE_CHECK(summary.find("\"verified\":false") != std::string::npos);
    }
}

void workflow(kairo::Executor &executor) {
    auto env = std::make_shared<Fixture>();
    auto actions = std::make_shared<d::ActionCoordinator>();
    auto tools = i::DesktopAtomToolset::build(
        env.get(), [](const auto &, const auto &, const auto &) { return true; }, {}, actions);
    auto binding = std::make_shared<i::MiraEnvironmentBinding>(env);
    mira::MiraRuntime runtime;
    MIRAGE_CHECK(runtime.initialize());
    const auto session = runtime.open_session(binding);
    MIRAGE_CHECK(session && session.value().command.outcome(std::chrono::seconds{5}));
    mira::WorkflowRuntime workflow(executor, runtime, session.value().id, binding);
    workflow.set_tool_registry(tools->registry());
    mira::WorkflowDefinition definition;
    definition.workflow_id = mira::WorkflowId::generate();
    definition.name = "Local semantic save";
    definition.allowed_policies = {mira::WorkflowPolicy::Strict, mira::WorkflowPolicy::Recoverable,
                                   mira::WorkflowPolicy::DryRun};
    mira::WorkflowStep step;
    step.id = mira::StepId::generate();
    step.name = "Save and verify";
    auto object = *knowledge().as_object();
    object.emplace_back("tool", "desktop.knowledge.activate");
    step.arguments = Json{std::move(object)};
    // The successful receipt exists only after this composite verifies new evidence.
    step.verification =
        mira::WorkflowPredicate{"step_result:" + step.id.to_string(),
                                mira::WorkflowPredicateOp::Contains, Json{"\"verified\":true"}};
    definition.steps = {step};
    const auto published = workflow.publish_validated(definition, "fixture", "local contract");
    if (!published)
        throw std::runtime_error("publish: " + published.error().safe_message);
    MIRAGE_CHECK(published && env->fake.activated_refs.empty());
    auto run = [&](mira::WorkflowPolicy policy) {
        auto created = workflow.create_run(definition.workflow_id, published.value().ir_digest,
                                           Json{Json::Object{}}, policy);
        MIRAGE_CHECK(created);
        auto executed = workflow.execute_run(created.value().run_id, context());
        MIRAGE_CHECK(executed);
        return executed.value();
    };
    const auto good = run(mira::WorkflowPolicy::Strict);
    MIRAGE_CHECK(good.state == mira::WorkflowRunState::Completed);
    MIRAGE_CHECK(good.steps.size() == 1 && good.steps[0].verification == "satisfied");
    MIRAGE_CHECK(env->fake.activated_refs.size() == 1);
    // Existing postcondition refuses replay and recoverable explicitly hands over.
    const auto waiting = run(mira::WorkflowPolicy::Recoverable);
    MIRAGE_CHECK(waiting.state == mira::WorkflowRunState::WaitingAgent);
    MIRAGE_CHECK(env->fake.activated_refs.size() == 1);
    MIRAGE_CHECK(workflow.agent_continuation(waiting.run_id));
    env->fake.snapshots["w1"].nodes.resize(1);
    auto cancelling = workflow.create_run(definition.workflow_id, published.value().ir_digest,
                                          Json{Json::Object{}}, mira::WorkflowPolicy::Strict);
    MIRAGE_CHECK(cancelling);
    env->on_activate = [&] { MIRAGE_CHECK(workflow.cancel_run(cancelling.value().run_id)); };
    const auto cancelled = workflow.execute_run(cancelling.value().run_id, context());
    MIRAGE_CHECK(cancelled && cancelled.value().state == mira::WorkflowRunState::Cancelled);
    MIRAGE_CHECK(env->fake.activated_refs.size() == 2);
    MIRAGE_CHECK(workflow.cancel_run(cancelling.value().run_id).value().state ==
                 mira::WorkflowRunState::Cancelled);
    const auto down = workflow.shutdown();
    MIRAGE_CHECK(down.clean && down.cancelled_runs >= 1);
    MIRAGE_CHECK(!workflow.create_run(definition, Json{Json::Object{}}, std::nullopt));
    const auto stop = runtime.request_shutdown();
    MIRAGE_CHECK(stop && stop.value().outcome(std::chrono::seconds{5}));
    MIRAGE_CHECK(runtime.finish_shutdown().clean);
}
} // namespace

int main() {
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 4;
    MIRAGE_CHECK(executor.initialize(config).ok);
    Fixture env;
    const auto contains = [](const auto &tools) {
        const auto atoms = tools->exposed_atoms();
        return std::any_of(atoms.begin(), atoms.end(), [](const auto &atom) {
            return atom.wire_name == "desktop.knowledge.activate";
        });
    };
    MIRAGE_CHECK(!contains(i::DesktopAtomToolset::build(&env, {})));
    env.scoped = false;
    MIRAGE_CHECK(!contains(
        i::DesktopAtomToolset::build(&env, {}, {}, std::make_shared<d::ActionCoordinator>())));
    positive(executor);
    for (const auto *mode :
         {"application",    "page",         "snapshot_page",  "missing",        "duplicate",
          "disabled",       "preexisting",  "budget",         "read_error",     "read_throw",
          "invalid_ref",    "version",      "overlong",       "empty",          "control",
          "utf8",           "ref_argument", "cancel_before",  "expired",        "denied",
          "focus",          "move",         "gate_page",      "epoch",          "cancel_gate",
          "stale_scope",    "no_post",      "duplicate_post", "post_identity",  "post_page",
          "provider_throw", "post_throw",   "cancel_after",   "takeover_after", "timeout_after"})
        negative(executor, mode);
    workflow(executor);
    bool exception_observed = false;
    try {
        executor.submit_auto([] { throw std::runtime_error("finite task failure"); }).get();
    } catch (...) {
        exception_observed = true;
    }
    MIRAGE_CHECK(exception_observed);
    executor.shutdown(true);
    bool rejected = false;
    try {
        executor.submit_auto([] {}).get();
    } catch (...) {
        rejected = true;
    }
    MIRAGE_CHECK(rejected);
    return mirage::testing::finish("local_semantic_workflow_test");
}
