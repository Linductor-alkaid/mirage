#pragma once
#include "content_task_assessment.hpp"
#include "content_visual_supply.hpp"
#include "perception_temporal_selection.hpp"
#include <cmath>
#include <utility>

namespace mirage::integration::detail {
inline bool content_temporal_event(const PerceptionEvent &event, const PerceptionView &view,
                                   std::chrono::steady_clock::time_point now) {
    return event.temporal_keyframe && event.kind == "visual_region_changed" && !event.confirmed &&
           !event.entity && !event.instance_generation && event.source_epoch == view.source_epoch &&
           event.source_epoch && event.frame_sequence && !event.frame_id.empty() &&
           event.frame_id.size() <= 64 && event.observed_at <= now &&
           now - event.observed_at < std::chrono::seconds{120} && std::isfinite(event.x) &&
           std::isfinite(event.y) && std::isfinite(event.width) && std::isfinite(event.height) &&
           event.x >= 0 && event.y >= 0 && event.width > 0 && event.height > 0;
}
// DEC-136: finite tool-local subscription. read() also syncs owner/authority evidence.
// Clock and subscription callbacks permit deterministic tests without sleeping.
template <class Read, class Wait, class Now>
std::string wait_for_content(Read read, Wait wait, Now clock, const ContentTaskAssessment &task,
                             const PerceptionContentEvidence &evidence,
                             const ContentVisualSupply &visual, const std::string &scope,
                             std::uint64_t after, std::chrono::milliseconds timeout,
                             const desktop::CancelToken &cancel) {
    if (cancel.cancelled())
        return "cancelled";
    const auto [initial_epoch, initial_dropped] = [&] {
        const auto initial = read();
        return std::pair{initial.source_epoch, initial.dropped};
    }();
    const auto generation = evidence.generation();
    const auto deadline = clock() + timeout;
    std::string previous;
    auto waited_at = clock(), waited_until = waited_at;
    unsigned rounds = 0;
    for (;;) {
        if (cancel.cancelled())
            return "cancelled";
        const auto current = read();
        const auto now = clock();
        if (cancel.cancelled())
            return "cancelled";
        if (!current.source_epoch || current.state == "failed" || current.state == "stopped" ||
            current.state == "degraded")
            return "source_unavailable";
        if (current.source_epoch != initial_epoch || current.dropped != initial_dropped ||
            evidence.generation() != generation ||
            (current.oldest > after && current.oldest - after > 1))
            return "gap";
        if (current.attention && !current.attention->invalidation_reason.empty())
            return "attention_invalidated";
        if (current.events.size() > 256 || current.previews.size() > 8)
            return "source_capacity_exceeded";
        if (std::any_of(current.events.begin(), current.events.end(), [&](const auto &event) {
                return event.sequence > after && !content_temporal_event(event, current, now);
            }))
            return "event";
        if (!task.wants_images())
            return "content_inactive";
        if (visual.ready(now)) {
            const auto selection = select_temporal_frames(current, evidence, scope,
                                                          std::chrono::seconds{30}, 4, now, cancel);
            if (selection.reason.empty())
                return "content_ready";
            if (selection.reason == "cancelled")
                return "cancelled";
        }
        if ((!previous.empty() && previous == "event" && current.newest <= after) ||
            (previous == "timeout" &&
             (now <= waited_at || waited_until - now > std::chrono::milliseconds{1})))
            return "wait_no_progress";
        if (now >= deadline)
            return "timeout";
        if (rounds++ >= 64)
            return "wake_budget_exhausted";
        auto duration = std::chrono::ceil<std::chrono::milliseconds>(deadline - now);
        const auto retry = visual.retry_after(now);
        if (retry.count() > 0)
            duration = std::min(duration, retry);
        after = std::max(after, current.newest);
        waited_at = now;
        waited_until = now + duration;
        previous = wait(after, duration, cancel);
        if (previous != "event" && previous != "timeout")
            return previous;
    }
}
} // namespace mirage::integration::detail
