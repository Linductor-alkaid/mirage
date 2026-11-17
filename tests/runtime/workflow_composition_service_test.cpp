#include "../support/workflow_recovery_service_fixture.hpp"
#include <mirage/integration/workflow_document.hpp>
namespace {
ipc::WorkflowPublished publish(ServiceFixture &f, const mira::WorkflowDefinition &definition,
                               bool reusable = true) {
    const auto response = f.call(ipc::WorkflowPublishRequest{
        mira::canonical_json_string(mira::workflow_definition_to_json(definition)), reusable});
    MIRAGE_CHECK(response.ok);
    if (!response.ok)
        throw std::runtime_error(definition.name + ": " + response.error.code + ": " +
                                 response.error.message);
    return std::get<ipc::WorkflowPublished>(response.payload);
}
mira::WorkflowDefinition definition(const std::string &name, const std::string &tool) {
    mira::WorkflowDefinition doc;
    doc.workflow_id = mira::WorkflowId::generate();
    doc.name = name;
    doc.summary = name;
    doc.allowed_policies = {mira::WorkflowPolicy::Strict, mira::WorkflowPolicy::DryRun};
    mira::WorkflowStep step;
    step.id = mira::StepId::generate();
    step.name = "read";
    step.arguments = Json::Object{{"tool", tool}};
    if (tool == "desktop.filesystem.read_text")
        step.arguments = Json::Object{{"tool", tool}, {"path", "/fixture/good"}};
    if (tool.starts_with("workflow."))
        step.verification = mira::WorkflowPredicate{"step_result:" + step.id.to_string(),
                                                    mira::WorkflowPredicateOp::Eq, Json{true}};
    doc.steps.push_back(step);
    return doc;
}
void workflow_calls_updates_cycles_and_cas() {
    ServiceFixture f(false);
    auto child = definition("被调用工作流", "desktop.filesystem.read_text");
    const auto published = publish(f, child);
    const auto wire = "workflow." + published.workflow_id;
    const auto atoms = f.call(ipc::WorkflowAtomCatalogRequest{});
    const auto &catalog = std::get<ipc::WorkflowAtomCatalog>(atoms.payload);
    MIRAGE_CHECK(std::any_of(catalog.tools.begin(), catalog.tools.end(),
                             [&](const auto &tool) { return tool.wire_name == wire; }));
    auto parent = definition("调用工作流", wire);
    const auto parent_published = publish(f, parent);
    const auto before = f.files->successes.load();
    const auto run = f.call(
        ipc::WorkflowRunRequest{parent_published.workflow_id, parent_published.digest, "{}", ""});
    MIRAGE_CHECK(run.ok);
    if (!run.ok)
        throw std::runtime_error(run.error.message);
    f.state(std::get<ipc::WorkflowRunStarted>(run.payload).run_id, "completed");
    MIRAGE_CHECK(f.files->successes == before + 1); // Real child action, not only a drawn call.
    const auto child_json = mira::canonical_json_string(mira::workflow_definition_to_json(child));
    const auto stale = f.call(ipc::WorkflowSaveRequest{child_json, std::string(64, 'f')});
    MIRAGE_CHECK(!stale.ok && stale.error.code == "conflict");
    // Publication rejects direct and indirect cycles before replacing any target.
    auto circular = child;
    circular.steps[0].arguments = Json::Object{{"tool", wire}};
    auto cycle = f.call(ipc::WorkflowPublishRequest{
        mira::canonical_json_string(mira::workflow_definition_to_json(circular)), true,
        published.digest});
    MIRAGE_CHECK(!cycle.ok && cycle.error.code == "workflow_cycle");
    circular.steps[0].arguments =
        Json::Object{{"tool", "workflow." + parent_published.workflow_id}};
    cycle = f.call(ipc::WorkflowPublishRequest{
        mira::canonical_json_string(mira::workflow_definition_to_json(circular)), true,
        published.digest});
    MIRAGE_CHECK(!cycle.ok && cycle.error.code == "workflow_cycle");
    child.name = "被调用流程 v2";
    auto extra = child.steps[0];
    extra.id = mira::StepId::generate();
    child.steps.push_back(extra);
    const auto next = f.call(ipc::WorkflowPublishRequest{
        mira::canonical_json_string(mira::workflow_definition_to_json(child)), true,
        published.digest});
    MIRAGE_CHECK(next.ok);
    const auto old_parent = f.call(
        ipc::WorkflowRunRequest{parent_published.workflow_id, parent_published.digest, "{}", ""});
    MIRAGE_CHECK(old_parent.ok);
    f.state(std::get<ipc::WorkflowRunStarted>(old_parent.payload).run_id, "completed");
    MIRAGE_CHECK(f.files->successes ==
                 before + 3); // Unchanged caller executes the UPDATED target (two real actions).
    // A longer indirect cycle must also be rejected; repeated acyclic references are legal.
    auto diamond = definition("重复调用", wire);
    auto repeat = diamond.steps[0];
    repeat.id = mira::StepId::generate();
    diamond.steps.push_back(repeat);
    const auto repeated = publish(f, diamond);
    MIRAGE_CHECK(!repeated.workflow_id.empty());
    // Resource-depth admission remains a secondary guard, not the cycle check.
    // Default depth 2 accepts two nested children; an additional level fails closed.
    const auto outer = publish(f, definition("外层", "workflow." + parent_published.workflow_id));
    const auto outer_run =
        f.call(ipc::WorkflowRunRequest{outer.workflow_id, outer.digest, "{}", ""});
    MIRAGE_CHECK(outer_run.ok);
    f.state(std::get<ipc::WorkflowRunStarted>(outer_run.payload).run_id, "completed");
    circular.steps[0].arguments = Json::Object{{"tool", "workflow." + outer.workflow_id}};
    cycle = f.call(ipc::WorkflowPublishRequest{
        mira::canonical_json_string(mira::workflow_definition_to_json(circular)), true,
        std::get<ipc::WorkflowPublished>(next.payload).digest});
    MIRAGE_CHECK(!cycle.ok && cycle.error.code == "workflow_cycle");
    const auto excess = publish(f, definition("超深层", "workflow." + outer.workflow_id));
    const auto rejected_run =
        f.call(ipc::WorkflowRunRequest{excess.workflow_id, excess.digest, "{}", ""});
    MIRAGE_CHECK(rejected_run.ok);
    f.state(std::get<ipc::WorkflowRunStarted>(rejected_run.payload).run_id, "failed");
}
void publication_during_call_and_cancellation() {
    ServiceFixture f(false);
    auto target = definition("动态目标", "desktop.filesystem.read_text");
    const auto first = publish(f, target);
    const auto caller = publish(f, definition("调用来源", "workflow." + first.workflow_id));
    auto start = [&] {
        ipc::Response response;
        until([&] {
            response = f.call(ipc::WorkflowRunRequest{caller.workflow_id, caller.digest, "{}", ""});
            return response.ok ||
                   (response.error.message != "workflow drive is still settling" &&
                    response.error.message.find("asynchronous drive capacity is exhausted") ==
                        std::string::npos);
        });
        MIRAGE_CHECK(response.ok);
        if (!response.ok)
            throw std::runtime_error("call admission: " + response.error.code + ": " +
                                     response.error.message);
        return std::get<ipc::WorkflowRunStarted>(response.payload).run_id;
    };
    f.files->hold_reads = true;
    const auto running = start();
    until([&] { return f.files->read_entered.load(); });
    auto extra = target.steps[0];
    extra.id = mira::StepId::generate();
    target.steps.push_back(extra);
    const auto next = publish(f, target);
    MIRAGE_CHECK(next.digest != first.digest);
    f.files->hold_reads = false;
    f.state(running, "completed");
    MIRAGE_CHECK(f.files->successes == 1); // The active call is not replayed on update.
    f.state(start(), "completed");
    MIRAGE_CHECK(f.files->successes == 3); // A later call resolves the updated target.
    f.files->hold_reads = true;
    f.files->read_entered = false;
    const auto cancelled = start();
    until([&] { return f.files->read_entered.load(); });
    const auto stopped = f.call(ipc::WorkflowCancelRunRequest{cancelled});
    MIRAGE_CHECK(stopped.ok);
    f.state(cancelled, "cancelled");
    f.files->hold_reads = false;
}

} // namespace
int main() {
    workflow_calls_updates_cycles_and_cas();
    publication_during_call_and_cancellation();
    return mirage::testing::finish("workflow_composition_service_test");
}
