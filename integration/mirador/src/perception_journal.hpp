#pragma once
#include <algorithm>
#include <mirage/integration/desktop_perception.hpp>

namespace mirage::integration::detail {
// Single-owner product retention. It does not schedule, track objects, or
// transport cross-context messages (the adapter uses kairo for that).
class PerceptionJournal {
    using Clock = std::chrono::steady_clock;
    struct Entry {
        PerceptionEvent event;
        std::optional<Clock::time_point> read;
    };
    struct Frame {
        std::shared_ptr<const PerceptionPreview> preview;
        std::optional<Clock::time_point> read;
    };
    std::vector<Entry> entries_;
    std::vector<Frame> frames_;
    desktop::VisualScene scene_;
    bool scene_valid_ = false;
    std::string scene_error_;
    Clock::time_point scene_at_{};
    std::uint64_t next_content_ = 0;
    std::optional<desktop::WindowGeometry> focus_;
    bool resync_ = false;
    std::uint64_t next_ = 0, dropped_ = 0, filtered_ = 0, epoch_ = 0, upstream_dropped_ = 0;
    static std::size_t bytes(const PerceptionEvent &e) {
        return sizeof(Entry) + e.kind.size() + e.text.size() + e.frame_id.size();
    }
    bool relevant(const PerceptionEvent &e) const {
        if (!focus_ || e.kind == "appeared" || e.kind == "disappeared" ||
            e.kind == "source_reset" || e.kind == "observed_unconfirmed")
            return true;
        return e.x < focus_->x + focus_->width && focus_->x < e.x + e.width &&
               e.y < focus_->y + focus_->height && focus_->y < e.y + e.height;
    }

  public:
    explicit PerceptionJournal(std::string source_session = "perception")
        : scene_(std::move(source_session)) {}
    void focus(const desktop::WindowGeometry &roi) { focus_ = roi; }
    void clear() {
        entries_.clear();
        frames_.clear();
        scene_ = desktop::VisualScene(scene_.snapshot().source_session);
        scene_valid_ = false;
    }
    void acknowledge(std::uint64_t cursor, Clock::time_point now) {
        if (cursor >= next_)
            resync_ = false;
        for (auto &e : entries_)
            if (e.event.sequence <= cursor && !e.read)
                e.read = now;
        for (auto &f : frames_) {
            const bool unread = std::any_of(entries_.begin(), entries_.end(), [&](const auto &e) {
                return e.event.frame_id == f.preview->id && !e.read;
            });
            if (!unread && !f.read)
                f.read = now;
        }
    }
    void acknowledge_frame(const std::string &id) {
        std::erase_if(frames_, [&](const auto &f) { return f.preview->id == id; });
    }
    std::size_t journal_bytes() const {
        std::size_t n = 0;
        for (const auto &e : entries_)
            n += bytes(e.event);
        return n;
    }
    std::size_t preview_bytes() const {
        std::size_t n = 0;
        for (const auto &f : frames_)
            n += f.preview->image.pixels.capacity();
        return n;
    }
    void collect(Clock::time_point now) {
        std::erase_if(entries_, [&](const auto &e) {
            const bool expired = e.read ? now - *e.read >= std::chrono::seconds(60)
                                        : now - e.event.observed_at >= std::chrono::seconds(120);
            if (expired && !e.read)
                ++dropped_;
            return expired;
        });
        std::erase_if(frames_, [&](const auto &f) {
            return now - f.preview->observed_at >= std::chrono::seconds(120) ||
                   (f.read && now - *f.read >= std::chrono::seconds(60));
        });
    }
    // Preview is already <=1 MiB; there is no retained full-size image here.
    void append(PerceptionSample sample, std::shared_ptr<const PerceptionPreview> preview,
                Clock::time_point now) {
        collect(now);
        if (epoch_ && sample.source_epoch != epoch_) {
            clear();
            ++dropped_;
            upstream_dropped_ = 0;
            resync_ = true;
        }
        epoch_ = sample.source_epoch;
        if (sample.objects_complete && sample.objects.size() <= 256) {
            std::vector<desktop::VisualSceneObject> objects;
            const bool gap = sample.resync || sample.dropped > upstream_dropped_;
            for (auto &entry : sample.objects) {
                auto object = std::move(entry.object);
                const auto *old = scene_.find(object.id);
                const bool same =
                    scene_valid_ && old && old->object.incarnation == object.incarnation;
                if (!entry.content_observed) {
                    if (!same)
                        continue;
                    auto previous = old->object;
                    previous.identity_confirmed = false;
                    previous.pending = true;
                    object = std::move(previous);
                }
                const bool changed =
                    !same || gap || entry.invalidate_content || old->object.text != object.text ||
                    old->object.label != object.label || old->object.source != object.source;
                object.content_revision = changed ? ++next_content_ : old->object.content_revision;
                objects.push_back(std::move(object));
            }
            const auto baseline = scene_.snapshot();
            auto applied = scene_.apply({baseline.source_session,
                                         sample.source_epoch,
                                         sample.frame_sequence,
                                         scene_.revision(),
                                         true,
                                         objects,
                                         {}});
            scene_valid_ = applied.ok;
            scene_error_ = applied.ok ? std::string{} : applied.error.code;
            scene_at_ = now;
        } else {
            scene_valid_ = false;
            scene_error_ = sample.objects.size() > 256 ? "object_projection_budget_exceeded"
                                                       : "incomplete_object_projection";
        }
        if (sample.dropped > upstream_dropped_)
            dropped_ += sample.dropped - upstream_dropped_;
        upstream_dropped_ = sample.dropped;
        resync_ = resync_ || sample.resync;
        bool retained = false;
        for (auto &e : sample.events) {
            if (!relevant(e)) {
                ++filtered_;
                continue;
            }
            if (e.kind.size() > 64 || e.text.size() > 256) {
                ++dropped_;
                continue;
            }
            e.kind = std::string(e.kind.data(), e.kind.size());
            e.text = std::string(e.text.data(), e.text.size());
            e.frame_id.clear();
            e.frame_id.shrink_to_fit();
            e.sequence = ++next_;
            e.observed_at = preview ? preview->observed_at : now;
            if (preview)
                e.frame_id = preview->id;
            entries_.push_back({std::move(e), std::nullopt});
            retained = true;
            while (entries_.size() > 256 || journal_bytes() > 256 * 1024) {
                if (!entries_.front().read)
                    ++dropped_;
                entries_.erase(entries_.begin());
            }
        }
        if (retained && preview && preview->image.pixels.capacity() <= (1u << 20)) {
            frames_.push_back({std::move(preview), std::nullopt});
            while (frames_.size() > 8 || preview_bytes() > (8u << 20))
                frames_.erase(frames_.begin());
        }
    }
    PerceptionView view() const {
        PerceptionView out;
        out.newest = next_;
        out.oldest = entries_.empty() ? next_ + 1 : entries_.front().event.sequence;
        out.source_epoch = epoch_;
        out.resync = resync_;
        out.dropped = dropped_;
        out.filtered = filtered_;
        out.journal_bytes = journal_bytes();
        out.preview_bytes = preview_bytes();
        out.scene_error = scene_error_;
        out.scene_observed_at = scene_at_;
        if (scene_valid_)
            out.scene = scene_.snapshot();
        for (const auto &e : entries_)
            out.events.push_back(e.event);
        for (const auto &f : frames_)
            out.previews.emplace_back(f.preview->id, f.preview);
        return out;
    }
};
} // namespace mirage::integration::detail
