#include "../support/workflow_recovery_fixture.hpp"

namespace {
void explicit_response_modes() {
    for (const std::string mode : {"text", "json_object"}) {
        for (const bool valid : {true, false}) {
            r::WorkflowRecoveryOptions options;
            options.decision_output_mode = mode;
            options.max_output_tokens = 1024;
            Fixture f(options);
            const auto run = f.waiting();
            f.provider->decision = valid ? patch(run.step) : "not a JSON decision";
            f.start_recovery(run.id);
            const auto settled = f.settled(run.id);
            MIRAGE_CHECK(f.provider->output_tokens == 1024);
            MIRAGE_CHECK(f.provider->output_mode ==
                         static_cast<int>(mode == "text" ? mira::OutputMode::Text
                                                         : mira::OutputMode::JsonObject));
            MIRAGE_CHECK(f.provider->last_text.find("Decision JSON schema") != std::string::npos);
            MIRAGE_CHECK(f.provider->tools_none);
            if (valid) {
                MIRAGE_CHECK(settled.outcome == "patched-and-resumed");
                f.state(run.id, "completed");
                MIRAGE_CHECK(f.files.successes == 1);
            } else {
                MIRAGE_CHECK(settled.outcome == "deferred-to-host");
                f.state(run.id, "waiting_agent");
                MIRAGE_CHECK(f.files.successes == 0);
            }
        }
    }
    Fixture f({}, false);
    for (unsigned n = 0; n < 4; ++n) {
        r::WorkflowRecoveryOptions options;
        if (n == 0)
            options.decision_output_mode = "function_tool";
        if (n == 1)
            options.max_output_tokens = 127;
        if (n == 2)
            options.max_output_tokens = 8193;
        if (n == 3)
            options.thinking = "invalid";
        MIRAGE_CHECK(!f.host.attach_workflow_recovery(f.model, options).ok);
    }
}

void repair_and_projection() {
    Fixture f;
    const auto run = f.waiting();
    f.provider->decision = patch(run.step);
    // Mutating the caller's binding cannot release the Host's retained gateway.
    f.model->gateway.reset();
    f.gateway.reset();
    f.start_recovery(run.id);
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
        f.start_recovery(run.id);
        const auto result = f.settled(run.id);
        if (action == "cancel") {
            if (result.outcome != "cancel-requested")
                throw std::runtime_error("cancel decision settled as " + result.outcome + " / " +
                                         result.reason_code);
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
        f.start_recovery(run.id);
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
        explicit_response_modes();
        repair_and_projection();
        decisions_and_limits();
        cancellation_capacity_and_shutdown();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
    return mirage::testing::finish("workflow_recovery_host_test");
}
