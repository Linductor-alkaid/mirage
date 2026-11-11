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
    const std::optional<std::chrono::steady_clock::time_point> &deadline,
    std::optional<std::chrono::steady_clock::time_point> captured_at,
    std::uint64_t sampling_generation, const PerceptionRecognitionWork &work) {
    PerceptionSample out;
    if ((sampling_generation != 0) != captured_at.has_value() ||
        (captured_at && (*captured_at == std::chrono::steady_clock::time_point{} ||
                         *captured_at > std::chrono::steady_clock::now()))) {
        out.error = {"invalid_argument", "capture provenance is incomplete"};
        return out;
    }
    if (sampling_generation && config_.session.enable_object_tracking) {
        out.error = {"invalid_argument", "external sampling requires worker tracking disabled"};
        return out;
    }
    if ((!work.ocr && !work.detector) || (!work.roi && (!work.ocr || !work.detector))) {
        out.error = {"invalid_argument",
                     "complete recognition requires both available capabilities"};
        return out;
    }
    if (work.roi) {
        const auto &r = *work.roi;
        if (!sampling_generation || r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
            std::int64_t(r.x) + r.width > image.width ||
            std::int64_t(r.y) + r.height > image.height ||
            std::int64_t(r.width) * r.height > std::int64_t(image.width) * image.height / 4) {
            out.error = {"invalid_argument", "regional recognition ROI/provenance is invalid"};
            return out;
        }
        std::lock_guard lock(sequence_mutex_);
        const bool same_focus =
            focus.has_value() == background_focus_.has_value() &&
            (!focus || (focus->x == background_focus_->x && focus->y == background_focus_->y &&
                        focus->width == background_focus_->width &&
                        focus->height == background_focus_->height));
        if (background_key_ != key || background_width_ != image.width ||
            background_height_ != image.height || sampling_generation_ != sampling_generation ||
            !same_focus || !background_recognition_.epoch) {
            out.error = {"recognition_requires_bootstrap", "regional source has no full baseline"};
            return out;
        }
    }
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
    bool followup, sample_media = false, force_age_refresh = false;
    const auto observed_at = captured_at.value_or(std::chrono::steady_clock::now());
    {
        std::lock_guard lock(sequence_mutex_);
        sequence = ++sequence_;
        if (background_key_ != key || background_width_ != image.width ||
            background_height_ != image.height || sampling_generation_ != sampling_generation) {
            sampling_generation_ = sampling_generation;
            background_key_ = key;
            background_width_ = image.width;
            background_height_ = image.height;
            ++background_revision_;
            background_followup_ = true;
            media_ = {}; // new source incarnation: media tracking restarts
            background_recognition_ = {};
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
            background_recognition_ = {};
        }
        revision = background_revision_;
        policy = background_policy_;
        followup = background_followup_;
        if (config_.experimental_recognition_refresh.count() > 0) {
            force_age_refresh =
                background_recognition_.observed_at == std::chrono::steady_clock::time_point{} ||
                observed_at - background_recognition_.observed_at >=
                    config_.experimental_recognition_refresh;
            sample_media = !force_age_refresh && background_recognition_.ready && media_.active;
        }
    }
    if (sampling_generation) {
        sample_media = false;
        force_age_refresh = true;
    }
    auto frame = to_mirador_frame(image, config_.source_id, sequence, observed_at);
    if (!frame) {
        out.error = {"invalid_frame", "continuous frame layout is invalid"};
        return out;
    }
    VisualAnalysisRequest request;
    request.frame = frame.take_value();
    request.image_session = request.background_session = true;
    request.analyze_change = true;
    request.run_ocr = work.ocr;
    request.fuse = true;
    request.ocr.cache_policy = mirador::CachePolicy::kRefresh;
    request.ocr.output_space = mirador::CoordinateSpaceId::kOriented;
    request.run_detector =
        work.detector && config_.background_detector && bool(config_.session.detector_backend);
    request.detector.min_confidence = config_.detector_min_confidence;
    request.detector.cache_policy = mirador::CachePolicy::kRefresh;
    request.detector.output_space = mirador::CoordinateSpaceId::kOriented;
    const PerceptionRecognitionWork executed{request.run_ocr, request.run_detector, work.roi};
    if (work.roi) {
        if (!request.run_ocr && !request.run_detector) {
            out.error = {"backend_unavailable", "requested regional capability is unavailable"};
            return out;
        }
        const auto &r = *work.roi;
        const mirador::RectF roi{float(r.x), float(r.y), float(r.width), float(r.height)};
        request.ocr.roi = request.detector.roi = roi;
        request.analyze_change = request.fuse = false;
        request.regional_only = true;
        request.external_cancel = bounded;
        request.deadline = end;
        auto pending = session_.submit(std::move(request));
        if (pending.wait_until(end) != std::future_status::ready) {
            out.error = {"deadline_exceeded", "regional recognition did not settle"};
            return out;
        }
        auto result = pending.get();
        if (result.outcome.kind != VisualAnalysisOutcome::Kind::kCompleted) {
            out.cancelled = result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled;
            out.skipped = result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected &&
                          result.outcome.message == "foreground analysis owns worker";
            out.ok = out.skipped;
            out.error = {"regional_analysis_failed", result.outcome.message.substr(0, 240)};
            return out;
        }
        {
            std::lock_guard lock(sequence_mutex_);
            if (background_key_ != key || background_revision_ != revision ||
                background_policy_ != policy || bounded.cancelled()) {
                out.cancelled = true;
                return out;
            }
            out.source_epoch = background_recognition_.epoch;
            out.dropped = background_recognition_.dropped;
            out.media = {media_.active, media_.x, media_.y, media_.width, media_.height};
        }
        out.ok = true;
        out.frame_sequence = sequence;
        out.recognition = executed;
        out.recognition_reason = "regional_unconfirmed";
        const auto append_region = [&](const auto &bounds, std::string text, const char *kind) {
            PerceptionEvent event;
            event.source_epoch = out.source_epoch;
            event.frame_sequence = image.source_sequence;
            event.kind = kind;
            conversation::truncate_text(text, 256);
            event.text = std::move(text);
            event.x = bounds.x;
            event.y = bounds.y;
            event.width = bounds.width;
            event.height = bounds.height;
            event.observed_at = observed_at;
            out.events.push_back(std::move(event));
        };
        for (const auto &line : result.text)
            append_region(line.bounds, line.utf8_text, "regional_text_observed");
        for (const auto &detection : result.detections)
            append_region(detection.bounds, detection.label, "regional_detection_observed");
        if (out.events.empty())
            append_region(roi, {}, "regional_observed_empty");
        return out;
    }
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
    out.recognition_reason = force_age_refresh ? "age_refresh" : "recognition";
    if (sample_media) {
        auto probe = request; // shared frame owner, no extra capture/pixel copy
        probe.sample_only = true;
        probe.run_ocr = probe.run_detector = probe.fuse = false;
        auto pending = session_.submit(std::move(probe));
        if (pending.wait_until(end) != std::future_status::ready) {
            out.error = {"deadline_exceeded", "background CV sample did not settle"};
            return out;
        }
        auto sampled = pending.get();
        out.tracking = sampled.tracking;
        if (sampled.outcome.kind == VisualAnalysisOutcome::Kind::kSampled && sampled.sampled &&
            sampled.admission_change) {
            std::lock_guard lock(sequence_mutex_);
            if (background_key_ != key || background_revision_ != revision ||
                background_policy_ != policy ||
                sampled.sampled->source_epoch != background_recognition_.epoch ||
                bounded.cancelled()) {
                out.cancelled = true;
                return out;
            }
            const double left = media_.x * image.width, top = media_.y * image.height;
            const double right = left + media_.width * image.width;
            const double bottom = top + media_.height * image.height;
            const auto inside_media = [&](const auto &change) {
                return change.classification == mirador::ChangeClassification::kNone ||
                       (!change.changed_regions.empty() &&
                        std::all_of(change.changed_regions.begin(), change.changed_regions.end(),
                                    [&](const auto &r) {
                                        return r.x >= left && r.y >= top &&
                                               r.x + r.width <= right && r.y + r.height <= bottom;
                                    }));
            };
            if (media_.active && (!sampled.tracking || !sampled.tracking->requires_recognition) &&
                inside_media(sampled.change) && inside_media(*sampled.admission_change) &&
                std::chrono::steady_clock::now() - background_recognition_.observed_at <
                    config_.experimental_recognition_refresh) {
                out.ok = out.objects_complete = out.recognition_deferred = true;
                out.recognition_reason = "media_deferred_unknown";
                out.source_epoch = background_recognition_.epoch;
                out.frame_sequence = sequence;
                out.dropped = background_recognition_.dropped;
                out.media = {true, media_.x, media_.y, media_.width, media_.height};
                for (const auto &[id, instance] : background_recognition_.identities) {
                    PerceptionObject entry;
                    entry.object.id = id;
                    entry.object.incarnation = instance;
                    entry.object.pending = true;
                    entry.invalidate_content = !background_recognition_.deferred;
                    out.objects.push_back(std::move(entry));
                }
                background_recognition_.deferred = true;
                const auto now = std::chrono::steady_clock::now();
                if (sampled.change.classification != mirador::ChangeClassification::kNone &&
                    sampled.change.frame_similarity < config_.media_keyframe_similarity &&
                    now - media_.last_event >= config_.media_event_min_interval) {
                    PerceptionEvent event;
                    event.source_epoch = out.source_epoch;
                    event.frame_sequence = image.source_sequence;
                    event.kind = "visual_region_changed";
                    event.text = "keyframe";
                    event.temporal_keyframe = true;
                    event.x = left;
                    event.y = top;
                    event.width = right - left;
                    event.height = bottom - top;
                    event.observed_at = observed_at;
                    out.events.push_back(std::move(event));
                    media_.last_event = now;
                }
                return out;
            }
            out.recognition_reason = sampled.tracking && sampled.tracking->requires_recognition
                                         ? "tracking_revalidation"
                                         : "outside_media_or_expired";
        } else if (sampled.outcome.kind != VisualAnalysisOutcome::Kind::kRejected) {
            out.cancelled = sampled.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled;
            out.error = {"analysis_failed", sampled.outcome.message.substr(0, 240)};
            return out;
        } else if (sampled.outcome.message == "foreground analysis owns worker") {
            out.ok = out.skipped = true;
            return out;
        } else {
            out.recognition_reason = "bootstrap_required";
        }
        // The probe consumed an analysis sequence; recognize the same owned
        // source pixels under a new sequence, preserving the original timestamp.
        {
            std::lock_guard lock(sequence_mutex_);
            if (background_key_ != key || background_revision_ != revision ||
                background_policy_ != policy) {
                out.cancelled = true;
                return out;
            }
            request.frame.sequence = sequence = ++sequence_;
        }
        request.admission = VisualAnalysisAdmission::always;
    }
    if (force_age_refresh)
        request.admission = VisualAnalysisAdmission::always;
    if (config_.experimental_recognition_refresh.count() > 0) {
        std::lock_guard lock(sequence_mutex_);
        background_recognition_.ready = false;
    }
    auto future = session_.submit(std::move(request));
    if (future.wait_until(end) != std::future_status::ready) {
        out.error = {"deadline_exceeded", "continuous analysis did not settle"};
        return out;
    }
    auto result = future.get();
    if (result.tracking) {
        if (out.tracking) {
            result.tracking->verified += out.tracking->verified;
            result.tracking->reused += out.tracking->reused;
            result.tracking->excluded += out.tracking->excluded;
            if (result.tracking->error.empty())
                result.tracking->error = out.tracking->error;
            result.tracking->candidates = std::move(out.tracking->candidates);
            for (auto &track : result.tracking->tracks)
                for (const auto &previous : out.tracking->tracks)
                    if (track.entity == previous.entity &&
                        track.instance_generation == previous.instance_generation) {
                        track.discontinuous = track.discontinuous || previous.discontinuous;
                        track.reacquired = track.reacquired || previous.reacquired;
                    }
        }
        out.tracking = std::move(result.tracking);
    }
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
        if (background_key_ != key || background_revision_ != revision ||
            background_policy_ != policy) {
            out.cancelled = true;
            return out;
        }
        background_followup_ = (result.admission_change ? result.admission_change->classification
                                                        : result.change.classification) !=
                               mirador::ChangeClassification::kNone;
    }
    const auto &continuous = *result.continuous;
    out.ok = true;
    out.recognition = executed;
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
                (out.tracking &&
                 std::any_of(out.tracking->tracks.begin(), out.tracking->tracks.end(),
                             [&](const auto &track) {
                                 return track.entity == entity.stable_id &&
                                        track.instance_generation == entity.instance_generation &&
                                        track.discontinuous;
                             })) ||
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
                            bool confirmed, std::uint64_t instance, const std::string &text = {},
                            bool temporal_keyframe = false) {
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
        event.temporal_keyframe = temporal_keyframe;
        event.observed_at = observed_at;
        event.x = bounds.x;
        event.y = bounds.y;
        event.width = bounds.width;
        event.height = bounds.height;
        if (!text.empty()) {
            event.text = text;
            conversation::truncate_text(event.text, 256);
        } else if (result.snapshot)
            if (const auto *region = mirador::find_region(*result.snapshot, entity)) {
                event.text = region->text;
                conversation::truncate_text(event.text, 256);
            }
        out.events.push_back(std::move(event));
    };
    bool semantic_events = false;
    for (const auto &event : continuous.events.events) {
        if (event.window.source_epoch != out.source_epoch)
            continue;
        semantic_events = true;
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
    // DEC-118 significance gating. Semantic entity events above are never
    // gated; this decides which bare pixel-change ROIs still reach the model,
    // advances the media-activity state machine, and emits media playback
    // state plus throttled keyframes instead of per-sample playback noise.
    const mirador::ChangeReport *change = nullptr;
    if (result.admission_change &&
        result.admission_change->classification != mirador::ChangeClassification::kNone)
        change = &*result.admission_change;
    else if (result.change.classification != mirador::ChangeClassification::kNone)
        change = &result.change;
    const double frame_area = double(image.width) * double(image.height);
    const auto rect_area = [](const mirador::RectF &r) {
        return double(r.width) * double(r.height);
    };
    const auto overlap_ratio = [&](const mirador::RectF &a, const mirador::RectF &b) {
        const double x = std::max(a.x, b.x), y = std::max(a.y, b.y);
        const double w = std::min(a.x + a.width, b.x + b.width) - x;
        const double h = std::min(a.y + a.height, b.y + b.height) - y;
        return w > 0 && h > 0 ? w * h / std::min(rect_area(a), rect_area(b)) : 0.0;
    };
    const auto to_rect = [](const mirador::RectI &roi) {
        return mirador::RectF{float(roi.x), float(roi.y), float(roi.width), float(roi.height)};
    };
    if (change == nullptr || !config_.significance_gate) {
        if (change != nullptr)
            for (const auto &roi : change->changed_regions)
                append(0, to_rect(roi), "visual_region_changed", false, 0);
        std::lock_guard lock(sequence_mutex_);
        out.media.active = media_.active;
        out.media.x = media_.x;
        out.media.y = media_.y;
        out.media.width = media_.width;
        out.media.height = media_.height;
    } else {
        mirador::RectF largest{};
        bool have_largest = false;
        for (const auto &roi : change->changed_regions) {
            const auto rect = to_rect(roi);
            if (!have_largest || rect_area(rect) > rect_area(largest)) {
                largest = rect;
                have_largest = true;
            }
        }
        // Stored media coordinates are frame-normalized; comparisons run in
        // analysis-frame pixels.
        const auto denorm = [&](double x, double y, double w, double h) {
            return mirador::RectF{float(x * image.width), float(y * image.height),
                                  float(w * image.width), float(h * image.height)};
        };
        const double min_media_area = config_.media_region_min_area_ratio * frame_area;
        const double tiny_area = config_.tiny_region_area_ratio * frame_area;
        const auto now = std::chrono::steady_clock::now();
        // A global layout/scene replacement (ratio >= 0.5 of the frame) is a
        // layout barrier, not playback evidence: it never confirms a media
        // candidate. An already-active region survives it — a windowed video
        // going fullscreen stays suppressed through the tracked geometry and
        // ends through the regular miss counting instead.
        const bool global = change->classification == mirador::ChangeClassification::kGlobal;
        std::lock_guard lock(sequence_mutex_);
        if (global) {
            media_.pending_x = media_.pending_y = media_.pending_width = media_.pending_height = 0;
            media_.hits = 0;
        } else if (have_largest && rect_area(largest) >= min_media_area) {
            const auto tracked = denorm(media_.x, media_.y, media_.width, media_.height);
            if (media_.active) {
                if (overlap_ratio(largest, tracked) >= 0.5) {
                    media_.x = largest.x / image.width;
                    media_.y = largest.y / image.height;
                    media_.width = largest.width / image.width;
                    media_.height = largest.height / image.height;
                    media_.misses = 0;
                    // Scene cuts and major content steps are the video's own
                    // "events"; throttle them to a bounded model-visible rate.
                    if (!semantic_events &&
                        change->frame_similarity < config_.media_keyframe_similarity &&
                        now - media_.last_event >= config_.media_event_min_interval) {
                        media_.last_event = now;
                        append(0, largest, "visual_region_changed", false, 0, "keyframe", true);
                    }
                } else if (++media_.misses >= config_.media_miss_samples) {
                    media_.active = false;
                    media_.hits = 0;
                    append(0, tracked, "media_playback", false, 0, "stopped");
                }
            } else {
                const auto pending = denorm(media_.pending_x, media_.pending_y,
                                            media_.pending_width, media_.pending_height);
                if (media_.hits > 0 && overlap_ratio(largest, pending) >= 0.5) {
                    ++media_.hits;
                } else {
                    media_.pending_x = largest.x / image.width;
                    media_.pending_y = largest.y / image.height;
                    media_.pending_width = largest.width / image.width;
                    media_.pending_height = largest.height / image.height;
                    media_.hits = 1;
                }
                if (media_.hits >= std::max(1, config_.media_confirm_samples)) {
                    media_.active = true;
                    media_.misses = 0;
                    media_.x = media_.pending_x;
                    media_.y = media_.pending_y;
                    media_.width = media_.pending_width;
                    media_.height = media_.pending_height;
                    media_.last_event = now;
                    append(0, largest, "media_playback", false, 0, "started");
                }
            }
        } else if (media_.active) {
            const auto tracked = denorm(media_.x, media_.y, media_.width, media_.height);
            if (++media_.misses >= config_.media_miss_samples) {
                media_.active = false;
                media_.hits = 0;
                append(0, tracked, "media_playback", false, 0, "stopped");
            }
        }
        // Bare ROI events: suppress media-explained changes (keyframes carry
        // the signal) and entity-free transient regions (cursor, caret, hover
        // feedback). Regions next to real entity events stay untouched.
        if (media_.active) {
            const auto tracked = denorm(media_.x, media_.y, media_.width, media_.height);
            for (const auto &roi : change->changed_regions) {
                const auto rect = to_rect(roi);
                if (overlap_ratio(rect, tracked) >= 0.5)
                    continue;
                if (!semantic_events && rect_area(rect) < tiny_area)
                    continue;
                append(0, rect, "visual_region_changed", false, 0);
            }
        } else {
            for (const auto &roi : change->changed_regions) {
                const auto rect = to_rect(roi);
                if (!semantic_events && rect_area(rect) < tiny_area)
                    continue;
                append(0, rect, "visual_region_changed", false, 0);
            }
        }
        out.media.active = media_.active;
        out.media.x = media_.x;
        out.media.y = media_.y;
        out.media.width = media_.width;
        out.media.height = media_.height;
    }
    for (const auto &entity : continuous.temporal.entities)
        if (!entity.confirmed_frame && entity.observed_frame == sequence &&
            entity.observation == mirador::ObservationState::kObserved)
            append(entity.stable_id, entity.bounds, "observed_unconfirmed", false,
                   entity.instance_generation);
    if (config_.experimental_recognition_refresh.count() > 0) {
        std::lock_guard lock(sequence_mutex_);
        if (background_key_ == key && background_revision_ == revision &&
            background_policy_ == policy) {
            background_recognition_.observed_at = observed_at;
            background_recognition_.epoch = out.source_epoch;
            background_recognition_.dropped = out.dropped;
            background_recognition_.deferred = false;
            background_recognition_.ready = out.objects_complete;
            background_recognition_.identities.clear();
            for (const auto &entry : out.objects) {
                background_recognition_.ready =
                    background_recognition_.ready && entry.content_observed &&
                    entry.object.identity_confirmed && !entry.object.pending;
                background_recognition_.identities.emplace_back(entry.object.id,
                                                                entry.object.incarnation);
            }
        }
    }
    return out;
}
} // namespace mirage::integration
