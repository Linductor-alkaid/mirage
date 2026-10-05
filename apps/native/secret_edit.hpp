#pragma once
#include <algorithm>
#include <optional>
#include <string>

namespace mirage::native_ui {
// Masked text and raw text share printable-ASCII byte positions. The prior
// selection and post-edit caret identify insert/delete even among equal masks.
inline std::optional<std::string> edit_secret(const std::string &raw, const std::string &next,
                                              int previous_cursor, int selection_start,
                                              int selection_end, int cursor) {
    if (next.size() > 2048 ||
        std::any_of(next.begin(), next.end(), [](unsigned char c) { return c < 33 || c > 126; }))
        return {};
    int begin = std::min(selection_start, selection_end);
    int end = std::max(selection_start, selection_end);
    const int delta = static_cast<int>(next.size()) - static_cast<int>(raw.size());
    if (begin == end) {
        begin = previous_cursor;
        end = begin;
        if (delta < 0) {
            begin = std::min(previous_cursor, cursor);
            end = begin - delta;
        }
    }
    const int inserted = delta + end - begin;
    if (begin < 0 || end < begin || end > static_cast<int>(raw.size()) || inserted < 0 ||
        begin + inserted > static_cast<int>(next.size()))
        return {};
    auto result = raw;
    result.replace(
        static_cast<std::size_t>(begin), static_cast<std::size_t>(end - begin),
        next.substr(static_cast<std::size_t>(begin), static_cast<std::size_t>(inserted)));
    return result;
}
} // namespace mirage::native_ui
