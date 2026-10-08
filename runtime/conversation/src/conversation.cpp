#include <algorithm>
#include <mira/json.hpp>
#include <mirage/conversation.hpp>
#include <unordered_set>

namespace mirage::conversation {
bool truncate_text(std::string &text, std::size_t limit) {
    if (text.size() <= limit)
        return false;
    while (limit && (static_cast<unsigned char>(text[limit]) & 0xc0) == 0x80)
        --limit;
    text.resize(limit);
    return true;
}
bool valid_parts(const std::vector<Part> &parts) {
    if (parts.size() > max_parts)
        return false;
    std::unordered_set<std::string> ids;
    std::size_t bytes = 0;
    for (const auto &part : parts) {
        if (part.id.empty() || part.id.size() > 128 || !ids.insert(part.id).second ||
            (part.kind != "thinking" && part.kind != "text" && part.kind != "tool") ||
            (part.status != "pending" && part.status != "running" && part.status != "complete" &&
             part.status != "failed" && part.status != "cancelled") ||
            part.name.size() > 128 || part.text.size() > max_part_text ||
            part.input.size() > max_part_text || part.output.size() > max_part_text ||
            (part.kind != "tool" && (part.status != "complete" || !part.name.empty() ||
                                     !part.input.empty() || !part.output.empty())) ||
            (part.kind == "tool" && (part.name.empty() || !part.text.empty())))
            return false;
        bytes += part.text.size() + part.input.size() + part.output.size();
        if (bytes > max_content_bytes)
            return false;
    }
    return true;
}
std::string encode_parts(const std::vector<Part> &parts) {
    mira::JsonValue::Array array;
    for (const auto &part : parts) {
        mira::JsonValue::Object object;
        object.emplace_back("id", part.id);
        object.emplace_back("kind", part.kind);
        object.emplace_back("status", part.status);
        if (!part.text.empty())
            object.emplace_back("text", part.text);
        if (!part.name.empty())
            object.emplace_back("name", part.name);
        if (!part.input.empty())
            object.emplace_back("input", part.input);
        if (!part.output.empty())
            object.emplace_back("output", part.output);
        object.emplace_back("truncated", part.truncated);
        array.emplace_back(std::move(object));
    }
    return mira::to_json_string(mira::JsonValue{std::move(array)});
}
bool decode_parts(std::string_view json, std::vector<Part> &parts, std::string &error) {
    const auto parsed = mira::parse_json(json);
    if (!parsed || !parsed.value().is_array() || parsed.value().as_array()->size() > max_parts) {
        error = "invalid or oversized conversation parts array";
        return false;
    }
    std::vector<Part> result;
    for (const auto &value : *parsed.value().as_array()) {
        if (!value.is_object()) {
            error = "conversation part must be an object";
            return false;
        }
        Part part;
        bool id = false, kind = false, status = false;
        for (const auto &[key, field] : *value.as_object()) {
            if (key == "truncated") {
                const auto flag = field.as_boolean();
                if (!flag) {
                    error = "invalid part truncation flag";
                    return false;
                }
                part.truncated = *flag;
                continue;
            }
            const auto *text = field.as_string();
            if (!text) {
                error = "conversation part fields must be strings";
                return false;
            }
            if (key == "id") {
                part.id = *text;
                id = true;
            } else if (key == "kind") {
                part.kind = *text;
                kind = true;
            } else if (key == "status") {
                part.status = *text;
                status = true;
            } else if (key == "text")
                part.text = *text;
            else if (key == "name")
                part.name = *text;
            else if (key == "input")
                part.input = *text;
            else if (key == "output")
                part.output = *text;
            else {
                error = "unknown conversation part member";
                return false;
            }
        }
        if (!id || !kind || !status) {
            error = "missing conversation part identity";
            return false;
        }
        result.push_back(std::move(part));
    }
    if (!valid_parts(result)) {
        error = "invalid conversation parts or display budget exceeded";
        return false;
    }
    parts = std::move(result);
    return true;
}
} // namespace mirage::conversation
