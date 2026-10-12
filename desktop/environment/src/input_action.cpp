#include <mirage/desktop/input_provider.hpp>
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
InputOutcome InputProvider::key_chord(const KeySym &, const InputLimits &, const CancelToken &) {
    return {false, false, {"unsupported_platform", "complete key actions are unavailable"}};
}
InputOutcome InputProvider::click_at(std::int32_t, std::int32_t, const MouseButton &,
                                     const InputLimits &, const CancelToken &) {
    return {false, false, {"unsupported_platform", "complete pointer actions are unavailable"}};
}
} // namespace mirage::desktop
