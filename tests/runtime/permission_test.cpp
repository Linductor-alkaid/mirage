// M1-06 desktop permission framework verification (independent verification
// pass). Covers the stable vocabulary (capability/rule/decision names and
// their parsers), the default policy, the PermissionController verdict
// semantics (Allow and Deny never touch the confirmation hook, Confirm
// consults it exactly once), the built-in confirmation handlers and the
// request trace fields reaching the handler untouched.

#include "../support/test.hpp"

#include <mirage/runtime/permission/capability.hpp>
#include <mirage/runtime/permission/permission.hpp>

#include <string>
#include <utility>

namespace {

namespace permission = mirage::runtime::permission;

using permission::AllowAllConfirmation;
using permission::Capability;
using permission::ConfirmationHandler;
using permission::Decision;
using permission::DenyAllConfirmation;
using permission::PermissionController;
using permission::PermissionPolicy;
using permission::PermissionRequest;
using permission::Rule;

/// Fake confirmation hook: counts invocations and records the last request,
/// so tests can assert exactly how often the gate consulted it and what the
/// handler observed.
class RecordingConfirmation final : public ConfirmationHandler {
  public:
    explicit RecordingConfirmation(bool verdict) : verdict_(verdict) {}

    bool confirm(const PermissionRequest &request) override {
        ++calls;
        last = request;
        return verdict_;
    }

    int calls = 0;
    PermissionRequest last;
    bool verdict_;
};

// --- stable vocabulary -------------------------------------------------------

void scenario_capability_names_and_parsing() {
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::FilesystemRead)) ==
                 "filesystem.read");
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::FilesystemWrite)) ==
                 "filesystem.write");
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::ProcessExecute)) ==
                 "process.execute");

    MIRAGE_CHECK(permission::capability_from_name("filesystem.read") == Capability::FilesystemRead);
    MIRAGE_CHECK(permission::capability_from_name("filesystem.write") ==
                 Capability::FilesystemWrite);
    MIRAGE_CHECK(permission::capability_from_name("process.execute") == Capability::ProcessExecute);

    // Anything else fails closed: empty, partial, oversized, wrong case.
    MIRAGE_CHECK(!permission::capability_from_name("").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("filesystem").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("process").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("filesystem.readx").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("process.execute ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("FILESYSTEM.READ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("Process.Execute").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("window.click").has_value());
}

void scenario_rule_names_and_parsing() {
    MIRAGE_CHECK(std::string(permission::rule_name(Rule::Allow)) == "allow");
    MIRAGE_CHECK(std::string(permission::rule_name(Rule::Confirm)) == "confirm");
    MIRAGE_CHECK(std::string(permission::rule_name(Rule::Deny)) == "deny");

    MIRAGE_CHECK(permission::rule_from_name("allow") == Rule::Allow);
    MIRAGE_CHECK(permission::rule_from_name("confirm") == Rule::Confirm);
    MIRAGE_CHECK(permission::rule_from_name("deny") == Rule::Deny);

    MIRAGE_CHECK(!permission::rule_from_name("").has_value());
    MIRAGE_CHECK(!permission::rule_from_name("ALLOW").has_value());
    MIRAGE_CHECK(!permission::rule_from_name("allowall").has_value());
    MIRAGE_CHECK(!permission::rule_from_name("yes").has_value());
    MIRAGE_CHECK(!permission::rule_from_name("confirn").has_value());
}

void scenario_decision_names() {
    MIRAGE_CHECK(std::string(permission::decision_name(Decision::Allowed)) == "allowed");
    MIRAGE_CHECK(std::string(permission::decision_name(Decision::AllowedByConfirmation)) ==
                 "confirmed");
    MIRAGE_CHECK(std::string(permission::decision_name(Decision::Denied)) == "denied");
    MIRAGE_CHECK(std::string(permission::decision_name(Decision::DeniedByConfirmation)) ==
                 "confirmation_rejected");
}

// --- policy ------------------------------------------------------------------

void scenario_default_policy() {
    const PermissionPolicy policy;
    MIRAGE_CHECK(policy.rule_for(Capability::FilesystemRead) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::FilesystemWrite) == Rule::Deny);
    MIRAGE_CHECK(policy.rule_for(Capability::ProcessExecute) == Rule::Allow);

    // The rules array stays indexable by capability; writing one slot must
    // not disturb the others.
    PermissionPolicy customized;
    customized.rules[static_cast<std::size_t>(Capability::ProcessExecute)] = Rule::Deny;
    customized.rules[static_cast<std::size_t>(Capability::FilesystemWrite)] = Rule::Confirm;
    MIRAGE_CHECK(customized.rule_for(Capability::FilesystemRead) == Rule::Allow);
    MIRAGE_CHECK(customized.rule_for(Capability::FilesystemWrite) == Rule::Confirm);
    MIRAGE_CHECK(customized.rule_for(Capability::ProcessExecute) == Rule::Deny);
}

// --- controller semantics ----------------------------------------------------

void scenario_allow_never_consults_confirmation() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::ProcessExecute)] = Rule::Allow;
    RecordingConfirmation handler(false);
    PermissionController controller(policy, handler);

    PermissionRequest request;
    request.capability = Capability::ProcessExecute;
    request.resource = "printf hi";
    request.task_id = "task-allow";
    request.operation_id = "op-1";

    const permission::PermissionVerdict verdict = controller.authorize(request);
    MIRAGE_CHECK(verdict.allowed);
    MIRAGE_CHECK(verdict.decision == Decision::Allowed);
    MIRAGE_CHECK(verdict.reason.empty());
    // An Allow rule is decided by policy alone: the hook stays untouched.
    MIRAGE_CHECK(handler.calls == 0);
}

void scenario_deny_never_consults_confirmation() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::ProcessExecute)] = Rule::Deny;
    RecordingConfirmation handler(true); // even an approving hook must not run
    PermissionController controller(policy, handler);

    PermissionRequest request;
    request.capability = Capability::ProcessExecute;
    request.resource = "rm -rf /";
    request.task_id = "task-deny";
    request.operation_id = "op-2";

    const permission::PermissionVerdict verdict = controller.authorize(request);
    MIRAGE_CHECK(!verdict.allowed);
    MIRAGE_CHECK(verdict.decision == Decision::Denied);
    MIRAGE_CHECK(handler.calls == 0);
    // Stable, UI-safe reason carrying the capability name.
    MIRAGE_CHECK(verdict.reason == "process.execute denied by policy");
    MIRAGE_CHECK(verdict.reason.find("process.execute") != std::string::npos);
}

void scenario_confirm_approved_calls_handler_once() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::FilesystemRead)] = Rule::Confirm;
    RecordingConfirmation handler(true);
    PermissionController controller(policy, handler);

    PermissionRequest request;
    request.capability = Capability::FilesystemRead;
    request.resource = "/tmp/notes.txt";
    request.task_id = "task-confirm";
    request.operation_id = "op-3";

    const permission::PermissionVerdict verdict = controller.authorize(request);
    MIRAGE_CHECK(verdict.allowed);
    MIRAGE_CHECK(verdict.decision == Decision::AllowedByConfirmation);
    MIRAGE_CHECK(verdict.reason.empty());
    MIRAGE_CHECK(handler.calls == 1);
}

void scenario_confirm_rejected_calls_handler_once() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::FilesystemRead)] = Rule::Confirm;
    RecordingConfirmation handler(false);
    PermissionController controller(policy, handler);

    PermissionRequest request;
    request.capability = Capability::FilesystemRead;
    request.resource = "/tmp/secrets.txt";
    request.task_id = "task-reject";
    request.operation_id = "op-4";

    const permission::PermissionVerdict verdict = controller.authorize(request);
    MIRAGE_CHECK(!verdict.allowed);
    MIRAGE_CHECK(verdict.decision == Decision::DeniedByConfirmation);
    MIRAGE_CHECK(handler.calls == 1);
    MIRAGE_CHECK(verdict.reason == "confirmation rejected for filesystem.read");
}

void scenario_request_reaches_handler_untouched() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::FilesystemWrite)] = Rule::Confirm;
    RecordingConfirmation handler(true);
    PermissionController controller(policy, handler);

    PermissionRequest request;
    request.capability = Capability::FilesystemWrite;
    request.resource = "/tmp/out.txt";
    request.task_id = "0123456789abcdef0123456789abcdef";
    request.operation_id = "fedcba9876543210fedcba9876543210";

    (void)controller.authorize(request);
    MIRAGE_CHECK(handler.calls == 1);
    // The trace fields (RULE-05) arrive exactly as the caller framed them.
    MIRAGE_CHECK(handler.last.capability == Capability::FilesystemWrite);
    MIRAGE_CHECK(handler.last.resource == "/tmp/out.txt");
    MIRAGE_CHECK(handler.last.task_id == "0123456789abcdef0123456789abcdef");
    MIRAGE_CHECK(handler.last.operation_id == "fedcba9876543210fedcba9876543210");
}

// --- built-in confirmation handlers ------------------------------------------

void scenario_builtin_confirmation_handlers() {
    DenyAllConfirmation deny_all;
    AllowAllConfirmation allow_all;
    const PermissionRequest request;
    MIRAGE_CHECK(!deny_all.confirm(request));
    MIRAGE_CHECK(allow_all.confirm(request));
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[permission_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("capability_names_and_parsing", scenario_capability_names_and_parsing);
    run_scenario("rule_names_and_parsing", scenario_rule_names_and_parsing);
    run_scenario("decision_names", scenario_decision_names);
    run_scenario("default_policy", scenario_default_policy);
    run_scenario("allow_never_consults_confirmation", scenario_allow_never_consults_confirmation);
    run_scenario("deny_never_consults_confirmation", scenario_deny_never_consults_confirmation);
    run_scenario("confirm_approved_calls_handler_once",
                 scenario_confirm_approved_calls_handler_once);
    run_scenario("confirm_rejected_calls_handler_once",
                 scenario_confirm_rejected_calls_handler_once);
    run_scenario("request_reaches_handler_untouched", scenario_request_reaches_handler_untouched);
    run_scenario("builtin_confirmation_handlers", scenario_builtin_confirmation_handlers);
    return mirage::testing::finish("permission_test");
}
