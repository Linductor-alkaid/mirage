#pragma once
#include "secret_edit.hpp"
#include "typography.hpp"
#include <components/input.h>

namespace mirage::native_ui {
// EUI-20261005-004: pinned InputBuilder has no password mode. Keep only
// masks in its editing state while hidden; reconstruct edits from its
// public InputModel selection/caret. The caller owns the live raw value: multiple
// input events may run before recomposition, so never capture a raw snapshot.
// No upstream code is changed.
inline void secret_input(core::dsl::Ui &ui, const std::string &id, float width, std::string &value,
                         bool visible, const std::string &placeholder,
                         const components::InputStyle &style,
                         std::function<void(const std::string &)> change) {
    using Model = components::input_detail::InputModel;
    auto &editing = ui.state<Model::InputState>(id);
    components::input(ui, id)
        .size(width, 32)
        .value(visible ? value : std::string(value.size(), '*'))
        .placeholder(placeholder)
        .fontSize(ui_font_size(14))
        .inset(12)
        .style(style)
        .onChange([&editing, visible, &value, change](const std::string &next) {
            std::optional<std::string> result;
            if (visible) {
                if (next.size() <= 2048 &&
                    std::none_of(next.begin(), next.end(),
                                 [](unsigned char c) { return c < 33 || c > 126; }))
                    result = next;
            } else if (!editing.undoStack.empty()) {
                // EUI can insert both text and clipboard data in one event,
                // pushing more than one snapshot before calling onChange.
                const auto before = editing.undoStack.front();
                result = edit_secret(value, next, before.cursor, before.selectionStart,
                                     before.selectionEnd, editing.cursor);
            }
            const auto raw = result.value_or(value);
            editing.text = visible ? raw : std::string(raw.size(), '*');
            ++editing.textRevision;
            editing.cursor = std::clamp(editing.cursor, 0, static_cast<int>(raw.size()));
            editing.selectionStart = editing.selectionEnd = editing.cursor;
            editing.undoStack.clear();
            editing.redoStack.clear();
            if (result)
                change(*result);
        })
        .build();
    if (auto *hit = ui.find(id + ".hit")) {
        auto handler = hit->onKeyEvent;
        hit->onKeyEvent = [handler](const core::KeyEvent &event) {
            if (event.isDown() && event.modifiers.shortcut() &&
                (event.key == core::InputKey::Z || event.key == core::InputKey::Y))
                return true;
            return handler ? handler(event) : false;
        };
    }
}
} // namespace mirage::native_ui
