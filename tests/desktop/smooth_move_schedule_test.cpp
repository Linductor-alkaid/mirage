#include "../support/test.hpp"

#include <mirage/desktop/input_session.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

// DEC-110 / M7-26: contract of the shared platform-neutral smooth-move
// schedule generator. The X11 backend (x11_smooth_move_test) and the Portal
// shared-input glide path both consume this generator, so its frozen
// semantics are pinned here once: zero distance travels nothing, the segment
// budget is min(requested-or-default, duration over the 4 ms cadence floor),
// positions follow the minimum-jerk easing monotonically toward the target,
// and the final sample lands exactly on the target.
namespace d = mirage::desktop;

namespace {

std::size_t expected_segments(std::chrono::milliseconds duration, std::uint32_t points) {
    const std::uint64_t requested =
        points != 0 ? points : d::input_session_budget::default_path_points;
    const auto fitting = static_cast<std::uint64_t>(std::max<std::int64_t>(1, duration.count() / 4));
    return static_cast<std::size_t>(std::min(requested, fitting));
}

void zero_distance_is_empty() {
    for (const auto &[from, to] :
         {std::pair{d::PointerPoint{0, 0}, d::PointerPoint{0, 0}},
          std::pair{d::PointerPoint{1920, 1080}, d::PointerPoint{1920, 1080}},
          std::pair{d::PointerPoint{-40, -7}, d::PointerPoint{-40, -7}}}) {
        const auto schedule =
            d::build_smooth_move_schedule(from, to, std::chrono::milliseconds{600}, 0);
        MIRAGE_CHECK(schedule.empty());
    }
}

void segment_budget() {
    // points=0 selects the frozen default whenever the duration fits it.
    MIRAGE_CHECK(d::build_smooth_move_schedule({0, 0}, {300, 0}, std::chrono::milliseconds{600}, 0)
                     .size() == d::input_session_budget::default_path_points);
    MIRAGE_CHECK(d::build_smooth_move_schedule({0, 0}, {300, 0}, std::chrono::milliseconds{2000}, 0)
                     .size() == d::input_session_budget::default_path_points);
    // The 4 ms cadence floor caps the segments for short durations:
    // 100 ms fits at most 25 segments regardless of the requested points.
    const auto floored =
        d::build_smooth_move_schedule({0, 0}, {300, 0}, std::chrono::milliseconds{100}, 0);
    MIRAGE_CHECK(floored.size() == 25);
    MIRAGE_CHECK(floored.size() ==
                 expected_segments(std::chrono::milliseconds{100}, 0));
    MIRAGE_CHECK(
        d::build_smooth_move_schedule({0, 0}, {300, 0}, std::chrono::milliseconds{100}, 32)
            .size() == 25);
    // A smaller explicit request wins over the floor.
    MIRAGE_CHECK(
        d::build_smooth_move_schedule({0, 0}, {300, 0}, std::chrono::milliseconds{100}, 10)
            .size() == 10);
    // Minimum duration edge: 50 ms fits 12 segments.
    MIRAGE_CHECK(d::build_smooth_move_schedule({0, 0}, {300, 0}, std::chrono::milliseconds{50}, 0)
                     .size() == 12);
    // Sample count always matches the effective segment budget.
    for (const auto duration : {std::chrono::milliseconds{50},
                                std::chrono::milliseconds{100},
                                std::chrono::milliseconds{333},
                                std::chrono::milliseconds{600},
                                std::chrono::milliseconds{2000}}) {
        for (const auto points : {0u, 1u, 2u, 7u, 32u, 64u}) {
            const auto schedule = d::build_smooth_move_schedule({10, 20}, {-110, 220}, duration,
                                                                points);
            MIRAGE_CHECK(schedule.size() == expected_segments(duration, points));
        }
    }
}

void landing_and_timing() {
    for (const auto &[from, to] :
         {std::pair{d::PointerPoint{100, 100}, d::PointerPoint{400, 250}},
          std::pair{d::PointerPoint{500, 12}, d::PointerPoint{-300, 7}},
          std::pair{d::PointerPoint{0, 0}, d::PointerPoint{1, 1}}}) {
        for (const auto duration : {std::chrono::milliseconds{50},
                                    std::chrono::milliseconds{100},
                                    std::chrono::milliseconds{600}}) {
            const auto schedule = d::build_smooth_move_schedule(from, to, duration, 0);
            MIRAGE_CHECK(!schedule.empty());
            MIRAGE_CHECK(schedule.front().at > std::chrono::milliseconds::zero());
            MIRAGE_CHECK(schedule.back().x == to.x && schedule.back().y == to.y);
            MIRAGE_CHECK(schedule.back().at == duration);
            // Timestamps advance monotonically toward the total duration.
            MIRAGE_CHECK(std::is_sorted(schedule.begin(), schedule.end(),
                                        [](const auto &a, const auto &b) { return a.at <= b.at; }));
        }
    }
}

void monotone_toward_target() {
    for (const auto &[from, to] :
         {std::pair{d::PointerPoint{100, 100}, d::PointerPoint{400, 250}},
          std::pair{d::PointerPoint{500, 400}, d::PointerPoint{60, 80}},
          std::pair{d::PointerPoint{-50, -50}, d::PointerPoint{50, -150}}}) {
        const auto schedule =
            d::build_smooth_move_schedule(from, to, std::chrono::milliseconds{600}, 0);
        MIRAGE_CHECK(schedule.size() >= 2);
        for (std::size_t n = 0; n < schedule.size(); ++n) {
            // Rounding may create plateaus; it may never overshoot the target
            // or reverse direction.
            if (to.x > from.x) {
                MIRAGE_CHECK(schedule[n].x >= from.x && schedule[n].x <= to.x);
                if (n)
                    MIRAGE_CHECK(schedule[n].x >= schedule[n - 1].x);
            } else if (to.x < from.x) {
                MIRAGE_CHECK(schedule[n].x <= from.x && schedule[n].x >= to.x);
                if (n)
                    MIRAGE_CHECK(schedule[n].x <= schedule[n - 1].x);
            }
            if (to.y > from.y) {
                MIRAGE_CHECK(schedule[n].y >= from.y && schedule[n].y <= to.y);
                if (n)
                    MIRAGE_CHECK(schedule[n].y >= schedule[n - 1].y);
            } else if (to.y < from.y) {
                MIRAGE_CHECK(schedule[n].y <= from.y && schedule[n].y >= to.y);
                if (n)
                    MIRAGE_CHECK(schedule[n].y <= schedule[n - 1].y);
            }
        }
    }
}

void minimum_jerk_easing() {
    // minimum_jerk_v1 has zero velocity/acceleration at both ends: the first
    // step advances a small fraction of the path and the midpoint of an even
    // segment count sits at half the distance (eased(0.5) == 0.5).
    const auto schedule =
        d::build_smooth_move_schedule({0, 0}, {320, 160}, std::chrono::milliseconds{600}, 32);
    MIRAGE_CHECK(schedule.size() == 32);
    MIRAGE_CHECK(schedule[0].x * 32 <= 2 * 320 + 32 && schedule[0].y * 32 <= 2 * 160 + 32);
    MIRAGE_CHECK(schedule[15].x == 160 && schedule[15].y == 80);
    // Slow start, slow finish: the edge quarters cover much less of the path
    // than the quarters adjacent to the midpoint (classic S-curve).
    const auto quarter = schedule[7].x, half = schedule[15].x, three_quarter = schedule[23].x;
    MIRAGE_CHECK(quarter < half - quarter);
    MIRAGE_CHECK(320 - three_quarter < three_quarter - half);
    // Deterministic: the same inputs rebuild the identical schedule.
    const auto again =
        d::build_smooth_move_schedule({0, 0}, {320, 160}, std::chrono::milliseconds{600}, 32);
    MIRAGE_CHECK(again.size() == schedule.size());
    for (std::size_t n = 0; n < schedule.size(); ++n)
        MIRAGE_CHECK(again[n].at == schedule[n].at && again[n].x == schedule[n].x &&
                     again[n].y == schedule[n].y);
}

} // namespace

int main() {
    MIRAGE_CHECK(d::automatic_pointer_duration({4, 5}, {4, 5}).count() == 0);
    MIRAGE_CHECK(d::automatic_pointer_duration({0, 0}, {100, 0}, 10) >
                 d::automatic_pointer_duration({0, 0}, {100, 0}, 100));
    MIRAGE_CHECK(d::automatic_pointer_duration({0, 0}, {100, 0}, 20) <
                 d::automatic_pointer_duration({0, 0}, {1000, 0}, 20));
    MIRAGE_CHECK(d::automatic_pointer_duration({INT32_MIN, INT32_MIN}, {INT32_MAX, INT32_MAX}) ==
                 d::pointer_motion_policy::max_duration);
    MIRAGE_CHECK(d::automatic_pointer_duration({0, 0}, {1, 0}, INT32_MAX) >=
                 d::pointer_motion_policy::min_duration);
    zero_distance_is_empty();
    segment_budget();
    landing_and_timing();
    monotone_toward_target();
    minimum_jerk_easing();
    return mirage::testing::finish("smooth_move_schedule");
}
