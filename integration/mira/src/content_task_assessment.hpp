#pragma once
#include "perception_content_evidence.hpp"

namespace mirage::integration::detail {
struct ContentTaskResult {
    bool active = false, satisfied = false;
    std::string answer, reason, limitations;
};
// DEC-133: product evidence policy, not a second Mira task/runtime state machine.
class ContentTaskAssessment {
    bool active_ = false, audio_ = false, automatic_ = false, navigation_ = false;
    std::string state_ = "idle", scope_, reason_, answer_, limitations_;
    std::vector<std::string> refs_;
    std::uint64_t generation_ = 0;
    std::uint64_t assessed_revision_ = 0;

  public:
    bool declare_navigation() {
        if (active_)
            return false;
        navigation_ = true;
        return true;
    }
    bool declared() const { return active_ || navigation_; }
    bool navigation() const { return navigation_ && !active_; }
    bool active() const { return active_; }
    bool automatic() const { return automatic_; }
    bool interrupted() const { return state_ == "interrupted"; }
    bool wants_images() const {
        return active_ && automatic_ && (state_ == "locating" || state_ == "observing_content");
    }
    void sync(const PerceptionContentEvidence &evidence, const std::string &owner_scope) {
        if (!active_ || state_ == "interrupted")
            return;
        if (generation_ != evidence.generation() ||
            std::any_of(refs_.begin(), refs_.end(), [&](const auto &ref) {
                return !evidence.contains_reference(owner_scope, ref);
            })) {
            state_ = "interrupted";
            reason_ = "content_evidence_retired";
            answer_.clear();
            limitations_.clear();
            refs_.clear();
        } else if ((state_ == "coverage_assessed" || state_ == "insufficient") &&
                   evidence.revision() != assessed_revision_) {
            state_ = evidence.size() ? "observing_content" : "locating";
            reason_ = "new_content_requires_assessment";
            answer_.clear();
            limitations_.clear();
            refs_.clear();
        } else if (state_ == "locating" && evidence.size())
            state_ = "observing_content";
    }
    bool begin(const std::string &scope, bool audio, const PerceptionContentEvidence &evidence,
               bool automatic = false) {
        if (scope != "observed_samples" && scope != "entire_video")
            return false;
        if (active_ && (scope_ != scope || audio_ != audio || automatic_ != automatic))
            return false; // No model-side downgrade of an already declared goal.
        if (active_ && state_ != "interrupted")
            return true;
        active_ = true;
        scope_ = scope;
        audio_ = audio;
        automatic_ = automatic;
        generation_ = evidence.generation();
        state_ = evidence.size() ? "observing_content" : "locating";
        reason_.clear();
        answer_.clear();
        limitations_.clear();
        refs_.clear();
        return true;
    }
    std::string assess(const std::string &verdict, const std::string &answer,
                       const std::string &limitations, const std::vector<std::string> &refs,
                       const PerceptionContentEvidence &evidence, const std::string &owner_scope) {
        if (!active_)
            return "content_task_not_started";
        if (state_ == "interrupted")
            return "content_task_interrupted";
        if ((verdict != "answerable" && verdict != "insufficient") || answer.size() > 1024 ||
            limitations.empty() || limitations.size() > 512 || refs.size() > 4)
            return "invalid_argument";
        for (std::size_t i = 0; i < refs.size(); ++i)
            if (!evidence.contains_reference(owner_scope, refs[i]) ||
                std::find(refs.begin(), refs.begin() + i, refs[i]) != refs.begin() + i)
                return "content_reference_not_delivered";
        if (verdict == "answerable" && scope_ == "observed_samples" && !audio_ &&
            (refs.empty() || answer.empty()))
            return "content_evidence_and_answer_required";
        assessed_revision_ = evidence.revision();
        generation_ = evidence.generation();
        refs_ = refs;
        limitations_ = limitations;
        if (verdict == "insufficient" || scope_ == "entire_video" || audio_) {
            state_ = "insufficient";
            reason_ = scope_ == "entire_video" ? "whole_video_coverage_unavailable"
                      : audio_                 ? "audio_evidence_unavailable"
                                               : "model_reports_insufficient";
            answer_.clear();
        } else {
            state_ = "coverage_assessed";
            reason_ = "sampled_content_only";
            answer_ = answer;
        }
        return {};
    }
    ContentTaskResult result() const {
        return {active_, state_ == "coverage_assessed", answer_,
                reason_.empty() && active_ ? "content_assessment_required" : reason_, limitations_};
    }
    mira::JsonValue project() const {
        using Json = mira::JsonValue;
        return Json{Json::Object{{"active", active_},
                                 {"purpose", active_       ? "content"
                                             : navigation_ ? "navigation"
                                                           : "undeclared"},
                                 {"state", state_},
                                 {"scope", scope_},
                                 {"requires_audio", audio_},
                                 {"auto_storyboard", automatic_},
                                 {"reason", reason_},
                                 {"cited_samples", static_cast<std::int64_t>(refs_.size())},
                                 {"assessment_source", "model_inference"},
                                 {"semantic_correctness_verified", false},
                                 {"playback_confirmed", false},
                                 {"complete_video", false}}};
    }
};
} // namespace mirage::integration::detail
