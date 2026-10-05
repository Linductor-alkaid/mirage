#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mirage::native_ui {
// UI-thread owned selection of rendered text, independent of source Markdown.
// Geometry is supplied by the EUI adapter; offsets are UTF-8 caret boundaries.
struct SelectionLine {
    std::string text;
    float x = 0, y = 0, height = 24;
    std::vector<int> bytes;
    std::vector<float> carets;
    std::size_t start = 0;
};
struct SelectionText {
    std::string text;
    std::vector<SelectionLine> lines;
    void append(SelectionLine line) {
        if (text.size() + line.text.size() + 1 > 16384 || lines.size() >= 8192)
            return;
        if (!lines.empty()) {
            const auto &previous = lines.back();
            if (std::abs(previous.y - line.y) > 2)
                text += '\n';
            else if (!previous.carets.empty() && line.x - previous.x - previous.carets.back() > 1)
                text += ' ';
        }
        line.start = text.size();
        text += line.text;
        lines.push_back(std::move(line));
    }
    std::size_t hit(float x, float y) const {
        const SelectionLine *best = nullptr;
        float distance = 1e30f;
        for (const auto &line : lines) {
            if (line.carets.empty())
                continue;
            const auto horizontal = std::max({line.x - x, x - line.x - line.carets.back(), 0.0f});
            const auto vertical = std::max({line.y - y, y - line.y - line.height, 0.0f});
            const auto score = vertical * 10000 + horizontal;
            if (score < distance) {
                distance = score;
                best = &line;
            }
        }
        if (!best)
            return 0;
        std::size_t index = 0;
        for (std::size_t i = 1; i < std::min(best->carets.size(), best->bytes.size()); ++i)
            if (std::abs(best->carets[i] - (x - best->x)) <
                std::abs(best->carets[index] - (x - best->x)))
                index = i;
        return best->start + static_cast<std::size_t>(best->bytes[index]);
    }
};
struct TextSelection {
    std::uint64_t session = 0, message = 0;
    std::size_t anchor = 0, caret = 0;
    bool dragging = false, ready = false;
    float origin_x = 0, origin_y = 0, row_x = 0, row_y = 0, scale = 1, popup_x = 0, popup_y = 0;
    std::string excerpt;
    void clear() { *this = {}; }
    void finish(const SelectionText &text) {
        dragging = false;
        const auto begin = std::min(anchor, caret), end = std::max(anchor, caret);
        excerpt = end <= text.text.size() ? text.text.substr(begin, end - begin) : std::string{};
        const auto first = excerpt.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            excerpt.clear();
        else
            excerpt = excerpt.substr(first, excerpt.find_last_not_of(" \t\r\n") - first + 1);
        ready = !excerpt.empty();
    }
};
} // namespace mirage::native_ui
