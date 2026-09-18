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
    // M2-02 desktop actions (DEC-015): appended to the frozen vocabulary.
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::WindowActivate)) ==
                 "window.activate");
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::ScreenCapture)) ==
                 "screen.capture");
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::InputInject)) ==
                 "input.inject");

    MIRAGE_CHECK(permission::capability_from_name("filesystem.read") == Capability::FilesystemRead);
    MIRAGE_CHECK(permission::capability_from_name("filesystem.write") ==
                 Capability::FilesystemWrite);
    MIRAGE_CHECK(permission::capability_from_name("process.execute") == Capability::ProcessExecute);
    MIRAGE_CHECK(permission::capability_from_name("window.activate") == Capability::WindowActivate);
    MIRAGE_CHECK(permission::capability_from_name("screen.capture") == Capability::ScreenCapture);
    MIRAGE_CHECK(permission::capability_from_name("input.inject") == Capability::InputInject);

    // Anything else fails closed: empty, partial, oversized, wrong case.
    MIRAGE_CHECK(!permission::capability_from_name("").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("filesystem").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("process").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("filesystem.readx").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("process.execute ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("FILESYSTEM.READ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("Process.Execute").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("window.click").has_value());
    // New names are exact: near misses and case tricks must not parse.
    MIRAGE_CHECK(!permission::capability_from_name("window").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("screen").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("input").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("window.activat").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("screen.capturex").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("input.inject ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("Window.Activate").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("SCREEN.CAPTURE").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("Input.Inject").has_value());
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
    // M2-02 desktop actions default to allow (DEC-015): the Observation ->
    // Action -> Observation loop is the milestone's purpose; tightening is
    // explicit configuration.
    MIRAGE_CHECK(policy.rule_for(Capability::WindowActivate) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::ScreenCapture) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::InputInject) == Rule::Allow);

    // The rules array stays indexable by capability; writing one slot must
    // not disturb the others.
    PermissionPolicy customized;
    customized.rules[static_cast<std::size_t>(Capability::ProcessExecute)] = Rule::Deny;
    customized.rules[static_cast<std::size_t>(Capability::FilesystemWrite)] = Rule::Confirm;
    MIRAGE_CHECK(customized.rule_for(Capability::FilesystemRead) == Rule::Allow);
    MIRAGE_CHECK(customized.rule_for(Capability::FilesystemWrite) == Rule::Confirm);
    MIRAGE_CHECK(customized.rule_for(Capability::ProcessExecute) == Rule::Deny);
    // Tightening a desktop capability leaves the rest of the new slots and
    // the M1 slots untouched.
    customized.rules[static_cast<std::size_t>(Capability::InputInject)] = Rule::Deny;
    MIRAGE_CHECK(customized.rule_for(Capability::WindowActivate) == Rule::Allow);
    MIRAGE_CHECK(customized.rule_for(Capability::ScreenCapture) == Rule::Allow);
    MIRAGE_CHECK(customized.rule_for(Capability::InputInject) == Rule::Deny);
    MIRAGE_CHECK(customized.rule_for(Capability::FilesystemRead) == Rule::Allow);
}

/// The M2-02 desktop capabilities flow through the same controller verdict
/// semantics as the M1 vocabulary: policy-only decisions never touch the
/// confirmation hook, and the reason strings carry the stable name.
void scenario_desktop_capability_controller_semantics() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::ScreenCapture)] = Rule::Deny;
    policy.rules[static_cast<std::size_t>(Capability::WindowActivate)] = Rule::Confirm;
    policy.rules[static_cast<std::size_t>(Capability::InputInject)] = Rule::Confirm;

    RecordingConfirmation approve(true);
    PermissionController controller(policy, approve);

    PermissionRequest denied;
    denied.capability = Capability::ScreenCapture;
    denied.resource = "display:0";
    denied.task_id = "task-cap-deny";
    denied.operation_id = "op-cap-1";
    const auto deny_verdict = controller.authorize(denied);
    MIRAGE_CHECK(!deny_verdict.allowed);
    MIRAGE_CHECK(deny_verdict.decision == Decision::Denied);
    MIRAGE_CHECK(deny_verdict.reason == "screen.capture denied by policy");
    MIRAGE_CHECK(approve.calls == 0); // policy-only: hook untouched

    PermissionRequest confirmed;
    confirmed.capability = Capability::WindowActivate;
    confirmed.resource = "window:4194305";
    confirmed.task_id = "task-cap-confirm";
    confirmed.operation_id = "op-cap-2";
    const auto confirm_verdict = controller.authorize(confirmed);
    MIRAGE_CHECK(confirm_verdict.allowed);
    MIRAGE_CHECK(confirm_verdict.decision == Decision::AllowedByConfirmation);
    MIRAGE_CHECK(approve.calls == 1);

    PermissionRequest rejected;
    rejected.capability = Capability::InputInject;
    rejected.resource = "type_text";
    rejected.task_id = "task-cap-reject";
    rejected.operation_id = "op-cap-3";
    RecordingConfirmation deny_hook(false);
    PermissionController rejecting_controller(policy, deny_hook);
    const auto reject_verdict = rejecting_controller.authorize(rejected);
    MIRAGE_CHECK(!reject_verdict.allowed);
    MIRAGE_CHECK(reject_verdict.decision == Decision::DeniedByConfirmation);
    MIRAGE_CHECK(reject_verdict.reason == "confirmation rejected for input.inject");
    MIRAGE_CHECK(deny_hook.calls == 1);

    // Default-policy desktop capabilities are decided by policy alone.
    PermissionController default_controller(PermissionPolicy{}, approve);
    PermissionRequest allowed;
    allowed.capability = Capability::ScreenCapture;
    allowed.resource = "display:0";
    const auto allow_verdict = default_controller.authorize(allowed);
    MIRAGE_CHECK(allow_verdict.allowed);
    MIRAGE_CHECK(allow_verdict.decision == Decision::Allowed);
    MIRAGE_CHECK(approve.calls == 1); // unchanged by the policy-only allow
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
    run_scenario("desktop_capability_controller_semantics",
                 scenario_desktop_capability_controller_semantics);
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
