#pragma once
#include "../common/typography.hpp"
#include "bubble_presentation.hpp"
#include <core/render/text.h>
#include <eui_neo.h>
#include <mirage/platform/pet_window.hpp>
#include <mirage/runtime/persistence/app_preferences.hpp>

namespace mirage::pet_ui {
inline std::string reduced_bubble_line(const std::string &text, float width, float size) {
    core::TextStyle style;
    style.text = text;
    style.fontSize = size;
    if (core::TextPrimitive::measureTextSize(style).x <= width)
        return text;
    std::vector<std::size_t> starts;
    for (std::size_t i = 0; i < text.size(); ++i)
        if ((static_cast<unsigned char>(text[i]) & 0xc0U) != 0x80U)
            starts.push_back(i);
    starts.push_back(text.size());
    std::size_t low = 0, high = starts.size() - 1;
    while (low < high) {
        const auto count = (low + high + 1) / 2;
        style.text = "…" + text.substr(starts[starts.size() - 1 - count]);
        if (core::TextPrimitive::measureTextSize(style).x <= width)
            low = count;
        else
            high = count - 1;
    }
    return "…" + text.substr(starts[starts.size() - 1 - low]);
}

inline void compose_bubble_view(eui::Ui &ui, const BubblePresentation &presentation,
                                const platform::pet_window::Layout &layout,
                                const runtime::persistence::AppPreferences &preferences,
                                BubblePresentation::Clock::time_point now) {
    if (!presentation.visible(now))
        return;
    const auto &b = *presentation.value();
    const float scale = layout.image_side / 240.0f;
    const float x = layout.image_x, y = layout.bubble_below
                                            ? layout.image_y + layout.image_side + 18 * scale
                                            : 24 * scale;
    const auto surface =
        preferences.dark ? eui::Color{0.133f, 0.133f, 0.133f, 1} : eui::Color{1, 1, 1, 1};
    const auto ink = preferences.dark ? eui::Color{0.933f, 0.933f, 0.933f, 1}
                                      : eui::Color{0.125f, 0.125f, 0.125f, 1};
    ui.rect("pet.bubble.body")
        .position(x, y)
        .size(layout.image_side, 36 * scale)
        .radius((b.kind == "thinking" ? 18 : 12) * scale)
        .color(surface)
        .shadow(12 * scale, 4 * scale, eui::Color{0, 0, 0, 0.14f})
        .hitTestMode(eui::dsl::HitTestMode::None)
        .build();
    if (b.kind == "thinking") {
        const auto first = layout.bubble_below ? y - 13 * scale : y + 39 * scale;
        const auto second = layout.bubble_below ? y - 22 * scale : y + 52 * scale;
        ui.rect("pet.bubble.tail.big")
            .position(x + 123 * scale, first)
            .size(10 * scale, 10 * scale)
            .radius(5 * scale)
            .color(surface)
            .hitTestMode(eui::dsl::HitTestMode::None)
            .build();
        ui.rect("pet.bubble.tail.small")
            .position(x + 116 * scale, second)
            .size(5 * scale, 5 * scale)
            .radius(2.5f * scale)
            .color(surface)
            .hitTestMode(eui::dsl::HitTestMode::None)
            .build();
    } else {
        const float base = layout.bubble_below ? y : y + 36 * scale;
        const float point = layout.bubble_below ? y - 10 * scale : y + 46 * scale;
        ui.polygon("pet.bubble.tail")
            .position(x + 108 * scale, layout.bubble_below ? point : base)
            .size(20 * scale, 10 * scale)
            .points({{0, layout.bubble_below ? 10 * scale : 0},
                     {20 * scale, layout.bubble_below ? 10 * scale : 0},
                     {10 * scale, layout.bubble_below ? 0 : 10 * scale}})
            .color(surface)
            .hitTestMode(eui::dsl::HitTestMode::None)
            .build();
    }
    ui.stack("pet.bubble.viewport")
        .position(x + 12 * scale, y + 8 * scale)
        .size(216 * scale, 20 * scale)
        .clip()
        .hitTestMode(eui::dsl::HitTestMode::None)
        .content([&] {
            if (b.kind == "thinking" && !preferences.reduced_motion) {
                const float offset = presentation.offset(now), period = presentation.period();
                const int copies =
                    std::min(8, static_cast<int>(std::ceil(216 * scale / period)) + 2);
                for (int i = 0; i < copies; ++i)
                    ui.text("pet.bubble.line." + std::to_string(i))
                        .position(i * period - offset, 0)
                        .size(presentation.width() + scale, 20 * scale)
                        .text(b.text)
                        .fontSize(native_ui::ui_font_size(13) * scale)
                        .lineHeight(20 * scale)
                        .wrap(false)
                        .verticalAlign(eui::VerticalAlign::Center)
                        .color(ink)
                        .hitTestMode(eui::dsl::HitTestMode::None)
                        .build();
            } else {
                std::string text = b.kind == "thinking"
                                       ? reduced_bubble_line(b.text, 216 * scale,
                                                             native_ui::ui_font_size(13) * scale)
                                       : b.text;
                core::TextStyle measure;
                measure.text = text;
                measure.fontSize = native_ui::ui_font_size(13) * scale;
                float width = core::TextPrimitive::measureTextSize(measure).x;
                if (width > 216 * scale && b.kind == "dialog") {
                    text = b.final ? "回复已结束。" : "正在回复…";
                    measure.text = text;
                    width = core::TextPrimitive::measureTextSize(measure).x;
                }
                const float left =
                    b.kind == "dialog" ? std::max(0.0f, (216 * scale - width) * 0.5f) : 0;
                ui.text("pet.bubble.line.static")
                    .position(left, 0)
                    .size(216 * scale, 20 * scale)
                    .text(text)
                    .fontSize(native_ui::ui_font_size(13) * scale)
                    .lineHeight(20 * scale)
                    .wrap(false)
                    .verticalAlign(eui::VerticalAlign::Center)
                    .color(ink)
                    .hitTestMode(eui::dsl::HitTestMode::None)
                    .build();
            }
        })
        .build();
}

} // namespace mirage::pet_ui
