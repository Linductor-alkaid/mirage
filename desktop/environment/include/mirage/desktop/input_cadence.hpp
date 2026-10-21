#pragma once

#include <mirage/desktop/input_provider.hpp>

namespace mirage::desktop {
/// DEC-110: host-only timing for complete actions, separate from held sessions.
namespace input_cadence_policy {
inline constexpr std::uint32_t version = 1;
inline constexpr std::chrono::milliseconds action_timeout{3000}, text_timeout{15000};
} // namespace input_cadence_policy

enum class InputCadencePhase : std::uint8_t {
    key_dwell,
    key_interval,
    modifier_interval,
    release_interval,
    wheel_dwell,
    wheel_interval,
    drag_press,
    drag_settle
};

/// Pure seeded planning; no waiting, input, mutable RNG, or model timing fields.
std::chrono::milliseconds input_cadence_delay(std::uint64_t seed, InputCadencePhase,
                                              std::size_t index = 0);
std::chrono::milliseconds chord_cadence_duration(std::uint64_t seed, std::size_t keys);
std::chrono::milliseconds wheel_cadence_duration(std::uint64_t seed, std::size_t steps);
std::chrono::milliseconds drag_cadence_duration(PointerPoint from, PointerPoint to,
                                                const PointerMotionVariation &);
} // namespace mirage::desktop
