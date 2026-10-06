#pragma once

#include "typography.hpp"
#include <algorithm>
#include <core/dsl.h>
#include <core/render/text.h>
#include <string_view>

namespace mirage::native_ui {

// EUI-20261004-004: the pinned public MarkdownBuilder inserts a fixed gap
// between every Unicode segment. Only compact adjacent CJK glyphs that are
// actually adjacent in the source; retain its parser, wrapping, style and IDs.
// No private Markdown types or upstream code are used here. Remove on upgrade.
inline bool cjk_glyph(std::string_view value) {
    if (value.empty())
        return false;
    unsigned int code = static_cast<unsigned char>(value.front());
    std::size_t count = 1;
    if ((code & 0xf0U) == 0xe0U) {
        code &= 0x0fU;
        count = 3;
    } else if ((code & 0xf8U) == 0xf0U) {
        code &= 0x07U;
        count = 4;
    } else
        return false;
    if (value.size() != count)
        return false;
    for (std::size_t i = 1; i < count; ++i) {
        const auto byte = static_cast<unsigned char>(value[i]);
        if ((byte & 0xc0U) != 0x80U)
            return false;
        code = (code << 6) | (byte & 0x3fU);
    }
    return (code >= 0x2e80 && code <= 0x9fff) || (code >= 0xf900 && code <= 0xfaff) ||
           (code >= 0xff01 && code <= 0xff60) || (code >= 0x20000 && code <= 0x3134f);
}
inline void compact_markdown_cjk(core::dsl::Element &root, std::string_view source) {
    core::dsl::Element *previous = nullptr;
    std::string previous_text;
    float shift = 0;
    float code_shift = 0;
    float line_y = -1;
    for (auto &child : root.children) {
        compact_markdown_cjk(*child, source);
        // EUI-20261005-006: center aligns each segment's own ink bounds.
        // A shared line box keeps Chinese, Latin and punctuation on one baseline.
        // Include styled segments with background/decor children, not just CJK.
        if (child->id.find(".seg.") != std::string::npos) {
            if (line_y != child->y) {
                code_shift = 0;
                line_y = child->y;
            }
            child->x -= code_shift;
            for (auto &run : child->children) {
                if (run->kind == core::dsl::ElementKind::Text && run->id.ends_with(".text")) {
                    if (run->fontFamily == "monospace") {
                        // Markdown derives inline code size as bodySize - 1.
                        // The platform code font must not receive the CJK factor.
                        run->fontSize = (run->fontSize + 1) / ui_font_size(1) - 1;
                        run->verticalAlign = core::VerticalAlign::Center;
                        run->y = 0;
                        // Keep the chip width consistent with its independent font.
                        // Wrapping/row budgets remain the upstream conservative ones.
                        const auto width = core::TextPrimitive::measureTextWidth(
                            run->text, run->fontFamily, run->fontSize, run->fontWeight);
                        const float shrink =
                            std::max(0.0f, child->width.value - width - run->x * 2);
                        child->width.value -= shrink;
                        run->width.value = child->width.value - run->x * 2;
                        for (auto &decoration : child->children)
                            if (decoration->id.ends_with(".bg"))
                                decoration->width.value = child->width.value;
                        code_shift += shrink;
                    } else {
                        run->verticalAlign = core::VerticalAlign::Top;
                        run->y = std::max(0.0f, (run->lineHeight - run->fontSize) * 0.5f);
                    }
                }
            }
        } else {
            code_shift = 0;
            line_y = -1;
        }
        if (child->id.find(".seg.") == std::string::npos || child->children.size() != 1 ||
            child->children.front()->kind != core::dsl::ElementKind::Text) {
            previous = nullptr;
            shift = 0;
            continue;
        }
        const auto &value = child->children.front()->text;
        if (!previous || previous->y != child->y)
            shift = 0;
        else if (cjk_glyph(previous_text) && cjk_glyph(value) &&
                 source.find(previous_text + value) != std::string_view::npos) {
            const float gap = child->x - (previous->x + shift + previous->width.value);
            if (gap >= 0 && gap <= 6)
                shift += gap;
        }
        child->x -= shift;
        previous = child.get();
        previous_text = value;
    }
}

} // namespace mirage::native_ui
