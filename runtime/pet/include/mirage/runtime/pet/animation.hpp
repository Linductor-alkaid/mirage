#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace mirage::runtime::pet {

// Frame ranges are zero-based, half-open, and refer to the approved v5 source.
// A candidate loop remains subject to visual seam acceptance before shipping.
struct Clip {
    std::string_view state;
    std::string_view file;
    std::uint16_t frame_count;
    std::uint16_t frame_duration_ms;
    std::uint16_t intro_end;
    std::uint16_t loop_end;
    bool once;
};

const std::array<Clip, 9> &v5_clips();
const Clip *clip_for_display(std::string_view display_state);

struct Frame {
    const Clip *clip = nullptr;
    std::uint16_t index = 0;
    bool finished = false;
    bool static_fallback = false;
};

// UI-owned, clock-driven state. It owns no timer, decoder, texture or worker.
// Rendering calls frame(now) whenever its Executor-owned wake arrives.
class AnimationPlayer {
  public:
    using Clock = std::chrono::steady_clock;
    void select(std::string_view owner_epoch, std::string_view display_state,
                std::string_view source_id, Clock::time_point now);
    Frame frame(Clock::time_point now) const;

  private:
    std::string owner_epoch_;
    std::string display_state_;
    std::string source_id_;
    const Clip *clip_ = nullptr;
    Clock::time_point entered_{};
};

} // namespace mirage::runtime::pet
