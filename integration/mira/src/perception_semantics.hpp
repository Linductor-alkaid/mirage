#pragma once

#include <map>
#include <mira/model_digest.hpp>
#include <mirage/conversation.hpp>
#include <mirage/integration/desktop_perception.hpp>

namespace mirage::integration::detail {
// The existing serialized harness task owns annotations. Capture workers only
// publish immutable observation snapshots; no additional communication path.
class PerceptionSemantics {
    using Clock = std::chrono::steady_clock;
    using Json = mira::JsonValue;
    std::optional<desktop::VisualScene> scene_;
    std::map<std::string, desktop::VisualSemanticBinding> offered_, trusted_;
    Clock::time_point observed_{};
    std::string error_;
    std::size_t rotation_ = 0;
    std::uint64_t source_frames_ = 0;
    Clock::time_point source_progress_at_{};

  public:
    void sync(const PerceptionView &view, Clock::time_point now = Clock::now()) {
        error_ = view.scene_error;
        if (view.frames != source_frames_) {
            source_frames_ = view.frames;
            source_progress_at_ = now;
        }
        // DEC-117: static OCR content remains advisory while the resident source
        // keeps sampling. Keep its original analysis age; never renew completion evidence.
        const bool sampling = view.frames && (view.state == "sampling" || view.state == "paused") &&
                              now - source_progress_at_ < std::chrono::seconds(5);
        if (view.state == "failed" || view.state == "stopped" || view.state == "degraded" ||
            !view.scene || !view.scene->ok || !view.scene->reset ||
            view.scene->updated.size() > 256 || view.scene_observed_at > now ||
            (!sampling && now - view.scene_observed_at >= std::chrono::seconds(120))) {
            scene_.reset();
            trusted_.clear();
            if (error_.empty())
                error_ = "semantic_scene_unavailable_or_expired";
            return;
        }
        const auto &snapshot = *view.scene;
        auto previous = scene_ ? scene_->snapshot() : desktop::VisualSceneDelta{};
        if (!scene_ || previous.source_session != snapshot.source_session) {
            scene_.emplace(snapshot.source_session);
            trusted_.clear();
            previous = scene_->snapshot();
        }
        if (previous.context_revision == snapshot.context_revision &&
            previous.frame_sequence == snapshot.frame_sequence)
            return;
        std::vector<desktop::VisualSceneObject> objects;
        for (const auto &record : snapshot.updated)
            objects.push_back(record.object);
        const auto result = scene_->apply({snapshot.source_session,
                                           snapshot.context_revision,
                                           snapshot.frame_sequence,
                                           scene_->revision(),
                                           true,
                                           objects,
                                           {}});
        if (!result.ok) {
            error_ = result.error.code;
            scene_.reset();
            trusted_.clear();
            return;
        }
        observed_ = view.scene_observed_at;
    }
    Json project() {
        offered_.clear();
        trusted_.clear();
        Json::Array output;
        std::size_t bytes = 0, count = 0, labeled_bytes = 0;
        if (scene_) {
            const auto snapshot = scene_->snapshot();
            count = snapshot.updated.size();
            std::vector<const desktop::VisualSceneRecord *> ordered;
            if (count)
                for (std::size_t i = 0; i < count; ++i) {
                    const auto &record = snapshot.updated[(i + rotation_) % count];
                    if (record.binding)
                        ordered.push_back(&record);
                }
            const bool has_unlabeled = ordered.size() < count;
            if (count)
                for (std::size_t i = 0; i < count; ++i) {
                    const auto &record = snapshot.updated[(i + rotation_) % count];
                    if (!record.binding)
                        ordered.push_back(&record);
                }
            ++rotation_;
            for (const auto *record : ordered) {
                const auto &o = record->object;
                const bool candidate =
                    o.identity_confirmed && !o.pending && (!o.text.empty() || !o.label.empty());
                const auto identity = snapshot.source_session + ":" +
                                      std::to_string(snapshot.context_revision) + ":" +
                                      std::to_string(o.id) + ":" + std::to_string(o.incarnation) +
                                      ":" + std::to_string(o.content_revision) + ":" +
                                      std::to_string(snapshot.frame_sequence);
                const auto ref = candidate ? mira::digest_string(identity).to_string() : "";
                const auto state = record->semantic_state == desktop::VisualSemanticState::kActive
                                       ? "active"
                                   : record->binding ? "suspended"
                                                     : "none";
                auto text = o.text, category = o.label;
                conversation::truncate_text(text, 256);
                conversation::truncate_text(category, 256);
                Json item{Json::Object{
                    {"object_ref", ref},
                    {"entity", static_cast<std::int64_t>(o.id)},
                    {"instance_generation", static_cast<std::int64_t>(o.incarnation)},
                    {"content_revision", static_cast<std::int64_t>(o.content_revision)},
                    {"text", text},
                    {"category", category},
                    {"content_truncated", text != o.text || category != o.label},
                    {"semantic_state", state},
                    {"identity_confirmed", o.identity_confirmed},
                    {"pending", o.pending},
                    {"x", o.bounds.x},
                    {"y", o.bounds.y},
                    {"width", o.bounds.width},
                    {"height", o.bounds.height}}};
                if (record->binding) {
                    item.set("semantic_label", record->binding->label);
                    item.set("annotation_source", "llm_annotation");
                    item.set("annotation_request_id", record->binding->model_request_id);
                    item.set("annotation_frame",
                             static_cast<std::int64_t>(record->binding->frame_sequence));
                }
                const auto size = mira::to_json_string(item).size();
                if (output.size() >= 16 || bytes + size > 2800 ||
                    (has_unlabeled && record->binding && labeled_bytes + size > 1800))
                    continue;
                bytes += size;
                if (record->binding)
                    labeled_bytes += size;
                output.push_back(std::move(item));
                if (candidate)
                    offered_.emplace(ref, desktop::VisualSemanticBinding{snapshot.source_session,
                                                                         {},
                                                                         {},
                                                                         snapshot.context_revision,
                                                                         o.id,
                                                                         o.incarnation,
                                                                         o.content_revision,
                                                                         snapshot.frame_sequence});
            }
        }
        return Json{
            Json::Object{{"objects", Json{output}},
                         {"truncated", output.size() < count},
                         {"total_objects", static_cast<std::int64_t>(count)},
                         {"error", error_},
                         {"dependency_scope", "recognized_text_and_category"},
                         {"input_actionable", false},
                         {"coordinate_space", "shared_source_pixels"},
                         {"age_ms", scene_ ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 Clock::now() - observed_)
                                                 .count()
                                           : std::int64_t{0}}}};
    }
    void consumed(const std::string &request_id) {
        trusted_ = offered_;
        for (auto &[ref, binding] : trusted_) {
            (void)ref;
            binding.model_request_id = request_id;
        }
    }
    desktop::VisualSceneDelta annotate(const std::string &ref, const std::string &label) {
        const auto it = trusted_.find(ref);
        if (!scene_ || it == trusted_.end()) {
            desktop::VisualSceneDelta result;
            result.error = {"semantic_reference_not_supplied",
                            "Object reference was not supplied to the completed model request"};
            return result;
        }
        auto binding = it->second;
        binding.label = label;
        return scene_->annotate(binding);
    }
};
} // namespace mirage::integration::detail
