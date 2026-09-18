#include <mirage/desktop/desktop_environment.hpp>

#include <mirage/desktop/element_reference.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <string_view>
#include <utility>

namespace mirage::desktop {

namespace {

constexpr std::array<std::string_view, 18> kNamedKeys = {
    "enter",  "tab",      "escape", "backspace", "delete", "insert", "home",  "end",
    "pageup", "pagedown", "left",   "right",     "up",     "down",   "space",
};

constexpr std::array<std::string_view, 4> kModifierPrefixes = {"ctrl+", "alt+", "shift+", "meta+"};

bool is_named_key(std::string_view name) {
    if (name.size() == 1) {
        const unsigned char ch = static_cast<unsigned char>(name.front());
        return ch >= 0x21 && ch <= 0x7E; // printable non-space; "space" is named
    }
    if (name.front() == 'f') { // f1..f12
        const std::string_view number = name.substr(1);
        if (number.size() == 1) {
            return number[0] >= '1' && number[0] <= '9';
        }
        return number == "10" || number == "11" || number == "12";
    }
    return std::find(kNamedKeys.begin(), kNamedKeys.end(), name) != kNamedKeys.end();
}

} // namespace

bool is_valid_key_name(const std::string &name) { return parse_key_chord(name).has_value(); }

std::optional<KeyChord> parse_key_chord(const std::string &name) {
    // Modifiers appear at most once each, in the documented canonical order;
    // anything after stripping them must be a plain key.
    std::string_view rest = name;
    KeyChord chord;
    const std::array<std::pair<std::string_view, bool *>, 4> modifiers = {
        {{kModifierPrefixes[0], &chord.ctrl},
         {kModifierPrefixes[1], &chord.alt},
         {kModifierPrefixes[2], &chord.shift},
         {kModifierPrefixes[3], &chord.meta}}};
    for (const auto &[prefix, flag] : modifiers) {
        if (rest.starts_with(prefix)) {
            rest.remove_prefix(prefix.size());
            *flag = true;
        }
    }
    if (rest.empty() || !is_named_key(rest)) {
        return std::nullopt;
    }
    chord.base = std::string(rest);
    return chord;
}

bool is_valid_utf8(const std::string &text) {
    std::size_t i = 0;
    const std::size_t n = text.size();
    auto continuation = [&](std::size_t count) {
        if (i + count > n) {
            return false;
        }
        for (std::size_t k = 0; k < count; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += count;
        return true;
    };
    while (i < n) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        ++i;
        if (lead <= 0x7F) {
            continue;
        }
        unsigned char second = 0;
        if (i < n) {
            second = static_cast<unsigned char>(text[i]);
        }
        if ((lead & 0xE0) == 0xC0) { // U+0080..U+07FF
            if ((lead & 0x1E) == 0 || second < 0x80 || second > 0xBF || !continuation(1)) {
                return false; // reject overlong encodings
            }
        } else if ((lead & 0xF0) == 0xE0) { // U+0800..U+FFFF
            const bool overlong = lead == 0xE0 && second < 0xA0;
            const bool surrogate = lead == 0xED && second > 0x9F;
            if (overlong || surrogate || second < 0x80 || second > 0xBF || !continuation(2)) {
                return false;
            }
        } else if ((lead & 0xF8) == 0xF0) { // U+10000..U+10FFFF
            const bool beyond = lead > 0xF4 || (lead == 0xF4 && second > 0x8F);
            const bool overlong = lead == 0xF0 && second < 0x90;
            if (beyond || overlong || second < 0x80 || second > 0xBF || !continuation(3)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

bool ElementTarget::has_any_hint() const { return hint_count() > 0; }

int ElementTarget::hint_count() const {
    int count = 0;
    if (!reference.id.empty()) {
        ++count;
    }
    if (!semantic.role.empty() || !semantic.name.empty()) {
        ++count;
    }
    if (!structural.path.empty()) {
        ++count;
    }
    if (!visual.ocr_text.empty() || !visual.template_id.empty()) {
        ++count;
    }
    if (!spatial.relative_to.id.empty()) {
        ++count;
    }
    if (raw.x != 0 || raw.y != 0) {
        ++count;
    }
    return count;
}

std::string render_semantic_snapshot(const SemanticSnapshot &snapshot) {
    std::string text;
    if (!snapshot.application.empty()) {
        text += "Application: " + snapshot.application + "\n";
    }
    if (!snapshot.window_title.empty()) {
        text += "Window: " + snapshot.window_title + "\n";
    }
    for (const SemanticNode &node : snapshot.nodes) {
        text += node.ref + " " + node.role;
        if (!node.name.empty()) {
            text += " \"" + node.name + "\"";
        }
        if (node.focused) {
            text += " [focused]";
        }
        if (!node.enabled) {
            text += " [disabled]";
        }
        text += "\n";
    }
    return text;
}

// Keeps the static library non-empty while provider implementations live in
// the platform backends; the observation schema tag is part of the
// DesktopObservation payload contract shared with Mira.
static_assert(kObservationSchemaVersion != nullptr);

} // namespace mirage::desktop
