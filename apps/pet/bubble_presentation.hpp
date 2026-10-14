#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <mirage/runtime/pet/bubble.hpp>
#include <string>

namespace mirage::pet_ui {
// UI-thread-only, bounded presentation state. Measurements use the renderer's
// physical glyph metrics; no text queue or private scheduler.
class BubblePresentation {
  public:
    using Clock = std::chrono::steady_clock;
    void update(const std::string &owner, const std::string &source,
                const std::optional<runtime::pet::Bubble> &bubble, Clock::time_point now,
                const std::function<float(const std::string &)> &measure, float scale) {
        const auto key = owner + ":" + source + ":" + (bubble ? bubble->request_id : "");
        const auto previous = value_;
        const bool reset = key_ != key || !bubble || (previous && previous->kind != bubble->kind) ||
                           (previous && previous->content_origin != bubble->content_origin);
        const auto current = offset(now);
        if (reset) {
            phase_ = 0;
            expiry_ = {};
        } else {
            phase_ = current;
            if (previous && previous->text != bubble->text &&
                !bubble->text.starts_with(previous->text)) {
                // A rolling snapshot removes a prefix. Preserve the visible
                // position when its new beginning still occurs in the old line.
                const auto prefix =
                    bubble->text.substr(0, std::min<std::size_t>(64, bubble->text.size()));
                const auto found = previous->text.find(prefix);
                if (found != std::string::npos)
                    phase_ -= measure(previous->text.substr(0, found));
            }
        }
        key_ = key;
        value_ = bubble;
        started_ = now;
        scale_ = scale;
        width_ = bubble ? measure(bubble->text) : 0;
        period_ = std::max(1.0f, width_ + 32 * scale);
        if (value_ && value_->final) {
            const auto deadline = now + std::chrono::milliseconds{value_->remaining_ms};
            expiry_ = expiry_ == Clock::time_point{} ? deadline : std::min(expiry_, deadline);
        }
    }
    bool visible(Clock::time_point now) const {
        return value_.has_value() && (!value_->final || now < expiry_);
    }
    float offset(Clock::time_point now) const {
        const auto elapsed = started_ == Clock::time_point{}
                                 ? 0.0f
                                 : std::chrono::duration<float>(now - started_).count();
        auto phase = std::fmod(phase_ + 24 * scale_ * elapsed, period_);
        return phase < 0 ? phase + period_ : phase;
    }
    const std::optional<runtime::pet::Bubble> &value() const { return value_; }
    float width() const { return width_; }
    float period() const { return period_; }

  private:
    std::optional<runtime::pet::Bubble> value_;
    std::string key_;
    float width_ = 0, period_ = 1, phase_ = 0, scale_ = 1;
    Clock::time_point started_{}, expiry_{};
};
} // namespace mirage::pet_ui
