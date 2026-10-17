#pragma once
#include <cstdint>
#include <mira/json.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <optional>

namespace mirage::integration::detail {
inline std::optional<std::uint32_t> pointer_click_count(const mira::JsonValue &args) {
    const auto *value = args.find("click_count");
    if (!value)
        return 1;
    const auto count = value->as_integer();
    if (!count || *count < 1 || *count > desktop::max_click_count)
        return std::nullopt;
    return static_cast<std::uint32_t>(*count);
}
inline std::optional<desktop::MouseButton> pointer_button(const mira::JsonValue &args) {
    const auto *value = args.find("button");
    if (!value)
        return "left";
    const auto *button = value->as_string();
    if (!button || !desktop::valid_action_button(*button))
        return std::nullopt;
    return *button;
}
inline mira::JsonValue button_schema() {
    return mira::JsonValue{mira::JsonValue::Object{
        {"type", "string"},
        {"enum", mira::JsonValue{mira::JsonValue::Array{"left", "middle", "right"}}},
        {"default", "left"}}};
}
inline std::optional<std::int32_t> wheel_steps(const mira::JsonValue &args, const char *key) {
    const auto *value = args.find(key);
    if (!value)
        return 0;
    const auto count = value->as_integer();
    if (!count || *count < -16 || *count > 16)
        return std::nullopt;
    return static_cast<std::int32_t>(*count);
}
inline mira::JsonValue click_count_schema() {
    return mira::JsonValue{mira::JsonValue::Object{
        {"type", "integer"},
        {"minimum", 1},
        {"maximum", static_cast<std::int64_t>(desktop::max_click_count)},
        {"default", 1},
        {"description", "Consecutive clicks in one action (default 1); 2=double, 3=triple"}}};
}
} // namespace mirage::integration::detail
