#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace mirage::runtime::pet {

// Frame ranges are zero-based, half-open, and refer to the approved v5 source.
// Running loop seams are visually accepted; failure once returns to idle.
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
// A started clip completes its first cycle before an ordinary state change;
// select() receives only the latest live status, so obsolete actions are skipped.
class AnimationPlayer {
  public:
    using Clock = std::chrono::steady_clock;
    bool select(std::string_view owner_epoch, std::string_view display_state,
                std::string_view source_id, Clock::time_point now);
    // The first frame starts its clock only after the asset is decoded. A
    // loading interval cannot silently consume the first cycle.
    void asset_ready(Clock::time_point now);
    void asset_failed(Clock::time_point now);
    void reset();
    Frame frame(Clock::time_point now) const;
    bool first_cycle_complete(Clock::time_point now) const;

  private:
    std::string owner_epoch_;
    std::string display_state_;
    std::string source_id_;
    std::string consumed_failure_owner_;
    std::string consumed_failure_source_;
    const Clip *clip_ = nullptr;
    Clock::time_point entered_{};
    bool ready_ = false;
};

} // namespace mirage::runtime::pet
