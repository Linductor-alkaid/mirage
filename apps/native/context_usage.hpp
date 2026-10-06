#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

namespace mirage::native_ui {
struct ContextUsage {
    std::uint64_t input_tokens = 0;
    std::uint64_t window_tokens = 0;
    std::string model;
};
inline std::optional<double> context_ratio(const std::optional<ContextUsage> &usage) {
    if (!usage || !usage->window_tokens)
        return std::nullopt;
    return static_cast<double>(usage->input_tokens) / static_cast<double>(usage->window_tokens);
}
inline std::string context_percent(double ratio) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << ratio * 100;
    auto value = out.str();
    if (value.ends_with(".0"))
        value.resize(value.size() - 2);
    return value + "%";
}
inline std::string token_number(std::uint64_t tokens) {
    auto value = std::to_string(tokens);
    for (auto i = static_cast<std::ptrdiff_t>(value.size()) - 3; i > 0; i -= 3)
        value.insert(static_cast<std::size_t>(i), ",");
    return value;
}
// SVG path arcs avoid depending on dash-array support in the native SVG loader.
inline std::string context_ring_svg(std::optional<double> ratio, bool dark) {
    std::ostringstream out;
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\" viewBox=\"0 0 24 "
           "24\">";
    const auto ink = dark ? "#b1b1b1" : "#737373";
    out << "<circle cx=\"12\" cy=\"12\" r=\"10\" fill=\"none\" stroke=\"" << ink
        << "\" stroke-opacity=\"0.25\" stroke-width=\"4\"/>";
    if (ratio && *ratio > 0) {
        const auto fill = std::clamp(*ratio, 0.0, 1.0);
        out << "<path fill=\"none\" stroke=\"" << ink
            << "\" stroke-opacity=\"0.7\" stroke-width=\"4\" stroke-linecap=\"round\" d=\"M12 2 ";
        if (fill >= 1)
            out << "A10 10 0 1 1 12 22 A10 10 0 1 1 12 2";
        else {
            const auto angle = fill * 6.283185307179586;
            out << "A10 10 0 " << (fill > .5 ? 1 : 0) << " 1 " << 12 + 10 * std::sin(angle) << " "
                << 12 - 10 * std::cos(angle);
        }
        out << "\"/>";
    }
    return out.str() + "</svg>";
}
} // namespace mirage::native_ui
