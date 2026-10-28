#pragma once
#include <algorithm>
#include <mirage/integration/desktop_perception.hpp>

namespace mirage::integration::detail {
// DEC-129: owned by one serialized task lease. No pixels, scheduling or transport.
class TaskAttention {
    using Clock = std::chrono::steady_clock;
    PerceptionAttention state_;
    std::uint64_t epoch_ = 0, dropped_ = 0;
    std::string source_;
    std::optional<std::uint64_t> context_;
    std::optional<desktop::VisualSemanticBinding> object_;
    static bool valid_region(const desktop::WindowGeometry &r) {
        return r.x >= 0 && r.y >= 0 && r.width > 0 && r.height > 0 &&
               std::int64_t(r.x) + r.width <= 1000000 && std::int64_t(r.y) + r.height <= 1000000;
    }
    void bind_scope(const PerceptionView &view) {
        epoch_ = view.source_epoch;
        dropped_ = view.dropped;
        context_.reset();
        source_.clear();
        if (view.scene && view.scene->source_session.size() <= 128) {
            source_ = view.scene->source_session;
            context_ = view.scene->context_revision;
        }
    }
    void invalidate(const char *reason) {
        state_.active = false;
        state_.region = {};
        state_.invalidation_reason = reason;
        object_.reset(); // No automatic resurrection, even on an ABA return.
    }
    const char *object_status(const PerceptionView &view, Clock::time_point now,
                              desktop::WindowGeometry &bounds) const {
        if (!view.scene || !view.scene->ok || !view.scene->reset ||
            view.scene->updated.size() > 256 || view.recognition_deferred)
            return "object_unknown";
        const auto &scene = *view.scene;
        if (scene.source_session != object_->source_session ||
            scene.context_revision != object_->context_revision)
            return "context_changed";
        if (view.scene_observed_at > now ||
            now - view.scene_observed_at >= std::chrono::seconds(120))
            return "observation_expired";
        const auto it =
            std::find_if(scene.updated.begin(), scene.updated.end(),
                         [&](const auto &r) { return r.object.id == object_->object_id; });
        if (it == scene.updated.end())
            return "object_missing";
        const auto &o = it->object;
        if (o.incarnation != object_->incarnation ||
            o.content_revision != object_->content_revision)
            return "object_version_changed";
        if (scene.frame_sequence < object_->frame_sequence || !o.identity_confirmed || o.pending ||
            !valid_region(o.bounds))
            return "object_unknown";
        bounds = o.bounds;
        return nullptr;
    }

  public:
    void clear() { *this = TaskAttention{}; }
    bool region(const desktop::WindowGeometry &region, const PerceptionView &view) {
        if (region.width <= 0 || region.height <= 0) {
            clear();
            return true;
        }
        if (!valid_region(region))
            return false;
        clear();
        state_.mode = "region";
        state_.active = true;
        state_.region = region;
        bind_scope(view);
        update(view);
        return state_.active;
    }
    bool object(const desktop::VisualSemanticBinding &binding, const PerceptionView &view,
                Clock::time_point now = Clock::now()) {
        if (binding.source_session.empty() || binding.source_session.size() > 128 ||
            binding.model_request_id.empty() || binding.model_request_id.size() > 128 ||
            binding.label.size() > 1024 || !binding.object_id || !binding.incarnation ||
            !binding.content_revision || !binding.context_revision || !binding.frame_sequence)
            return false;
        TaskAttention proposed;
        proposed.object_ = binding;
        proposed.object_->label.clear(); // Meaning stays in the task's semantic store.
        proposed.state_.mode = "object";
        proposed.state_.active = true;
        proposed.state_.entity = binding.object_id;
        proposed.state_.instance_generation = binding.incarnation;
        proposed.state_.content_revision = binding.content_revision;
        proposed.bind_scope(view);
        proposed.update(view, now);
        if (!proposed.state_.active)
            return false; // A rejected replacement leaves prior attention intact.
        *this = std::move(proposed);
        return true;
    }
    const PerceptionAttention &update(const PerceptionView &view,
                                      Clock::time_point now = Clock::now()) {
        if (!state_.active)
            return state_;
        if (view.state == "failed" || view.state == "stopped" || view.state == "degraded")
            invalidate("source_unavailable");
        else if (view.source_epoch != epoch_)
            invalidate("source_changed");
        else if (view.dropped != dropped_)
            invalidate("observation_gap");
        else if (context_ && view.scene &&
                 (*context_ != view.scene->context_revision ||
                  source_ != view.scene->source_session))
            invalidate("context_changed");
        else if (object_) {
            desktop::WindowGeometry bounds;
            if (const auto *reason = object_status(view, now, bounds))
                invalidate(reason);
            else
                state_.region = bounds;
        }
        return state_;
    }
};
} // namespace mirage::integration::detail
