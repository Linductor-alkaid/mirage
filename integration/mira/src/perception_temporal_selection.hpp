#pragma once
#include "perception_content_evidence.hpp"

namespace mirage::integration::detail {
struct TemporalFrameSelection {
    std::vector<std::string> ids;
    std::size_t eligible = 0;
    std::string reason;
};
// DEC-134: bounded product evidence selection; timestamps are observer time.
inline TemporalFrameSelection
select_temporal_frames(const PerceptionView &view, const PerceptionContentEvidence &evidence,
                       const std::string &owner_scope, std::chrono::milliseconds window,
                       std::size_t limit, std::chrono::steady_clock::time_point now,
                       const desktop::CancelToken &cancel) {
    TemporalFrameSelection result;
    const auto reject = [&](const char *reason) {
        result.ids.clear();
        result.reason = reason;
        return result;
    };
    if (cancel.cancelled())
        return reject("cancelled");
    if (window < std::chrono::milliseconds{100} || window > std::chrono::seconds{120} ||
        limit < 2 || limit > 4)
        return reject("invalid_argument");
    if (view.previews.size() > 8 || view.events.size() > 256)
        return reject("source_capacity_exceeded");
    if (!evidence.current({evidence.generation(), view.source_epoch, {}}))
        return reject("source_unavailable");
    struct Candidate {
        ContentEvidenceSample sample;
        std::int32_t width, height;
    };
    std::vector<Candidate> candidates;
    for (const auto &entry : view.previews) {
        if (cancel.cancelled())
            return reject("cancelled");
        const auto preview = entry.second.lock();
        if (!preview || preview->observed_at > now || now - preview->observed_at > window)
            continue;
        const auto offer = evidence.offer(view, std::span<const std::string>{&entry.first, 1}, now);
        if (!offer)
            continue;
        const auto &sample = offer->samples.front();
        const auto ref = PerceptionContentEvidence::reference(owner_scope, *offer, sample);
        if (evidence.before_retained_window(sample.observed_at) ||
            evidence.contains_reference(owner_scope, ref))
            continue;
        candidates.push_back({sample, preview->image.width, preview->image.height});
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b) {
        return a.sample.observed_at < b.sample.observed_at;
    });
    result.eligible = candidates.size();
    if (candidates.size() < 2)
        return reject(candidates.empty() ? "no_new_temporal_frames"
                                         : "need_two_new_temporal_frames");
    for (std::size_t i = 1; i < candidates.size(); ++i) {
        const auto &a = candidates[i - 1], &b = candidates[i];
        if (a.sample.observed_at >= b.sample.observed_at ||
            a.sample.frame_sequence >= b.sample.frame_sequence ||
            std::any_of(candidates.begin(), candidates.begin() + i,
                        [&](const auto &c) { return c.sample.frame_id == b.sample.frame_id; }))
            return reject("ambiguous_temporal_order");
        if (a.width != b.width || a.height != b.height)
            return reject("source_geometry_changed");
    }
    std::vector<std::size_t> selected{0, candidates.size() - 1};
    while (selected.size() < std::min(limit, candidates.size())) {
        std::size_t best = 0;
        auto largest = std::chrono::steady_clock::duration::min();
        for (std::size_t i = 1; i + 1 < candidates.size(); ++i) {
            if (std::find(selected.begin(), selected.end(), i) != selected.end())
                continue;
            auto nearest = std::chrono::steady_clock::duration::max();
            for (const auto j : selected) {
                const auto a = candidates[i].sample.observed_at,
                           b = candidates[j].sample.observed_at;
                nearest = std::min(nearest, a > b ? a - b : b - a);
            }
            if (nearest > largest) {
                largest = nearest;
                best = i;
            }
        }
        selected.push_back(best);
    }
    std::sort(selected.begin(), selected.end());
    for (const auto i : selected)
        result.ids.push_back(candidates[i].sample.frame_id);
    return result;
}
} // namespace mirage::integration::detail
