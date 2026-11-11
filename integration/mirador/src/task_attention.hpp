#pragma once
#include <algorithm>
#include <cmath>
#include <mirage/integration/desktop_perception.hpp>
#include <utility>

namespace mirage::integration::detail {
// DEC-129: owned by one serialized task lease. No pixels, scheduling or transport.
class TaskAttention {
    using Clock = std::chrono::steady_clock;
    PerceptionAttention state_;
    std::uint64_t epoch_ = 0, dropped_ = 0;
    std::string source_;
    std::optional<std::uint64_t> context_;
    std::vector<PerceptionFocusTarget> objects_;
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
        objects_.clear(); // No automatic resurrection, even on an ABA return.
        for (auto &target : state_.targets)
            target.region = {};
    }
    const char *object_status(const desktop::VisualSemanticBinding &binding,
                              const PerceptionView &view, Clock::time_point now,
                              desktop::WindowGeometry &bounds) const {
        if (!view.scene || !view.scene->ok || !view.scene->reset ||
            view.scene->updated.size() > 256 || view.recognition_deferred)
            return "object_unknown";
        const auto &scene = *view.scene;
        if (scene.source_session != binding.source_session ||
            scene.context_revision != binding.context_revision)
            return "context_changed";
        if (view.scene_observed_at > now ||
            now - view.scene_observed_at >= std::chrono::seconds(120))
            return "observation_expired";
        const auto it =
            std::find_if(scene.updated.begin(), scene.updated.end(),
                         [&](const auto &r) { return r.object.id == binding.object_id; });
        if (it == scene.updated.end())
            return "object_missing";
        const auto &o = it->object;
        if (o.incarnation != binding.incarnation || o.content_revision != binding.content_revision)
            return "object_version_changed";
        if (scene.frame_sequence < binding.frame_sequence || !o.identity_confirmed || o.pending ||
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
        return objects({{binding, true}}, view, now, false);
    }
    bool objects(const std::vector<PerceptionFocusTarget> &targets, const PerceptionView &view,
                 Clock::time_point now = Clock::now(), bool multiple = true) {
        if (targets.empty() || targets.size() > 4 || (!multiple && targets.size() != 1) ||
            std::none_of(targets.begin(), targets.end(), [](const auto &t) { return t.wake; }))
            return false;
        TaskAttention proposed;
        proposed.state_.mode = multiple ? "objects" : "object";
        proposed.state_.active = true;
        for (const auto &target : targets) {
            const auto &binding = target.object;
            if (binding.source_session.empty() || binding.source_session.size() > 128 ||
                binding.model_request_id.empty() || binding.model_request_id.size() > 128 ||
                binding.label.size() > 1024 || !binding.object_id || !binding.incarnation ||
                !binding.content_revision || !binding.context_revision || !binding.frame_sequence ||
                std::any_of(proposed.objects_.begin(), proposed.objects_.end(),
                            [&](const auto &t) { return t.object.object_id == binding.object_id; }))
                return false;
            proposed.objects_.push_back(target);
            std::string{}.swap(proposed.objects_.back().object.label); // Retain no label storage.
            if (multiple)
                proposed.state_.targets.push_back({{},
                                                   binding.object_id,
                                                   binding.incarnation,
                                                   binding.content_revision,
                                                   target.wake});
        }
        if (!multiple) {
            const auto &binding = targets.front().object;
            proposed.state_.entity = binding.object_id;
            proposed.state_.instance_generation = binding.incarnation;
            proposed.state_.content_revision = binding.content_revision;
        }
        proposed.bind_scope(view);
        proposed.update(view, now);
        if (!proposed.state_.active)
            return false; // A rejected replacement leaves prior attention intact.
        *this = std::move(proposed);
        return true;
    }
    // DEC-131: only an explicitly lower-priority, confirmed identity can defer
    // a position/appearance wake. All events remain in the consumer view.
    bool wakes(const PerceptionEvent &event) const {
        if (!state_.active || state_.mode != "objects" ||
            event.kind != "position_or_appearance_changed" || !event.confirmed ||
            !event.source_epoch || !event.entity || !event.instance_generation ||
            !std::isfinite(event.x) || !std::isfinite(event.y) || !std::isfinite(event.width) ||
            !std::isfinite(event.height) || event.width <= 0 || event.height <= 0)
            return true;
        const auto overlaps = [&](const desktop::WindowGeometry &r) {
            return event.x < std::int64_t(r.x) + r.width &&
                   event.y < std::int64_t(r.y) + r.height && event.x + event.width > r.x &&
                   event.y + event.height > r.y;
        };
        bool deferred = false;
        for (const auto &target : state_.targets) {
            const bool identity = event.source_epoch == epoch_ && event.entity == target.entity &&
                                  event.instance_generation == target.instance_generation;
            if (target.wake && (identity || overlaps(target.region)))
                return true;
            deferred = deferred || (!target.wake && identity);
        }
        return !deferred;
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
        else if (!objects_.empty()) {
            for (std::size_t i = 0; i < objects_.size(); ++i) {
                desktop::WindowGeometry bounds;
                if (const auto *reason = object_status(objects_[i].object, view, now, bounds)) {
                    invalidate(reason);
                    break;
                }
                if (state_.mode == "objects")
                    state_.targets[i].region = bounds;
                else
                    state_.region = bounds;
            }
        }
        return state_;
    }
};
} // namespace mirage::integration::detail
