#include "visual_object_tracking.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <mirador/color_convert.hpp>
#include <mirador/crop.hpp>
#include <mirador/geometric_proposal.hpp>
#include <mirador/segment_growing_line_detector.hpp>

namespace mirage::integration::detail {
namespace {
using namespace mirador;
const char *state_name(TrackState state) {
    switch (state) {
    case TrackState::kTracking:
        return "tracking";
    case TrackState::kUncertain:
        return "uncertain";
    case TrackState::kLost:
        return "lost";
    case TrackState::kTerminated:
        return "terminated";
    }
    return "unknown";
}
desktop::WindowGeometry rect(const RectF &r) {
    return {int(std::floor(r.x)), int(std::floor(r.y)), int(std::ceil(r.width)),
            int(std::ceil(r.height))};
}
bool same_rect(const RectF &a, const RectF &b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool overlaps(const RectF &a, const RectF &b) {
    const auto w = std::min(a.x + a.width, b.x + b.width) - std::max(a.x, b.x);
    const auto h = std::min(a.y + a.height, b.y + b.height) - std::max(a.y, b.y);
    return w > 0 && h > 0 && w * h >= .5F * std::min(a.width * a.height, b.width * b.height);
}
Status interrupted(const ExecutionContext &ctx) {
    if (is_cancelled(ctx))
        return {ErrorCode::kCancelled, "object tracking cancelled"};
    if (ctx.deadline && std::chrono::steady_clock::now() >= *ctx.deadline)
        return {ErrorCode::kTimeout, "object tracking expired"};
    return Status::success();
}
} // namespace

mirador::Result<void> VisualObjectTracking::ensure_pool() {
    if (pool_)
        return {};
    mirador::ObjectTrackerOptions options;
    options.max_targets = 8;
    options.max_position_history = 4;
    options.max_templates = 1; // pinned adoption patch, no drift-learning
    options.max_negative_templates = 0;
    options.template_thumb_side = 16;
    options.pool_budget_bytes = 64 * 1024;
    options.verification_work_budget_bytes = 1 << 20;
    options.verification_roi_diagonal_ratio = .25;
    options.uncertain_frame_limit = 3;
    options.redetect_max_attempts = 2;
    options.max_redetection_records = 8;
    auto created = mirador::ObjectTracker::create(options);
    if (!created)
        return created.status();
    pool_ = created.take_value();
    return {};
}
void VisualObjectTracking::reset() {
    pool_.reset();
    bindings_.clear();
    next_ = 0;
}
PerceptionTrack VisualObjectTracking::project(const Binding &b) const {
    PerceptionTrack output;
    output.entity = b.entity;
    output.instance_generation = b.instance;
    if (const auto *track = pool_->find_track(b.token)) {
        output.state = state_name(track->state);
        output.bounds = rect(track->last_bounds);
        output.verified_sequence = track->last_verified_sequence;
    }
    return output;
}
PerceptionTracking VisualObjectTracking::seed(const mirador::Frame &frame,
                                              const mirador::SemanticSnapshot &snapshot,
                                              const mirador::TemporalSemanticState &temporal,
                                              const mirador::EventDrain &events,
                                              const mirador::ExecutionContext &ctx) try {
    PerceptionTracking output;
    output.frame_sequence = frame.sequence;
    const auto fail = [&](const mirador::Status &status) {
        reset();
        output.tracks.clear();
        output.error = std::string(mirador::error_code_name(status.code()));
        output.requires_recognition = true;
        output.pool_bytes = 0;
        return output;
    };
    if (const auto status = interrupted(ctx); !status)
        return fail(status);
    if (auto ready = ensure_pool(); !ready)
        return fail(ready.status());
    for (const auto &event : events.events) {
        if (event.semantic != mirador::EventSemantic::kEnd)
            continue;
        auto found = std::find_if(bindings_.begin(), bindings_.end(), [&](const auto &b) {
            return b.entity == event.stable_id && b.instance == event.instance_generation;
        });
        if (found == bindings_.end())
            continue;
        if (const auto *track = pool_->find_track(found->token);
            track && track->state != mirador::TrackState::kTerminated) {
            if (auto ended = pool_->terminate(found->token, frame.sequence); !ended)
                return fail(ended.status());
        }
        bindings_.erase(found);
    }
    // A recognition confirms text, not appearance continuity. Replace rather
    // than inherit when content or position differs from the private track.
    for (const auto &entity : temporal.entities) {
        const auto *region = mirador::find_region(snapshot, entity.stable_id);
        if (!region || !entity.instance_generation || !entity.confirmed_frame ||
            entity.observed_frame != frame.sequence ||
            entity.observation != mirador::ObservationState::kObserved)
            continue;
        if (output.tracks.size() >= 8 || region->bounds.width > 96 || region->bounds.height > 96 ||
            region->bounds.width * region->bounds.height > 4096 || region->text.size() > 1024 ||
            region->label.size() > 1024) {
            ++output.excluded;
            continue;
        }
        bool discontinuous = false;
        for (auto it = bindings_.begin(); it != bindings_.end();) {
            if (it->entity != entity.stable_id || it->instance == entity.instance_generation) {
                ++it;
                continue;
            }
            if (const auto *track = pool_->find_track(it->token);
                track && track->state != mirador::TrackState::kTerminated) {
                if (auto ended = pool_->terminate(it->token, frame.sequence); !ended)
                    return fail(ended.status());
            }
            it = bindings_.erase(it);
            discontinuous = true;
        }
        auto binding = std::find_if(bindings_.begin(), bindings_.end(), [&](const auto &b) {
            return b.entity == entity.stable_id && b.instance == entity.instance_generation;
        });
        if (binding != bindings_.end()) {
            const auto *track = pool_->find_track(binding->token);
            if (track && track->state == mirador::TrackState::kTracking &&
                track->semantics.text == region->text && track->semantics.label == region->label &&
                same_rect(track->last_bounds, region->bounds)) {
                auto item = project(*binding);
                item.evidence = "recognized_content";
                output.tracks.push_back(std::move(item));
                continue;
            }
            if (track && track->state != mirador::TrackState::kTerminated) {
                if (auto ended = pool_->terminate(binding->token, frame.sequence); !ended)
                    return fail(ended.status());
            }
            bindings_.erase(binding);
            discontinuous = true;
        }
        if (next_ == std::numeric_limits<std::uint64_t>::max())
            return fail({mirador::ErrorCode::kBudgetExceeded, "tracking identity exhausted"});
        auto candidate = *region;
        candidate.stable_id = ++next_;
        auto adopted = pool_->adopt_track(candidate, frame.image, frame.sequence, ctx);
        if (!adopted)
            return fail(adopted.status());
        output.evicted += adopted.value().evicted_track_ids.size();
        std::erase_if(bindings_, [&](const auto &b) { return !pool_->find_track(b.token); });
        bindings_.push_back({candidate.stable_id, entity.stable_id, entity.instance_generation});
        auto item = project(bindings_.back());
        item.evidence = "adopted_recognition";
        item.discontinuous = discontinuous;
        output.tracks.push_back(std::move(item));
    }
    if (const auto status = interrupted(ctx); !status)
        return fail(status);
    output.pool_bytes = pool_->byte_size();
    output.layout_generation = pool_->layout_generation();
    return output;
} catch (...) {
    reset();
    PerceptionTracking output;
    output.frame_sequence = frame.sequence;
    output.requires_recognition = true;
    output.error = "tracking_failed";
    return output;
}

PerceptionTracking VisualObjectTracking::seed_observed(const mirador::Frame &frame,
                                                       const PerceptionSample &sample,
                                                       const mirador::ExecutionContext &ctx) try {
    mirador::SemanticSnapshot snapshot;
    mirador::TemporalSemanticState temporal;
    snapshot.frame_sequence = frame.sequence;
    std::size_t excluded = 0;
    if (sample.objects_complete && sample.objects.size() <= 256 && sample.ok &&
        !sample.recognition_deferred && !sample.skipped && !sample.cancelled)
        for (const auto &entry : sample.objects) {
            const auto &o = entry.object;
            if (!entry.content_observed || !o.identity_confirmed || o.pending || !o.id ||
                !o.incarnation)
                continue;
            if (snapshot.regions.size() >= 8 || o.text.size() > 1024 || o.label.size() > 1024 ||
                o.bounds.width <= 0 || o.bounds.height <= 0 || o.bounds.width > 96 ||
                o.bounds.height > 96 || std::int64_t(o.bounds.width) * o.bounds.height > 4096) {
                ++excluded;
                continue;
            }
            mirador::VisualRegion region;
            region.stable_id = o.id;
            region.bounds = {float(o.bounds.x), float(o.bounds.y), float(o.bounds.width),
                             float(o.bounds.height)};
            region.text = o.text;
            region.label = o.label;
            region.confidence = static_cast<float>(o.confidence);
            snapshot.regions.push_back(std::move(region));
            mirador::TemporalEntityState entity;
            entity.stable_id = o.id;
            entity.instance_generation = o.incarnation;
            entity.confirmed_frame = entity.observed_frame = frame.sequence;
            entity.observation = mirador::ObservationState::kObserved;
            temporal.entities.push_back(entity);
        }
    auto result = seed(frame, snapshot, temporal, {}, ctx);
    result.excluded += excluded;
    return result;
} catch (...) {
    reset();
    PerceptionTracking out;
    out.requires_recognition = true;
    out.error = "tracking_failed";
    return out;
}

void VisualObjectTracking::geometry(const mirador::Frame &frame,
                                    const mirador::ChangeReport &change, PerceptionTracking &output,
                                    const mirador::ExecutionContext &ctx) {
    if (change.changed_regions.empty())
        return;
    const auto roi = *std::max_element(
        change.changed_regions.begin(), change.changed_regions.end(),
        [](const auto &a, const auto &b) { return a.width * a.height < b.width * b.height; });
    if (roi.width > 128 || roi.height > 128 || roi.width <= 0 || roi.height <= 0) {
        ++output.excluded;
        return;
    }
    auto cropped = mirador::crop(frame.image, roi, 128 * 128 * 4);
    if (!cropped) {
        output.error = mirador::error_code_name(cropped.status().code());
        output.requires_recognition = true;
        return;
    }
    auto gray =
        mirador::convert_color(cropped.value().view(), mirador::PixelFormat::kGray8, 128 * 128);
    if (!gray) {
        output.error = mirador::error_code_name(gray.status().code());
        output.requires_recognition = true;
        return;
    }
    mirador::SegmentGrowingParams params;
    params.max_segments = 128;
    params.work_budget_bytes = 128 * 128;
    mirador::SegmentGrowingLineDetector detector(params);
    auto lines = detector.detect(gray.value().view(), {}, ctx);
    if (!lines) {
        output.error = mirador::error_code_name(lines.status().code());
        output.requires_recognition = true;
        return;
    }
    auto merged = mirador::merge_collinear(lines.value().segments, {});
    if (!merged) {
        output.error = mirador::error_code_name(merged.status().code());
        output.requires_recognition = true;
        return;
    }
    mirador::GeometricProposalParams proposals;
    proposals.max_segments = 128;
    proposals.max_proposals = 8;
    auto regions = mirador::propose_regions(merged.value(), proposals, ctx);
    if (!regions) {
        output.error = mirador::error_code_name(regions.status().code());
        output.requires_recognition = true;
        return;
    }
    for (const auto &region : regions.value()) {
        const auto &r = region.tight_bounds;
        mirador::RectF bounds{float(r.x + roi.x), float(r.y + roi.y), float(r.width),
                              float(r.height)};
        output.candidates.push_back(rect(bounds));
        if (std::none_of(bindings_.begin(), bindings_.end(), [&](const auto &b) {
                const auto *track = pool_->find_track(b.token);
                return track && track->state == mirador::TrackState::kTracking &&
                       overlaps(bounds, track->last_bounds);
            }))
            output.requires_recognition = true;
    }
}

mirador::Result<PerceptionTracking>
VisualObjectTracking::sample(const mirador::Frame &frame, const mirador::ChangeReport &change,
                             const mirador::ExecutionContext &ctx) try {
    const auto fail = [&](const mirador::Status &status) -> mirador::Result<PerceptionTracking> {
        reset(); // no multi-track transaction is published after partial failure
        return status;
    };
    if (const auto status = interrupted(ctx); !status)
        return fail(status);
    PerceptionTracking output;
    output.frame_sequence = frame.sequence;
    if (change.classification == mirador::ChangeClassification::kGlobal) {
        reset();
        output.error = "global_layout_barrier";
        output.requires_recognition = true;
        return output;
    }
    if (!pool_) {
        output.error = "tracking_bootstrap_required";
        output.requires_recognition = true;
        return output;
    }
    auto gate = pool_->evaluate_change_gate(change, ctx);
    if (!gate)
        return fail(gate.status());
    for (auto &binding : bindings_) {
        const auto *track = pool_->find_track(binding.token);
        if (!track)
            continue;
        auto item = project(binding);
        if (track->state == mirador::TrackState::kTerminated) {
            item.evidence = "closed";
            output.requires_recognition = true;
            output.tracks.push_back(std::move(item));
            continue;
        }
        const auto verdict =
            std::find_if(gate.value().tracks.begin(), gate.value().tracks.end(),
                         [&](const auto &g) { return g.track_id == binding.token; });
        if (verdict != gate.value().tracks.end() &&
            verdict->decision == mirador::ChangeGateDecision::kReuse) {
            ++output.reused;
            item.evidence = "approximate_change_gate"; // never refreshes verified_sequence
        } else {
            bool hold = false;
            if (track->state == mirador::TrackState::kLost) {
                auto redetect = pool_->evaluate_redetection_gate(
                    binding.token, change.classification, frame.sequence, ctx);
                if (!redetect)
                    return fail(redetect.status());
                hold = redetect.value().verdict != mirador::RedetectionGateVerdict::kTrigger;
            }
            if (hold) {
                item.evidence = "redetection_held";
            } else {
                auto verification =
                    pool_->verify_track(binding.token, frame.image, std::nullopt, ctx);
                if (!verification) {
                    if (verification.status().code() != mirador::ErrorCode::kBudgetExceeded)
                        return fail(verification.status());
                    ++output.excluded;
                    output.error = "verification_budget";
                    output.requires_recognition = true;
                    item.evidence = "budget_excluded";
                    item.discontinuous = true;
                    output.tracks.push_back(std::move(item));
                    continue;
                }
                ++output.verified;
                auto bounds = track->last_bounds;
                bounds.x += verification.value().appearance.best_offset_dx;
                bounds.y += verification.value().appearance.best_offset_dy;
                const bool ambiguous =
                    std::any_of(bindings_.begin(), bindings_.end(), [&](const auto &other) {
                        const auto *neighbor = pool_->find_track(other.token);
                        return other.token != binding.token && neighbor &&
                               neighbor->state != mirador::TrackState::kTerminated &&
                               overlaps(bounds, neighbor->last_bounds);
                    });
                if (ambiguous) {
                    item.evidence = "ambiguous";
                    item.discontinuous = true;
                    output.requires_recognition = true;
                } else {
                    mirador::TrackPositionEvidence position;
                    position.inside_gate = true; // candidate is from verification_roi
                    auto committed = pool_->commit_track_evidence(
                        binding.token, verification.value(), position, std::nullopt, frame.image,
                        frame.sequence, ctx);
                    if (!committed)
                        return fail(committed.status());
                    if (committed.value().state == mirador::TrackState::kLost &&
                        !binding.lost_sequence)
                        binding.lost_sequence = frame.sequence;
                    item = project(binding);
                    item.evidence = committed.value().grade == mirador::EvidenceGrade::kConfirmed
                                        ? "appearance_confirmed"
                                    : committed.value().grade == mirador::EvidenceGrade::kTentative
                                        ? "appearance_tentative"
                                        : "placeholder";
                    if (item.state == "tracking" && binding.lost_sequence &&
                        committed.value().grade == mirador::EvidenceGrade::kConfirmed) {
                        auto recaptured = pool_->record_redetection_recapture(
                            binding.token, frame.sequence, binding.lost_sequence, ctx);
                        if (!recaptured)
                            return fail(recaptured.status());
                        item.reacquired = true;
                        binding.lost_sequence = 0;
                    } else if (committed.value().previous_state == mirador::TrackState::kLost &&
                               item.state == "lost") {
                        auto failed =
                            pool_->record_redetection_failure(binding.token, frame.sequence, ctx);
                        if (!failed)
                            return fail(failed.status());
                        item = project(binding);
                        item.evidence = "redetection_failed";
                    }
                    if (committed.value().grade != mirador::EvidenceGrade::kConfirmed) {
                        item.discontinuous = true;
                        output.requires_recognition = true;
                    }
                }
            }
        }
        if (item.state != "tracking")
            output.requires_recognition = true;
        output.tracks.push_back(std::move(item));
    }
    geometry(frame, change, output, ctx);
    if (const auto status = interrupted(ctx); !status)
        return fail(status);
    output.pool_bytes = pool_->byte_size();
    output.layout_generation = pool_->layout_generation();
    return output;
} catch (...) {
    reset();
    return mirador::Status{mirador::ErrorCode::kBackendFailure, "tracking_failed"};
}
} // namespace mirage::integration::detail
