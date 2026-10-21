#include <mirage/desktop/target_text_value.hpp>

#include <algorithm>
#include <mirage/desktop/accessibility_provider.hpp>

namespace mirage::desktop {

TextValueVerification verify_text_value(AccessibilityProvider &provider,
                                        const TextValueCondition &condition,
                                        const TextValueReadLimits &limits,
                                        const CancelToken &cancel) {
    TextValueVerification result;
    const auto unavailable = [&](const char *reason) {
        result.reason = reason;
        return result;
    };
    if (condition.target.reference.id.empty() || condition.target.snapshot_scope.empty() ||
        condition.target.hint_count() != 1 || limits.max_bytes == 0 ||
        limits.max_bytes > text_value_policy::max_bytes || limits.timeout.count() <= 0 ||
        condition.expected.size() > limits.max_bytes || !is_valid_utf8(condition.expected))
        return unavailable("invalid_condition");
    if (cancel.cancelled())
        return unavailable("cancelled");
    try {
        if (!provider.text_value_read_supported())
            return unavailable("text_value_read_unsupported");
    } catch (...) {
        return unavailable("text_value_read_failed");
    }

    const auto started = std::chrono::steady_clock::now();
    const auto budget = std::min(limits.timeout, text_value_policy::timeout);
    const auto deadline = started + budget;
    const CancelToken bounded(
        [&] { return cancel.cancelled() || std::chrono::steady_clock::now() >= deadline; });
    TextValueReadOutcome read;
    try {
        read = provider.read_text_value(condition.target, {limits.max_bytes, budget}, bounded);
    } catch (...) {
        return unavailable("text_value_read_failed");
    }
    const auto finished = std::chrono::steady_clock::now();
    if (cancel.cancelled() || read.cancelled)
        return unavailable("cancelled");
    if (finished >= deadline)
        return unavailable("deadline_exceeded");
    if (!read.ok)
        return unavailable("text_value_read_unavailable");
    if (read.reference != condition.target.reference.id ||
        read.snapshot_scope != condition.target.snapshot_scope)
        return unavailable("target_changed");
    if (read.read_started < started || read.read_finished < read.read_started ||
        read.read_finished > finished)
        return unavailable("stale_value_evidence");
    if (read.protected_content || !read.complete || !read.committed ||
        read.source != TextValueSource::accessibility)
        return unavailable("complete_committed_value_unavailable");
    if (read.utf8_text.size() > limits.max_bytes || !is_valid_utf8(read.utf8_text))
        return unavailable("invalid_value_evidence");

    result.source = read.source;
    result.reference = read.reference;
    result.snapshot_scope = read.snapshot_scope;
    result.read_started = read.read_started;
    result.read_finished = read.read_finished;
    result.status = read.utf8_text == condition.expected ? TextValueStatus::matched
                                                         : TextValueStatus::mismatched;
    result.reason = result.status == TextValueStatus::matched ? "exact_value_matched"
                                                              : "exact_value_mismatched";
    return result;
}

} // namespace mirage::desktop
