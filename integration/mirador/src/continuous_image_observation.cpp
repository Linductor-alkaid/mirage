#include <algorithm>
#include <mirador/cache_policy.hpp>
#include <mirage/conversation.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_observation_mapper.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>

namespace mirage::integration {
PerceptionSample VisualObservationPipeline::observe_image(
    const desktop::ImageFrame &image, const std::string &key,
    const std::optional<desktop::WindowGeometry> &focus, const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    PerceptionSample out;
    if (!config_.session.enable_background_session || key.empty() || key.size() > 256 ||
        image.width <= 0 || image.height <= 0 || std::max(image.width, image.height) > 1600) {
        out.error = {"invalid_argument", "continuous image/source is unavailable"};
        return out;
    }
    const auto end =
        std::min(deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds(3)),
                 std::chrono::steady_clock::now() + std::chrono::seconds(3));
    const desktop::CancelToken bounded(
        [cancel, end] { return cancel.cancelled() || std::chrono::steady_clock::now() >= end; });
    std::uint64_t sequence, revision, policy;
    bool followup;
    {
        std::lock_guard lock(sequence_mutex_);
        sequence = ++sequence_;
        if (background_key_ != key || background_width_ != image.width ||
            background_height_ != image.height) {
            background_key_ = key;
            background_width_ = image.width;
            background_height_ = image.height;
            ++background_revision_;
            background_followup_ = true;
        }
        const auto equal_focus = [&] {
            if (focus.has_value() != background_focus_.has_value())
                return false;
            return !focus ||
                   (focus->x == background_focus_->x && focus->y == background_focus_->y &&
                    focus->width == background_focus_->width &&
                    focus->height == background_focus_->height);
        };
        if (!equal_focus()) {
            background_focus_ = focus;
            ++background_policy_;
            background_followup_ = true;
        }
        revision = background_revision_;
        policy = background_policy_;
        followup = background_followup_;
    }
    auto frame =
        to_mirador_frame(image, config_.source_id, sequence, std::chrono::steady_clock::now());
    if (!frame) {
        out.error = {"invalid_frame", "continuous frame layout is invalid"};
        return out;
    }
    VisualAnalysisRequest request;
    request.frame = frame.take_value();
    request.image_session = request.background_session = true;
    request.analyze_change = true;
    request.run_ocr = true;
    request.fuse = true;
    request.ocr.cache_policy = mirador::CachePolicy::kRefresh;
    request.ocr.output_space = mirador::CoordinateSpaceId::kOriented;
    request.run_detector = config_.background_detector && bool(config_.session.detector_backend);
    request.detector.min_confidence = config_.detector_min_confidence;
    request.detector.cache_policy = mirador::CachePolicy::kRefresh;
    request.detector.output_space = mirador::CoordinateSpaceId::kOriented;
    request.continuous = VisualContinuousRequest{};
    request.continuous->source_revision = revision;
    request.continuous->full_frame_evidence_digest = 1;
    request.continuous->importance.allow_bootstrap_full_frame = true;
    // Full-frame OCR was actually executed; cover the complete image, including
    // previously known entities that have just disappeared. Focus only controls
    // product notification filtering; it must not hide transient outside arrivals.
    mirador::AttentionCondition condition;
    condition.roi = mirador::RectF{0, 0, float(image.width), float(image.height)};
    request.continuous->attention.conditions.push_back(condition);
    request.analysis_policy_revision = policy;
    request.admission =
        followup ? VisualAnalysisAdmission::always : VisualAnalysisAdmission::when_changed;
    request.external_cancel = bounded;
    request.deadline = end;
    auto future = session_.submit(std::move(request));
    if (future.wait_until(end) != std::future_status::ready) {
        out.error = {"deadline_exceeded", "continuous analysis did not settle"};
        return out;
    }
    auto result = future.get();
    // Completed cycles have already committed/drained Mirador's events. A
    // late resource pause must not discard them; the session owner still checks
    // task/authority cancellation before delivering any history.
    if (bounded.cancelled() && result.outcome.kind != VisualAnalysisOutcome::Kind::kCompleted) {
        out.cancelled = true;
        return out;
    }
    if (result.outcome.kind == VisualAnalysisOutcome::Kind::kSkippedUnchanged ||
        (result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected &&
         result.outcome.message == "foreground analysis owns worker")) {
        out.ok = out.skipped = true;
        return out;
    }
    if (result.outcome.kind != VisualAnalysisOutcome::Kind::kCompleted || !result.continuous) {
        out.cancelled = result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled;
        out.error = {result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected
                         ? "analysis_rejected"
                         : "analysis_failed",
                     result.outcome.message.empty() ? "background analysis did not complete"
                                                    : result.outcome.message.substr(0, 240)};
        return out;
    }
    {
        std::lock_guard lock(sequence_mutex_);
        if (background_key_ != key || background_revision_ != revision) {
            out.cancelled = true;
            return out;
        }
        background_followup_ = (result.admission_change ? result.admission_change->classification
                                                        : result.change.classification) !=
                               mirador::ChangeClassification::kNone;
    }
    const auto &continuous = *result.continuous;
    out.ok = true;
    out.source_epoch = continuous.source_epoch;
    out.frame_sequence = sequence;
    out.dropped = continuous.events.dropped_total;
    out.resync = continuous.cycle.resync_required;
    out.objects_complete = bool(result.snapshot) && continuous.temporal.entities.size() <= 256;
    if (out.objects_complete)
        for (const auto &entity : continuous.temporal.entities) {
            if (!entity.instance_generation)
                continue;
            const bool ended =
                std::any_of(continuous.events.events.begin(), continuous.events.events.end(),
                            [&](const auto &event) {
                                return event.stable_id == entity.stable_id &&
                                       event.instance_generation == entity.instance_generation &&
                                       event.semantic == mirador::EventSemantic::kEnd;
                            });
            if (ended)
                continue;
            PerceptionObject entry;
            const auto *region = mirador::find_region(*result.snapshot, entity.stable_id);
            entry.content_observed = region && entity.observed_frame == sequence &&
                                     entity.observation == mirador::ObservationState::kObserved &&
                                     (entity.freshness == mirador::Freshness::kFresh ||
                                      entity.freshness == mirador::Freshness::kValidatedReuse);
            if (entry.content_observed)
                entry.object = to_visual_scene_object(*region, entity.instance_generation, 1,
                                                      entity.confirmed_frame != 0);
            else {
                entry.object.id = entity.stable_id;
                entry.object.incarnation = entity.instance_generation;
                entry.object.pending = true;
            }
            // A major layout replacement may reuse the same text at the same
            // coordinates. Do not carry task meaning across that ambiguity.
            entry.invalidate_content =
                (result.admission_change && result.admission_change->classification ==
                                                mirador::ChangeClassification::kGlobal) ||
                std::any_of(continuous.events.events.begin(), continuous.events.events.end(),
                            [&](const auto &event) {
                                return event.stable_id == entity.stable_id &&
                                       event.change_kind ==
                                           mirador::SemanticChangeKind::kRelationChanged;
                            });
            out.objects.push_back(std::move(entry));
        }
    const auto append = [&](std::uint64_t entity, const mirador::RectF &bounds, const char *kind,
                            bool confirmed, std::uint64_t instance) {
        if (out.events.size() >= 256) {
            out.resync = true;
            return;
        }
        PerceptionEvent event;
        event.entity = entity;
        event.instance_generation = instance;
        event.source_epoch = out.source_epoch;
        event.frame_sequence = image.source_sequence;
        event.kind = kind;
        event.confirmed = confirmed;
        event.observed_at = std::chrono::steady_clock::now();
        event.x = bounds.x;
        event.y = bounds.y;
        event.width = bounds.width;
        event.height = bounds.height;
        if (result.snapshot)
            if (const auto *region = mirador::find_region(*result.snapshot, entity)) {
                event.text = region->text;
                conversation::truncate_text(event.text, 256);
            }
        out.events.push_back(std::move(event));
    };
    for (const auto &event : continuous.events.events) {
        if (event.window.source_epoch != out.source_epoch)
            continue;
        const char *kind = "changed";
        switch (event.change_kind) {
        case mirador::SemanticChangeKind::kAppeared:
            kind = "appeared";
            break;
        case mirador::SemanticChangeKind::kTextChanged:
            kind = "text_changed";
            break;
        case mirador::SemanticChangeKind::kAppearanceChanged:
            kind = "position_or_appearance_changed";
            break;
        case mirador::SemanticChangeKind::kRelationChanged:
            kind = "relation_changed";
            break;
        case mirador::SemanticChangeKind::kDisappeared:
            kind = "disappeared";
            break;
        case mirador::SemanticChangeKind::kSourceReset:
            kind = "source_reset";
            break;
        }
        append(event.stable_id, event.bounds, kind, true, event.instance_generation);
    }
    if (result.admission_change &&
        result.admission_change->classification != mirador::ChangeClassification::kNone) {
        for (const auto &roi : result.admission_change->changed_regions)
            append(0,
                   mirador::RectF{float(roi.x), float(roi.y), float(roi.width), float(roi.height)},
                   "visual_region_changed", false, 0);
    }
    for (const auto &entity : continuous.temporal.entities)
        if (!entity.confirmed_frame && entity.observed_frame == sequence &&
            entity.observation == mirador::ObservationState::kObserved)
            append(entity.stable_id, entity.bounds, "observed_unconfirmed", false,
                   entity.instance_generation);
    return out;
}
} // namespace mirage::integration
