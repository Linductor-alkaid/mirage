#include "../support/workflow_recovery_service_fixture.hpp"
#include <fstream>
#include <mirage/runtime/persistence/workflow_catalog.hpp>

namespace {
namespace p = mirage::runtime::persistence;
const std::string target_id(32, 'f'), caller_id(32, '1'), root_id(32, '2');
std::string json(const mira::WorkflowDefinition &definition) {
    return mira::canonical_json_string(mira::workflow_definition_to_json(definition));
}
mira::WorkflowDefinition definition(const std::string &id, const std::string &tool) {
    mira::WorkflowDefinition d;
    d.workflow_id = *mira::WorkflowId::parse(id);
    d.name = id;
    d.summary = "durable fixture";
    d.allowed_policies = {mira::WorkflowPolicy::Strict, mira::WorkflowPolicy::DryRun};
    mira::WorkflowStep step;
    step.id = mira::StepId::generate();
    step.name = "fixture action";
    step.arguments = Json::Object{{"tool", tool}};
    if (tool == "desktop.filesystem.read_text")
        step.arguments.set("path", "/fixture/good");
    if (tool.starts_with("workflow."))
        step.verification = mira::WorkflowPredicate{"step_result:" + step.id.to_string(),
                                                    mira::WorkflowPredicateOp::Eq, true};
    d.steps.push_back(step);
    return d;
}
std::string read(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const std::filesystem::path &file, const std::string &body) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << body;
}
struct CatalogFixture {
    mirage::testing::TempDir directory;
    std::shared_ptr<Files> files = std::make_shared<Files>();
    r::ServiceConfig config;
    std::unique_ptr<r::RuntimeService> service;
    std::unique_ptr<ipc::IpcClient> client;
    bool running = false;
    CatalogFixture() {
        config.executor_threads = 4;
        config.socket_path = (directory.root() / "service.sock").string();
        config.recovery_directory = directory.root() / "recovery";
        config.session_state_directory = directory.root() / "sessions";
        config.workflow_state_directory = directory.root() / "workflows";
        config.workflow_learning_enabled = false;
    }
    ~CatalogFixture() { stop(); }
    r::HostOutcome start(bool expected = true) {
        service = std::make_unique<r::RuntimeService>(config);
        const auto outcome = service->start(std::make_shared<i::MiraEnvironmentBinding>(files));
        if (expected)
            ok(outcome);
        running = outcome.ok;
        if (outcome.ok)
            client = std::make_unique<ipc::IpcClient>(config.socket_path);
        return outcome;
    }
    void stop() {
        client.reset();
        if (service) {
            files->hold_reads = false;
            service->request_shutdown();
            const auto report = service->run();
            MIRAGE_CHECK(!running || report.clean);
            service.reset();
        }
    }
    void restart() {
        stop();
        start();
    }
    std::filesystem::path file() const {
        return config.workflow_state_directory / "workflow-catalog.json";
    }
    template <class T> ipc::Response call(const T &request) { return client->call(request, 10s); }
    ipc::WorkflowPublished publish(const mira::WorkflowDefinition &d,
                                   std::optional<std::string> expected = {}) {
        auto response = call(ipc::WorkflowPublishRequest{json(d), true, expected});
        MIRAGE_CHECK(response.ok);
        if (!response.ok)
            throw std::runtime_error("catalog publish: " + response.error.message);
        return std::get<ipc::WorkflowPublished>(response.payload);
    }
    std::string run(const std::string &id, const std::string &digest) {
        ipc::Response response;
        until([&] {
            response = call(ipc::WorkflowRunRequest{id, digest, "{}", ""});
            return response.ok ||
                   response.error.message.find("drive capacity") == std::string::npos;
        });
        MIRAGE_CHECK(response.ok);
        if (!response.ok)
            throw std::runtime_error(response.error.message);
        const auto run_id = std::get<ipc::WorkflowRunStarted>(response.payload).run_id;
        until([&] {
            const auto list = call(ipc::WorkflowRunsRequest{});
            for (const auto &run : std::get<ipc::WorkflowRunList>(list.payload).runs)
                if (run.run_id == run_id)
                    return run.state == "completed";
            return false;
        });
        return run_id;
    }
};
void restart_calls_drafts_and_delete() {
    CatalogFixture f;
    f.start();
    auto target = definition(target_id, "desktop.filesystem.read_text");
    const auto b = f.publish(target);
    const auto a = f.publish(definition(caller_id, "workflow." + target_id));
    const auto c = f.publish(definition(root_id, "workflow." + caller_id));
    auto draft = target;
    draft.name = "unpublished edit";
    draft.steps[0].arguments.set("path", "/fixture/missing");
    auto saved = f.call(ipc::WorkflowSaveRequest{json(draft), b.digest});
    MIRAGE_CHECK(saved.ok);
    const auto draft_digest = std::get<ipc::WorkflowSaved>(saved.payload).digest;
    // Saving identical published bytes as a draft must not revoke an existing call target.
    const auto view_a = f.call(ipc::WorkflowGetRequest{caller_id});
    MIRAGE_CHECK(
        f.call(ipc::WorkflowSaveRequest{
                   std::get<ipc::WorkflowDefinitionView>(view_a.payload).definition_json, a.digest})
            .ok);
    MIRAGE_CHECK(f.files->reads == 0);
    const auto disk = read(f.file());
    f.restart();
    MIRAGE_CHECK(read(f.file()) == disk && f.files->reads == 0);
    MIRAGE_CHECK(
        std::get<ipc::WorkflowRunList>(f.call(ipc::WorkflowRunsRequest{}).payload).runs.empty());
    const auto view =
        std::get<ipc::WorkflowDefinitionView>(f.call(ipc::WorkflowGetRequest{target_id}).payload);
    MIRAGE_CHECK(view.digest == draft_digest &&
                 view.definition_json.find("unpublished edit") != std::string::npos);
    f.run(root_id, c.digest);
    MIRAGE_CHECK(f.files->successes == 1);
    auto second = target.steps[0];
    second.id = mira::StepId::generate();
    target.steps.push_back(second);
    const auto updated = f.publish(target, draft_digest);
    f.run(root_id, c.digest);
    MIRAGE_CHECK(f.files->successes == 3);
    auto circular = target;
    circular.steps = definition(target_id, "workflow." + root_id).steps;
    const auto before = read(f.file());
    const auto rejected = f.call(ipc::WorkflowPublishRequest{json(circular), true, updated.digest});
    MIRAGE_CHECK(!rejected.ok && rejected.error.code == "workflow_cycle" &&
                 read(f.file()) == before);
    const auto removed = f.call(ipc::WorkflowDeleteRequest{target_id});
    MIRAGE_CHECK(!removed.ok && read(f.file()) == before);
    f.restart();
    MIRAGE_CHECK(f.files->successes == 3);
    f.run(root_id, c.digest);
    MIRAGE_CHECK(f.files->successes == 5);
    for (const auto &id : {root_id, caller_id, target_id})
        MIRAGE_CHECK(f.call(ipc::WorkflowDeleteRequest{id}).ok);
    f.restart();
    MIRAGE_CHECK(
        std::get<ipc::WorkflowList>(f.call(ipc::WorkflowListRequest{}).payload).workflows.empty());
    const auto atoms = f.call(ipc::WorkflowAtomCatalogRequest{});
    for (const auto &tool : std::get<ipc::WorkflowAtomCatalog>(atoms.payload).tools)
        MIRAGE_CHECK(!tool.wire_name.starts_with("workflow."));
}
void write_failure_preserves_target() {
    CatalogFixture f;
    f.start();
    auto target = definition(target_id, "desktop.filesystem.read_text");
    const auto first = f.publish(target);
    const auto caller = f.publish(definition(caller_id, "workflow." + target_id));
    const auto body = read(f.file());
    const auto backup = f.directory.root() / "previous";
    std::filesystem::rename(f.config.workflow_state_directory, backup);
    write(f.config.workflow_state_directory, "blocked parent");
    auto extra = target.steps[0];
    extra.id = mira::StepId::generate();
    target.steps.push_back(extra);
    const auto failed = f.call(ipc::WorkflowPublishRequest{json(target), true, first.digest});
    MIRAGE_CHECK(!failed.ok && failed.error.code == "io_error");
    const auto uncommitted = f.call(ipc::WorkflowRunRequest{
        target_id, mira::workflow_definition_digest(target).to_string(), "{}", ""});
    MIRAGE_CHECK(!uncommitted.ok &&
                 uncommitted.error.message == "workflow version is not committed");
    MIRAGE_CHECK(
        std::get<ipc::WorkflowDefinitionView>(f.call(ipc::WorkflowGetRequest{target_id}).payload)
            .digest == first.digest);
    MIRAGE_CHECK(!f.call(ipc::WorkflowSaveRequest{json(target), first.digest}).ok);
    MIRAGE_CHECK(!f.call(ipc::WorkflowDeleteRequest{caller_id}).ok);
    const auto completed = f.run(caller_id, caller.digest);
    MIRAGE_CHECK(f.files->successes == 1);
    const auto candidate = f.call(ipc::CompileWorkflowExperienceRequest{
        completed, root_id, "import fixture", "durable import"});
    MIRAGE_CHECK(candidate.ok);
    if (!candidate.ok)
        throw std::runtime_error(candidate.error.message);
    const auto archive = ipc::encode_response(candidate);
    const auto import_failed = f.call(ipc::ImportWorkflowExperienceRequest{archive});
    MIRAGE_CHECK(!import_failed.ok && import_failed.error.code == "io_error");
    MIRAGE_CHECK(!f.call(ipc::WorkflowGetRequest{root_id}).ok);
    MIRAGE_CHECK(read(backup / "workflow-catalog.json") == body);
    std::filesystem::remove(f.config.workflow_state_directory);
    std::filesystem::rename(backup, f.config.workflow_state_directory);
    f.restart();
    MIRAGE_CHECK(read(f.file()) == body);
    MIRAGE_CHECK(f.call(ipc::ImportWorkflowExperienceRequest{archive}).ok);
    const auto imported = f.call(ipc::WorkflowGetRequest{root_id});
    MIRAGE_CHECK(imported.ok);
    const auto imported_digest = std::get<ipc::WorkflowDefinitionView>(imported.payload).digest;
    f.restart();
    MIRAGE_CHECK(
        std::get<ipc::WorkflowDefinitionView>(f.call(ipc::WorkflowGetRequest{root_id}).payload)
            .digest == imported_digest);
    MIRAGE_CHECK(!f.call(ipc::WorkflowRunRequest{root_id, "", "{}", ""}).ok);
    f.run(caller_id, caller.digest);
    MIRAGE_CHECK(f.files->successes == 2);
}
void invalid_catalog_fails_closed() {
    CatalogFixture f;
    write(f.file(), "{broken");
    const auto failed = f.start(false);
    MIRAGE_CHECK(!failed.ok && failed.error.code == "workflow_catalog" &&
                 read(f.file()) == "{broken");
    f.stop();
    f.config.persist_workflow_state = false;
    MIRAGE_CHECK(f.start().ok); // Explicit ephemeral mode does not touch a bad durable file.
    MIRAGE_CHECK(read(f.file()) == "{broken");
    f.stop();
    f.config.persist_workflow_state = true;
    std::filesystem::resize_file(f.file(), p::kWorkflowCatalogMaxBytes + 1);
    MIRAGE_CHECK(!f.start(false).ok &&
                 std::filesystem::file_size(f.file()) == p::kWorkflowCatalogMaxBytes + 1);
}
} // namespace
int main() {
    restart_calls_drafts_and_delete();
    write_failure_preserves_target();
    invalid_catalog_fails_closed();
    return mirage::testing::finish("workflow_catalog_service_test");
}
