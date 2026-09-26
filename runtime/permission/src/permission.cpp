#include <mirage/runtime/permission/permission.hpp>

#include <utility>

namespace mirage::runtime::permission {

const char *rule_name(Rule rule) {
    switch (rule) {
    case Rule::Allow:
        return "allow";
    case Rule::Confirm:
        return "confirm";
    case Rule::Deny:
        return "deny";
    }
    return "unknown";
}

std::optional<Rule> rule_from_name(std::string_view name) {
    if (name == "allow") {
        return Rule::Allow;
    }
    if (name == "confirm") {
        return Rule::Confirm;
    }
    if (name == "deny") {
        return Rule::Deny;
    }
    return std::nullopt;
}

ConfirmationResult DenyAllConfirmation::confirm(const PermissionRequest &, const CancelProbe &) {
    return ConfirmationResult::Rejected;
}

ConfirmationResult AllowAllConfirmation::confirm(const PermissionRequest &, const CancelProbe &) {
    return ConfirmationResult::Approved;
}

const char *decision_name(Decision decision) {
    switch (decision) {
    case Decision::Allowed:
        return "allowed";
    case Decision::AllowedByConfirmation:
        return "confirmed";
    case Decision::Denied:
        return "denied";
    case Decision::DeniedByConfirmation:
        return "confirmation_rejected";
    }
    return "unknown";
}

PermissionController::PermissionController(PermissionPolicy policy,
                                           ConfirmationHandler &confirmation)
    : policy_(policy), confirmation_(confirmation) {}

PermissionVerdict PermissionController::authorize(const PermissionRequest &request,
                                                  const CancelProbe &cancelled) const {
    PermissionVerdict verdict;
    switch (policy_.rule_for(request.capability)) {
    case Rule::Allow:
        verdict.allowed = true;
        verdict.decision = Decision::Allowed;
        return verdict;
    case Rule::Confirm:
        // Probe-first (DEC-020): a caller already asked to stop never
        // raises a confirmation request.
        if (cancelled && cancelled()) {
            verdict.allowed = false;
            verdict.decision = Decision::DeniedByConfirmation;
            verdict.reason =
                "confirmation cancelled for " + std::string(capability_name(request.capability));
            return verdict;
        }
        switch (confirmation_.confirm(request, cancelled)) {
        case ConfirmationResult::Approved:
            verdict.allowed = true;
            verdict.decision = Decision::AllowedByConfirmation;
            return verdict;
        case ConfirmationResult::Rejected:
            verdict.allowed = false;
            verdict.decision = Decision::DeniedByConfirmation;
            verdict.reason =
                "confirmation rejected for " + std::string(capability_name(request.capability));
            return verdict;
        case ConfirmationResult::TimedOut:
            verdict.allowed = false;
            verdict.decision = Decision::DeniedByConfirmation;
            verdict.reason =
                "confirmation timed out for " + std::string(capability_name(request.capability));
            return verdict;
        case ConfirmationResult::Unresolved:
            verdict.allowed = false;
            verdict.decision = Decision::DeniedByConfirmation;
            verdict.reason =
                "confirmation unavailable for " + std::string(capability_name(request.capability));
            return verdict;
        case ConfirmationResult::Cancelled:
            verdict.allowed = false;
            verdict.decision = Decision::DeniedByConfirmation;
            verdict.reason =
                "confirmation cancelled for " + std::string(capability_name(request.capability));
            return verdict;
        }
        return verdict;
    case Rule::Deny:
        verdict.allowed = false;
        verdict.decision = Decision::Denied;
        verdict.reason = std::string(capability_name(request.capability)) + " denied by policy";
        return verdict;
    }
    return verdict;
}

} // namespace mirage::runtime::permission
