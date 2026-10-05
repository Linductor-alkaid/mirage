#pragma once

#include <string>
#include <string_view>

namespace mirage::native_ui {
// A modal excerpt is one bounded line; never fit the whole conversation message.
inline std::string conversation_preview(std::string_view value) {
    constexpr std::size_t budget = 256;
    const auto newline = value.find_first_of("\r\n");
    auto end = value.size() < budget ? value.size() : budget;
    if (newline < end)
        end = newline;
    while (end > 0 && end < value.size() && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
        --end;
    std::string result(value.substr(0, end));
    for (auto &byte : result)
        if (static_cast<unsigned char>(byte) < 0x20 || byte == 0x7f)
            byte = ' ';
    if (end < value.size())
        result += "…";
    return result;
}
} // namespace mirage::native_ui
