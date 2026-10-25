#pragma once
#include <cstdint>
#include <mirage/desktop/input_provider.hpp>

namespace mirage::desktop {
/// DEC-113. Serialized by the capture-session owner; never schedules or retries input.
class InputRecovery {
  public:
    enum class State { Ready, AwaitingFrame, Exhausted, Unavailable };
    void failed(const InputOutcome &out, std::uint64_t sequence) {
        if (out.ok || !out.side_effect_may_have_occurred)
            return;
        watermark_ = sequence;
        if (out.error.code == "input_lag" && out.input_released) {
            if (failures_ < 3)
                ++failures_;
            state_ = failures_ >= 3 ? State::Exhausted : State::AwaitingFrame;
        } else {
            state_ = State::Unavailable;
        }
    }
    void completed() {
        failures_ = 0;
        state_ = State::Ready;
    }
    void observe(std::uint64_t sequence) {
        if (state_ == State::AwaitingFrame && sequence > watermark_)
            state_ = State::Ready;
    }
    bool blocked() const { return state_ != State::Ready; }
    bool awaiting_frame() const { return state_ == State::AwaitingFrame; }
    std::uint64_t watermark() const { return watermark_; }
    const char *reason() const {
        return state_ == State::AwaitingFrame ? "input_recovery_observation_required"
               : state_ == State::Exhausted   ? "input_recovery_budget_exhausted"
               : state_ == State::Unavailable ? "input_path_unavailable"
                                              : "";
    }

  private:
    State state_ = State::Ready;
    unsigned failures_ = 0;
    std::uint64_t watermark_ = 0;
};
} // namespace mirage::desktop
