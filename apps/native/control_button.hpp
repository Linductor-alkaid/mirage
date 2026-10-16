#pragma once

#include "control_metrics.hpp"
#include "typography.hpp"
#include <cmath>
#include <components/button.h>
#include <eui_neo.h>

namespace mirage::native_ui {
inline std::string fit_control_text(const std::string &value, float width, float font_size) {
    core::TextStyle measured;
    measured.fontSize = font_size;
    measured.text = value;
    if (core::TextPrimitive::measureTextSize(measured).x <= width)
        return value;
    measured.text = "…";
    if (core::TextPrimitive::measureTextSize(measured).x > width)
        return {};
    auto prefix = value;
    while (!prefix.empty()) {
        auto last = prefix.size() - 1;
        while (last > 0 && (static_cast<unsigned char>(prefix[last]) & 0xc0) == 0x80)
            --last;
        prefix.resize(last);
        measured.text = prefix + "…";
        if (core::TextPrimitive::measureTextSize(measured).x <= width)
            return measured.text;
    }
    return "…";
}

inline float control_button_width(const std::string &label, float em, float icon_size = 0) {
    core::TextStyle measured;
    measured.fontSize = ui_font_size(em);
    measured.text = label;
    // Pinned ButtonBuilder reserves iconSize * 1.15. Rendering below uses the
    // actual generated icon width; the renderer regression detects pin drift.
    const float icon_space = icon_size > 0 ? icon_size * 1.15f + control_icon_gap : 0;
    return std::ceil(core::TextPrimitive::measureTextSize(measured).x + 2 * control_text_inset +
                     icon_space);
}

// Keep EUI's state, disabled and event handling. Adapt only its public DSL
// geometry, since ButtonBuilder has no text-inset or label-fitting option.
class ControlButton {
  public:
    ControlButton(eui::Ui &ui, std::string id) : ui_(ui), id_(std::move(id)), button_(ui, id_) {
        button_.text(""); // Icon-only controls must not inherit the upstream example label.
    }
    ControlButton &position(float x, float y) {
        button_.position(x, y);
        return *this;
    }
    ControlButton &size(float width, float height) {
        button_.size(width, height);
        return *this;
    }
    ControlButton &text(const std::string &value) {
        button_.text(value);
        return *this;
    }
    ControlButton &icon(unsigned int glyph) {
        button_.icon(glyph);
        return *this;
    }
    ControlButton &trailingIcon(unsigned int glyph) {
        button_.icon(glyph);
        trailing_icon_ = true;
        return *this;
    }
    ControlButton &fontSize(float size) {
        button_.fontSize(size);
        return *this;
    }
    ControlButton &iconSize(float size) {
        button_.iconSize(size);
        return *this;
    }
    ControlButton &style(const components::ButtonStyle &value) {
        button_.style(value);
        return *this;
    }
    ControlButton &disabled(bool value = true) {
        button_.disabled(value);
        return *this;
    }
    ControlButton &onClick(std::function<void()> action) {
        button_.onClick(std::move(action));
        return *this;
    }
    void build() {
        button_.build();
        auto *label = ui_.find(id_ + ".text");
        if (!label)
            return;
        const auto *root = ui_.find(id_);
        const auto *glyph = ui_.find(id_ + ".icon");
        const float icon_space = glyph ? glyph->width.value + control_icon_gap : 0;
        const float available =
            std::max(0.0f, root->width.value - 2 * control_text_inset - icon_space);
        label->width = core::SizeValue::fixed(available);
        label->text = fit_control_text(label->text, available, label->fontSize);
        if (auto *content = ui_.find(id_ + ".content")) {
            content->spacing = glyph ? control_icon_gap : 0;
            if (trailing_icon_ && content->children.size() == 2)
                std::swap(content->children[0], content->children[1]);
        }
    }

  private:
    eui::Ui &ui_;
    std::string id_;
    components::ButtonBuilder button_;
    bool trailing_icon_ = false;
};

inline ControlButton control_button(eui::Ui &ui, const std::string &id) {
    return ControlButton(ui, id);
}
} // namespace mirage::native_ui
