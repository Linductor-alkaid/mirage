#pragma once
#include "text_selection.hpp"
#include <components/input_model.h>
#include <core/dsl.h>

namespace mirage::native_ui {
struct SelectionCache {
    std::shared_ptr<SelectionText> rendered;
};
// EUI-20261005-005: pinned EUI has no selectable read-only text/Markdown component. Keep this
// adapter at the UI boundary; reuse its shaping/caret/wrapping facilities.
inline void collect_selection_text(const core::dsl::Element &element, SelectionText &out,
                                   float x = 0, float y = 0) {
    x = element.frame.x;
    y = element.frame.y;
    if (element.kind == core::dsl::ElementKind::Text && !element.text.empty()) {
        using Model = components::input_detail::InputModel;
        Model::InputState state;
        state.text = element.text;
        const float width = element.frame.width;
        const float line_height =
            element.lineHeight > 0 ? element.lineHeight : element.fontSize * 1.5f;
        auto layout = Model::InputLayout::build(state, width, 100000, width, 0, 0, line_height,
                                                element.fontFamily, element.fontSize, element.wrap);
        for (const auto &line : layout.lineList()) {
            const auto value = element.text.substr(static_cast<std::size_t>(line.start),
                                                   static_cast<std::size_t>(line.end - line.start));
            const auto metrics = core::TextPrimitive::measureTextMetrics(
                value, element.fontFamily, element.fontSize, element.fontWeight);
            out.append({value, x, y, line_height, metrics.byteIndices, metrics.caretX});
            y += line_height;
        }
        return;
    }
    for (const auto &child : element.children)
        collect_selection_text(*child, out, x, y);
}
} // namespace mirage::native_ui
