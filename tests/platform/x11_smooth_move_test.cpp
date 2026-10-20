#include "../support/test.hpp"
#include "../support/xvfb_display.hpp"

#include <mirage/desktop/input_session.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

// Xlib macros follow the pinned C++ headers (same include discipline as the
// other platform fixtures).
#include <X11/Xlib.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

// DEC-110 / M7-26 first increment: real X11 event-level evidence for the
// opt-in smooth pointer travel. The backend is exercised against a private
// headless Xvfb; an INDEPENDENT second Xlib connection samples the pointer
// during the travel because the backend call holds its mutex (and the
// calling thread) for the whole trip.
namespace d = mirage::desktop;

namespace {

struct Sample {
    std::chrono::steady_clock::time_point at;
    int x = -1, y = -1;
};

class Fixture {
  public:
    mirage::testing::XvfbDisplay server{mirage::testing::find_xvfb(), "800x600x24"};
    mirage::platform::linux_backend::LinuxDesktopEnvironment env{
        {}, {.enabled = true, .display = server.display_name()}};
    Display *sampler = XOpenDisplay(server.display_name().c_str());

    Fixture() {
        if (!sampler)
            throw std::runtime_error("private sampling X client unavailable");
        MIRAGE_CHECK(env.input() != nullptr);
        MIRAGE_CHECK(env.input()->timed_pointer_actions_supported());
    }
    ~Fixture() {
        if (sampler)
            XCloseDisplay(sampler);
    }
    Fixture(const Fixture &) = delete;
    Fixture &operator=(const Fixture &) = delete;

    bool query(int &x, int &y) const {
        Window root{}, child{};
        int rx = -1, ry = -1, wx = 0, wy = 0;
        unsigned int mask = 0;
        if (!XQueryPointer(sampler, DefaultRootWindow(sampler), &root, &child, &rx, &ry, &wx, &wy,
                           &mask))
            return false;
        x = rx;
        y = ry;
        return true;
    }
};

/// Full travel: sample the pointer from the independent connection while the
/// backend paces the minimum-jerk path, then verify corridor, landing and
/// wall-clock budget with the concrete numbers on stderr.
void travel_trajectory(bool automatic = false) {
    Fixture f;
    auto *input = f.env.input();
    const d::PointerPoint start{200, 300}, target{600, 300};
    const auto moved = input->pointer_move(start.x, start.y);
    MIRAGE_CHECK(moved.ok);
    int x = -1, y = -1;
    MIRAGE_CHECK(f.query(x, y) && x == start.x && y == start.y);

    std::vector<Sample> samples;
    std::atomic_bool done{false};
    // Test-scaffold thread (allowed by the workspace discipline): pure
    // observer on its own X connection, joined before the fixture dies.
    std::thread sampler([&] {
        while (!done.load(std::memory_order_acquire)) {
            Sample s{std::chrono::steady_clock::now()};
            if (f.query(s.x, s.y))
                samples.push_back(s);
            std::this_thread::sleep_for(std::chrono::milliseconds{10});
        }
    });
    const auto began = std::chrono::steady_clock::now();
    const auto planned = automatic ? d::automatic_pointer_duration(start, target, 32)
                                   : std::chrono::milliseconds{600};
    const auto out = automatic ? input->approach_target(target, 32, {}, {})
                               : input->smooth_move_to(target.x, target.y, planned, 40, {}, {});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began);
    done.store(true, std::memory_order_release);
    sampler.join();

    MIRAGE_CHECK(out.ok && !out.cancelled && out.error.code.empty());
    MIRAGE_CHECK(out.side_effect_may_have_occurred);
    MIRAGE_CHECK(!samples.empty());
    // Every observation stays on the horizontal corridor between start and
    // target (2 px endpoint tolerance) and never moves backwards: the
    // minimum-jerk baseline is monotone and the X server only advances.
    bool corridor = true, monotone = true;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (samples[index].x < start.x - 2 || samples[index].x > target.x + (automatic ? 4 : 2) ||
            std::abs(samples[index].y - start.y) > (automatic ? 28 : 2)) {
            corridor = false;
            std::fprintf(stderr, "off-corridor sample %zu: (%d,%d)\n", index, samples[index].x,
                         samples[index].y);
        }
        if (index > 0 && samples[index].x < samples[index - 1].x) {
            monotone = false;
            std::fprintf(stderr, "backwards sample %zu: (%d,%d) after (%d,%d)\n", index,
                         samples[index].x, samples[index].y, samples[index - 1].x,
                         samples[index - 1].y);
        }
    }
    MIRAGE_CHECK(corridor);
    MIRAGE_CHECK(automatic || monotone);
    if (automatic)
        MIRAGE_CHECK(std::any_of(samples.begin(), samples.end(),
                                 [&](const auto &s) { return std::abs(s.y - start.y) > 2; }));
    // The timed path must be observable on the server WHILE it runs: at
    // least one independent observation strictly between the endpoints. A
    // travel that only lands on the target at its final XSync is a teleport
    // with no real hover effects, not a paced minimum-jerk path.
    const bool mid_flight_seen = std::any_of(samples.begin(), samples.end(), [&](const Sample &s) {
        return s.x > start.x + 2 && s.x < target.x - 2;
    });
    if (!mid_flight_seen) {
        std::fprintf(stderr, "no intermediate position observed on the X server during the travel "
                             "(segments buffered client-side until the final XSync)\n");
    }
    MIRAGE_CHECK(mid_flight_seen);
    // Trajectory evidence (requirement: the sampled sequence is reported).
    std::fprintf(stderr, "travel samples=%zu:", samples.size());
    for (std::size_t index = 0; index < samples.size(); ++index) {
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(samples[index].at - began)
                .count();
        std::fprintf(stderr, " %lldms:(%d,%d)", static_cast<long long>(ms), samples[index].x,
                     samples[index].y);
    }
    std::fprintf(stderr, "\n");
    // Landing: read back through both the independent connection and the
    // provider query (±2 px tolerance).
    MIRAGE_CHECK(f.query(x, y) && std::abs(x - target.x) <= 2 && std::abs(y - target.y) <= 2);
    const auto position = input->pointer_position({});
    MIRAGE_CHECK(position.ok && std::abs(position.position.x - target.x) <= 2 &&
                 std::abs(position.position.y - target.y) <= 2);
    // Wall clock covers the paced travel with margin on both sides.
    std::fprintf(stderr, "travel automatic=%d elapsed=%lldms planned=%lldms\n", automatic,
                 static_cast<long long>(elapsed.count()), static_cast<long long>(planned.count()));
    MIRAGE_CHECK(
        elapsed.count() >= (automatic ? planned.count() * 88 / 100 : planned.count()) - 50 &&
        elapsed.count() <= (automatic ? planned.count() * 112 / 100 + 1 : planned.count()) + 500);
}

void natural_screen_edge() {
    Fixture f;
    auto *input = f.env.input();
    MIRAGE_CHECK(input->pointer_move(0, 300).ok);
    unsigned observed = 0;
    d::CancelToken observer([&] {
        int x = 0, y = 0;
        MIRAGE_CHECK(f.query(x, y) && x >= 0 && x < 800 && y >= 0 && y < 600);
        ++observed;
        return false;
    });
    const auto out = input->smooth_move_to(0, 599, std::chrono::milliseconds{200}, 32, {}, observer,
                                           d::PointerMotionVariation{0, 8});
    MIRAGE_CHECK(out.ok && observed > 8);
    int x = -1, y = -1;
    MIRAGE_CHECK(f.query(x, y) && x == 0 && y == 599);
    const auto invalid = input->smooth_move_to(800, 0, std::chrono::milliseconds{200}, 32, {}, {},
                                               d::PointerMotionVariation{0, 8});
    MIRAGE_CHECK(!invalid.ok && !invalid.side_effect_may_have_occurred);
}

/// Same start and target: the backend reports success immediately and never
/// sends a motion (the early success return precedes the side-effect flag).
void zero_distance() {
    Fixture f;
    auto *input = f.env.input();
    MIRAGE_CHECK(input->pointer_move(411, 233).ok);
    const auto began = std::chrono::steady_clock::now();
    const auto out = input->smooth_move_to(411, 233, std::chrono::milliseconds{600}, 0, {}, {});
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began);
    MIRAGE_CHECK(out.ok && !out.cancelled);
    MIRAGE_CHECK(!out.side_effect_may_have_occurred);
    MIRAGE_CHECK(elapsed.count() < 100);
    int x = -1, y = -1;
    MIRAGE_CHECK(f.query(x, y) && x == 411 && y == 233);
    std::fprintf(stderr, "zero-distance elapsed=%lldms\n", static_cast<long long>(elapsed.count()));
}

/// The frozen budget matrix is enforced by the backend before any event;
/// a timeout smaller than the travel duration is invalid too. The pointer
/// stays exactly where it was.
void rejection_matrix() {
    Fixture f;
    auto *input = f.env.input();
    MIRAGE_CHECK(input->pointer_move(300, 300).ok);
    int before_x = -1, before_y = -1;
    MIRAGE_CHECK(f.query(before_x, before_y));

    const auto too_fast = input->smooth_move_to(500, 300, std::chrono::milliseconds{49}, 0, {}, {});
    MIRAGE_CHECK(!too_fast.ok && too_fast.error.code == "invalid_argument" &&
                 !too_fast.side_effect_may_have_occurred);
    const auto too_slow =
        input->smooth_move_to(500, 300, std::chrono::milliseconds{2001}, 0, {}, {});
    MIRAGE_CHECK(!too_slow.ok && too_slow.error.code == "result_too_large" &&
                 !too_slow.side_effect_may_have_occurred);
    const auto too_many_points =
        input->smooth_move_to(500, 300, std::chrono::milliseconds{600}, 65, {}, {});
    MIRAGE_CHECK(!too_many_points.ok && too_many_points.error.code == "result_too_large" &&
                 !too_many_points.side_effect_may_have_occurred);
    d::InputLimits tight;
    tight.timeout = std::chrono::milliseconds{300};
    const auto short_budget =
        input->smooth_move_to(500, 300, std::chrono::milliseconds{600}, 0, tight, {});
    MIRAGE_CHECK(!short_budget.ok && short_budget.error.code == "invalid_argument" &&
                 !short_budget.side_effect_may_have_occurred);
    // Zero and negative durations are timing rejections on the backend too.
    const auto zero = input->smooth_move_to(500, 300, std::chrono::milliseconds{0}, 0, {}, {});
    MIRAGE_CHECK(!zero.ok && zero.error.code == "invalid_argument");

    int after_x = -1, after_y = -1;
    MIRAGE_CHECK(f.query(after_x, after_y) && after_x == before_x && after_y == before_y);
    // A timeout that covers the travel (the default 1000 ms over 600 ms) is
    // accepted, so the rejections above are about coverage, not the limit.
    const auto covered =
        input->smooth_move_to(340, 300, std::chrono::milliseconds{600}, 0, tight, {});
    // tight.timeout (300 ms) < duration (600 ms) is rejected; restore a
    // covering budget for the positive control.
    MIRAGE_CHECK(!covered.ok && covered.error.code == "invalid_argument");
    d::InputLimits covering;
    covering.timeout = std::chrono::milliseconds{1000};
    const auto ok_travel =
        input->smooth_move_to(340, 300, std::chrono::milliseconds{600}, 0, covering, {});
    MIRAGE_CHECK(ok_travel.ok);
}

/// A cancel requested ~150 ms into a 600 ms travel stops between segments:
/// cancelled outcome, real hover side effects already sent, pointer left
/// strictly between start and target (no backlog replay).
void cancellation_mid_travel(bool automatic = false) {
    Fixture f;
    auto *input = f.env.input();
    MIRAGE_CHECK(input->pointer_move(200, 300).ok);
    d::CancelToken stop;
    const auto began = std::chrono::steady_clock::now();
    d::CancelToken probe{[&] {
        if (std::chrono::steady_clock::now() - began > std::chrono::milliseconds{150})
            stop.request_cancel();
        return stop.cancelled();
    }};
    const auto out =
        automatic ? input->approach_target({600, 300}, 32, {}, probe)
                  : input->smooth_move_to(600, 300, std::chrono::milliseconds{600}, 40, {}, probe);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - began);
    MIRAGE_CHECK(!out.ok && out.cancelled);
    MIRAGE_CHECK(out.side_effect_may_have_occurred);
    // The travel stopped at a segment boundary well before its full budget.
    std::fprintf(stderr, "cancel elapsed=%lldms\n", static_cast<long long>(elapsed.count()));
    MIRAGE_CHECK(elapsed.count() >= 100 && elapsed.count() < 600);
    int x = -1, y = -1;
    MIRAGE_CHECK(f.query(x, y));
    std::fprintf(stderr, "cancel stopped at (%d,%d)\n", x, y);
    // The cancel fires between segments ~10 and ~12 of 40 (the probe needs
    // >150 ms and segments are paced at 15 ms), so a travel delivered in
    // real time has already advanced the pointer past the start when it
    // stops short of the target; the remaining path is never replayed.
    MIRAGE_CHECK(x > 205 && x < 595);
    MIRAGE_CHECK(std::abs(y - 300) <= (automatic ? 28 : 2));
}

/// Arrival is a click precondition, not a substitute: after a real travel
/// the ordinary complete click still lands and reports release.
void click_after_travel() {
    Fixture f;
    auto *input = f.env.input();
    const auto travel = input->smooth_move_to(600, 300, std::chrono::milliseconds{600}, 40, {}, {});
    MIRAGE_CHECK(travel.ok && travel.side_effect_may_have_occurred);
    const auto clicked = input->click_sequence_at(600, 300, "left", 1, {}, {});
    MIRAGE_CHECK(clicked.ok && clicked.input_released);
    int x = -1, y = -1;
    MIRAGE_CHECK(f.query(x, y) && x == 600 && y == 300);
    Window root{}, child{};
    int rx = -1, ry = -1, wx = 0, wy = 0;
    unsigned int mask = 0;
    MIRAGE_CHECK(XQueryPointer(f.sampler, DefaultRootWindow(f.sampler), &root, &child, &rx, &ry,
                               &wx, &wy, &mask));
    MIRAGE_CHECK(!(mask & Button1Mask)); // no button left held on the server
}

} // namespace

int main() {
    if (mirage::testing::find_xvfb().empty()) {
        std::fprintf(stderr, "[x11_smooth_move] SKIP: no Xvfb binary (set MIRAGE_XVFB)\n");
        return 0;
    }
    try {
        travel_trajectory();
        travel_trajectory(true);
        natural_screen_edge();
        zero_distance();
        rejection_matrix();
        cancellation_mid_travel();
        cancellation_mid_travel(true);
        click_after_travel();
    } catch (const std::exception &error) {
        std::fprintf(stderr, "smooth move fixture: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("x11_smooth_move");
}
