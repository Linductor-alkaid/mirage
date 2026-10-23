#pragma once

#include "chat_model.hpp"
#include <algorithm>
#include <utility>

namespace mirage::native_ui {

struct MessageGeometry {
    float bubble_width = 0, body_height = 0, body_y = 0, process_y = 0, row_height = 0,
          failure_height = 0;
    std::vector<float> part_heights;
};

// UI-thread-only geometry for the active conversation (DEC-108 / M6-48).
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
                *found = {message, {}};
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

    template <typename Measure>
    MessageGeometry measure(const LocalMessage &message, float width, Measure &&compute) {
        auto found = find(message.id);
        if (found == entries_.end())
            return compute();
        for (const auto &entry : found->measurements)
            if (entry.width == width)
                return entry.geometry;
        auto geometry = compute();
        // ScrollView measures with and without its scrollbar gutter.
        if (found->measurements.size() == 2)
            found->measurements.erase(found->measurements.begin());
        found->measurements.push_back({width, geometry});
        return geometry;
    }

  private:
    static constexpr std::size_t max_bytes = 8 * 1024 * 1024;
    struct Measurement {
        float width;
        MessageGeometry geometry;
    };
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
            bytes += sizeof(conversation::Part) + 2 * sizeof(float) + part.id.size() +
                     part.kind.size() + part.status.size() + part.text.size() + part.name.size() +
                     part.input.size() + part.output.size();
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
