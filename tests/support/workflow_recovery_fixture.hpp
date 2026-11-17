#pragma once

// Shared controlled Host/model fixture for recovery and learning integration.
#include "test.hpp"
#include <kairo/executor.hpp>
#include <mira/adapters/simulator/simulator_environment.hpp>
#include <mira/model_provider.hpp>
#include <mira/workflow_events.hpp>
#include <mira/workflow_ir.hpp>
#include <mirage/integration/workflow_learning_memory.hpp>
#include <mirage/integration/workflow_recovery_model.hpp>
#include <mirage/runtime/mira_host.hpp>

#include <atomic>
#include <functional>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
namespace r = mirage::runtime;
namespace i = mirage::integration;
namespace d = mirage::desktop;
using Json = mira::JsonValue;
namespace {
template <class T>
T take(mira::Result<T> result, const std::source_location at = std::source_location::current()) {
    MIRAGE_CHECK(result);
    if (!result)
        throw std::runtime_error("recovery fixture failed at " + std::to_string(at.line()));
    return std::move(result.value());
}
inline void ok(const r::HostOutcome &result) {
    MIRAGE_CHECK(result.ok);
    if (!result.ok)
        throw std::runtime_error("recovery Host operation failed: " + result.error.code);
}
template <class F>
void until(F fn, const std::source_location at = std::source_location::current()) {
    const auto end = std::chrono::steady_clock::now() + 10s;
    while (!fn()) {
        if (std::chrono::steady_clock::now() >= end)
            throw std::runtime_error("recovery fixture wait timed out at " +
                                     std::to_string(at.line()));
        std::this_thread::sleep_for(1ms);
    }
}
struct ExecutorOwner {
    kairo::Executor value;
    ExecutorOwner() {
        kairo::ExecutorConfig c;
        c.min_threads = c.max_threads = 4;
        MIRAGE_CHECK(value.initialize(c));
    }
    ~ExecutorOwner() { MIRAGE_CHECK(value.shutdown(true) == kairo::ShutdownResult::Completed); }
};
class Binding final : public i::DesktopEnvironmentBinding, public mira::IEnvironment {
    mira::adapters::simulator::SimulatorEnvironment simulator_{
        mira::adapters::simulator::SimulatorSetup::single_display()};

  public:
    const char *binding_name() const override { return "recovery-test.simulator"; }
    mira::EnvironmentCapabilities capabilities() const override {
        return simulator_.capabilities();
    }
    mira::Result<mira::Observation> observe(const mira::ObservationRequest &v,
                                            const mira::OperationContext &c) override {
        return simulator_.observe(v, c);
    }
    mira::Result<mira::ExecutionReceipt> execute(const mira::InputSequence &v,
                                                 const mira::OperationContext &c) override {
        return simulator_.execute(v, c);
    }
    mira::Result<void> interrupt(const mira::OperationContext &c) override {
        return simulator_.interrupt(c);
    }
};
class Files final : public d::DesktopEnvironment, public d::FilesystemProvider {
  public:
    std::atomic<unsigned> reads{0}, successes{0};
    std::atomic<bool> hold_reads{false}, read_entered{false};
    d::EnvironmentInfo info() const override { return {"recovery-fixture", "test"}; }
    d::FilesystemProvider *filesystem() override { return this; }
    d::FileReadOutcome read_text_file(const std::filesystem::path &path, const d::FileReadLimits &l,
                                      const d::CancelToken &cancel) override {
        ++reads;
        read_entered = true;
        const auto limit = std::chrono::steady_clock::now() + 10s;
        while (hold_reads && !cancel.cancelled() && std::chrono::steady_clock::now() < limit)
            std::this_thread::sleep_for(1ms);
        d::FileReadOutcome result;
        if (!cancel.cancelled() && l.max_bytes >= 7 && path.generic_string() == "/fixture/good") {
            result.ok = true;
            result.content = "fixture";
            ++successes;
        } else
            result.error = {"not_found", "controlled missing file"};
        return result;
    }
};
class Provider final : public mira::IModelProvider {
  public:
    mira::ModelProfile configured;
    std::string decision, last_text;
    std::atomic<unsigned> calls{0};
    std::atomic<unsigned> output_tokens{0};
    std::atomic<int> output_mode{-1}, thinking_mode{-1};
    std::atomic<bool> held{false}, entered{false}, ignore_cancel{false}, tools_none{false};
    bool fail = false, throws = false, diagnostics_throw = false;
    const mira::TransportTrace &last_trace() const override {
        if (diagnostics_throw)
            throw std::runtime_error("private-provider-diagnostic");
        return mira::IModelProvider::last_trace();
    }
    const mira::SseStreamStats &last_sse_stats() const override {
        if (diagnostics_throw)
            throw std::runtime_error("private-provider-diagnostic");
        return mira::IModelProvider::last_sse_stats();
    }
    std::optional<std::chrono::milliseconds> last_retry_after_hint() const override {
        if (diagnostics_throw)
            throw std::runtime_error("private-provider-diagnostic");
        return mira::IModelProvider::last_retry_after_hint();
    }
    const mira::ModelProfile &profile() const override { return configured; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &ctx,
                                            const mira::ProviderInferOptions &) override {
        ++calls;
        output_tokens = static_cast<unsigned>(request.generation.max_output_tokens.value_or(0));
        output_mode = static_cast<int>(request.output_contract.mode);
        thinking_mode =
            request.generation.thinking ? static_cast<int>(*request.generation.thinking) : -1;
        entered = true;
        tools_none =
            request.tools.empty() && request.tool_choice.mode == mira::ToolChoiceMode::None;
        last_text.clear();
        for (const auto &item : request.input)
            for (const auto &part : item.content)
                if (const auto *text = std::get_if<mira::TextPart>(&part))
                    last_text += text->text;
        while (held && (ignore_cancel || !ctx.cancelled_or_expired(mira::Timestamp::now())))
            std::this_thread::sleep_for(1ms);
        if (!ignore_cancel && ctx.cancelled())
            return mira::make_model_error(mira::ModelDomainCode::ModelCancelled,
                                          "private-provider-cancelled", false,
                                          request.operation_id);
        if (!ignore_cancel && ctx.expired(mira::Timestamp::now()))
            return mira::make_model_error(mira::ModelDomainCode::ModelDeadlineExceeded,
                                          "private-provider-timeout", false, request.operation_id);
        if (throws)
            throw std::runtime_error("private-provider-exception");
        if (fail) {
            mira::Error e;
            e.code = mira::ErrorCode::Unavailable;
            e.domain = "fixture.provider";
            e.domain_code = 42;
            e.operation_id = request.operation_id;
            e.safe_message = "private-provider-error";
            return e;
        }
        mira::ModelResponse response;
        response.contract_version = {1, 0};
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = configured.id;
        response.requested_model = configured.model_selector;
        response.status = mira::ModelCompletionStatus::Completed;
        mira::MessageOutput message;
        message.role = mira::ModelRole::Assistant;
        mira::OutputTextPart text;
        text.text = decision;
        message.content.emplace_back(std::move(text));
        response.output.emplace_back(std::move(message));
        return response;
    }
};
inline std::string decision(const std::string &action) {
    return mira::canonical_json_string(
        Json{Json::Object{{"action", action},
                          {"used_lessons", Json::Array{}},
                          {"rationale", "private-rationale-marker"}}});
}
inline std::string patch(const std::string &step, const std::string &path = "/fixture/good") {
    return mira::canonical_json_string(Json{Json::Object{
        {"action", "patch_and_resume"},
        {"used_lessons", Json::Array{}},
        {"rationale", "private-rationale-marker"},
        {"patch_entries",
         Json::Array{Json::Object{{"target", "step_arguments"},
                                  {"op", "set"},
                                  {"path", step},
                                  {"value", Json::Object{{"tool", "desktop.filesystem.read_text"},
                                                         {"path", path}}}}}}}});
}
struct Run {
    std::string id, step, workflow, digest;
};
struct Fixture {
    ExecutorOwner executor;
    Files files;
    std::atomic<bool> allow{true};
    std::shared_ptr<Provider> provider = std::make_shared<Provider>();
    std::shared_ptr<mira::ModelGateway> gateway;
    std::shared_ptr<i::WorkflowRecoveryModel> model = std::make_shared<i::WorkflowRecoveryModel>();
    std::shared_ptr<i::WorkflowEventBridge> events = std::make_shared<i::WorkflowEventBridge>();
    std::shared_ptr<i::WorkflowLearningMemory> learning;
    r::MiraHost host;
    using MemoryFactory = std::function<std::shared_ptr<mira::IMemory>(kairo::Executor &)>;
    explicit Fixture(r::WorkflowRecoveryOptions options = {}, bool attach = true,
                     MemoryFactory memory_factory = {}, bool scripted_model = true,
                     const std::string &agent_subject = {}) {
        if (scripted_model) {
            auto &p = provider->configured;
            p.id = mira::ModelProfileId::generate();
            p.display_name = "recovery-fixture";
            p.version = {1, 0, 0};
            p.endpoint_origin = "https://recovery.invalid";
            p.api_prefix = "/v1";
            p.model_selector = "scripted";
            p.credential = {"fixture-unused"};
            p.capabilities.text = p.capabilities.strict_json_schema = {
                true, mira::CapabilityEvidence::FixtureVerified, ""};
            p.deadlines.total = 5s;
            mira::ModelRouter router;
            router.register_profile(std::make_shared<mira::ModelProfile>(p));
            gateway = std::make_shared<mira::ModelGateway>(executor.value, std::move(router),
                                                           nullptr, mira::PriceTable{});
            gateway->register_provider(provider);
            model->gateway = gateway;
            model->profile_id = p.id;
            model->provider = provider;
        }
        ok(host.start(std::make_shared<Binding>()));
        const auto tools = i::DesktopAtomToolset::build(
            &files, [this](const auto &cap, const auto &, const auto &) {
                return cap == "filesystem.read" && allow.load();
            });
        if (memory_factory) {
            learning = std::make_shared<i::WorkflowLearningMemory>();
            learning->memory = memory_factory(executor.value);
            learning->agent_subject_id = agent_subject;
        }
        ok(host.attach_workflow_surface(executor.value, events, tools, learning));
        if (gateway)
            gateway->set_event_store(events, mira::RuntimeId::generate(),
                                     *mira::SessionId::parse(host.primary_session().id));
        if (attach && scripted_model)
            ok(host.attach_workflow_recovery(model, options));
    }
    Run start_new(const std::string &path = "/fixture/missing",
                  const std::string &policy = "recoverable") {
        mira::WorkflowDefinition def;
        def.workflow_id = mira::WorkflowId::generate();
        def.name = "controlled recovery";
        def.default_policy = mira::WorkflowPolicy::Recoverable;
        def.allowed_policies = {mira::WorkflowPolicy::Recoverable, mira::WorkflowPolicy::DryRun,
                                mira::WorkflowPolicy::Strict};
        mira::WorkflowParameterSpec secret;
        secret.name = "secret";
        secret.type = mira::WorkflowParameterType::String;
        secret.default_value = "private-parameter-marker";
        def.parameters.push_back(secret);
        mira::WorkflowStep step;
        step.id = mira::StepId::generate();
        step.kind = mira::WorkflowStepKind::ToolCall;
        step.arguments = Json::Object{{"tool", "desktop.filesystem.read_text"}, {"path", path}};
        def.steps.push_back(step);
        const auto published = host.publish_workflow_definition(
            mira::canonical_json_string(mira::workflow_definition_to_json(def)), "fixture");
        MIRAGE_CHECK(published.ok);
        if (!published.ok)
            throw std::runtime_error("cannot publish recovery fixture");
        const auto run =
            host.start_workflow_run(published.workflow_id, published.digest, "{}", policy);
        MIRAGE_CHECK(run.ok);
        if (!run.ok)
            throw std::runtime_error("cannot start recovery fixture");
        return {run.run_id, step.id.to_string(), published.workflow_id, published.digest};
    }
    Run start_same(const Run &source, const std::string &policy = "recoverable") {
        const auto run = host.start_workflow_run(source.workflow, source.digest, "{}", policy);
        MIRAGE_CHECK(run.ok);
        if (!run.ok)
            throw std::runtime_error("cannot restart controlled workflow");
        return {run.run_id, source.step, source.workflow, source.digest};
    }
    Run waiting() {
        const auto run = start_new();
        state(run.id, "waiting_agent");
        return run;
    }
    void start_recovery(const std::string &run) {
        until([&] {
            const auto result = host.start_workflow_recovery(run);
            if (!result.ok && (result.error.code != "invalid_state" ||
                               result.error.message != "workflow drive is still settling"))
                throw std::runtime_error("recovery admission failed: " + result.error.code);
            return result.ok;
        });
    }
    void state(const std::string &run, const std::string &expected) {
        std::string last;
        try {
            until([&] {
                const auto view = host.workflow_run_view(run);
                if (view.ok)
                    last = view.view.state;
                return view.ok && last == expected;
            });
        } catch (...) {
            throw std::runtime_error("expected Run state " + expected + ", observed " + last);
        }
    }
    r::WorkflowRecoveryAttemptView settled(const std::string &run, std::uint32_t after = 0) {
        r::WorkflowRecoveryPollResult result;
        until([&] {
            result = host.poll_workflow_recovery(run, after);
            if (!result.ok)
                throw std::runtime_error("recovery poll failed: " + result.error.code);
            return result.settled;
        });
        return result.attempt;
    }
    ~Fixture() {
        provider->held = false;
        MIRAGE_CHECK(host.shutdown_workflow_surface().ok);
        const auto stopped = host.shutdown();
        MIRAGE_CHECK(stopped.ok && stopped.report.clean);
    }
};

} // namespace
