#pragma once
#include <algorithm>
#include <mira/json.hpp>
#include <mira/model_digest.hpp>
#include <mirage/integration/desktop_perception.hpp>
#include <span>

namespace mirage::integration::detail {
// DEC-132: serialized task metadata only. No preview or artifact ownership.
struct ContentEvidenceSample {
    std::string frame_id;
    std::uint64_t frame_sequence = 0;
    std::chrono::steady_clock::time_point observed_at{};
};
struct ContentEvidenceOffer {
    std::uint64_t generation = 0, source_epoch = 0;
    std::vector<ContentEvidenceSample> samples;
};
class PerceptionContentEvidence {
    using Clock = std::chrono::steady_clock;
    std::vector<ContentEvidenceSample> samples_; // At most 32; latest observation-time window.
    std::uint64_t generation_ = 1, epoch_ = 0, dropped_ = 0, revision_ = 0;
    bool initialized_ = false, unavailable_ = false, resync_ = false, truncated_ = false;
    std::string interruption_, rejection_;
    void barrier(const char *reason) {
        ++generation_;
        ++revision_;
        samples_.clear();
        truncated_ = false;
        interruption_ = reason;
    }

  public:
    void replace_source() {
        barrier("source_replaced");
        initialized_ = false;
    }
    void sync(const PerceptionView &view, bool authorized = true) {
        const bool unavailable = !authorized || view.state == "failed" || view.state == "stopped" ||
                                 view.state == "degraded";
        if (initialized_) {
            if (epoch_ != view.source_epoch)
                barrier("source_changed");
            else if (dropped_ != view.dropped)
                barrier("observation_gap");
            else if (!resync_ && view.resync)
                barrier("source_resync");
            else if (!unavailable_ && unavailable)
                barrier(authorized ? "source_unavailable" : "authority_changed");
        } else if (unavailable)
            barrier(authorized ? "source_unavailable" : "authority_changed");
        initialized_ = true;
        epoch_ = view.source_epoch;
        dropped_ = view.dropped;
        resync_ = view.resync;
        unavailable_ = unavailable;
    }
    bool current(const ContentEvidenceOffer &offer) const {
        return initialized_ && !unavailable_ && epoch_ && offer.source_epoch == epoch_ &&
               offer.generation == generation_;
    }
    std::optional<ContentEvidenceOffer> offer(const PerceptionView &view,
                                              std::span<const std::string> ids,
                                              Clock::time_point now = Clock::now()) const {
        ContentEvidenceOffer result{generation_, view.source_epoch, {}};
        if (!current(result) || ids.empty() || ids.size() > 4)
            return std::nullopt;
        for (const auto &id : ids) {
            if (id.empty() || id.size() > 64 ||
                std::any_of(result.samples.begin(), result.samples.end(),
                            [&](const auto &s) { return s.frame_id == id; }))
                return std::nullopt;
            const auto p = std::find_if(view.previews.begin(), view.previews.end(),
                                        [&](const auto &entry) { return entry.first == id; });
            const auto preview = p == view.previews.end() ? nullptr : p->second.lock();
            if (!preview || preview->id != id || preview->observed_at > now ||
                now - preview->observed_at >= std::chrono::seconds(120))
                return std::nullopt;
            const auto event =
                std::find_if(view.events.begin(), view.events.end(), [&](const auto &e) {
                    return e.frame_id == id && e.source_epoch == epoch_ && e.frame_sequence &&
                           e.observed_at == preview->observed_at;
                });
            if (event == view.events.end())
                return std::nullopt;
            result.samples.push_back({id, event->frame_sequence, preview->observed_at});
        }
        return result;
    }
    bool consumed(const ContentEvidenceOffer &offer, Clock::time_point now = Clock::now()) {
        const auto reject = [&](const char *reason) {
            rejection_ = reason;
            return false;
        };
        if (!current(offer))
            return reject("source_or_segment_retired");
        if (offer.samples.empty() || offer.samples.size() > 4)
            return reject("invalid_sample_batch");
        for (const auto &s : offer.samples) {
            if (s.frame_id.empty() || s.frame_id.size() > 64 || !s.frame_sequence ||
                s.observed_at > now || now - s.observed_at >= std::chrono::seconds(120))
                return reject("invalid_or_expired_sample");
            const auto conflict = [&](const auto &other) {
                const bool same = s.frame_id == other.frame_id ||
                                  s.frame_sequence == other.frame_sequence ||
                                  s.observed_at == other.observed_at;
                return (same &&
                        (s.frame_id != other.frame_id || s.frame_sequence != other.frame_sequence ||
                         s.observed_at != other.observed_at)) ||
                       (s.observed_at < other.observed_at &&
                        s.frame_sequence >= other.frame_sequence) ||
                       (s.observed_at > other.observed_at &&
                        s.frame_sequence <= other.frame_sequence);
            };
            if (std::any_of(samples_.begin(), samples_.end(), conflict) ||
                std::any_of(offer.samples.begin(), offer.samples.end(), conflict))
                return reject("conflicting_sample_identity_or_order");
        }
        for (const auto &s : offer.samples) {
            if (std::any_of(samples_.begin(), samples_.end(),
                            [&](const auto &other) { return other.frame_id == s.frame_id; }))
                continue;
            if (samples_.size() == 32 && s.observed_at < samples_.front().observed_at) {
                truncated_ = true;
                continue;
            }
            samples_.push_back(s);
            ++revision_;
            std::sort(samples_.begin(), samples_.end(),
                      [](const auto &a, const auto &b) { return a.observed_at < b.observed_at; });
            if (samples_.size() > 32) {
                samples_.erase(samples_.begin());
                truncated_ = true;
            }
        }
        rejection_.clear();
        return true;
    }
    std::uint64_t generation() const { return generation_; }
    std::uint64_t revision() const { return revision_; }
    std::size_t size() const { return samples_.size(); }
    static std::string reference(const std::string &scope, const ContentEvidenceOffer &offer,
                                 const ContentEvidenceSample &sample) {
        using Json = mira::JsonValue;
        return mira::canonical_json_digest(
                   Json{Json::Array{scope, std::to_string(offer.generation),
                                    std::to_string(offer.source_epoch), sample.frame_id,
                                    std::to_string(sample.frame_sequence),
                                    std::to_string(sample.observed_at.time_since_epoch().count())}})
            .to_string();
    }
    bool contains_reference(const std::string &scope, const std::string &ref) const {
        if (unavailable_ || !initialized_ || !epoch_ || ref.size() != 64)
            return false;
        const ContentEvidenceOffer current{generation_, epoch_, {}};
        return std::any_of(samples_.begin(), samples_.end(), [&](const auto &sample) {
            return reference(scope, current, sample) == ref;
        });
    }
    mira::JsonValue references(const std::string &scope) const {
        using Json = mira::JsonValue;
        Json::Array result;
        const ContentEvidenceOffer current{generation_, epoch_, {}};
        const auto begin = samples_.size() > 4 ? samples_.size() - 4 : 0;
        if (!unavailable_)
            for (std::size_t i = begin; i < samples_.size(); ++i) {
                Json entry{Json::Object{
                    {"evidence_ref", reference(scope, current, samples_[i])},
                    {"frame_id", samples_[i].frame_id},
                    {"frame_sequence", static_cast<std::int64_t>(samples_[i].frame_sequence)}}};
                auto candidate = result;
                candidate.push_back(std::move(entry));
                if (mira::to_json_string(Json{candidate}).size() <= 1024)
                    result = std::move(candidate);
            }
        return Json{std::move(result)};
    }
    mira::JsonValue project(Clock::time_point now = Clock::now()) const {
        const auto ms = [](auto duration) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        };
        std::int64_t gap = 0;
        for (std::size_t i = 1; i < samples_.size(); ++i)
            gap = std::max(gap, ms(samples_[i].observed_at - samples_[i - 1].observed_at));
        using Json = mira::JsonValue;
        return Json{Json::Object{
            {"state", samples_.empty() ? (interruption_.empty() ? "unobserved" : "interrupted")
                                       : "sparse_observation"},
            {"scope", "retained_event_frames_only"},
            {"lifetime", "current_harness_turn"},
            {"segment", static_cast<std::int64_t>(generation_)},
            {"source_epoch", static_cast<std::int64_t>(epoch_)},
            {"retained_samples", static_cast<std::int64_t>(samples_.size())},
            {"history_truncated", truncated_},
            {"observer_span_ms",
             samples_.empty() ? 0 : ms(samples_.back().observed_at - samples_.front().observed_at)},
            {"largest_between_samples_ms", gap},
            {"first_sample_age_ms", samples_.empty() ? 0 : ms(now - samples_.front().observed_at)},
            {"last_sample_age_ms", samples_.empty() ? 0 : ms(now - samples_.back().observed_at)},
            {"last_interruption", interruption_},
            {"last_rejection", rejection_},
            {"producer_resync", resync_},
            {"producer_dropped", static_cast<std::int64_t>(dropped_)},
            {"time_basis", "observer_monotonic_not_player_time"},
            {"continuous_coverage", false},
            {"audio", false},
            {"complete_video", false},
            {"content_understanding", "not_verified"},
            {"input_actionable", false}}};
    }
};
} // namespace mirage::integration::detail
