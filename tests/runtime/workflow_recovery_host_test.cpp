#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mira/adapters/simulator/simulator_environment.hpp>
#include <mira/model_provider.hpp>
#include <mira/workflow_events.hpp>
#include <mira/workflow_ir.hpp>
#include <mirage/integration/workflow_recovery_model.hpp>
#include <mirage/runtime/mira_host.hpp>

#include <atomic>
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
void ok(const r::HostOutcome &result) {
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
    d::EnvironmentInfo info() const override { return {"recovery-fixture", "test"}; }
    d::FilesystemProvider *filesystem() override { return this; }
    d::FileReadOutcome read_text_file(const std::filesystem::path &path, const d::FileReadLimits &l,
                                      const d::CancelToken &cancel) override {
        ++reads;
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
std::string decision(const std::string &action) {
    return mira::canonical_json_string(
        Json{Json::Object{{"action", action},
                          {"used_lessons", Json::Array{}},
                          {"rationale", "private-rationale-marker"}}});
}
std::string patch(const std::string &step, const std::string &path = "/fixture/good") {
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
    std::string id, step;
};
struct Fixture {
    ExecutorOwner executor;
    Files files;
    std::atomic<bool> allow{true};
    std::shared_ptr<Provider> provider = std::make_shared<Provider>();
    std::shared_ptr<mira::ModelGateway> gateway;
    std::shared_ptr<i::WorkflowRecoveryModel> model = std::make_shared<i::WorkflowRecoveryModel>();
    std::shared_ptr<i::WorkflowEventBridge> events = std::make_shared<i::WorkflowEventBridge>();
    r::MiraHost host;
    explicit Fixture(r::WorkflowRecoveryOptions options = {}, bool attach = true) {
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
        gateway = std::make_shared<mira::ModelGateway>(executor.value, std::move(router), nullptr,
                                                       mira::PriceTable{});
        gateway->register_provider(provider);
        model->gateway = gateway;
        model->profile_id = p.id;
        model->provider = provider;
        ok(host.start(std::make_shared<Binding>()));
        const auto tools = i::DesktopAtomToolset::build(
            &files, [this](const auto &cap, const auto &, const auto &) {
                return cap == "filesystem.read" && allow.load();
            });
        ok(host.attach_workflow_surface(executor.value, events, tools));
        gateway->set_event_store(events, mira::RuntimeId::generate(),
                                 *mira::SessionId::parse(host.primary_session().id));
        if (attach)
            ok(host.attach_workflow_recovery(model, options));
    }
    Run waiting() {
        mira::WorkflowDefinition def;
        def.workflow_id = mira::WorkflowId::generate();
        def.name = "controlled recovery";
        def.default_policy = mira::WorkflowPolicy::Recoverable;
        def.allowed_policies = {mira::WorkflowPolicy::Recoverable, mira::WorkflowPolicy::DryRun};
        mira::WorkflowParameterSpec secret;
        secret.name = "secret";
        secret.type = mira::WorkflowParameterType::String;
        secret.default_value = "private-parameter-marker";
        def.parameters.push_back(secret);
        mira::WorkflowStep step;
        step.id = mira::StepId::generate();
        step.kind = mira::WorkflowStepKind::ToolCall;
        step.arguments =
            Json::Object{{"tool", "desktop.filesystem.read_text"}, {"path", "/fixture/missing"}};
        def.steps.push_back(step);
        const auto published = host.publish_workflow_definition(
            mira::canonical_json_string(mira::workflow_definition_to_json(def)), "fixture");
        MIRAGE_CHECK(published.ok);
        if (!published.ok)
            throw std::runtime_error("cannot publish recovery fixture");
        const auto run =
            host.start_workflow_run(published.workflow_id, published.digest, "{}", "recoverable");
        MIRAGE_CHECK(run.ok);
        if (!run.ok)
            throw std::runtime_error("cannot start recovery fixture");
        state(run.run_id, "waiting_agent");
        return {run.run_id, step.id.to_string()};
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

void repair_and_projection() {
    Fixture f;
    const auto run = f.waiting();
    f.provider->decision = patch(run.step);
    // Mutating the caller's binding cannot release the Host's retained gateway.
    f.model->gateway.reset();
    f.gateway.reset();
    ok(f.host.start_workflow_recovery(run.id));
    const auto attempt = f.settled(run.id);
    MIRAGE_CHECK(attempt.outcome == "patched-and-resumed");
    MIRAGE_CHECK(!attempt.patch_id.empty() && !attempt.decision_digest.empty() &&
                 !attempt.model_request_id.empty());
    f.state(run.id, "completed");
    MIRAGE_CHECK(f.files.reads == 2 && f.files.successes == 1);
    const auto candidate = f.host.compile_workflow_experience(
        run.id, mira::WorkflowId::generate().to_string(), "repaired candidate");
    MIRAGE_CHECK(candidate.ok);
    MIRAGE_CHECK(candidate.definition_json.find("/fixture/good") != std::string::npos);
    MIRAGE_CHECK(candidate.definition_json.find("/fixture/missing") == std::string::npos);
    MIRAGE_CHECK(f.provider->tools_none);
    MIRAGE_CHECK(f.provider->last_text.find("private-parameter-marker") == std::string::npos);
    MIRAGE_CHECK(!f.host.start_workflow_recovery(run.id).ok);
    auto page = take(
        f.events->read({*mira::SessionId::parse(f.host.primary_session().id), std::nullopt, 1000}));
    unsigned audits = 0;
    for (const auto &e : page.events) {
        if (e.payload.type != "WorkflowRecoveryAttempted")
            continue;
        ++audits;
        const auto audit = take(mira::parse_workflow_recovery_attempted(e.payload));
        MIRAGE_CHECK(audit.run_id.to_string() == run.id && audit.ordinal == attempt.ordinal);
        MIRAGE_CHECK(audit.patch_id && audit.patch_id->to_string() == attempt.patch_id);
        MIRAGE_CHECK(e.payload.data.find("private-rationale-marker") == std::string::npos);
        MIRAGE_CHECK(e.payload.data.find("private-parameter-marker") == std::string::npos);
    }
    MIRAGE_CHECK(audits == 1);
}
void decisions_and_limits() {
    for (const std::string action : {"need_user", "cancel", "resume", "invalid", "bad_patch",
                                     "unavailable", "throw", "diagnostics"}) {
        Fixture f;
        const auto run = f.waiting();
        f.provider->decision = action == "invalid"     ? "invalid json"
                               : action == "bad_patch" ? patch(mira::StepId::generate().to_string())
                                                       : decision(action);
        f.provider->fail = action == "unavailable" || action == "diagnostics";
        f.provider->throws = action == "throw";
        f.provider->diagnostics_throw = action == "diagnostics";
        ok(f.host.start_workflow_recovery(run.id));
        const auto result = f.settled(run.id);
        if (action == "cancel") {
            f.state(run.id, "cancelled");
            MIRAGE_CHECK(result.outcome == "cancel-requested");
        } else {
            f.state(run.id, "waiting_agent");
            MIRAGE_CHECK(result.outcome == "deferred-to-host" || result.outcome == "aborted" ||
                         result.outcome == "resumed-without-patch");
        }
        if (action == "need_user") {
            const auto calls = f.provider->calls.load();
            MIRAGE_CHECK(!f.host.start_workflow_recovery(run.id).ok);
            const auto already_read = f.host.poll_workflow_recovery(run.id, result.ordinal);
            MIRAGE_CHECK(already_read.ok && !already_read.settled);
            MIRAGE_CHECK(f.provider->calls == calls);
        }
        const auto page = take(f.events->read(
            {*mira::SessionId::parse(f.host.primary_session().id), std::nullopt, 1000}));
        for (const auto &event : page.events) {
            MIRAGE_CHECK(event.payload.data.find("private-provider-error") == std::string::npos);
            MIRAGE_CHECK(event.payload.data.find("private-provider-exception") ==
                         std::string::npos);
            MIRAGE_CHECK(event.payload.data.find("private-provider-diagnostic") ==
                         std::string::npos);
        }
    }
    Fixture f({}, false);
    MIRAGE_CHECK(!f.host.attach_workflow_recovery(nullptr).ok);
    for (unsigned which = 0; which < 4; ++which) {
        r::WorkflowRecoveryOptions options;
        if (which == 0)
            options.model_call_deadline = 0ms;
        if (which == 1)
            options.model_call_deadline = 31s;
        if (which == 2)
            options.max_decision_repairs = 2;
        if (which == 3)
            options.max_tracked_runs = 0;
        MIRAGE_CHECK(!f.host.attach_workflow_recovery(f.model, options).ok);
    }
    ok(f.host.attach_workflow_recovery(f.model));
    MIRAGE_CHECK(!f.host.attach_workflow_recovery(f.model).ok);
    MIRAGE_CHECK(!f.host.start_workflow_recovery("bad").ok);
    MIRAGE_CHECK(!f.host.poll_workflow_recovery("bad").ok);
    MIRAGE_CHECK(!f.host.cancel_workflow_recovery("bad").ok);
    MIRAGE_CHECK(!f.host.start_workflow_recovery(mira::WorkflowRunId::generate().to_string()).ok);
    ok(f.host.shutdown_workflow_recovery());
    MIRAGE_CHECK(!f.host.attach_workflow_recovery(f.model).ok);
}
void cancellation_capacity_and_shutdown() {
    for (const std::string mode :
         {"cancel_attempt", "cancel_run", "shutdown", "timeout", "permissions"}) {
        r::WorkflowRecoveryOptions options;
        if (mode == "timeout")
            options.model_call_deadline = 50ms;
        Fixture f(options);
        const auto run = f.waiting();
        const auto second = mode == "cancel_attempt" ? f.waiting() : Run{};
        f.provider->decision = patch(run.step);
        if (mode != "permissions")
            f.provider->held = true;
        if (mode == "cancel_run")
            f.provider->ignore_cancel = true;
        if (mode == "permissions")
            f.allow = false;
        ok(f.host.start_workflow_recovery(run.id));
        if (mode != "timeout")
            until([&] { return f.provider->entered.load(); });
        if (mode == "cancel_attempt") {
            const auto pending = f.host.poll_workflow_recovery(run.id);
            MIRAGE_CHECK(pending.ok && !pending.settled);
            MIRAGE_CHECK(!f.host.start_workflow_recovery(run.id).ok);
            MIRAGE_CHECK(!f.host.start_workflow_recovery(second.id).ok);
            ok(f.host.cancel_workflow_recovery(run.id));
        }
        if (mode == "cancel_run") {
            MIRAGE_CHECK(f.host.cancel_workflow_run(run.id).ok);
            f.state(run.id, "cancelled");
            f.provider->held = false;
        }
        if (mode == "shutdown")
            ok(f.host.shutdown_workflow_surface());
        const auto result = f.settled(run.id);
        if (mode == "permissions") {
            std::cerr << "recovery lifecycle " << mode << ": " << result.outcome << " / "
                      << result.reason_code << '\n';
            f.state(run.id, "waiting_agent");
            MIRAGE_CHECK(f.files.successes == 0);
        } else {
            std::cerr << "recovery lifecycle " << mode << ": " << result.outcome << " / "
                      << result.reason_code << '\n';
            MIRAGE_CHECK(result.outcome == "aborted");
            MIRAGE_CHECK(f.files.successes == 0);
            if (mode == "cancel_run")
                f.state(run.id, "cancelled");
            else if (mode != "shutdown")
                f.state(run.id, "waiting_agent");
        }
        if (mode == "shutdown") {
            MIRAGE_CHECK(!f.host.start_workflow_recovery(run.id).ok);
            MIRAGE_CHECK(!f.host.workflow_run_view(run.id).ok);
            const auto page = take(f.events->read(
                {*mira::SessionId::parse(f.host.primary_session().id), std::nullopt, 1000}));
            bool terminal = false;
            for (const auto &event : page.events) {
                if (event.payload.type != "WorkflowRunSettled")
                    continue;
                const auto settled = take(mira::parse_workflow_run_settled(event.payload));
                if (settled.run_id.to_string() == run.id &&
                    settled.terminal_state == mira::WorkflowRunState::Cancelled)
                    terminal = true;
            }
            MIRAGE_CHECK(terminal);
        }
    }
}
} // namespace
int main() {
    try {
        repair_and_projection();
        decisions_and_limits();
        cancellation_capacity_and_shutdown();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return mirage::testing::finish("workflow_recovery_host_test");
}
