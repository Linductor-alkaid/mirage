#include "../support/test.hpp"
#include <array>
#include <mirage/desktop/input_cadence.hpp>
#include <mirage/desktop/input_session.hpp>
#include <set>
#include <utility>

int main() {
    using namespace mirage::desktop;
    const std::array<std::pair<int, int>, 8> bounds{
        {{45, 85}, {35, 85}, {12, 28}, {8, 18}, {8, 16}, {30, 65}, {45, 80}, {35, 65}}};
    std::set<long long> times;
    for (std::uint64_t seed = 0; seed < 128; ++seed) {
        for (std::size_t phase = 0; phase < bounds.size(); ++phase)
            for (std::size_t index = 0; index < 32; ++index) {
                const auto kind = static_cast<InputCadencePhase>(phase);
                const auto delay = input_cadence_delay(seed, kind, index);
                MIRAGE_CHECK(delay.count() >= bounds[phase].first &&
                             delay.count() <= bounds[phase].second);
                MIRAGE_CHECK(delay == input_cadence_delay(seed, kind, index));
            }
        for (const auto keys : {1u, 3u, 5u}) {
            const auto total = chord_cadence_duration(seed, keys);
            MIRAGE_CHECK(total.count() >= 45 + (keys - 1) * 20 &&
                         total.count() <= 85 + (keys - 1) * 46);
        }
        const auto wheel = wheel_cadence_duration(seed, 16);
        MIRAGE_CHECK(wheel.count() >= 16 * 8 + 15 * 30 && wheel.count() <= 16 * 16 + 15 * 65);
        times.insert(wheel.count());
        for (const auto distance : {1, 300, 2048}) {
            PointerMotionVariation variation{seed, 32, false};
            const auto duration = drag_cadence_duration({0, 0}, {distance, 0}, variation);
            MIRAGE_CHECK(duration.count() >= 220 && duration.count() <= 900);
            const auto path =
                build_smooth_move_schedule({0, 0}, {distance, 0}, duration, 0, variation);
            MIRAGE_CHECK(path.back().x == distance && path.back().y == 0 &&
                         path.back().at == duration && path.size() <= 32);
            for (const auto &sample : path)
                MIRAGE_CHECK(sample.x >= 0 && sample.x <= distance);
        }
    }
    MIRAGE_CHECK(times.size() > 32);
    MIRAGE_CHECK(chord_cadence_duration(0, 0).count() == 0);
    MIRAGE_CHECK(wheel_cadence_duration(0, 0).count() == 0);
    MIRAGE_CHECK(drag_cadence_duration({1, 1}, {1, 1}, {}).count() == 0);
    return mirage::testing::finish("input_cadence_test");
}
