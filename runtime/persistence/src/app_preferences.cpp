#include <mirage/runtime/persistence/app_preferences.hpp>
#include <mira/json.hpp>
#include <algorithm>
namespace mirage::runtime::persistence {
AppPreferences decode_app_preferences(std::string_view text) {
    AppPreferences out;
    if (text.size() > kMaxAppPreferencesBytes) {
        out.default_access = "chat_only";
        return out;
    }
    const auto parsed = mira::parse_json(text);
    if (!parsed || !parsed.value().as_object()) {
        out.default_access = "chat_only";
        return out;
    }
    const auto &root = parsed.value();
    auto flag = [&](const char *key, bool &value) {
        if (const auto *v = root.find(key); v && v->as_boolean())
            value = *v->as_boolean();
    };
    auto step = [&](const char *key, int &value) {
        if (const auto *v = root.find(key); v && v->as_integer())
            value = static_cast<int>(std::clamp<std::int64_t>(*v->as_integer(), 0, 2));
    };
    flag("dark", out.dark);
    flag("reduced_motion", out.reduced_motion);
    step("font_size_step", out.font_size_step);
    step("code_font_size_step", out.code_font_size_step);
    if (const auto *v = root.find("default_access")) {
        const auto *access = v->as_string();
        out.default_access = access && (*access == "default" || *access == "read_only" ||
                                        *access == "chat_only") ? *access : "chat_only";
    }
    if (const auto *v = root.find("default_reasoning");
        v && v->as_string() && v->as_string()->size() <= 64)
        out.default_reasoning = *v->as_string();
    return out;
}
std::string encode_app_preferences(const AppPreferences &prefs) {
    const auto access = prefs.default_access == "default" || prefs.default_access == "read_only" ||
                                prefs.default_access == "chat_only"
                            ? prefs.default_access : "chat_only";
    return mira::to_json_string(mira::JsonValue{mira::JsonValue::Object{
        {"dark", prefs.dark}, {"reduced_motion", prefs.reduced_motion},
        {"font_size_step", std::int64_t(std::clamp(prefs.font_size_step, 0, 2))},
        {"default_access", access},
        {"default_reasoning", prefs.default_reasoning.size() <= 64 ? prefs.default_reasoning : ""},
        {"code_font_size_step", std::int64_t(std::clamp(prefs.code_font_size_step, 0, 2))}}});
}
} // namespace mirage::runtime::persistence
