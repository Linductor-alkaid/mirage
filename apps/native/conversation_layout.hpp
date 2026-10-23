#pragma once

#include "chat_model.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

namespace mirage::native_ui {

struct MessageGeometry {
    float bubble_width = 0, body_height = 0, body_y = 0, process_y = 0, row_height = 0,
          failure_height = 0;
    std::vector<float> part_heights;
};

// A synchronous read-only view of exact-width segment geometry. The owner keeps
// it alive only until the measurement callback returns.
struct MessageGeometryReuse {
    const MessageGeometry *previous = nullptr;
    const std::vector<bool> *parts = nullptr;
    bool body = false, failure = false;
    std::optional<float> part_height(std::size_t index) const {
        if (previous && parts && index < parts->size() && (*parts)[index] &&
            index < previous->part_heights.size())
            return previous->part_heights[index];
        return {};
    }
};

// UI-thread-only geometry for the active conversation (DEC-108 / M6-48 / M6-52).
// Keep exact source comparisons: hashes or event sequence numbers alone cannot
// detect direct edits, expansion changes, or restored history with the same ID.
class ConversationLayout {
  public:
    struct Context {
        float width, height, scale;
        int font_step, code_step;
        bool dark;
        friend bool operator==(const Context &, const Context &) = default;
    };

    std::string prepare(const LocalSession &session, Context context) {
        bool changed = session_ != session.id || !context_ || *context_ != context;
        if (session_ != session.id)
            entries_.clear();
        else if (context_ &&
                 (context_->scale != context.scale || context_->font_step != context.font_step ||
                  context_->code_step != context.code_step))
            for (auto &entry : entries_)
                entry.measurements.clear();
        session_ = session.id;
        context_ = context;
        // This bounds the extra source snapshots, independently of the model.
        // Over-budget input keeps the original uncached measurement semantics.
        std::size_t bytes = 0;
        for (const auto &message : session.messages) {
            bytes += source_bytes(message);
            if (bytes > max_bytes || session.messages.size() > ChatModel::max_messages) {
                entries_.clear();
                return {}; // ScrollView also measures afresh.
            }
        }
        if (entries_.size() != session.messages.size())
            changed = true;
        std::erase_if(entries_, [&](const auto &entry) {
            return std::none_of(session.messages.begin(), session.messages.end(),
                                [&](const auto &message) { return message.id == entry.source.id; });
        });
        for (const auto &message : session.messages) {
            auto found = find(message.id);
            if (found == entries_.end()) {
                entries_.push_back({message, {}});
                changed = true;
            } else if (!same_source(found->source, message)) {
                for (auto &measurement : found->measurements)
                    invalidate_segments(measurement, found->source, message);
                found->source = message;
                changed = true;
            }
        }
        // Order affects ScrollView layout even when every message is unchanged.
        std::vector<std::uint64_t> order;
        for (const auto &message : session.messages)
            order.push_back(message.id);
        changed |= order != order_;
        order_ = std::move(order);
        if (changed)
            ++revision_;
        return std::to_string(revision_);
    }

    // ScrollView's first pass only decides whether it needs a scrollbar.
    // prepare() has removed exited rows and marked stale measurements. Unknown rows have
    // non-negative height; known rows plus gaps therefore give a lower bound.
    // This is never the final content extent, which still uses exact geometry.
    bool proves_overflow(float width, float viewport_height, float gap) const {
        if (entries_.empty())
            return false;
        double lower_bound = std::max(0.0f, gap) * static_cast<double>(entries_.size() - 1);
        for (const auto &entry : entries_)
            for (const auto &measurement : entry.measurements)
                if (measurement.current && measurement.width == width) {
                    const auto height = measurement.geometry.row_height;
                    if (!std::isfinite(height) || height < 0)
                        return false;
                    lower_bound += height;
                    break;
                }
        // Leave the boundary to the full pass (including float layout rounding).
        return lower_bound > static_cast<double>(viewport_height) + 1.0;
    }

    template <typename Measure>
    MessageGeometry measure(const LocalMessage &message, float width, Measure &&compute) {
        auto found = find(message.id);
        if (found == entries_.end())
            return compute(MessageGeometryReuse{});
        for (auto &entry : found->measurements)
            if (entry.width == width) {
                if (!entry.current) {
                    auto geometry = compute(MessageGeometryReuse{&entry.geometry, &entry.parts,
                                                                 entry.body, entry.failure});
                    entry = {width, std::move(geometry),
                             true,  true,
                             true,  std::vector<bool>(message.parts.size(), true)};
                }
                return entry.geometry;
            }
        auto geometry = compute(MessageGeometryReuse{});
        // ScrollView can measure with and without its scrollbar gutter.
        if (found->measurements.size() == 2)
            found->measurements.erase(found->measurements.begin());
        found->measurements.push_back(
            {width, geometry, true, true, true, std::vector<bool>(message.parts.size(), true)});
        return geometry;
    }

  private:
    static constexpr std::size_t max_bytes = 8 * 1024 * 1024;
    struct Measurement {
        float width;
        MessageGeometry geometry;
        bool current = true, body = true, failure = true;
        std::vector<bool> parts;
    };
    static bool expanded(const LocalMessage &message, const std::string &id) {
        return std::find(message.expanded_parts.begin(), message.expanded_parts.end(), id) !=
               message.expanded_parts.end();
    }
    static void invalidate_segments(Measurement &measurement, const LocalMessage &old,
                                    const LocalMessage &next) {
        measurement.current = false; // stale totals must never prove overflow
        const bool body_same = old.text == next.text && old.role == next.role;
        measurement.body &= body_same;
        measurement.failure &= body_same && old.error == next.error && old.status == next.status;
        std::vector<float> heights(next.parts.size(), 0);
        std::vector<bool> valid(next.parts.size(), false);
        if (old.role == next.role)
            for (std::size_t i = 0; i < next.parts.size(); ++i) {
                const auto &part = next.parts[i];
                auto previous = old.parts.end();
                if (i < old.parts.size() && old.parts[i].id == part.id)
                    previous = old.parts.begin() + static_cast<std::ptrdiff_t>(i);
                else
                    previous = std::find_if(old.parts.begin(), old.parts.end(),
                                            [&](const auto &item) { return item.id == part.id; });
                if (previous == old.parts.end())
                    continue;
                const auto index = static_cast<std::size_t>(previous - old.parts.begin());
                if (index < measurement.parts.size() && measurement.parts[index] &&
                    index < measurement.geometry.part_heights.size() && *previous == part &&
                    expanded(old, part.id) == expanded(next, part.id)) {
                    heights[i] = measurement.geometry.part_heights[index];
                    valid[i] = true;
                }
            }
        measurement.geometry.part_heights = std::move(heights);
        measurement.parts = std::move(valid);
    }
    struct Entry {
        LocalMessage source;
        std::vector<Measurement> measurements;
    };
    std::vector<Entry>::iterator find(std::uint64_t id) {
        return std::find_if(entries_.begin(), entries_.end(),
                            [id](const auto &entry) { return entry.source.id == id; });
    }
    static bool same_source(const LocalMessage &a, const LocalMessage &b) {
        return a.text == b.text && a.role == b.role && a.status == b.status && a.error == b.error &&
               a.parts == b.parts && a.expanded_parts == b.expanded_parts && a.timings == b.timings;
    }
    static std::size_t source_bytes(const LocalMessage &message) {
        auto bytes = sizeof(Entry) + message.text.size() + message.role.size() +
                     message.status.size() + message.error.size() + message.turn_id.size() +
                     message.preview_request.size();
        for (const auto &part : message.parts)
            bytes += sizeof(conversation::Part) + 2 * (sizeof(float) + sizeof(bool)) +
                     part.id.size() + part.kind.size() + part.status.size() + part.text.size() +
                     part.name.size() + part.input.size() + part.output.size();
        for (const auto &part : message.expanded_parts)
            bytes += sizeof(std::string) + part.size();
        return bytes;
    }
    std::uint64_t session_ = 0, revision_ = 0;
    std::optional<Context> context_;
    std::vector<Entry> entries_;
    std::vector<std::uint64_t> order_;
};
} // namespace mirage::native_ui
