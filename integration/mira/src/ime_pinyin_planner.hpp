#pragma once
#include "ime_text_input.hpp"
#include <mira/model_gateway.hpp>
namespace mirage::integration::detail {
inline constexpr char ime_pinyin_prompt[] =
    "Convert the supplied short text to a Chinese IME spelling plan. This is a pure language "
    "task, not desktop control or visual recognition. Return ONLY a JSON object: "
    "{\"units\":[{\"text\":\"你好\",\"pinyin\":\"nihao\"}]}. Prefer common Han words/phrases "
    "of 2 to 8 characters per unit; use single characters only where needed. Use lowercase "
    "ASCII pinyin without tones or separators, at most 64 letters per unit (use v for ü); consider "
    "the "
    "full text for polyphonic characters. Group printable ASCII verbatim in units with empty "
    "pinyin. Preserve every character exactly and in order. Never output candidate numbers, "
    "commands, images or tools. Unsupported scripts must not be transliterated into other text.";
ImePlanner make_ime_planner(mira::ModelGateway &, mira::ModelProfileId);
} // namespace mirage::integration::detail
