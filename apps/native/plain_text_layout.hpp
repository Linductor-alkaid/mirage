#pragma once

#include "typography.hpp"
#include <algorithm>
#include <core/render/text.h>
#include <deque>
#include <utility>

namespace mirage::native_ui {
// UI-owned, bounded memoization of the same glyph layout used by the renderer.
// Physical font sizes matter: FreeType's quantization is not scale invariant.
class PlainTextLayout {
  public:
    core::Vec2 measure(const std::string &value, float width, float size, float line, float scale) {
        scale = std::max(0.01f, scale);
        core::TextStyle style;
        style.text = value;
        style.fontSize = ui_font_size(size) * scale;
        style.maxWidth = width > 0 ? width * scale : 0;
        style.wrap = width > 0;
        style.lineHeight = line * scale;
        for (const auto &entry : entries_)
            if (entry.style.text == style.text && entry.style.fontSize == style.fontSize &&
                entry.style.maxWidth == style.maxWidth &&
                entry.style.lineHeight == style.lineHeight)
                return {entry.pixels.x / scale, entry.pixels.y / scale};
        core::TextPrimitive shaped;
        shaped.setStyle(style);
        const auto pixels = shaped.measuredSize();
        if (value.size() <= max_bytes) {
            while (!entries_.empty() &&
                   (entries_.size() >= max_entries || bytes_ + value.size() > max_bytes)) {
                bytes_ -= entries_.front().style.text.size();
                entries_.pop_front();
            }
            bytes_ += value.size();
            entries_.push_back({std::move(style), pixels});
        }
        return {pixels.x / scale, pixels.y / scale};
    }

  private:
    static constexpr std::size_t max_entries = 128;
    static constexpr std::size_t max_bytes = 256 * 1024;
    struct Entry {
        core::TextStyle style;
        core::Vec2 pixels;
    };
    std::deque<Entry> entries_;
    std::size_t bytes_ = 0;
};
} // namespace mirage::native_ui
