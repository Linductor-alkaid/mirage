#include "../support/workflow_recovery_service_fixture.hpp"

namespace {
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
