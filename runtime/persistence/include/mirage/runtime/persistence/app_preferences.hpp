#pragma once
#include <cstddef>
#include <string>
#include <string_view>
namespace mirage::runtime::persistence {
inline constexpr std::size_t kMaxAppPreferencesBytes = 4096;
struct AppPreferences {
    bool dark = false;
    bool reduced_motion = false;
    int font_size_step = 1;
    std::string default_access = "default";
    std::string default_reasoning;
    int code_font_size_step = 1;
};
// DEC-080: malformed/unsupported persisted access never elevates to full access.
AppPreferences decode_app_preferences(std::string_view);
std::string encode_app_preferences(const AppPreferences &);
} // namespace mirage::runtime::persistence
