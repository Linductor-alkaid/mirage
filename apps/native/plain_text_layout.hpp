#pragma once

#include "../common/typography.hpp"
#include <algorithm>
#include <cmath>
#include <core/render/text.h>
#include <deque>
#include <utility>

namespace mirage::native_ui {
// UI-owned, bounded memoization of the same glyph layout used by the renderer.
// Physical font sizes matter: FreeType's quantization is not scale invariant.
class PlainTextLayout {
  public:
    // A growing composer only needs height up to its viewport cap. Each hard
    // paragraph contributes at least one line, so later paragraphs cannot
    // change a saturated result. Keep complete paragraphs for shaping.
    float height_up_to(const std::string &value, float width, float size, float line, float scale,
                       float limit) {
        const auto paragraphs = static_cast<std::size_t>(std::ceil(limit / line));
        std::size_t end = 0;
        for (std::size_t i = 0; i < paragraphs; ++i) {
            end = value.find('\n', end);
            if (end == std::string::npos)
                return std::min(limit, measure(value, width, size, line, scale).y);
            if (i + 1 < paragraphs)
                ++end;
        }
        return std::min(limit, measure(value.substr(0, end), width, size, line, scale).y);
    }

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
        // Measurement must not instantiate a drawable: measuredSize() on a
        // fresh TextPrimitive loads/rasterizes glyphs and builds render lines.
        // The public metrics-only API shares the renderer's shaping and size
        // cache without doing that work on every edited draft.
        const auto pixels = core::TextPrimitive::measureTextSize(style);
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
