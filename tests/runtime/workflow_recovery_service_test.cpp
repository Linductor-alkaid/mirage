#include "../support/ipc_io.hpp"
#include "../support/workflow_recovery_fixture.hpp"

#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/runtime_service.hpp>

namespace {
namespace ipc = mirage::runtime::ipc;
struct ServiceFixture {
    mirage::testing::TempDir directory;
    std::shared_ptr<Files> files = std::make_shared<Files>();
    std::shared_ptr<Provider> provider = std::make_shared<Provider>();
    r::ServiceConfig config;
    std::unique_ptr<r::RuntimeService> service;
    std::unique_ptr<ipc::IpcClient> client;
    explicit ServiceFixture(bool model = true) {
        config.executor_threads = 4;
        config.socket_path = (directory.root() / "service.sock").string();
        config.recovery_directory = directory.root() / "recovery";
        config.session_state_directory = directory.root() / "sessions";
        config.workflow_learning_directory = directory.root() / "learning";
        config.model.enabled = model;
        config.model.model_selector = "fixture-model";
        config.model.endpoint_origin = "https://fixture.invalid";
        config.model_provider_override =
            std::make_shared<i::ModelProviderOverride>([p = provider](const auto &profile) {
                p->configured = profile;
                return p;
            });
        service = std::make_unique<r::RuntimeService>(config);
        ok(service->start(std::make_shared<i::MiraEnvironmentBinding>(files)));
        client = std::make_unique<ipc::IpcClient>(config.socket_path);
    }
    ~ServiceFixture() {
        provider->held = false;
        if (service) {
            service->request_shutdown();
            MIRAGE_CHECK(service->run().clean);
        }
    }
    template <class T> ipc::Response call(const T &request) { return client->call(request, 10s); }
    Run waiting() {
        mira::WorkflowDefinition def;
        def.workflow_id = mira::WorkflowId::generate();
        def.name = "product recovery fixture";
        def.default_policy = mira::WorkflowPolicy::Recoverable;
        def.allowed_policies = {mira::WorkflowPolicy::Recoverable, mira::WorkflowPolicy::DryRun,
                                mira::WorkflowPolicy::Strict};
        mira::WorkflowStep step;
        step.id = mira::StepId::generate();
        step.kind = mira::WorkflowStepKind::ToolCall;
        step.arguments =
            Json::Object{{"tool", "desktop.filesystem.read_text"}, {"path", "/fixture/missing"}};
        def.steps.push_back(step);
        const auto published = call(ipc::WorkflowPublishRequest{
            mira::canonical_json_string(mira::workflow_definition_to_json(def))});
        MIRAGE_CHECK(published.ok);
        const auto *version = std::get_if<ipc::WorkflowPublished>(&published.payload);
        if (!version)
            throw std::runtime_error("publish fixture failed: " + published.error.code + " / " +
                                     published.error.message);
        const auto started =
            call(ipc::WorkflowRunRequest{version->workflow_id, version->digest, "{}", ""});
        MIRAGE_CHECK(started.ok);
        const auto *run = std::get_if<ipc::WorkflowRunStarted>(&started.payload);
        if (!run)
            throw std::runtime_error("run fixture failed");
        state(run->run_id, "waiting_agent");
        return {run->run_id, step.id.to_string(), version->workflow_id, version->digest};
    }
    void state(const std::string &id, const std::string &wanted) {
        until([&] {
            const auto response = call(ipc::WorkflowRunsRequest{});
            const auto *list = std::get_if<ipc::WorkflowRunList>(&response.payload);
            if (!response.ok || !list)
                return false;
            for (const auto &run : list->runs)
                if (run.run_id == id)
                    return run.state == wanted;
            return false;
        });
    }
    void start(const std::string &id) {
        until([&] {
            const auto response = call(ipc::WorkflowRecoveryRequest{id, "start"});
            if (!response.ok && response.error.message != "workflow drive is still settling")
                throw std::runtime_error("recovery admission: " + response.error.code + " / " +
                                         response.error.message);
            return response.ok;
        });
    }
    ipc::WorkflowRecoveryStatus settled(const std::string &id) {
        ipc::WorkflowRecoveryStatus result;
        until([&] {
            const auto response = call(ipc::WorkflowRecoveryRequest{id, "status"});
            MIRAGE_CHECK(response.ok);
            const auto *status = std::get_if<ipc::WorkflowRecoveryStatus>(&response.payload);
            if (status)
                result = *status;
            return status && status->settled;
        });
        return result;
    }
};
void model_reservation_boundary() {
    ExecutorOwner owner;
    auto provider = std::make_shared<Provider>();
    i::ModelLayerConfig config;
    config.enabled = true;
    config.model_selector = "fixture";
    i::ModelProviderOverride override([provider](const auto &profile) {
        provider->configured = profile;
        return provider;
    });
    i::ModelLayer layer(owner.value, config, &override);
    MIRAGE_CHECK(layer.running() && !layer.workflow_recovery_model());
    MIRAGE_CHECK(layer.reserve_workflow_recovery());
    MIRAGE_CHECK(!layer.reserve_workflow_recovery());
    MIRAGE_CHECK(layer.workflow_recovery_model());
    MIRAGE_CHECK(layer.complete_dialog_turn("", "hello", {}).failed);
    MIRAGE_CHECK(layer.complete_harness_turn("", "hello", {}).failed);
    MIRAGE_CHECK(provider->calls == 0);
    layer.release_workflow_recovery();
    provider->held = true;
    provider->decision = "fixture reply";
    auto inference =
        owner.value.submit_auto([&layer] { return layer.complete_dialog_turn("", "hello", {}); });
    until([&] { return provider->entered.load(); });
    MIRAGE_CHECK(!layer.reserve_workflow_recovery());
    provider->held = false;
    MIRAGE_CHECK(inference.get().ok);
    MIRAGE_CHECK(layer.reserve_workflow_recovery());
    layer.release_workflow_recovery();
    layer.shutdown();
    MIRAGE_CHECK(!layer.reserve_workflow_recovery() && !layer.workflow_recovery_model());
}

void repair_learning_and_model_lifetime() {
    ServiceFixture f;
    // Existing model hot update is still usable before the first binding.
    const auto before = f.call(ipc::GetModelRequest{});
    const auto *configuration = std::get_if<ipc::ModelConfiguration>(&before.payload);
    MIRAGE_CHECK(before.ok && configuration);
    if (!configuration)
        return;
    const auto settings = configuration->settings_json;
    MIRAGE_CHECK(f.call(ipc::SetModelRequest{settings, {}}).ok);
    const auto run = f.waiting();
    f.provider->decision = patch(run.step);
    f.provider->held = true;
    f.start(run.id);
    until([&] { return f.provider->entered.load(); });
    const auto pending = f.call(ipc::WorkflowRecoveryRequest{run.id, "status"});
    const auto *view = std::get_if<ipc::WorkflowRecoveryStatus>(&pending.payload);
    MIRAGE_CHECK(pending.ok && view && !view->settled);
    const auto sessions = f.call(ipc::ListSessionsRequest{});
    const auto *list = std::get_if<ipc::SessionList>(&sessions.payload);
    MIRAGE_CHECK(sessions.ok && list && !list->sessions.empty());
    if (list && !list->sessions.empty()) {
        const auto chat = f.call(ipc::SessionChatRequest{list->sessions.front().id, "hello"});
        MIRAGE_CHECK(!chat.ok && chat.error.code == "unavailable");
    }
    const auto change = f.call(ipc::SetModelRequest{settings, std::string("must-not-write")});
    MIRAGE_CHECK(!change.ok && change.error.code == "invalid_state");
    MIRAGE_CHECK(change.error.message.find("restart") != std::string::npos);
    MIRAGE_CHECK(f.provider->calls == 1);
    f.provider->held = false;
    const auto result = f.settled(run.id);
    MIRAGE_CHECK(result.outcome == "patched-and-resumed" && result.ordinal == 1);
    MIRAGE_CHECK(f.provider->output_mode == static_cast<int>(mira::OutputMode::Text));
    MIRAGE_CHECK(f.provider->output_tokens == 2048 && f.provider->tools_none);
    f.state(run.id, "completed");
    MIRAGE_CHECK(f.files->successes == 1);
    ipc::Response lesson;
    until([&] {
        lesson = f.call(ipc::WorkflowRecoveryRequest{run.id, "lesson"});
        if (!lesson.ok && lesson.error.message != "workflow drive is still settling")
            throw std::runtime_error("lesson settlement failed");
        return lesson.ok;
    });
    const auto *learned = std::get_if<ipc::WorkflowRecoveryStatus>(&lesson.payload);
    MIRAGE_CHECK(learned && !learned->lesson_id.empty() && learned->lesson_digest.size() == 64);
    MIRAGE_CHECK(ipc::encode_response(f.call(ipc::WorkflowRecoveryRequest{run.id, "lesson"}))
                     .find(learned ? learned->lesson_digest : "missing") != std::string::npos);
    MIRAGE_CHECK(!f.call(ipc::WorkflowRecoveryRequest{run.id, "start"}).ok);
    // Normal chat uses the same configured provider after the future settles.
    if (list && !list->sessions.empty()) {
        f.provider->decision = "fixture dialog reply";
        const auto chat = f.call(ipc::SessionChatRequest{list->sessions.front().id, "hello"});
        MIRAGE_CHECK(chat.ok);
        until([&] { return f.provider->calls.load() == 2; });
    }
    MIRAGE_CHECK(!f.call(ipc::SetModelRequest{settings, {}}).ok);
}
void chat_prevents_recovery() {
    ServiceFixture f;
    const auto run = f.waiting();
    const auto opened = f.call(ipc::OpenSessionRequest{});
    const auto *session = std::get_if<ipc::SessionOpened>(&opened.payload);
    MIRAGE_CHECK(opened.ok && session);
    if (!session)
        return;
    f.provider->decision = "fixture dialog";
    f.provider->held = true;
    MIRAGE_CHECK(f.call(ipc::SessionChatRequest{session->session_id, "hello"}).ok);
    until([&] { return f.provider->entered.load(); });
    const auto rejected = f.call(ipc::WorkflowRecoveryRequest{run.id, "start"});
    MIRAGE_CHECK(!rejected.ok && rejected.error.code == "invalid_state");
    MIRAGE_CHECK(f.provider->calls == 1);
    f.provider->held = false;
    until([&] {
        const auto history = f.call(ipc::ChatHistoryRequest{session->session_id, {}});
        const auto *dialog = std::get_if<ipc::DialogHistory>(&history.payload);
        return history.ok && dialog && !dialog->turns.empty() &&
               dialog->turns.back().status != "pending";
    });
    f.provider->decision = decision("need_user");
    f.start(run.id);
    MIRAGE_CHECK(f.settled(run.id).outcome == "deferred-to-host");
    MIRAGE_CHECK(f.provider->calls == 2);
}
void timeout_and_concurrent_admission() {
    ServiceFixture f;
    const auto run = f.waiting();
    const auto other = f.waiting();
    f.provider->held = true;
    f.provider->decision = decision("need_user");
    f.start(run.id);
    until([&] { return f.provider->entered.load(); });
    const auto rejected = f.call(ipc::WorkflowRecoveryRequest{other.id, "start"});
    MIRAGE_CHECK(!rejected.ok && rejected.error.code == "unavailable");
    MIRAGE_CHECK(f.provider->calls == 1);
    // Actual product request deadline: no test-only shortened service option.
    const auto end = std::chrono::steady_clock::now() + 40s;
    ipc::WorkflowRecoveryStatus attempt;
    do {
        const auto response = f.call(ipc::WorkflowRecoveryRequest{run.id, "status"});
        const auto *status = std::get_if<ipc::WorkflowRecoveryStatus>(&response.payload);
        if (!response.ok || !status)
            throw std::runtime_error("timeout status failed");
        attempt = *status;
        if (attempt.settled)
            break;
        std::this_thread::sleep_for(10ms);
    } while (std::chrono::steady_clock::now() < end);
    MIRAGE_CHECK(attempt.settled && attempt.outcome == "aborted");
    MIRAGE_CHECK(f.files->successes == 0 && f.provider->calls == 1);
    f.state(run.id, "waiting_agent");
    MIRAGE_CHECK(!f.call(ipc::WorkflowRecoveryRequest{run.id, "start"}).ok);
}

void failures_and_cancellation() {
    for (const std::string mode :
         {"need_user", "invalid", "throw", "cancel", "run_cancel", "shutdown"}) {
        ServiceFixture f;
        const auto run = f.waiting();
        f.provider->decision = mode == "invalid" ? "invalid JSON" : decision("need_user");
        f.provider->throws = mode == "throw";
        f.provider->held = mode == "cancel" || mode == "run_cancel" || mode == "shutdown";
        f.start(run.id);
        until([&] { return f.provider->entered.load(); });
        if (mode == "shutdown") {
            // Do not release provider: request_shutdown/run must cancel and drain it.
            f.service->request_shutdown();
            MIRAGE_CHECK(f.service->run().clean);
            f.service.reset();
            continue;
        }
        if (mode == "cancel")
            MIRAGE_CHECK(f.call(ipc::WorkflowRecoveryRequest{run.id, "cancel"}).ok);
        if (mode == "run_cancel")
            MIRAGE_CHECK(f.call(ipc::WorkflowCancelRunRequest{run.id}).ok);
        const auto result = f.settled(run.id);
        MIRAGE_CHECK(result.outcome == "deferred-to-host" || result.outcome == "aborted");
        MIRAGE_CHECK(f.provider->calls == 1 && f.files->successes == 0);
        MIRAGE_CHECK(!f.call(ipc::WorkflowRecoveryRequest{run.id, "lesson"}).ok);
        MIRAGE_CHECK(!f.call(ipc::WorkflowRecoveryRequest{run.id, "start"}).ok);
        const auto wire =
            ipc::encode_response(f.call(ipc::WorkflowRecoveryRequest{run.id, "status"}));
        MIRAGE_CHECK(wire.find("private-") == std::string::npos);
    }
    ServiceFixture f(false);
    const auto run = f.waiting();
    const auto dark = f.call(ipc::WorkflowRecoveryRequest{run.id, "start"});
    MIRAGE_CHECK(!dark.ok && dark.error.code == "unavailable");
    const auto unknown =
        f.call(ipc::WorkflowRecoveryRequest{mira::WorkflowRunId::generate().to_string(), "start"});
    MIRAGE_CHECK(!unknown.ok && unknown.error.code == "not_found");
}
} // namespace
int main() {
    model_reservation_boundary();
    repair_learning_and_model_lifetime();
    chat_prevents_recovery();
    timeout_and_concurrent_admission();
    failures_and_cancellation();
    return mirage::testing::finish("workflow_recovery_service_test");
}
