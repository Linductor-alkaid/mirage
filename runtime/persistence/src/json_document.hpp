#pragma once

// Shared mira/json document helpers for the persistence codecs (DEC-011):
// one copy of the object-building and strict-decode primitives the state
// documents use. Internal to the persistence target — never installed.

#include <mira/json.hpp>

#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace mirage::runtime::persistence::json_doc {

using mira::JsonValue;

inline JsonValue make_object() { return JsonValue{JsonValue::Object{}}; }

inline void put(JsonValue &object, std::string key, JsonValue value) {
    object.set(std::move(key), std::move(value));
}

inline const JsonValue *member(const JsonValue &object, std::string_view key) {
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
inline bool has_unknown_member(const JsonValue &object,
                               std::initializer_list<std::string_view> known) {
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

} // namespace mirage::runtime::persistence::json_doc
