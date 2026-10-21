#include <algorithm>
#include <array>
#include <mirage/desktop/input_cadence.hpp>
#include <mirage/desktop/input_session.hpp>
#include <random>
#include <utility>

namespace mirage::desktop {
std::chrono::milliseconds input_cadence_delay(std::uint64_t seed, InputCadencePhase phase,
                                              std::size_t index) {
    constexpr std::array<std::pair<int, int>, 8> ranges{
        {{45, 85}, {35, 85}, {12, 28}, {8, 18}, {8, 16}, {30, 65}, {45, 80}, {35, 65}}};
    const auto kind = static_cast<std::size_t>(phase);
    if (kind >= ranges.size())
        return {};
    std::mt19937_64 random(seed ^ (std::uint64_t{kind + 1} << 56) ^
                           (std::uint64_t{index} * UINT64_C(0x9e3779b97f4a7c15)));
    const auto [low, high] = ranges[kind];
    return std::chrono::milliseconds{low + static_cast<int>(random() % (high - low + 1))};
}
std::chrono::milliseconds long_press_cadence_duration(std::uint64_t seed) {
    return std::chrono::milliseconds{900} +
           4 * input_cadence_delay(seed, InputCadencePhase::key_dwell);
}
std::chrono::milliseconds chord_cadence_duration(std::uint64_t seed, std::size_t keys) {
    auto total = input_cadence_delay(seed, InputCadencePhase::key_dwell);
    for (std::size_t i = 1; i < keys; ++i)
        total += input_cadence_delay(seed, InputCadencePhase::modifier_interval, i - 1) +
                 input_cadence_delay(seed, InputCadencePhase::release_interval, i - 1);
    return keys ? total : std::chrono::milliseconds{};
}
std::chrono::milliseconds wheel_cadence_duration(std::uint64_t seed, std::size_t steps) {
    std::chrono::milliseconds total{};
    for (std::size_t i = 0; i < steps; ++i) {
        if (i)
            total += input_cadence_delay(seed, InputCadencePhase::wheel_interval, i);
        total += input_cadence_delay(seed, InputCadencePhase::wheel_dwell, i);
    }
    return total;
}
std::chrono::milliseconds drag_cadence_duration(PointerPoint from, PointerPoint to,
                                                const PointerMotionVariation &variation) {
    if (from.x == to.x && from.y == to.y)
        return {};
    return std::clamp(varied_pointer_duration(from, to, variation) * 14 / 10,
                      std::chrono::milliseconds{220}, std::chrono::milliseconds{900});
}
} // namespace mirage::desktop
