#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/input_session.hpp>
namespace mirage::desktop {
bool valid_pointer_scope(const PointerActionScope &scope, PointerPoint p,
                         const InputLimits &limits) {
    const auto &g = scope.geometry;
    return !scope.window_id.empty() && scope.window_id.size() <= 256 && g.width > 0 &&
           g.height > 0 && p.x >= g.x && p.y >= g.y &&
           std::int64_t{p.x} < std::int64_t{g.x} + g.width &&
           std::int64_t{p.y} < std::int64_t{g.y} + g.height && limits.timeout.count() > 0 &&
           limits.timeout.count() <= 1000;
}
bool valid_scroll_steps(std::int32_t horizontal, std::int32_t vertical) {
    if (horizontal < -16 || horizontal > 16 || vertical < -16 || vertical > 16)
        return false;
    const auto total =
        (horizontal < 0 ? -horizontal : horizontal) + (vertical < 0 ? -vertical : vertical);
    return total > 0 && total <= 16;
}
bool valid_drag_path(PointerPoint from, PointerPoint to) {
    const auto dx = std::int64_t{to.x} - from.x, dy = std::int64_t{to.y} - from.y;
    // Bound differences before multiplication to avoid INT32 endpoint overflow.
    return dx >= -2048 && dx <= 2048 && dy >= -2048 && dy <= 2048 && (dx || dy) &&
           dx * dx + dy * dy <= 2048LL * 2048;
}
InputOutcome InputProvider::scroll_at(const PointerActionScope &, PointerPoint, std::int32_t,
                                      std::int32_t, const InputLimits &, const CancelToken &) {
    return {false, false, {"unsupported_platform", "bounded scroll is unavailable"}};
}
InputOutcome InputProvider::drag_to(const PointerActionScope &, PointerPoint, PointerPoint,
                                    const InputLimits &, const CancelToken &) {
    return {false, false, {"unsupported_platform", "bounded drag is unavailable"}};
}
InputOutcome InputProvider::drag_with_button(const PointerActionScope &scope, PointerPoint from,
                                             PointerPoint to, const MouseButton &button,
                                             const InputLimits &limits, const CancelToken &cancel) {
    if (!valid_action_button(button))
        return {false, false, {"invalid_argument", "invalid drag button"}};
    if (button == "left")
        return drag_to(scope, from, to, limits, cancel);
    return {false, false, {"unsupported_platform", "selected drag button is unavailable"}};
}
InputOutcome InputProvider::key_chord(const KeySym &, const InputLimits &, const CancelToken &) {
    return {false, false, {"unsupported_platform", "complete key actions are unavailable"}};
}
InputOutcome InputProvider::click_at(std::int32_t, std::int32_t, const MouseButton &,
                                     const InputLimits &, const CancelToken &) {
    return {false, false, {"unsupported_platform", "complete pointer actions are unavailable"}};
}
InputOutcome InputProvider::click_sequence_at(std::int32_t x, std::int32_t y,
                                              const MouseButton &button, std::uint32_t count,
                                              const InputLimits &limits,
                                              const CancelToken &cancel) {
    if (!valid_click_count(count))
        return {false, false, {"invalid_argument", "click count exceeds the action bound"}};
    if (count == 1)
        return click_at(x, y, button, limits, cancel);
    return {false, false, {"unsupported_platform", "complete repeated click is unavailable"}};
}

InputOutcome InputProvider::approach_target(PointerPoint target, std::int32_t target_width,
                                            const InputLimits &limits, const CancelToken &cancel) {
    if (cancel.cancelled())
        return {false, true, {"cancelled", "pointer approach cancelled"}};
    if (!timed_pointer_actions_supported())
        return {false, false, {"unsupported_platform", "automatic pointer travel is unavailable"}};
    const auto origin = pointer_position(cancel);
    if (!origin.ok)
        return {false, origin.cancelled, origin.error};
    if (limits.timeout.count() <= 0)
        return {false, false, {"input_timeout", "pointer approach has no remaining time"}};
    if (origin.position.x == target.x && origin.position.y == target.y)
        return {true, false, {}, false, true};
    PointerMotionVariation variation;
    std::chrono::milliseconds duration;
    try {
        variation = make_pointer_motion_variation(target_width);
        duration =
            varied_pointer_duration({origin.position.x, origin.position.y}, target, variation);
    } catch (...) {
        return {false, false, {"pointer_plan_failed", "could not prepare pointer approach"}};
    }
    if (limits.timeout < duration || limits.timeout.count() <= 0)
        return {false, false, {"input_timeout", "pointer approach exceeds remaining action time"}};
    return smooth_move_to(target.x, target.y, duration, 0, limits, cancel, variation);
}

InputOutcome InputProvider::smooth_move_to(std::int32_t, std::int32_t,
                                           std::chrono::milliseconds duration,
                                           std::uint32_t path_points, const InputLimits &,
                                           const CancelToken &,
                                           std::optional<PointerMotionVariation>) {
    if (const auto timing = validate_smooth_move_timing(duration, path_points))
        return {false, false, *timing};
    return {false, false, {"unsupported_platform", "timed pointer travel is unavailable"}};
}
} // namespace mirage::desktop
