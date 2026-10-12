#include <mirage/runtime/pet/animation.hpp>

#include <algorithm>

namespace mirage::runtime::pet {
const std::array<Clip, 9> &v5_clips() {
    // Candidate intervals are individually chosen from the approved source.
    // See docs/compatibility/data/mira-pet-loop-candidates-v5-20261012.json.
    static constexpr std::array<Clip, 9> clips{{
        {"enter", "mira_enter.webp", 54, 83, 54, 54, true},
        {"rest", "mira_rest.webp", 50, 167, 8, 44, false},
        {"think", "mira_think.webp", 55, 125, 22, 33, false},
        {"look", "mira_look.webp", 96, 100, 22, 89, false},
        {"mouse", "mira_mouse.webp", 59, 91, 22, 33, false},
        {"typing", "mira_typing.webp", 74, 71, 27, 51, false},
        {"done", "mira_done.webp", 56, 83, 46, 54, false},
        {"fail", "mira_fail.webp", 55, 125, 12, 25, false},
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
void AnimationPlayer::select(std::string_view owner_epoch, std::string_view display_state,
                             std::string_view source_id, Clock::time_point now) {
    if (owner_epoch_ == owner_epoch && display_state_ == display_state && source_id_ == source_id)
        return;
    owner_epoch_ = owner_epoch;
    display_state_ = display_state;
    source_id_ = source_id;
    clip_ = clip_for_display(display_state);
    entered_ = now;
}
Frame AnimationPlayer::frame(Clock::time_point now) const {
    if (!clip_) {
        // Safe states do not retain a stale input animation. A single idle
        // image may be rendered behind their explicit text label.
        return {clip_for_display("idle"), 0, false, true};
    }
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
