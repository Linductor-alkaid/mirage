#include "../support/test.hpp"

#include <mira/json.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/input_session.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace d = mirage::desktop;
namespace b = d::input_session_budget;

namespace {

using mira::JsonValue;

// Frozen lifecycle enumeration, in declaration (golden array) order.
constexpr std::array<d::InputSessionState, 6> kStates = {
    d::InputSessionState::ready,
    d::InputSessionState::executing,
    d::InputSessionState::awaiting_feedback,
    d::InputSessionState::releasing,
    d::InputSessionState::released,
    d::InputSessionState::fenced,
};

constexpr std::array<d::InputSessionEvent, 8> kEvents = {
    d::InputSessionEvent::admit,
    d::InputSessionEvent::command_admitted,
    d::InputSessionEvent::command_settled,
    d::InputSessionEvent::observe,
    d::InputSessionEvent::observe_repeat,
    d::InputSessionEvent::release_requested,
    d::InputSessionEvent::release_confirmed,
    d::InputSessionEvent::release_failed,
};

constexpr std::array<d::InputSessionOutcome, 5> kOutcomes = {
    d::InputSessionOutcome::admitted,  d::InputSessionOutcome::delivered,
    d::InputSessionOutcome::holding,   d::InputSessionOutcome::released,
    d::InputSessionOutcome::uncertain,
};

// Documentary reconciliation of the frozen InputSessionReceipt field set
// (input_session.hpp), in declaration order.
constexpr std::array<std::string_view, 13> kReceiptFields = {
    "state",
    "outcome",
    "state_version",
    "command_sequence",
    "budget_version",
    "commands_remaining",
    "idle_budget_ms_remaining",
    "held_keys",
    "held_buttons",
    "side_effect_may_have_occurred",
    "release_confirmed",
    "cancelled",
    "error",
};

const JsonValue *object_field(const JsonValue &value, const char *key) {
    const auto *field = value.find(key);
    MIRAGE_CHECK(field != nullptr);
    return field;
}

/// Optional-object lookup: absent keys are legitimate for matrix rows that
/// do not use a field, so absence is reported to the caller, not flagged.
const JsonValue *find_field(const JsonValue &value, const char *key) { return value.find(key); }

std::optional<std::int64_t> integer_field(const JsonValue &value, const char *key) {
    const auto *field = object_field(value, key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto number = field->as_integer();
    MIRAGE_CHECK(number.has_value());
    return number;
}

/// Optional variant: returns nullopt both for absent keys and non-integers
/// (the latter records a failure through the value check below).
std::optional<std::int64_t> optional_integer_field(const JsonValue &value, const char *key) {
    const auto *field = find_field(value, key);
    if (field == nullptr) {
        return std::nullopt;
    }
    const auto number = field->as_integer();
    MIRAGE_CHECK(number.has_value());
    return number;
}

const std::string *string_field(const JsonValue &value, const char *key) {
    const auto *field = object_field(value, key);
    if (field == nullptr) {
        return nullptr;
    }
    const auto *text = field->as_string();
    MIRAGE_CHECK(text != nullptr);
    return text;
}

const std::string *optional_string_field(const JsonValue &value, const char *key) {
    const auto *field = find_field(value, key);
    if (field == nullptr) {
        return nullptr;
    }
    const auto *text = field->as_string();
    MIRAGE_CHECK(text != nullptr);
    return text;
}

const JsonValue::Array *array_field(const JsonValue &value, const char *key) {
    const auto *field = object_field(value, key);
    if (field == nullptr) {
        return nullptr;
    }
    const auto *array = field->as_array();
    MIRAGE_CHECK(array != nullptr);
    return array;
}

// Independent oracle for the bare-modifier spelling (test-side, not the
// implementation under test).
std::optional<d::SessionKeyName::Modifier> modifier_from_name(std::string_view name) {
    if (name == "ctrl") {
        return d::SessionKeyName::Modifier::ctrl;
    }
    if (name == "alt") {
        return d::SessionKeyName::Modifier::alt;
    }
    if (name == "shift") {
        return d::SessionKeyName::Modifier::shift;
    }
    if (name == "meta") {
        return d::SessionKeyName::Modifier::meta;
    }
    return std::nullopt;
}

} // namespace

int main() {
    std::ifstream golden_file(MIRAGE_INPUT_SESSION_CONTRACT_GOLDEN);
    const auto golden =
        mira::parse_json(std::string(std::istreambuf_iterator<char>(golden_file), {}));
    MIRAGE_CHECK(golden && golden.value().as_object());
    if (!golden || !golden.value().as_object()) {
        return mirage::testing::finish("input_session_contract_test");
    }
    const JsonValue &root = golden.value();

    // -------------------------------------------------------------------
    // (a) Golden reconciliation: capabilities, budgets, states, events,
    // receipt identity.
    // -------------------------------------------------------------------
    const auto *capabilities = array_field(root, "capabilities");
    const auto all_capabilities = d::all_input_capabilities();
    MIRAGE_CHECK(capabilities != nullptr && capabilities->size() == all_capabilities.size());
    if (capabilities != nullptr) {
        for (std::size_t i = 0; i < capabilities->size() && i < all_capabilities.size(); ++i) {
            const auto *name = (*capabilities)[i].as_string();
            MIRAGE_CHECK(name != nullptr);
            if (name == nullptr) {
                continue;
            }
            // The golden array follows the declaration (bit) order.
            MIRAGE_CHECK(static_cast<std::uint16_t>(all_capabilities[i]) == (1u << i));
            MIRAGE_CHECK(*name == d::input_capability_name(all_capabilities[i]));
            MIRAGE_CHECK(d::parse_input_capability(*name) == all_capabilities[i]);
        }
    }

    const auto *budgets = object_field(root, "budgets");
    MIRAGE_CHECK(budgets != nullptr);
    if (budgets != nullptr) {
        const auto budget_value = [&budgets](const char *key) -> std::int64_t {
            return integer_field(*budgets, key).value_or(-1);
        };
        MIRAGE_CHECK(budget_value("max_held_inputs") ==
                     static_cast<std::int64_t>(b::max_held_inputs));
        MIRAGE_CHECK(budget_value("max_session_commands") ==
                     static_cast<std::int64_t>(b::max_session_commands));
        MIRAGE_CHECK(budget_value("max_path_points") ==
                     static_cast<std::int64_t>(b::max_path_points));
        MIRAGE_CHECK(budget_value("default_path_points") ==
                     static_cast<std::int64_t>(b::default_path_points));
        MIRAGE_CHECK(budget_value("min_move_duration_ms") == b::min_move_duration.count());
        MIRAGE_CHECK(budget_value("max_move_duration_ms") == b::max_move_duration.count());
        MIRAGE_CHECK(budget_value("max_press_dwell_ms") == b::max_press_dwell.count());
        MIRAGE_CHECK(budget_value("max_idle_ms") == b::max_idle.count());
        MIRAGE_CHECK(budget_value("max_session_total_ms") == b::max_session_total.count());
        const auto *profile = string_field(*budgets, "default_move_profile");
        MIRAGE_CHECK(profile != nullptr && *profile == b::default_move_profile);
    }
    const auto golden_budget_version = integer_field(root, "budget_version");
    MIRAGE_CHECK(golden_budget_version == std::optional<std::int64_t>(b::budget_version));

    const auto *states = array_field(root, "states");
    MIRAGE_CHECK(states != nullptr && states->size() == kStates.size());
    if (states != nullptr) {
        for (std::size_t i = 0; i < states->size() && i < kStates.size(); ++i) {
            const auto *name = (*states)[i].as_string();
            MIRAGE_CHECK(name != nullptr && *name == d::input_session_state_name(kStates[i]));
            if (name != nullptr) {
                MIRAGE_CHECK(d::parse_input_session_state(*name) == kStates[i]);
            }
        }
    }

    const auto *events = array_field(root, "events");
    MIRAGE_CHECK(events != nullptr && events->size() == kEvents.size());
    if (events != nullptr) {
        for (std::size_t i = 0; i < events->size() && i < kEvents.size(); ++i) {
            const auto *name = (*events)[i].as_string();
            MIRAGE_CHECK(name != nullptr && *name == d::input_session_event_name(kEvents[i]));
        }
    }

    const auto *receipt = object_field(root, "receipt");
    MIRAGE_CHECK(receipt != nullptr);
    if (receipt != nullptr) {
        const auto version = integer_field(*receipt, "version");
        MIRAGE_CHECK(version == std::optional<std::int64_t>(d::InputSessionReceipt::version));
        const auto *outcomes = array_field(*receipt, "outcomes");
        MIRAGE_CHECK(outcomes != nullptr && outcomes->size() == kOutcomes.size());
        if (outcomes != nullptr) {
            for (std::size_t i = 0; i < outcomes->size() && i < kOutcomes.size(); ++i) {
                const auto *name = (*outcomes)[i].as_string();
                MIRAGE_CHECK(name != nullptr &&
                             *name == d::input_session_outcome_name(kOutcomes[i]));
                MIRAGE_CHECK(static_cast<std::uint8_t>(kOutcomes[i]) == i);
            }
        }
        const auto *fields = array_field(*receipt, "fields");
        MIRAGE_CHECK(fields != nullptr && fields->size() == kReceiptFields.size());
        if (fields != nullptr) {
            for (std::size_t i = 0; i < fields->size() && i < kReceiptFields.size(); ++i) {
                const auto *name = (*fields)[i].as_string();
                MIRAGE_CHECK(name != nullptr && *name == kReceiptFields[i]);
            }
        }
    }
    const auto golden_receipt_version = integer_field(root, "receipt_version");
    MIRAGE_CHECK(golden_receipt_version ==
                 std::optional<std::int64_t>(d::InputSessionReceipt::version));

    // -------------------------------------------------------------------
    // (b) Full transition matrix: every state x event against the golden
    // rows, in the golden (states outer, events inner) order.
    // -------------------------------------------------------------------
    const auto *transitions = array_field(root, "transitions");
    MIRAGE_CHECK(transitions != nullptr && transitions->size() == kStates.size() * kEvents.size());
    if (transitions != nullptr) {
        std::size_t index = 0;
        for (const auto &state : kStates) {
            for (const auto &event : kEvents) {
                if (index >= transitions->size()) {
                    break;
                }
                const auto &row = (*transitions)[index];
                const auto next = d::advance_input_session(state, event);
                const std::string actual = next ? d::input_session_state_name(*next) : "rejected";
                const auto *from_name = string_field(row, "from");
                const auto *event_name = string_field(row, "event");
                const auto *to_name = string_field(row, "to");
                const bool row_ok =
                    from_name != nullptr && event_name != nullptr && to_name != nullptr &&
                    *from_name == d::input_session_state_name(state) &&
                    *event_name == d::input_session_event_name(event) && *to_name == actual;
                if (!row_ok) {
                    std::fprintf(stderr,
                                 "transition %zu mismatch: from='%s' event='%s' golden to='%s' "
                                 "actual='%s'\n",
                                 index, from_name == nullptr ? "?" : from_name->c_str(),
                                 event_name == nullptr ? "?" : event_name->c_str(),
                                 to_name == nullptr ? "?" : to_name->c_str(), actual.c_str());
                }
                MIRAGE_CHECK(row_ok);
                ++index;
            }
        }
        MIRAGE_CHECK(index == transitions->size());
    }

    // -------------------------------------------------------------------
    // (c) Session key vocabulary.
    // -------------------------------------------------------------------
    const auto *vocabulary = object_field(root, "session_key_vocabulary");
    MIRAGE_CHECK(vocabulary != nullptr);
    std::vector<std::string> legal_names;
    if (vocabulary != nullptr) {
        const auto *bare = array_field(*vocabulary, "bare_modifiers");
        MIRAGE_CHECK(bare != nullptr);
        if (bare != nullptr) {
            for (const auto &item : *bare) {
                const auto *name = item.as_string();
                MIRAGE_CHECK(name != nullptr);
                if (name == nullptr) {
                    continue;
                }
                legal_names.push_back(*name);
                const auto parsed = d::parse_session_key_name(*name);
                MIRAGE_CHECK(parsed.has_value());
                if (!parsed) {
                    continue;
                }
                MIRAGE_CHECK(parsed->base.empty());
                MIRAGE_CHECK(parsed->side == d::SessionKeyName::Side::none);
                MIRAGE_CHECK(parsed->modifier == modifier_from_name(*name));
            }
        }

        const auto *sided = array_field(*vocabulary, "sided_modifiers");
        MIRAGE_CHECK(sided != nullptr);
        if (sided != nullptr) {
            for (const auto &item : *sided) {
                const auto *name = item.as_string();
                MIRAGE_CHECK(name != nullptr);
                if (name == nullptr) {
                    continue;
                }
                legal_names.push_back(*name);
                const bool is_left = name->ends_with("left");
                const bool is_right = name->ends_with("right");
                MIRAGE_CHECK(is_left != is_right);
                if (is_left == is_right) {
                    continue;
                }
                const std::string stem = name->substr(0, name->size() - (is_left ? 4 : 5));
                const auto expected_modifier = modifier_from_name(stem);
                MIRAGE_CHECK(expected_modifier.has_value());
                const auto parsed = d::parse_session_key_name(*name);
                MIRAGE_CHECK(parsed.has_value());
                if (!parsed) {
                    continue;
                }
                MIRAGE_CHECK(parsed->base.empty());
                MIRAGE_CHECK(parsed->modifier == expected_modifier);
                MIRAGE_CHECK(parsed->side == (is_left ? d::SessionKeyName::Side::left
                                                      : d::SessionKeyName::Side::right));
            }
        }

        const auto *named_base = array_field(*vocabulary, "named_base_keys");
        MIRAGE_CHECK(named_base != nullptr);
        if (named_base != nullptr) {
            for (const auto &item : *named_base) {
                const auto *name = item.as_string();
                MIRAGE_CHECK(name != nullptr);
                if (name == nullptr) {
                    continue;
                }
                legal_names.push_back(*name);
                const auto parsed = d::parse_session_key_name(*name);
                MIRAGE_CHECK(parsed.has_value());
                if (!parsed) {
                    continue;
                }
                MIRAGE_CHECK(parsed->modifier == d::SessionKeyName::Modifier::none);
                MIRAGE_CHECK(parsed->side == d::SessionKeyName::Side::none);
                MIRAGE_CHECK(parsed->base == *name);
            }
        }

        // Single-character base-key samples from the printable KeySym table.
        for (const char sample : {'a', '5', '?', '+'}) {
            const std::string name(1, sample);
            legal_names.push_back(name);
            const auto parsed = d::parse_session_key_name(name);
            MIRAGE_CHECK(parsed.has_value());
            if (!parsed) {
                continue;
            }
            MIRAGE_CHECK(parsed->modifier == d::SessionKeyName::Modifier::none);
            MIRAGE_CHECK(parsed->side == d::SessionKeyName::Side::none);
            MIRAGE_CHECK(parsed->base == name);
        }

        const auto *invalid = array_field(*vocabulary, "invalid_names");
        MIRAGE_CHECK(invalid != nullptr);
        if (invalid != nullptr) {
            for (const auto &item : *invalid) {
                const auto *name = item.as_string();
                MIRAGE_CHECK(name != nullptr);
                if (name == nullptr) {
                    continue;
                }
                MIRAGE_CHECK(!d::parse_session_key_name(*name).has_value());
                MIRAGE_CHECK(!d::is_valid_session_key_name(*name));
            }
        }

        // Canonical round trip over every legal name collected above.
        for (const auto &name : legal_names) {
            const auto parsed = d::parse_session_key_name(name);
            MIRAGE_CHECK(parsed.has_value());
            if (!parsed) {
                continue;
            }
            MIRAGE_CHECK(d::session_key_name_text(*parsed) == name);
        }
    }

    // -------------------------------------------------------------------
    // (d) Command validation matrix.
    // -------------------------------------------------------------------
    const auto *command_matrix = array_field(root, "command_matrix");
    MIRAGE_CHECK(command_matrix != nullptr);
    if (command_matrix != nullptr) {
        for (const auto &row : *command_matrix) {
            const auto *case_name = string_field(row, "case");
            const auto *kind_name = string_field(row, "kind");
            const auto *case_capabilities = array_field(row, "capabilities");
            const auto *expect_name = string_field(row, "expect");
            MIRAGE_CHECK(case_name != nullptr && kind_name != nullptr &&
                         case_capabilities != nullptr && expect_name != nullptr);
            if (case_name == nullptr || kind_name == nullptr || case_capabilities == nullptr ||
                expect_name == nullptr) {
                continue;
            }
            const auto kind = d::parse_input_command_kind(*kind_name);
            MIRAGE_CHECK(kind.has_value());
            if (!kind) {
                continue;
            }
            d::InputSessionCommand command;
            command.kind = *kind;
            if (const auto *key = optional_string_field(row, "key")) {
                command.key_name = *key;
            }
            if (const auto *button = optional_string_field(row, "button")) {
                command.button = *button;
            }
            if (const auto x = optional_integer_field(row, "x")) {
                command.x = static_cast<std::int32_t>(*x);
            }
            if (const auto y = optional_integer_field(row, "y")) {
                command.y = static_cast<std::int32_t>(*y);
            }
            if (const auto dwell = optional_integer_field(row, "dwell_ms")) {
                command.dwell = std::chrono::milliseconds(*dwell);
            }
            if (const auto duration = optional_integer_field(row, "duration_ms")) {
                command.duration = std::chrono::milliseconds(*duration);
            }
            if (const auto points = optional_integer_field(row, "path_points")) {
                command.path_points = static_cast<std::uint32_t>(*points);
            }
            d::InputSessionCommandContext context;
            for (const auto &item : *case_capabilities) {
                const auto *name = item.as_string();
                MIRAGE_CHECK(name != nullptr);
                const auto capability =
                    name != nullptr ? d::parse_input_capability(*name) : std::nullopt;
                MIRAGE_CHECK(capability.has_value());
                if (capability) {
                    context.capabilities = context.capabilities.with(*capability);
                }
            }
            if (const auto held = optional_integer_field(row, "held")) {
                context.held_inputs = static_cast<std::uint32_t>(*held);
            }
            if (const auto used = optional_integer_field(row, "commands")) {
                context.commands_used = static_cast<std::uint32_t>(*used);
            }
            const auto error = d::validate_input_session_command(command, context);
            if (*expect_name == "ok") {
                if (error.has_value()) {
                    std::fprintf(stderr, "command case '%s': expected ok, got '%s' (%s)\n",
                                 case_name->c_str(), error->code.c_str(), error->message.c_str());
                }
                MIRAGE_CHECK(!error.has_value());
            } else {
                if (!error.has_value() || error->code != *expect_name) {
                    std::fprintf(stderr, "command case '%s': expected '%s', got '%s'\n",
                                 case_name->c_str(), expect_name->c_str(),
                                 error.has_value() ? error->code.c_str() : "ok");
                }
                MIRAGE_CHECK(error.has_value() && error->code == *expect_name);
            }
        }
    }

    // -------------------------------------------------------------------
    // (e) Legacy chord vocabulary regression (shared base-key table).
    // -------------------------------------------------------------------
    const auto shift_tap = d::parse_key_chord("shift");
    MIRAGE_CHECK(shift_tap && shift_tap->base == "shift" && !shift_tap->shift);
    MIRAGE_CHECK(!d::is_base_key_symbol("shift"));
    MIRAGE_CHECK(!d::parse_key_chord("ctrl+shift"));
    const auto chord = d::parse_key_chord("ctrl+shift+t");
    MIRAGE_CHECK(chord.has_value());
    if (chord) {
        MIRAGE_CHECK(chord->ctrl && chord->shift && !chord->alt && !chord->meta);
        MIRAGE_CHECK(chord->base == "t");
    }
    MIRAGE_CHECK(d::is_base_key_symbol("enter"));
    MIRAGE_CHECK(!d::is_base_key_symbol(""));
    MIRAGE_CHECK(!d::is_base_key_symbol("ctrl+alt"));

    // -------------------------------------------------------------------
    // (f) Receipt defaults.
    // -------------------------------------------------------------------
    const d::InputSessionReceipt default_receipt;
    MIRAGE_CHECK(default_receipt.budget_version == b::budget_version);
    MIRAGE_CHECK(!default_receipt.release_confirmed);

    return mirage::testing::finish("input_session_contract_test");
}
