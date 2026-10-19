#pragma once

#include "frames.hpp"

namespace mirage::pet_ui {

// UI-owned frame publication. A null result retains the current GPU
// frame/texture; it never inserts idle between ordinary animation clips.
class FramePresentation {
  public:
    using Pixels = std::shared_ptr<const std::vector<std::uint8_t>>;

    void restart() { uploaded_index_ = -1; }
    void clear() {
        clear_needed_ = true;
        uploaded_clip_ = nullptr;
        uploaded_index_ = -1;
        fallback_visible_ = false;
    }
    void suspend() {
        neutral_.reset();
        clear();
    }
    Pixels next(const runtime::pet::Frame &frame, const DecodedClip &decoded, bool safe_fallback) {
        // A ready target replaces the old frame directly, with one submission
        // per UI tick even when clear/fallback was requested in that tick.
        if (frame.clip && decoded.clip == frame.clip && frame.index < decoded.rgba_frames.size()) {
            if (!clear_needed_ && uploaded_clip_ == frame.clip && uploaded_index_ == frame.index)
                return {};
            uploaded_clip_ = frame.clip;
            uploaded_index_ = frame.index;
            clear_needed_ = fallback_visible_ = false;
            if (frame.clip->state == "rest" && decoded.rgba_frames.size() > 8)
                if (!neutral_ || neutral_clip_ != decoded.clip) {
                    neutral_ = decoded.pixels(8);
                    neutral_clip_ = decoded.clip;
                }
            return decoded.pixels(frame.index);
        }
        if (clear_needed_ || (safe_fallback && !fallback_visible_)) {
            const bool clear = clear_needed_;
            clear_needed_ = false;
            fallback_visible_ = true;
            uploaded_clip_ = nullptr;
            uploaded_index_ = -1;
            if (!clear && neutral_)
                return neutral_;
            static const auto transparent = std::make_shared<const std::vector<std::uint8_t>>(
                std::size_t{512 * 512 * 4}, std::uint8_t{0});
            return transparent;
        }
        return {}; // normal decode gap: retain the previous visible frame
    }

  private:
    Pixels neutral_;
    const runtime::pet::Clip *neutral_clip_ = nullptr;
    const runtime::pet::Clip *uploaded_clip_ = nullptr;
    int uploaded_index_ = -1;
    bool clear_needed_ = true;
    bool fallback_visible_ = false;
};
} // namespace mirage::pet_ui
