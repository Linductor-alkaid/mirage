#include "../support/test.hpp"

#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/input_session.hpp>

#include <chrono>

// DEC-110 / M7-26 first increment: contract of the opt-in timed pointer
// travel on the platform-neutral InputProvider surface. The default
// implementation must validate the frozen smooth-move budgets through the
// shared timing validator (so the bounds cannot diverge between layers) and
// then refuse with unsupported_platform until a backend opts in.
namespace d = mirage::desktop;

namespace {

/// Minimal bare provider: implements exactly the pure-virtual surface and
/// deliberately does not override the timed-pointer members, so every
/// smooth_move_to call exercises the InputProvider default implementation.
class BareProvider final : public d::InputProvider {
  public:
    d::InputOutcome inject_key(const d::KeySym &, bool, const d::InputLimits &,
                               const d::CancelToken &) override {
        return {};
    }
    d::InputOutcome type_text(const std::string &, const d::InputLimits &,
                              const d::CancelToken &) override {
        return {};
    }
    d::InputOutcome pointer_move(std::int32_t, std::int32_t, const d::InputLimits &,
                                 const d::CancelToken &) override {
        return {};
    }
    d::InputOutcome pointer_button(const d::MouseButton &, bool, const d::InputLimits &,
                                   const d::CancelToken &) override {
        return {};
    }
    d::PointerQueryOutcome pointer_position(const d::CancelToken &) override { return {}; }
};

void default_provider_contract() {
    BareProvider provider;
    // The capability is a separate opt-in: a plain provider never declares it.
    MIRAGE_CHECK(!provider.timed_pointer_actions_supported());

    // Below the frozen minimum: invalid_argument before any platform claim.
    const auto too_fast =
        provider.smooth_move_to(10, 10, std::chrono::milliseconds{49}, 0, {}, {});
    MIRAGE_CHECK(!too_fast.ok);
    MIRAGE_CHECK(!too_fast.cancelled);
    MIRAGE_CHECK(!too_fast.side_effect_may_have_occurred);
    MIRAGE_CHECK(too_fast.input_released);
    MIRAGE_CHECK(too_fast.error.code == "invalid_argument");

    // Zero and negative durations are invalid arguments (a negative value
    // must not slip through as an unsigned wrap or an overflow).
    for (const auto duration : {std::chrono::milliseconds{0}, std::chrono::milliseconds{-1}}) {
        const auto out = provider.smooth_move_to(10, 10, duration, 0, {}, {});
        MIRAGE_CHECK(!out.ok);
        MIRAGE_CHECK(out.error.code == "invalid_argument");
        MIRAGE_CHECK(!out.side_effect_may_have_occurred);
    }

    // Above the frozen maximum: result_too_large.
    const auto too_slow =
        provider.smooth_move_to(10, 10, std::chrono::milliseconds{2001}, 0, {}, {});
    MIRAGE_CHECK(!too_slow.ok);
    MIRAGE_CHECK(too_slow.error.code == "result_too_large");
    MIRAGE_CHECK(!too_slow.side_effect_may_have_occurred);

    // Over the point budget: result_too_large even though the timing is valid.
    const auto too_many_points =
        provider.smooth_move_to(10, 10, std::chrono::milliseconds{600}, 65, {}, {});
    MIRAGE_CHECK(!too_many_points.ok);
    MIRAGE_CHECK(too_many_points.error.code == "result_too_large");
    MIRAGE_CHECK(!too_many_points.side_effect_may_have_occurred);

    // In-budget travel on a provider that never opted in: unsupported after
    // the timing validation, never a silent success. path_points=0 selects
    // the frozen default and stays in budget.
    const auto unsupported =
        provider.smooth_move_to(10, 10, std::chrono::milliseconds{600}, 0, {}, {});
    MIRAGE_CHECK(!unsupported.ok);
    MIRAGE_CHECK(!unsupported.cancelled);
    MIRAGE_CHECK(unsupported.error.code == "unsupported_platform");
    MIRAGE_CHECK(!unsupported.side_effect_may_have_occurred);
    const auto unsupported_points =
        provider.smooth_move_to(10, 10, std::chrono::milliseconds{600}, 32, {}, {});
    MIRAGE_CHECK(!unsupported_points.ok);
    MIRAGE_CHECK(unsupported_points.error.code == "unsupported_platform");
}

void timing_validator_boundaries() {
    // Accepted edges of the frozen budget table: duration in [50, 2000],
    // path_points in [0, 64] (0 selects default_path_points).
    for (const auto duration : {std::chrono::milliseconds{50}, std::chrono::milliseconds{2000}}) {
        MIRAGE_CHECK(!d::validate_smooth_move_timing(duration, 0));
        MIRAGE_CHECK(!d::validate_smooth_move_timing(duration, 64));
    }
    MIRAGE_CHECK(!d::validate_smooth_move_timing(std::chrono::milliseconds{600}, 0));

    // Rejections: negative/short durations are invalid_argument, long
    // durations and over-budget points are result_too_large.
    const auto negative = d::validate_smooth_move_timing(std::chrono::milliseconds{-1}, 0);
    MIRAGE_CHECK(negative.has_value());
    MIRAGE_CHECK(negative->code == "invalid_argument");
    const auto short_duration = d::validate_smooth_move_timing(std::chrono::milliseconds{49}, 0);
    MIRAGE_CHECK(short_duration.has_value());
    MIRAGE_CHECK(short_duration->code == "invalid_argument");
    const auto long_duration = d::validate_smooth_move_timing(std::chrono::milliseconds{2001}, 0);
    MIRAGE_CHECK(long_duration.has_value());
    MIRAGE_CHECK(long_duration->code == "result_too_large");
    const auto too_many_points =
        d::validate_smooth_move_timing(std::chrono::milliseconds{600}, 65);
    MIRAGE_CHECK(too_many_points.has_value());
    MIRAGE_CHECK(too_many_points->code == "result_too_large");
}

} // namespace

int main() {
    default_provider_contract();
    timing_validator_boundaries();
    return mirage::testing::finish("smooth_move_contract");
}
