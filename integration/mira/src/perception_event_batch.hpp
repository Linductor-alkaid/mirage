#pragma once
#include <algorithm>
#include <mira/json.hpp>
#include <mirage/integration/desktop_perception.hpp>
#include <utility>

namespace mirage::integration::detail {
// DEC-130: transient borrowed groups only; the source journal is never mutated.
struct PerceptionEventBatch {
    mira::JsonValue::Array rows;
    std::uint64_t cursor = 0;
};
inline PerceptionEventBatch perception_event_batch(const PerceptionView &view, std::uint64_t after,
                                                   std::chrono::steady_clock::time_point now) {
    using Json = mira::JsonValue;
    const auto sample = [&](const PerceptionEvent &e, bool with_sequence = false) {
        const bool available =
            std::any_of(view.previews.begin(), view.previews.end(), [&](const auto &p) {
                return p.first == e.frame_id && !p.second.expired();
            });
        Json::Object fields{
            {"cursor", static_cast<std::int64_t>(e.sequence)},
            {"frame_id", available ? e.frame_id : std::string{}},
            {"preview_available", available},
            {"age_ms",
             std::chrono::duration_cast<std::chrono::milliseconds>(now - e.observed_at).count()},
            {"x", e.x},
            {"y", e.y},
            {"width", e.width},
            {"height", e.height},
            {"frame_sequence", static_cast<std::int64_t>(e.frame_sequence)}};
        if (!with_sequence)
            fields.pop_back();
        return Json{std::move(fields)};
    };
    const auto compatible = [&](const PerceptionEvent &first, const PerceptionEvent &last,
                                const PerceptionEvent &next) {
        return !view.resync && !first.temporal_keyframe && !next.temporal_keyframe &&
               first.kind == "position_or_appearance_changed" && first.confirmed && first.entity &&
               first.instance_generation && first.source_epoch && next.kind == first.kind &&
               next.confirmed && next.entity == first.entity &&
               next.instance_generation == first.instance_generation &&
               next.source_epoch == first.source_epoch && next.text == first.text &&
               next.sequence > last.sequence && next.sequence - last.sequence == 1 &&
               first.observed_at <= last.observed_at && last.observed_at <= next.observed_at &&
               next.observed_at <= now &&
               next.observed_at - first.observed_at <= std::chrono::seconds(1) &&
               first.frame_sequence && next.frame_sequence >= last.frame_sequence;
    };
    PerceptionEventBatch out;
    std::size_t bytes = 2; // Include JSON array brackets and separators.
    for (std::size_t i = 0; i < view.events.size() && out.rows.size() < 16;) {
        if (view.events[i].sequence <= after) {
            ++i;
            continue;
        }
        const auto &first = view.events[i];
        std::size_t count = 1;
        while (count < 8 && i + count < view.events.size() &&
               compatible(first, view.events[i + count - 1], view.events[i + count]))
            ++count;
        const std::size_t separator = out.rows.empty() ? 0 : 1;
        Json row;
        std::size_t size = 0;
        for (;;) {
            const auto &last = view.events[i + count - 1];
            row = sample(last, count > 1);
            row.set("kind", last.kind);
            row.set("text", last.text);
            row.set("confirmed", last.confirmed);
            if (last.temporal_keyframe)
                row.set("temporal_keyframe", true);
            row.set("entity", static_cast<std::int64_t>(last.entity));
            row.set("instance_generation", static_cast<std::int64_t>(last.instance_generation));
            row.set("source_epoch", static_cast<std::int64_t>(last.source_epoch));
            if (count > 1) {
                Json::Array previous;
                for (std::size_t j = 0; j + 1 < count; ++j) {
                    previous.push_back(sample(view.events[i + j], true));
                }
                row.set("sample_count", static_cast<std::int64_t>(count));
                row.set("prior_samples", Json{std::move(previous)});
            }
            size = mira::to_json_string(row).size();
            if (bytes + separator + size <= 7000 || count == 1)
                break;
            --count; // Only the prefix that actually fits may be acknowledged.
        }
        if (bytes + separator + size > 7000)
            break;
        bytes += separator + size;
        out.cursor = view.events[i + count - 1].sequence;
        out.rows.push_back(std::move(row));
        i += count;
    }
    return out;
}
} // namespace mirage::integration::detail
