#include <mirage/desktop/input_session.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace mirage::desktop {
namespace {

constexpr std::array<std::pair<std::string_view, SessionKeyName::Modifier>, 4> kBareModifiers = {{
    {"ctrl", SessionKeyName::Modifier::ctrl},
    {"alt", SessionKeyName::Modifier::alt},
    {"shift", SessionKeyName::Modifier::shift},
    {"meta", SessionKeyName::Modifier::meta},
}};

struct CapabilityName {
    InputCapability capability;
    std::string_view name;
};
// Bit order matches InputCapability; all_input_capabilities and the golden
// contract both follow this order.
constexpr std::array<CapabilityName, 11> kCapabilityNames = {{
    {InputCapability::software_injection, "software_injection"},
    {InputCapability::smooth_move, "smooth_move"},
    {InputCapability::timed_press, "timed_press"},
    {InputCapability::independent_modifiers, "independent_modifiers"},
    {InputCapability::cross_observation_hold, "cross_observation_hold"},
    {InputCapability::hold_capture, "hold_capture"},
    {InputCapability::key_repeat, "key_repeat"},
    {InputCapability::ime_state, "ime_state"},
    {InputCapability::ime_control, "ime_control"},
    {InputCapability::kernel_virtual_device, "kernel_virtual_device"},
    {InputCapability::external_hid, "external_hid"},
}};

struct StateTransition {
    InputSessionState from;
    InputSessionEvent event;
    InputSessionState to;
};
// Full frozen matrix; anything not listed is refused without side effects.
constexpr std::array<StateTransition, 12> kTransitions = {{
    {InputSessionState::ready, InputSessionEvent::admit, InputSessionState::executing},
    {InputSessionState::executing, InputSessionEvent::command_admitted,
     InputSessionState::executing},
    {InputSessionState::awaiting_feedback, InputSessionEvent::command_admitted,
     InputSessionState::executing},
    {InputSessionState::executing, InputSessionEvent::command_settled,
     InputSessionState::executing},
    {InputSessionState::executing, InputSessionEvent::observe,
     InputSessionState::awaiting_feedback},
    {InputSessionState::awaiting_feedback, InputSessionEvent::observe_repeat,
     InputSessionState::awaiting_feedback},
    {InputSessionState::ready, InputSessionEvent::release_requested,
     InputSessionState::releasing},
    {InputSessionState::executing, InputSessionEvent::release_requested,
     InputSessionState::releasing},
    {InputSessionState::awaiting_feedback, InputSessionEvent::release_requested,
     InputSessionState::releasing},
    // Idempotent aborts while already releasing collapse into the same
    // state; terminals never transition.
    {InputSessionState::releasing, InputSessionEvent::release_requested,
     InputSessionState::releasing},
    {InputSessionState::releasing, InputSessionEvent::release_confirmed,
     InputSessionState::released},
    {InputSessionState::releasing, InputSessionEvent::release_failed,
     InputSessionState::fenced},
}};

bool has_injection_source(const InputSessionCommandContext &context) {
    return context.capabilities.has(InputCapability::software_injection) ||
           context.capabilities.has(InputCapability::kernel_virtual_device) ||
           context.capabilities.has(InputCapability::external_hid);
}

bool is_modifier(const SessionKeyName &key) { return key.modifier != SessionKeyName::Modifier::none; }

} // namespace

std::string_view input_capability_name(InputCapability capability) {
    for (const auto &entry : kCapabilityNames) {
        if (entry.capability == capability) {
            return entry.name;
        }
    }
    return "";
}

std::optional<InputCapability> parse_input_capability(std::string_view name) {
    for (const auto &entry : kCapabilityNames) {
        if (entry.name == name) {
            return entry.capability;
        }
    }
    return std::nullopt;
}

std::array<InputCapability, 11> all_input_capabilities() {
    std::array<InputCapability, 11> capabilities{};
    for (std::size_t i = 0; i < capabilities.size(); ++i) {
        capabilities[i] = kCapabilityNames[i].capability;
    }
    return capabilities;
}

std::optional<SessionKeyName> parse_session_key_name(std::string_view name) {
    SessionKeyName key;
    for (const auto &[bare, modifier] : kBareModifiers) {
        if (name == bare) {
            key.modifier = modifier;
            return key;
        }
        constexpr std::string_view kLeft = "left", kRight = "right";
        for (const auto &[suffix, side] :
             {std::pair<std::string_view, SessionKeyName::Side>{kLeft, SessionKeyName::Side::left},
              {kRight, SessionKeyName::Side::right}}) {
            if (name.size() == bare.size() + suffix.size() &&
                name.starts_with(bare) && name.ends_with(suffix)) {
                key.modifier = modifier;
                key.side = side;
                return key;
            }
        }
    }
    if (is_base_key_symbol(name)) {
        key.base = std::string(name);
        return key;
    }
    return std::nullopt;
}

bool is_valid_session_key_name(std::string_view name) {
    return parse_session_key_name(name).has_value();
}

std::string session_key_name_text(const SessionKeyName &key) {
    if (key.modifier == SessionKeyName::Modifier::none) {
        return key.base;
    }
    std::string text;
    for (const auto &[bare, modifier] : kBareModifiers) {
        if (modifier == key.modifier) {
            text = bare;
            break;
        }
    }
    if (key.side == SessionKeyName::Side::left) {
        text += "left";
    } else if (key.side == SessionKeyName::Side::right) {
        text += "right";
    }
    return text;
}

const char *input_session_state_name(InputSessionState state) {
    switch (state) {
    case InputSessionState::ready: return "ready";
    case InputSessionState::executing: return "executing";
    case InputSessionState::awaiting_feedback: return "awaiting_feedback";
    case InputSessionState::releasing: return "releasing";
    case InputSessionState::released: return "released";
    case InputSessionState::fenced: return "fenced";
    }
    return "";
}

std::optional<InputSessionState> parse_input_session_state(std::string_view name) {
    for (const auto state : {InputSessionState::ready, InputSessionState::executing,
                             InputSessionState::awaiting_feedback, InputSessionState::releasing,
                             InputSessionState::released, InputSessionState::fenced}) {
        if (name == input_session_state_name(state)) {
            return state;
        }
    }
    return std::nullopt;
}

const char *input_session_event_name(InputSessionEvent event) {
    switch (event) {
    case InputSessionEvent::admit: return "admit";
    case InputSessionEvent::command_admitted: return "command_admitted";
    case InputSessionEvent::command_settled: return "command_settled";
    case InputSessionEvent::observe: return "observe";
    case InputSessionEvent::observe_repeat: return "observe_repeat";
    case InputSessionEvent::release_requested: return "release_requested";
    case InputSessionEvent::release_confirmed: return "release_confirmed";
    case InputSessionEvent::release_failed: return "release_failed";
    }
    return "";
}

std::optional<InputSessionState> advance_input_session(InputSessionState state,
                                                       InputSessionEvent event) {
    for (const auto &transition : kTransitions) {
        if (transition.from == state && transition.event == event) {
            return transition.to;
        }
    }
    return std::nullopt;
}

const char *input_session_outcome_name(InputSessionOutcome outcome) {
    switch (outcome) {
    case InputSessionOutcome::admitted: return "admitted";
    case InputSessionOutcome::delivered: return "delivered";
    case InputSessionOutcome::holding: return "holding";
    case InputSessionOutcome::released: return "released";
    case InputSessionOutcome::uncertain: return "uncertain";
    }
    return "";
}

const char *input_command_kind_name(InputSessionCommand::Kind kind) {
    switch (kind) {
    case InputSessionCommand::Kind::key_down: return "key_down";
    case InputSessionCommand::Kind::key_up: return "key_up";
    case InputSessionCommand::Kind::key_tap: return "key_tap";
    case InputSessionCommand::Kind::button_down: return "button_down";
    case InputSessionCommand::Kind::button_up: return "button_up";
    case InputSessionCommand::Kind::button_tap: return "button_tap";
    case InputSessionCommand::Kind::smooth_move: return "smooth_move";
    }
    return "";
}

std::optional<InputSessionCommand::Kind> parse_input_command_kind(std::string_view name) {
    for (const auto kind : {InputSessionCommand::Kind::key_down, InputSessionCommand::Kind::key_up,
                            InputSessionCommand::Kind::key_tap,
                            InputSessionCommand::Kind::button_down,
                            InputSessionCommand::Kind::button_up,
                            InputSessionCommand::Kind::button_tap,
                            InputSessionCommand::Kind::smooth_move}) {
        if (name == input_command_kind_name(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

std::optional<ProviderError> validate_input_session_command(
    const InputSessionCommand &command, const InputSessionCommandContext &context) {
    if (!has_injection_source(context)) {
        return ProviderError{"unsupported_platform", "no input source capability is declared"};
    }
    if (context.commands_used >= input_session_budget::max_session_commands) {
        return ProviderError{"result_too_large", "session command budget is exhausted"};
    }
    const bool needs_key = command.kind == InputSessionCommand::Kind::key_down ||
                           command.kind == InputSessionCommand::Kind::key_up ||
                           command.kind == InputSessionCommand::Kind::key_tap;
    const bool needs_button = command.kind == InputSessionCommand::Kind::button_down ||
                              command.kind == InputSessionCommand::Kind::button_up ||
                              command.kind == InputSessionCommand::Kind::button_tap;
    const bool is_down = command.kind == InputSessionCommand::Kind::key_down ||
                         command.kind == InputSessionCommand::Kind::button_down;
    if (command.dwell.count() < 0 || command.duration.count() < 0) {
        return ProviderError{"invalid_argument", "negative timing is not a session command"};
    }
    if (is_down &&
        context.held_inputs >= input_session_budget::max_held_inputs) {
        return ProviderError{"result_too_large", "held input budget is exhausted"};
    }
    if (needs_key) {
        const auto key = parse_session_key_name(command.key_name);
        if (!key) {
            return ProviderError{"invalid_argument", "key name is outside the session vocabulary"};
        }
        if (is_modifier(*key)) {
            if (!context.capabilities.has(InputCapability::independent_modifiers)) {
                return ProviderError{"unsupported_platform",
                                     "independent modifiers are not declared"};
            }
        } else if (command.kind != InputSessionCommand::Kind::key_tap) {
            // A bare down/up of a base key is exactly a cross-observation hold.
            if (!context.capabilities.has(InputCapability::cross_observation_hold)) {
                return ProviderError{"unsupported_platform",
                                     "cross-observation hold is not declared"};
            }
        }
    }
    if (needs_button) {
        if (!valid_action_button(command.button)) {
            return ProviderError{"invalid_argument", "button is outside the action vocabulary"};
        }
        if (command.kind != InputSessionCommand::Kind::button_tap &&
            !context.capabilities.has(InputCapability::cross_observation_hold)) {
            return ProviderError{"unsupported_platform",
                                 "cross-observation hold is not declared"};
        }
    }
    if (command.kind == InputSessionCommand::Kind::key_tap ||
        command.kind == InputSessionCommand::Kind::button_tap) {
        if (command.dwell > std::chrono::milliseconds::zero() &&
            !context.capabilities.has(InputCapability::timed_press)) {
            return ProviderError{"unsupported_platform", "timed press is not declared"};
        }
        if (command.dwell > input_session_budget::max_press_dwell) {
            return ProviderError{"result_too_large", "press dwell exceeds the action bound"};
        }
    }
    if (command.kind == InputSessionCommand::Kind::smooth_move) {
        if (!context.capabilities.has(InputCapability::smooth_move)) {
            return ProviderError{"unsupported_platform", "smooth move is not declared"};
        }
        if (const auto timing = validate_smooth_move_timing(command.duration, command.path_points)) {
            return timing;
        }
    }
    return std::nullopt;
}

std::optional<ProviderError> validate_smooth_move_timing(std::chrono::milliseconds duration,
                                                         std::uint32_t path_points) {
    if (duration.count() < 0) {
        return ProviderError{"invalid_argument", "negative move duration is not a timing"};
    }
    if (duration < input_session_budget::min_move_duration) {
        return ProviderError{"invalid_argument", "move duration is below the frozen minimum"};
    }
    if (duration > input_session_budget::max_move_duration) {
        return ProviderError{"result_too_large", "move duration exceeds the action bound"};
    }
    if (path_points > input_session_budget::max_path_points) {
        return ProviderError{"result_too_large", "path point budget is exhausted"};
    }
    return std::nullopt;
}

std::vector<SmoothMoveSample> build_smooth_move_schedule(PointerPoint from, PointerPoint to,
                                                         std::chrono::milliseconds duration,
                                                         std::uint32_t path_points) {
    if (from.x == to.x && from.y == to.y)
        return {};
    const std::int64_t dx = std::int64_t{to.x} - from.x, dy = std::int64_t{to.y} - from.y;
    std::uint64_t segments = path_points != 0 ? path_points
                                              : input_session_budget::default_path_points;
    const std::int64_t max_fitting = std::max<std::int64_t>(1, duration.count() / 4);
    segments = std::min<std::uint64_t>(segments, static_cast<std::uint64_t>(max_fitting));
    std::vector<SmoothMoveSample> samples;
    samples.reserve(segments);
    for (std::uint32_t index = 1; index <= segments; ++index) {
        const double u = static_cast<double>(index) / segments;
        const double eased = u * u * u * (10.0 + u * (-15.0 + 6.0 * u));
        SmoothMoveSample sample;
        sample.at = std::chrono::milliseconds(std::llround(duration.count() * u));
        sample.x = from.x + static_cast<std::int32_t>(std::llround(eased * static_cast<double>(dx)));
        sample.y = from.y + static_cast<std::int32_t>(std::llround(eased * static_cast<double>(dy)));
        samples.push_back(sample);
    }
    samples.back().x = to.x;
    samples.back().y = to.y;
    return samples;
}

} // namespace mirage::desktop
