#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <mirage/runtime/permission/capability.hpp>

namespace mirage::runtime::permission {

/// Policy rule for one capability (design doc section 15): allowed outright,
/// gated behind user confirmation, or denied outright.
enum class Rule {
    Allow,
    Confirm,
    Deny,
};

/// Stable string form of a rule ("allow" / "confirm" / "deny").
const char *rule_name(Rule rule);

/// Parses a stable rule name; nullopt for anything else.
std::optional<Rule> rule_from_name(std::string_view name);

/// Per-capability policy, indexed by Capability. Defaults keep the M1
/// development topology working (DEC-010): reads and shell execution are
/// allowed outright — always still inside the provider's hard PathScope and
/// budget boundaries — while filesystem.write, which no M1 provider exposes,
/// is denied fail closed. The M2 desktop actions default to allow for the
/// same reason (DEC-015 decision 6): the Observation -> Action -> Observation
/// loop is the M2 milestone's purpose, and tightening is explicit
/// configuration until the product-level policy surface lands (M5). M2-04
/// appends the clipboard directions and M2-05 the application /
/// notification actions under the same default: application.launch is
/// bounded to a desktop entry's fixed command (narrower than the already
/// allowed process.execute), application.terminate only signals a live
/// instance without forcing a kill, and notification.post posts user-facing
/// notifications.
struct PermissionPolicy {
    std::array<Rule, 11> rules{
        Rule::Allow, // filesystem.read
        Rule::Deny,  // filesystem.write
        Rule::Allow, // process.execute
        Rule::Allow, // window.activate
        Rule::Allow, // screen.capture
        Rule::Allow, // input.inject
        Rule::Allow, // clipboard.read
        Rule::Allow, // clipboard.write
        Rule::Allow, // application.launch
        Rule::Allow, // application.terminate
        Rule::Allow, // notification.post
    };

    Rule rule_for(Capability capability) const {
        return rules[static_cast<std::size_t>(capability)];
    }
};

/// One permission judgment request. `task_id` and `operation_id` tie the
/// decision to the agent trace (RULE-05); `resource` is the path or command
/// line the capability would act on, already size-capped by the request
/// surface that produced it.
struct PermissionRequest {
    Capability capability = Capability::FilesystemRead;
    std::string resource;
    std::string task_id;
    std::string operation_id;
};

/// User confirmation hook (design doc section 15, DEC-010 / DEC-020). The
/// M1 surface was synchronous and in-process ("return promptly, never block
/// on interactive input"); M5-03 (DEC-020) replaces that contract: an
/// implementation may wait for an external answer, but the wait must be
/// bounded by its own budget, must poll the caller's cancellation probe and
/// must converge to a stable result — silence never approves.
enum class ConfirmationResult {
    /// The user approved within the hook's contract.
    Approved,
    /// The user explicitly rejected.
    Rejected,
    /// The hook's wait budget elapsed without an answer (fail closed).
    TimedOut,
    /// The surface cannot take the request (for example at capacity) —
    /// fail closed without raising it.
    Unresolved,
    /// The caller's cancellation probe fired before the outcome converged.
    Cancelled,
};

/// Cancellation probe the hook polls while waiting (DEC-020): true when the
/// calling task has been asked to stop. An empty probe never fires.
using CancelProbe = std::function<bool()>;

class ConfirmationHandler {
  public:
    virtual ~ConfirmationHandler() = default;

    /// Judges one requested use of the capability. Implementations must
    /// return a bounded result and observe `cancelled` while waiting.
    virtual ConfirmationResult confirm(const PermissionRequest &request,
                                       const CancelProbe &cancelled) = 0;
};

/// Fail-closed default for headless topologies: every confirmation request
/// is rejected (DEC-010).
class DenyAllConfirmation final : public ConfirmationHandler {
  public:
    ConfirmationResult confirm(const PermissionRequest &request,
                               const CancelProbe &cancelled) override;
};

/// Development/test handler that approves every confirmation request.
class AllowAllConfirmation final : public ConfirmationHandler {
  public:
    ConfirmationResult confirm(const PermissionRequest &request,
                               const CancelProbe &cancelled) override;
};

/// What the gate decided for one request.
enum class Decision {
    /// The policy allows the capability outright.
    Allowed,
    /// The policy asked for confirmation and the user approved.
    AllowedByConfirmation,
    /// The policy denies the capability outright.
    Denied,
    /// The policy asked for confirmation and the user rejected.
    DeniedByConfirmation,
};

/// Stable string form of a decision ("allowed" / "confirmed" / "denied" /
/// "confirmation_rejected"); never null.
const char *decision_name(Decision decision);

/// Result of one authorize() call: `allowed` is the executable answer;
/// `decision` is the full outcome for the trace; `reason` carries a stable,
/// UI-safe denial summary (empty when allowed).
struct PermissionVerdict {
    bool allowed = false;
    Decision decision = Decision::Denied;
    std::string reason;
};

/// The RULE-05 gate between an intended desktop action and the desktop
/// environment: pure policy values plus the confirmation hook — no threads,
/// no I/O, no platform types. It stacks on top of the provider-side hard
/// boundaries (PathScope, budgets, cancellation), which stay in force
/// regardless of the verdict (DEC-009, DEC-010).
class PermissionController {
  public:
    /// `confirmation` must outlive this controller.
    PermissionController(PermissionPolicy policy, ConfirmationHandler &confirmation);

    /// Judges one request. A Confirm rule consults the confirmation handler
    /// exactly once; Allow and Deny rules never touch it. A Confirm rule
    /// checks `cancelled` before and while consulting the hook (DEC-020): a
    /// probe that fired yields DeniedByConfirmation with the stable
    /// "confirmation cancelled" reason, and the hook itself observes the
    /// same probe while waiting.
    PermissionVerdict authorize(const PermissionRequest &request,
                                const CancelProbe &cancelled = {}) const;

    const PermissionPolicy &policy() const { return policy_; }

  private:
    PermissionPolicy policy_;
    ConfirmationHandler &confirmation_;
};

} // namespace mirage::runtime::permission
