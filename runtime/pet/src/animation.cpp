#include <mirage/runtime/pet/animation.hpp>

#include <algorithm>

namespace mirage::runtime::pet {
const std::array<Clip, 9> &v5_clips() {
    // Running loops are visually accepted; result/enter/exit clips play once.
    // See docs/compatibility/data/mira-pet-playback-v5-20261013.json.
    static constexpr std::array<Clip, 9> clips{{
        {"enter", "mira_enter.webp", 54, 83, 54, 54, true},
        {"rest", "mira_rest.webp", 50, 167, 8, 44, false},
        {"think", "mira_think.webp", 55, 125, 22, 33, false},
        {"look", "mira_look.webp", 96, 100, 22, 89, false},
        {"mouse", "mira_mouse.webp", 59, 91, 22, 33, false},
        {"typing", "mira_typing.webp", 74, 71, 27, 51, false},
        {"done", "mira_done.webp", 56, 83, 56, 56, true},
        {"fail", "mira_fail.webp", 55, 125, 55, 55, true},
        {"exit", "mira_exit.webp", 52, 100, 52, 52, true},
    }};
    return clips;
}
const Clip *clip_for_display(std::string_view display_state) {
    std::string_view state;
    if (display_state == "starting")
        state = "enter";
    else if (display_state == "idle")
        state = "rest";
    else if (display_state == "thinking")
        state = "think";
    else if (display_state == "looking")
        state = "look";
    else if (display_state == "mouse")
        state = "mouse";
    else if (display_state == "typing")
        state = "typing";
    else if (display_state == "task_success")
        state = "done";
    else if (display_state == "task_failure")
        state = "fail";
    else if (display_state == "exiting")
        state = "exit";
    else
        return nullptr;
    const auto &clips = v5_clips();
    const auto found = std::find_if(clips.begin(), clips.end(),
                                    [state](const Clip &clip) { return clip.state == state; });
    return found == clips.end() ? nullptr : &*found;
}
bool AnimationPlayer::select(std::string_view owner_epoch, std::string_view display_state,
                             std::string_view source_id, Clock::time_point now) {
    if (owner_epoch_ == owner_epoch && display_state_ == display_state && source_id_ == source_id) {
        if (clip_ && clip_->state == "fail" && first_cycle_complete(now)) {
            // Keep the failure identity consumed while presenting idle. Later
            // snapshots for this result cannot replay its once animation.
            consumed_failure_owner_ = owner_epoch_;
            consumed_failure_source_ = source_id_;
            clip_ = clip_for_display("idle");
            entered_ = now;
            ready_ = false;
            return true;
        } else {
            return false;
        }
    }
    const Clip *requested = clip_for_display(display_state);
    if (display_state == "task_failure" && owner_epoch == consumed_failure_owner_ &&
        source_id == consumed_failure_source_)
        requested = clip_for_display("idle");
    const bool safety_interrupt =
        requested == nullptr || display_state == "exiting" ||
        (!owner_epoch_.empty() && owner_epoch_ != "pet-local" && owner_epoch_ != owner_epoch);
    // Only an animation that has actually started owns the first-cycle gate.
    // A state superseded during decode must never play after the action ended.
    if (!safety_interrupt && ready_ && !first_cycle_complete(now))
        return false;
    owner_epoch_ = owner_epoch;
    display_state_ = display_state;
    source_id_ = source_id;
    clip_ = requested;
    entered_ = now;
    ready_ = false;
    return true;
}
void AnimationPlayer::asset_ready(Clock::time_point now) {
    if (clip_ && !ready_) {
        entered_ = now;
        ready_ = true;
    }
}
void AnimationPlayer::asset_failed(Clock::time_point now) {
    if (clip_) {
        entered_ = now - std::chrono::milliseconds{
                             static_cast<int>((clip_->once ? clip_->frame_count : clip_->loop_end) *
                                              clip_->frame_duration_ms)};
        ready_ = true;
    }
}
void AnimationPlayer::reset() {
    owner_epoch_.clear();
    display_state_.clear();
    source_id_.clear();
    clip_ = nullptr;
    entered_ = {};
    ready_ = false;
}
bool AnimationPlayer::first_cycle_complete(Clock::time_point now) const {
    if (!clip_)
        return true;
    if (!ready_)
        return false;
    const auto duration = std::chrono::milliseconds{static_cast<int>(
        (clip_->once ? clip_->frame_count : clip_->loop_end) * clip_->frame_duration_ms)};
    return now >= entered_ + duration;
}
Frame AnimationPlayer::frame(Clock::time_point now) const {
    if (!clip_) {
        // Safe states do not retain a stale input animation.
        return {clip_for_display("idle"), 0, false, true};
    }
    if (!ready_)
        return {clip_, 0, false, false};
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(now - entered_, Clock::duration::zero()));
    const auto absolute = static_cast<std::uint64_t>(elapsed.count()) / clip_->frame_duration_ms;
    if (clip_->once)
        return {
            clip_,
            static_cast<std::uint16_t>(std::min<std::uint64_t>(absolute, clip_->frame_count - 1)),
            absolute >= clip_->frame_count, false};
    if (absolute < clip_->intro_end)
        return {clip_, static_cast<std::uint16_t>(absolute), false, false};
    const auto loop_size = static_cast<std::uint64_t>(clip_->loop_end - clip_->intro_end);
    return {
        clip_,
        static_cast<std::uint16_t>(clip_->intro_end + (absolute - clip_->intro_end) % loop_size),
        false, false};
}
} // namespace mirage::runtime::pet
