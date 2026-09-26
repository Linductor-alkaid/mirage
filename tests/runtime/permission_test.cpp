// M1-06 desktop permission framework verification (independent verification
// pass). Covers the stable vocabulary (capability/rule/decision names and
// their parsers), the default policy, the PermissionController verdict
// semantics (Allow and Deny never touch the confirmation hook, Confirm
// consults it exactly once), the built-in confirmation handlers and the
// request trace fields reaching the handler untouched. M5-03 (DEC-020)
// replaces the bool hook contract with ConfirmationResult + CancelProbe and
// adds the AsyncConfirmationHub surface: bounded waits, first-response-wins
// resolution, capacity rejection and probe-first cancellation.

#include "../support/test.hpp"

#include <mirage/runtime/permission/capability.hpp>
#include <mirage/runtime/permission/confirmation_hub.hpp>
#include <mirage/runtime/permission/permission.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace {

namespace permission = mirage::runtime::permission;

using permission::AllowAllConfirmation;
using permission::AsyncConfirmationHub;
using permission::CancelProbe;
using permission::Capability;
using permission::ConfirmationHandler;
using permission::ConfirmationResult;
using permission::Decision;
using permission::DenyAllConfirmation;
using permission::PermissionController;
using permission::PermissionPolicy;
using permission::PermissionRequest;
using permission::Rule;

/// Fake confirmation hook: counts invocations and records the last request,
/// so tests can assert exactly how often the gate consulted it and what the
/// handler observed. The bool form maps onto the DEC-020 result set
/// (approve/reject); the explicit form selects any outcome.
class RecordingConfirmation final : public ConfirmationHandler {
  public:
    explicit RecordingConfirmation(bool approved)
        : result_(approved ? ConfirmationResult::Approved : ConfirmationResult::Rejected) {}
    explicit RecordingConfirmation(ConfirmationResult result) : result_(result) {}

    ConfirmationResult confirm(const PermissionRequest &request, const CancelProbe &) override {
        ++calls;
        last = request;
        return result_;
    }

    int calls = 0;
    PermissionRequest last;
    ConfirmationResult result_;
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

// M2-04 clipboard directions: appended to the frozen vocabulary, so the
// stable names, the parse round-trip and the near-miss surface must all hold
// exactly like the older entries.
void scenario_clipboard_capability_vocabulary() {
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::ClipboardRead)) ==
                 "clipboard.read");
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::ClipboardWrite)) ==
                 "clipboard.write");
    MIRAGE_CHECK(permission::capability_from_name("clipboard.read") == Capability::ClipboardRead);
    MIRAGE_CHECK(permission::capability_from_name("clipboard.write") == Capability::ClipboardWrite);

    // Near misses fail closed: bare noun, slash-joined pair, truncated and
    // extended suffixes, surrounding whitespace, case tricks, hyphen and
    // unknown directions.
    MIRAGE_CHECK(!permission::capability_from_name("clipboard").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.read/write").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.rea").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.readx").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.read ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name(" clipboard.read").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.write ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("CLIPBOARD.READ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("Clipboard.Write").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.WRITE").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard-read").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.read.extra").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.writes").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("clipboard.paste").has_value());
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
    // M2-04 clipboard directions default to allow under the same DEC-015
    // decision 6 rationale as the other desktop actions.
    MIRAGE_CHECK(policy.rule_for(Capability::ClipboardRead) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::ClipboardWrite) == Rule::Allow);
    // M2-05 application / notification actions default to allow under the
    // same rationale; filesystem.write stays denied fail closed.
    MIRAGE_CHECK(policy.rule_for(Capability::ApplicationLaunch) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::ApplicationTerminate) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::NotificationPost) == Rule::Allow);

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

// M2-04: the clipboard capabilities ride the same controller verdict
// semantics, and the vocabulary stays append-only — the enumerators index
// the policy array, so their positions are part of the frozen contract.
void scenario_clipboard_policy_and_controller() {
    static_assert(static_cast<std::size_t>(Capability::ClipboardRead) == 6);
    static_assert(static_cast<std::size_t>(Capability::ClipboardWrite) == 7);
    static_assert(std::tuple_size<decltype(PermissionPolicy{}.rules)>::value == 11);

    const PermissionPolicy policy;
    MIRAGE_CHECK(policy.rule_for(Capability::ClipboardRead) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::ClipboardWrite) == Rule::Allow);
    MIRAGE_CHECK(policy.rules[static_cast<std::size_t>(Capability::ClipboardRead)] == Rule::Allow);
    MIRAGE_CHECK(policy.rules[static_cast<std::size_t>(Capability::ClipboardWrite)] == Rule::Allow);

    // Overriding by --perm-style subscript moves the verdict through the
    // controller like every other capability: deny without the hook...
    PermissionPolicy tightened;
    tightened.rules[static_cast<std::size_t>(Capability::ClipboardRead)] = Rule::Deny;
    tightened.rules[static_cast<std::size_t>(Capability::ClipboardWrite)] = Rule::Confirm;

    RecordingConfirmation approve(true);
    PermissionController controller(tightened, approve);

    PermissionRequest denied;
    denied.capability = Capability::ClipboardRead;
    denied.resource = "clipboard";
    denied.task_id = "task-clip-deny";
    denied.operation_id = "op-clip-1";
    const auto deny_verdict = controller.authorize(denied);
    MIRAGE_CHECK(!deny_verdict.allowed);
    MIRAGE_CHECK(deny_verdict.decision == Decision::Denied);
    MIRAGE_CHECK(deny_verdict.reason == "clipboard.read denied by policy");
    MIRAGE_CHECK(approve.calls == 0); // policy-only: hook untouched

    // ...and confirm through the hook exactly once, with the request fields
    // arriving untouched.
    PermissionRequest confirmed;
    confirmed.capability = Capability::ClipboardWrite;
    confirmed.resource = "clipboard";
    confirmed.task_id = "task-clip-confirm";
    confirmed.operation_id = "op-clip-2";
    const auto confirm_verdict = controller.authorize(confirmed);
    MIRAGE_CHECK(confirm_verdict.allowed);
    MIRAGE_CHECK(confirm_verdict.decision == Decision::AllowedByConfirmation);
    MIRAGE_CHECK(approve.calls == 1);
    MIRAGE_CHECK(approve.last.capability == Capability::ClipboardWrite);
    MIRAGE_CHECK(approve.last.resource == "clipboard");
    MIRAGE_CHECK(approve.last.task_id == "task-clip-confirm");
    MIRAGE_CHECK(approve.last.operation_id == "op-clip-2");

    // A rejecting hook denies the confirmed write once per call.
    RecordingConfirmation reject(false);
    PermissionController rejecting(tightened, reject);
    const auto reject_verdict = rejecting.authorize(confirmed);
    MIRAGE_CHECK(!reject_verdict.allowed);
    MIRAGE_CHECK(reject_verdict.decision == Decision::DeniedByConfirmation);
    MIRAGE_CHECK(reject_verdict.reason == "confirmation rejected for clipboard.write");
    MIRAGE_CHECK(reject.calls == 1);

    // The default policy decides both directions by policy alone.
    RecordingConfirmation untouched(true);
    PermissionController default_controller(PermissionPolicy{}, untouched);
    MIRAGE_CHECK(default_controller.authorize(denied).allowed);
    MIRAGE_CHECK(default_controller.authorize(confirmed).allowed);
    MIRAGE_CHECK(default_controller.authorize(denied).decision == Decision::Allowed);
    MIRAGE_CHECK(untouched.calls == 0);

    // Tightening the clipboard slots leaves every other slot at its default.
    MIRAGE_CHECK(tightened.rule_for(Capability::FilesystemRead) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::FilesystemWrite) == Rule::Deny);
    MIRAGE_CHECK(tightened.rule_for(Capability::ProcessExecute) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::WindowActivate) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::ScreenCapture) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::InputInject) == Rule::Allow);
}

// M2-05 application / notification actions: appended to the frozen vocabulary
// (DEC-015 decision 6), so the stable names, the parse round-trip and the
// near-miss surface must all hold exactly like the older entries — and the
// neighboring clipboard names stay untouched.
void scenario_application_notification_capability_vocabulary() {
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::ApplicationLaunch)) ==
                 "application.launch");
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::ApplicationTerminate)) ==
                 "application.terminate");
    MIRAGE_CHECK(std::string(permission::capability_name(Capability::NotificationPost)) ==
                 "notification.post");
    MIRAGE_CHECK(permission::capability_from_name("application.launch") ==
                 Capability::ApplicationLaunch);
    MIRAGE_CHECK(permission::capability_from_name("application.terminate") ==
                 Capability::ApplicationTerminate);
    MIRAGE_CHECK(permission::capability_from_name("notification.post") ==
                 Capability::NotificationPost);

    // Near misses fail closed: bare nouns, truncated and extended suffixes,
    // surrounding whitespace, case tricks, hyphens, wrong separators and
    // invented actions.
    MIRAGE_CHECK(!permission::capability_from_name("application").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("notification").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.launch!").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.launch ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name(" application.launch").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.launchx").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.lauch").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.terminate ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.terminat").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.terminatex").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.terminate.extra").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("APPLICATION.LAUNCH").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("Application.Terminate").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application-launch").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application/launch").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("notification.post ").has_value());
    MIRAGE_CHECK(!permission::capability_from_name(" notification.post").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("notification.postx").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("notification.pos").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("notification.post.extra").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("NOTIFICATION.POST").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("Notification.Post").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("notification.send").has_value());
    MIRAGE_CHECK(!permission::capability_from_name("application.launch.terminate").has_value());

    // The M2-04 clipboard names are unaffected by the append.
    MIRAGE_CHECK(permission::capability_from_name("clipboard.read") == Capability::ClipboardRead);
    MIRAGE_CHECK(permission::capability_from_name("clipboard.write") == Capability::ClipboardWrite);
}

// M2-05: the application / notification capabilities ride the same frozen
// positions and controller semantics — default allow, subscript overrides
// move through Deny and Confirm, the Confirm hook is touched exactly once,
// and tightening one slot isolates every other.
void scenario_application_notification_policy_and_controller() {
    static_assert(static_cast<std::size_t>(Capability::ApplicationLaunch) == 8);
    static_assert(static_cast<std::size_t>(Capability::ApplicationTerminate) == 9);
    static_assert(static_cast<std::size_t>(Capability::NotificationPost) == 10);
    static_assert(std::tuple_size<decltype(PermissionPolicy{}.rules)>::value == 11);

    const PermissionPolicy policy;
    MIRAGE_CHECK(policy.rule_for(Capability::ApplicationLaunch) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::ApplicationTerminate) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::NotificationPost) == Rule::Allow);
    MIRAGE_CHECK(policy.rule_for(Capability::FilesystemWrite) == Rule::Deny);
    MIRAGE_CHECK(policy.rules[static_cast<std::size_t>(Capability::ApplicationLaunch)] ==
                 Rule::Allow);
    MIRAGE_CHECK(policy.rules[static_cast<std::size_t>(Capability::ApplicationTerminate)] ==
                 Rule::Allow);
    MIRAGE_CHECK(policy.rules[static_cast<std::size_t>(Capability::NotificationPost)] ==
                 Rule::Allow);

    // Overriding by subscript moves the verdicts through the controller:
    // deny without the hook...
    PermissionPolicy tightened;
    tightened.rules[static_cast<std::size_t>(Capability::ApplicationLaunch)] = Rule::Deny;
    tightened.rules[static_cast<std::size_t>(Capability::ApplicationTerminate)] = Rule::Confirm;
    tightened.rules[static_cast<std::size_t>(Capability::NotificationPost)] = Rule::Confirm;

    RecordingConfirmation approve(true);
    PermissionController controller(tightened, approve);

    PermissionRequest denied;
    denied.capability = Capability::ApplicationLaunch;
    denied.resource = "org.gnome.Nautilus.desktop";
    denied.task_id = "task-app-deny";
    denied.operation_id = "op-app-1";
    const auto deny_verdict = controller.authorize(denied);
    MIRAGE_CHECK(!deny_verdict.allowed);
    MIRAGE_CHECK(deny_verdict.decision == Decision::Denied);
    MIRAGE_CHECK(deny_verdict.reason == "application.launch denied by policy");
    MIRAGE_CHECK(approve.calls == 0); // policy-only: hook untouched

    // ...and confirm through the hook exactly once, with the request fields
    // arriving untouched.
    PermissionRequest confirmed;
    confirmed.capability = Capability::ApplicationTerminate;
    confirmed.resource = "org.gnome.Nautilus.desktop";
    confirmed.task_id = "task-app-confirm";
    confirmed.operation_id = "op-app-2";
    const auto confirm_verdict = controller.authorize(confirmed);
    MIRAGE_CHECK(confirm_verdict.allowed);
    MIRAGE_CHECK(confirm_verdict.decision == Decision::AllowedByConfirmation);
    MIRAGE_CHECK(approve.calls == 1);
    MIRAGE_CHECK(approve.last.capability == Capability::ApplicationTerminate);
    MIRAGE_CHECK(approve.last.resource == "org.gnome.Nautilus.desktop");
    MIRAGE_CHECK(approve.last.task_id == "task-app-confirm");
    MIRAGE_CHECK(approve.last.operation_id == "op-app-2");

    // A rejecting hook denies the confirmed notification once per call.
    PermissionRequest rejected;
    rejected.capability = Capability::NotificationPost;
    rejected.resource = "title";
    rejected.task_id = "task-notify-reject";
    rejected.operation_id = "op-app-3";
    RecordingConfirmation deny_hook(false);
    PermissionController rejecting_controller(tightened, deny_hook);
    const auto reject_verdict = rejecting_controller.authorize(rejected);
    MIRAGE_CHECK(!reject_verdict.allowed);
    MIRAGE_CHECK(reject_verdict.decision == Decision::DeniedByConfirmation);
    MIRAGE_CHECK(reject_verdict.reason == "confirmation rejected for notification.post");
    MIRAGE_CHECK(deny_hook.calls == 1);

    // The default policy decides all three by policy alone.
    RecordingConfirmation untouched(true);
    PermissionController default_controller(PermissionPolicy{}, untouched);
    MIRAGE_CHECK(default_controller.authorize(denied).allowed);
    MIRAGE_CHECK(default_controller.authorize(confirmed).allowed);
    MIRAGE_CHECK(default_controller.authorize(rejected).allowed);
    MIRAGE_CHECK(default_controller.authorize(denied).decision == Decision::Allowed);
    MIRAGE_CHECK(untouched.calls == 0);

    // Tightening the three new slots leaves every earlier slot at its default
    // (slot isolation across the append boundary).
    MIRAGE_CHECK(tightened.rule_for(Capability::FilesystemRead) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::FilesystemWrite) == Rule::Deny);
    MIRAGE_CHECK(tightened.rule_for(Capability::ProcessExecute) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::WindowActivate) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::ScreenCapture) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::InputInject) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::ClipboardRead) == Rule::Allow);
    MIRAGE_CHECK(tightened.rule_for(Capability::ClipboardWrite) == Rule::Allow);
    // ...and vice versa: tightening an M2-04 slot leaves the M2-05 slots.
    PermissionPolicy clip_tightened;
    clip_tightened.rules[static_cast<std::size_t>(Capability::ClipboardWrite)] = Rule::Deny;
    MIRAGE_CHECK(clip_tightened.rule_for(Capability::ApplicationLaunch) == Rule::Allow);
    MIRAGE_CHECK(clip_tightened.rule_for(Capability::ApplicationTerminate) == Rule::Allow);
    MIRAGE_CHECK(clip_tightened.rule_for(Capability::NotificationPost) == Rule::Allow);
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
    MIRAGE_CHECK(deny_all.confirm(request, CancelProbe{}) == ConfirmationResult::Rejected);
    MIRAGE_CHECK(allow_all.confirm(request, CancelProbe{}) == ConfirmationResult::Approved);
}

// --- DEC-020 async confirmation surface --------------------------------------

/// Every non-approve hook outcome converges to the same wire decision with
/// its own stable reason string; the probe never fires by default.
void scenario_confirmation_outcome_reasons() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::FilesystemRead)] = Rule::Confirm;

    struct Wanted {
        const char *reason;
        ConfirmationResult result;
    };
    const Wanted wanted[] = {
        {"confirmation rejected for filesystem.read", ConfirmationResult::Rejected},
        {"confirmation timed out for filesystem.read", ConfirmationResult::TimedOut},
        {"confirmation unavailable for filesystem.read", ConfirmationResult::Unresolved},
        {"confirmation cancelled for filesystem.read", ConfirmationResult::Cancelled},
    };
    for (const auto &expectation : wanted) {
        RecordingConfirmation handler(expectation.result);
        PermissionController controller(policy, handler);
        PermissionRequest request;
        request.capability = Capability::FilesystemRead;
        const auto verdict = controller.authorize(request);
        MIRAGE_CHECK(!verdict.allowed);
        MIRAGE_CHECK(verdict.decision == Decision::DeniedByConfirmation);
        MIRAGE_CHECK(verdict.reason == expectation.reason);
        MIRAGE_CHECK(handler.calls == 1);
    }
}

/// Probe-first (DEC-020): a caller already asked to stop never raises a
/// confirmation request, so the hook is not consulted at all.
void scenario_probe_fires_before_hook() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::ProcessExecute)] = Rule::Confirm;
    RecordingConfirmation handler(true);
    PermissionController controller(policy, handler);
    PermissionRequest request;
    request.capability = Capability::ProcessExecute;
    const auto verdict = controller.authorize(request, [] { return true; });
    MIRAGE_CHECK(!verdict.allowed);
    MIRAGE_CHECK(verdict.decision == Decision::DeniedByConfirmation);
    MIRAGE_CHECK(verdict.reason == "confirmation cancelled for process.execute");
    MIRAGE_CHECK(handler.calls == 0);
}

/// No probe (the M1 call shape) still reaches the hook: the empty probe
/// never fires, so existing callers keep their behavior.
void scenario_empty_probe_reaches_hook() {
    PermissionPolicy policy;
    policy.rules[static_cast<std::size_t>(Capability::FilesystemWrite)] = Rule::Confirm;
    RecordingConfirmation handler(true);
    PermissionController controller(policy, handler);
    PermissionRequest request;
    request.capability = Capability::FilesystemWrite;
    const auto verdict = controller.authorize(request);
    MIRAGE_CHECK(verdict.allowed);
    MIRAGE_CHECK(verdict.decision == Decision::AllowedByConfirmation);
    MIRAGE_CHECK(handler.calls == 1);
}

/// The hub resolves an approval raised inside the wait: the "responder" is
/// simulated from within the probe the wait loop polls (the test discipline
/// forbids spawning threads; the probe is a legal rendezvous point).
void scenario_hub_approve_resolution() {
    AsyncConfirmationHub hub(std::chrono::milliseconds{5000});
    std::optional<permission::PendingConfirmation> raised;
    hub.set_publish_hook(
        [&raised](const permission::PendingConfirmation &pending) { raised = pending; });
    PermissionRequest request;
    request.capability = Capability::FilesystemWrite;
    request.resource = "/tmp/out.txt";
    request.task_id = "task-hub-approve";
    const auto probe = [&hub]() -> bool {
        const auto views = hub.pending();
        if (!views.empty()) {
            MIRAGE_CHECK(views.size() == 1);
            return hub.resolve(views.front().request_id, true) ? false : true;
        }
        return false;
    };
    MIRAGE_CHECK(hub.confirm(request, probe) == ConfirmationResult::Approved);
    MIRAGE_CHECK(raised.has_value());
    if (raised) {
        MIRAGE_CHECK(raised->capability == "filesystem.write");
        MIRAGE_CHECK(raised->resource == "/tmp/out.txt");
        MIRAGE_CHECK(raised->task_id == "task-hub-approve");
        MIRAGE_CHECK(raised->request_id.rfind("perm-", 0) == 0);
    }
    MIRAGE_CHECK(hub.pending().empty());
}

/// Same resolution shape for an explicit rejection, plus the decided-id
/// surface: a second resolve (or an unknown id) is not a winner.
void scenario_hub_reject_resolution() {
    AsyncConfirmationHub hub(std::chrono::milliseconds{5000});
    std::string raised_id;
    hub.set_publish_hook([&raised_id](const permission::PendingConfirmation &pending) {
        raised_id = pending.request_id;
    });
    PermissionRequest request;
    request.capability = Capability::ProcessExecute;
    request.resource = "rm -rf /tmp/stale-build";
    request.task_id = "task-hub-reject";
    const auto probe = [&hub]() -> bool {
        const auto views = hub.pending();
        if (!views.empty()) {
            hub.resolve(views.front().request_id, false);
        }
        return false;
    };
    MIRAGE_CHECK(hub.confirm(request, probe) == ConfirmationResult::Rejected);
    MIRAGE_CHECK(!raised_id.empty());
    MIRAGE_CHECK(hub.pending().empty());
    MIRAGE_CHECK(!hub.resolve(raised_id, true)); // decided: not a winner
    MIRAGE_CHECK(!hub.resolve("perm-no-such", true));
}

/// The wait budget converges fail closed: with no responder the wait ends
/// TimedOut and the pending set is cleaned up.
void scenario_hub_timeout_fails_closed() {
    AsyncConfirmationHub hub(std::chrono::milliseconds{30});
    PermissionRequest request;
    request.capability = Capability::ClipboardWrite;
    request.task_id = "task-hub-timeout";
    const auto started = std::chrono::steady_clock::now();
    MIRAGE_CHECK(hub.confirm(request, CancelProbe{}) == ConfirmationResult::TimedOut);
    const auto waited = std::chrono::steady_clock::now() - started;
    MIRAGE_CHECK(waited >= std::chrono::milliseconds{30});
    MIRAGE_CHECK(waited < std::chrono::seconds{5});
    MIRAGE_CHECK(hub.pending().empty());
}

/// The probe outranks the wait: a cancelled caller retracts the raised
/// request and returns Cancelled.
void scenario_hub_cancelled_by_probe() {
    AsyncConfirmationHub hub(std::chrono::milliseconds{5000});
    PermissionRequest request;
    request.capability = Capability::FilesystemRead;
    request.task_id = "task-hub-cancel";
    const auto probe = [] { return true; };
    MIRAGE_CHECK(hub.confirm(request, probe) == ConfirmationResult::Cancelled);
    MIRAGE_CHECK(hub.pending().empty());
}

/// Capacity is explicit (RULE-07): a second request raised while one is
/// pending fails closed as Unresolved without disturbing the first.
void scenario_hub_capacity_fails_closed() {
    AsyncConfirmationHub hub(std::chrono::milliseconds{5000}, /*max_pending=*/1);
    std::optional<ConfirmationResult> nested;
    PermissionRequest first;
    first.capability = Capability::FilesystemRead;
    first.task_id = "task-hub-cap-1";
    PermissionRequest second;
    second.capability = Capability::FilesystemWrite;
    second.task_id = "task-hub-cap-2";
    const auto probe = [&]() -> bool {
        if (nested.has_value()) {
            return false;
        }
        const auto views = hub.pending();
        if (views.empty()) {
            // confirm()'s probe-first entry check: nothing raised yet.
            return false;
        }
        // The outer request is pending: simulate the concurrent second
        // driver, which the surface at capacity refuses instead of queueing.
        nested = hub.confirm(second, CancelProbe{});
        const auto after = hub.pending();
        MIRAGE_CHECK(after.size() == 1);
        if (after.size() == 1) {
            MIRAGE_CHECK(after.front().task_id == "task-hub-cap-1");
            hub.resolve(after.front().request_id, true);
        }
        return false;
    };
    MIRAGE_CHECK(hub.confirm(first, probe) == ConfirmationResult::Approved);
    MIRAGE_CHECK(nested.has_value() && *nested == ConfirmationResult::Unresolved);
    MIRAGE_CHECK(hub.pending().empty());
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[permission_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("capability_names_and_parsing", scenario_capability_names_and_parsing);
    run_scenario("clipboard_capability_vocabulary", scenario_clipboard_capability_vocabulary);
    run_scenario("application_notification_capability_vocabulary",
                 scenario_application_notification_capability_vocabulary);
    run_scenario("rule_names_and_parsing", scenario_rule_names_and_parsing);
    run_scenario("decision_names", scenario_decision_names);
    run_scenario("default_policy", scenario_default_policy);
    run_scenario("desktop_capability_controller_semantics",
                 scenario_desktop_capability_controller_semantics);
    run_scenario("clipboard_policy_and_controller", scenario_clipboard_policy_and_controller);
    run_scenario("application_notification_policy_and_controller",
                 scenario_application_notification_policy_and_controller);
    run_scenario("allow_never_consults_confirmation", scenario_allow_never_consults_confirmation);
    run_scenario("deny_never_consults_confirmation", scenario_deny_never_consults_confirmation);
    run_scenario("confirm_approved_calls_handler_once",
                 scenario_confirm_approved_calls_handler_once);
    run_scenario("confirm_rejected_calls_handler_once",
                 scenario_confirm_rejected_calls_handler_once);
    run_scenario("request_reaches_handler_untouched", scenario_request_reaches_handler_untouched);
    run_scenario("builtin_confirmation_handlers", scenario_builtin_confirmation_handlers);
    run_scenario("confirmation_outcome_reasons", scenario_confirmation_outcome_reasons);
    run_scenario("probe_fires_before_hook", scenario_probe_fires_before_hook);
    run_scenario("empty_probe_reaches_hook", scenario_empty_probe_reaches_hook);
    run_scenario("hub_approve_resolution", scenario_hub_approve_resolution);
    run_scenario("hub_reject_resolution", scenario_hub_reject_resolution);
    run_scenario("hub_timeout_fails_closed", scenario_hub_timeout_fails_closed);
    run_scenario("hub_cancelled_by_probe", scenario_hub_cancelled_by_probe);
    run_scenario("hub_capacity_fails_closed", scenario_hub_capacity_fails_closed);
    return mirage::testing::finish("permission_test");
}
