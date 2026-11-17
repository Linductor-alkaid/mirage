#pragma once

// Shared isolated product service for Host/IPC and native bridge acceptance.
#include "ipc_io.hpp"
#include "workflow_recovery_fixture.hpp"
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
        files->hold_reads = false;
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
} // namespace
