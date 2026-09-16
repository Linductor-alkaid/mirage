#include <mirage/runtime/persistence/settings.hpp>

#include <mira/json.hpp>

namespace mirage::runtime::persistence {
namespace {

using mira::JsonValue;

JsonValue make_object() { return JsonValue{JsonValue::Object{}}; }

void put(JsonValue &object, std::string key, JsonValue value) {
    object.set(std::move(key), std::move(value));
}

const JsonValue *member(const JsonValue &object, std::string_view key) {
    const auto *entries = object.as_object();
    if (entries == nullptr) {
        return nullptr;
    }
    for (const auto &entry : *entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

/// True when the document carries a member the decoder does not know.
/// Strictness is deliberate (DEC-011): a typoed key must fail loudly, not
/// silently degrade to defaults.
bool has_unknown_member(const JsonValue &object, std::initializer_list<std::string_view> known) {
    const auto *entries = object.as_object();
    if (entries == nullptr) {
        return false;
    }
    for (const auto &entry : *entries) {
        bool found = false;
        for (const std::string_view name : known) {
            if (entry.first == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> string_member(const JsonValue &object, std::string_view key,
                                         std::size_t max_bytes) {
    const JsonValue *value = member(object, key);
    if (value == nullptr) {
        return std::nullopt; // absent: keep the built-in default
    }
    const std::string *text = value->as_string();
    if (text == nullptr || text->size() > max_bytes) {
        return std::nullopt; // wrong type or oversized: caller reports
    }
    return *text;
}

/// Absent -> nullopt (keep default); present -> validated rule string.
std::optional<std::string> rule_member(const JsonValue &object, std::string_view key,
                                       std::string &error) {
    const JsonValue *value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    const std::string *text = value->as_string();
    if (text == nullptr || (*text != "allow" && *text != "confirm" && *text != "deny")) {
        error = "member '" + std::string(key) + "' must be one of \"allow\", \"confirm\", \"deny\"";
        return std::nullopt;
    }
    return *text;
}

std::optional<std::vector<std::string>> read_roots_member(const JsonValue &object,
                                                          std::string &error) {
    std::vector<std::string> roots;
    const JsonValue *value = member(object, "read_roots");
    if (value == nullptr) {
        return roots;
    }
    const auto *array = value->as_array();
    if (array == nullptr) {
        error = "member 'read_roots' must be an array of strings";
        return std::nullopt;
    }
    if (array->size() > kMaxReadRoots) {
        error = "member 'read_roots' exceeds " + std::to_string(kMaxReadRoots) + " entries";
        return std::nullopt;
    }
    for (const JsonValue &entry : *array) {
        const std::string *text = entry.as_string();
        if (text == nullptr || text->size() > kMaxPathBytes) {
            error = "member 'read_roots' entries must be strings of at most " +
                    std::to_string(kMaxPathBytes) + " bytes";
            return std::nullopt;
        }
        roots.push_back(*text);
    }
    return roots;
}

} // namespace

std::string encode_settings(const LocalSettings &settings) {
    JsonValue object = make_object();
    put(object, "schema", JsonValue{static_cast<std::int64_t>(settings.schema)});
    if (!settings.socket_path.empty()) {
        put(object, "socket", JsonValue{settings.socket_path});
    }
    if (!settings.read_roots.empty()) {
        JsonValue::Array roots;
        roots.reserve(settings.read_roots.size());
        for (const std::string &root : settings.read_roots) {
            roots.emplace_back(root);
        }
        put(object, "read_roots", JsonValue{std::move(roots)});
    }
    JsonValue rules = make_object();
    if (settings.filesystem_read_rule) {
        put(rules, "filesystem.read", JsonValue{*settings.filesystem_read_rule});
    }
    if (settings.filesystem_write_rule) {
        put(rules, "filesystem.write", JsonValue{*settings.filesystem_write_rule});
    }
    if (settings.process_execute_rule) {
        put(rules, "process.execute", JsonValue{*settings.process_execute_rule});
    }
    if (!rules.as_object()->empty()) {
        put(object, "permission", std::move(rules));
    }
    if (settings.confirmation) {
        put(object, "confirmation", JsonValue{*settings.confirmation});
    }
    return mira::to_json_string(object);
}

SettingsDecode decode_settings(std::string_view body) {
    SettingsDecode result;
    auto parsed = mira::parse_json(body);
    if (!parsed) {
        result.error = "invalid JSON: " + parsed.error().safe_message;
        return result;
    }
    const JsonValue &document = parsed.value();
    if (!document.is_object()) {
        result.error = "settings document must be a JSON object";
        return result;
    }
    if (has_unknown_member(document,
                           {"schema", "socket", "read_roots", "permission", "confirmation"})) {
        result.error = "settings document contains an unknown member";
        return result;
    }
    LocalSettings settings;
    if (const JsonValue *schema = member(document, "schema")) {
        const auto version = schema->as_integer();
        if (!version || *version != kSettingsSchema) {
            result.error = "unsupported settings schema version";
            return result;
        }
    } else {
        result.error = "settings document lacks the 'schema' member";
        return result;
    }
    if (auto socket = string_member(document, "socket", kMaxPathBytes)) {
        settings.socket_path = std::move(*socket);
    } else if (member(document, "socket") != nullptr) {
        result.error = "member 'socket' must be a string of at most " +
                       std::to_string(kMaxPathBytes) + " bytes";
        return result;
    }
    if (auto roots = read_roots_member(document, result.error)) {
        settings.read_roots = std::move(*roots);
    } else {
        return result; // error already carries the stable reason
    }
    if (const JsonValue *rules = member(document, "permission")) {
        if (!rules->is_object() ||
            has_unknown_member(*rules,
                               {"filesystem.read", "filesystem.write", "process.execute"})) {
            result.error = "member 'permission' must hold only the DEC-010 capability "
                           "names";
            return result;
        }
        auto read_rule = rule_member(*rules, "filesystem.read", result.error);
        if (member(*rules, "filesystem.read") != nullptr && !read_rule) {
            return result;
        }
        settings.filesystem_read_rule = std::move(read_rule);
        auto write_rule = rule_member(*rules, "filesystem.write", result.error);
        if (member(*rules, "filesystem.write") != nullptr && !write_rule) {
            return result;
        }
        settings.filesystem_write_rule = std::move(write_rule);
        auto execute_rule = rule_member(*rules, "process.execute", result.error);
        if (member(*rules, "process.execute") != nullptr && !execute_rule) {
            return result;
        }
        settings.process_execute_rule = std::move(execute_rule);
    }
    const JsonValue *confirmation = member(document, "confirmation");
    if (confirmation != nullptr) {
        const std::string *text = confirmation->as_string();
        if (text == nullptr || (*text != "allow" && *text != "deny")) {
            result.error = "member 'confirmation' must be \"allow\" or \"deny\"";
            return result;
        }
        settings.confirmation = *text;
    }
    result.ok = true;
    result.settings = std::move(settings);
    return result;
}

} // namespace mirage::runtime::persistence
