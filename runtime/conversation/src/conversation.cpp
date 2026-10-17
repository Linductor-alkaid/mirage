#include <algorithm>
#include <limits>
#include <mira/json.hpp>
#include <mirage/conversation.hpp>
#include <unordered_set>

namespace mirage::conversation {
std::optional<DesktopEvidence> desktop_evidence(const Part &part) {
    if (part.kind != "tool" || (part.status != "complete" && part.status != "failed") ||
        !part.name.starts_with("desktop.") || part.output.size() > max_part_text)
        return std::nullopt;
    const auto parsed = mira::parse_json(part.output);
    if (!parsed)
        return std::nullopt;
    const auto *v = parsed.value().find("verification");
    if (!v && part.name == "desktop.action.verify")
        v = &parsed.value();
    if (!v)
        return std::nullopt;
    const auto field = [&](const char *key, std::size_t bound) {
        const auto *s = v->find(key);
        return s && s->as_string() && s->as_string()->size() <= bound ? *s->as_string() : "";
    };
    DesktopEvidence view{field("status", 16), field("window_id", 256), field("evidence_id", 128),
                         field("application_id", 256), field("display_id", 256)};
    const auto action = field("action", 128);
    const auto condition = field("verify_kind", 32);
    const bool shared =
        (action == "desktop.session.click" || action == "desktop.session.key" ||
         action == "desktop.session.type_text" || action == "desktop.session.scroll") &&
        part.name == action &&
        (condition == "shared_frame_observed" || condition == "shared_text_exists");
    const auto *verified = v->find("verified");
    if ((view.status != "verified" && view.status != "unverified" && view.status != "uncertain" &&
         view.status != "rejected") ||
        (view.window_id.empty() && !shared &&
         !(field("action", 128) == "desktop.vision.click_region" && !view.display_id.empty()) &&
         (view.application_id.empty() || field("verify_kind", 32) != "application_running" ||
          (field("action", 128) != "desktop.application.launch" &&
           field("action", 128) != "desktop.application.open_uri"))) ||
        (view.status == "verified" &&
         (part.status != "complete" || !verified || !verified->as_boolean().value_or(false) ||
          view.evidence_id.empty())))
        return std::nullopt;
    view.shared_desktop = shared;
    view.scope = field("evidence_scope", 64);
    if (view.scope.empty()) {
        if (condition == "shared_frame_observed")
            view.scope = "fresh_frame";
        else if (condition == "shared_text_exists")
            view.scope = "text_presence";
        else if (condition == "application_running")
            view.scope = "process_presence";
        else if (condition == "window_focused")
            view.scope = "focus_identity";
    }
    return view;
}
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
    std::size_t tool_bytes = 0, text_bytes = 0, thinking_bytes = 0;
    for (const auto &part : parts) {
        // DEC-093: tool parts can have larger input/output; non-tool text is capped at
        // max_part_text.
        std::size_t text_limit = (part.kind == "tool") ? max_tool_budget : max_part_text;
        std::size_t input_limit = (part.kind == "tool") ? max_tool_budget : max_part_text;
        std::size_t output_limit = (part.kind == "tool") ? max_tool_budget : max_part_text;

        if (part.id.empty() || part.id.size() > 128 || !ids.insert(part.id).second ||
            (part.kind != "thinking" && part.kind != "text" && part.kind != "tool") ||
            (part.status != "pending" && part.status != "running" && part.status != "complete" &&
             part.status != "failed" && part.status != "cancelled") ||
            part.name.size() > 128 || part.text.size() > text_limit ||
            part.input.size() > input_limit || part.output.size() > output_limit ||
            (part.kind != "tool" && (part.status != "complete" || !part.name.empty() ||
                                     !part.input.empty() || !part.output.empty())) ||
            (part.kind == "tool" && (part.name.empty() || !part.text.empty())))
            return false;
        // DEC-093: categorized budget.
        std::size_t part_bytes = part.text.size() + part.input.size() + part.output.size();
        if (part.kind == "tool") {
            tool_bytes += part_bytes;
            if (tool_bytes > max_tool_budget)
                return false;
        } else if (part.kind == "text") {
            text_bytes += part_bytes;
            if (text_bytes > max_text_budget)
                return false;
        } else if (part.kind == "thinking") {
            thinking_bytes += part_bytes;
            if (thinking_bytes > max_thinking_budget)
                return false;
        }
    }
    // Total budget must not exceed max_content_bytes.
    if (tool_bytes + text_bytes + thinking_bytes > max_content_bytes)
        return false;
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
        if (part.omitted_for_display_budget)
            object.emplace_back("omitted_for_display_budget", true);
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
            if (key == "truncated" || key == "omitted_for_display_budget") {
                const auto flag = field.as_boolean();
                if (!flag) {
                    error = "invalid part truncation flag";
                    return false;
                }
                if (key == "truncated")
                    part.truncated = *flag;
                else
                    part.omitted_for_display_budget = *flag;
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
std::string encode_timings(const Timings &value) {
    mira::JsonValue::Object object;
    for (const auto &[name, phase] : {std::pair{"inference", value.inference},
                                      {"capture", value.capture},
                                      {"ocr", value.ocr},
                                      {"settlement", value.settlement}}) {
        if (phase.calls)
            object.emplace_back(
                name, mira::JsonValue{mira::JsonValue::Object{
                          {"calls", static_cast<std::int64_t>(phase.calls)},
                          {"duration_ms", static_cast<std::int64_t>(phase.duration_ms)}}});
    }
    return mira::to_json_string(mira::JsonValue{std::move(object)});
}
bool decode_timings(std::string_view json, Timings &out, std::string &error) {
    const auto parsed = mira::parse_json(json);
    if (!parsed || !parsed.value().as_object()) {
        error = "invalid process timings";
        return false;
    }
    Timings value;
    for (const auto &[name, phase] : *parsed.value().as_object()) {
        auto *target = name == "inference"    ? &value.inference
                       : name == "capture"    ? &value.capture
                       : name == "ocr"        ? &value.ocr
                       : name == "settlement" ? &value.settlement
                                              : nullptr;
        if (!target || !phase.as_object() || phase.as_object()->size() != 2) {
            error = "invalid process timing phase";
            return false;
        }
        const auto *calls = phase.find("calls"), *duration = phase.find("duration_ms");
        if (!calls || !duration || !calls->as_integer() || *calls->as_integer() <= 0 ||
            !duration->as_integer() || *duration->as_integer() < 0) {
            error = "invalid process timing counters";
            return false;
        }
        target->calls = static_cast<std::uint64_t>(*calls->as_integer());
        target->duration_ms = static_cast<std::uint64_t>(*duration->as_integer());
    }
    out = value;
    return true;
}
} // namespace mirage::conversation
