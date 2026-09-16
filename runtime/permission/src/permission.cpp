#include <mirage/runtime/permission/permission.hpp>

namespace mirage::runtime::permission {

const char* rule_name(Rule rule) {
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

bool DenyAllConfirmation::confirm(const PermissionRequest&) {
    return false;
}

bool AllowAllConfirmation::confirm(const PermissionRequest&) {
    return true;
}

const char* decision_name(Decision decision) {
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
                                           ConfirmationHandler& confirmation)
    : policy_(policy), confirmation_(confirmation) {}

PermissionVerdict PermissionController::authorize(
    const PermissionRequest& request) const {
    PermissionVerdict verdict;
    switch (policy_.rule_for(request.capability)) {
    case Rule::Allow:
        verdict.allowed = true;
        verdict.decision = Decision::Allowed;
        return verdict;
    case Rule::Confirm:
        if (confirmation_.confirm(request)) {
            verdict.allowed = true;
            verdict.decision = Decision::AllowedByConfirmation;
        } else {
            verdict.allowed = false;
            verdict.decision = Decision::DeniedByConfirmation;
            verdict.reason =
                "confirmation rejected for " +
                std::string(capability_name(request.capability));
        }
        return verdict;
    case Rule::Deny:
        verdict.allowed = false;
        verdict.decision = Decision::Denied;
        verdict.reason = std::string(capability_name(request.capability)) +
                         " denied by policy";
        return verdict;
    }
    return verdict;
}

} // namespace mirage::runtime::permission
