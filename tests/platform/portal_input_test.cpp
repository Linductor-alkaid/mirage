#include "../../platform/linux/src/portal_input.hpp"
#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/desktop/input_session.hpp>
#include <chrono>
#include <poll.h>
#include <stdexcept>
#include <vector>
int main() {
    using namespace mirage;
    using namespace platform::linux_backend;
    using Kind = desktop::SharedInputAction::Kind;
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    MIRAGE_CHECK(executor.initialize(config));
    auto work = executor.submit_auto([] {
        desktop::ActionCoordinator coordinator;
        const auto authority = coordinator.authority_epoch();
        desktop::CancelToken reentrant([&] { return coordinator.authority_epoch() != authority; });
        {
            auto lease = coordinator.try_begin(reentrant, coordinator.epoch());
            MIRAGE_CHECK(bool(lease));
        }
        coordinator.takeover();
        MIRAGE_CHECK(!coordinator.try_begin(reentrant));
        std::vector<PortalInputEvent> events;
        PortalInputContext context{3,           1600, 1000, 3200, 2000,
                                  false,       0,    0,    false, 0, 0, {}, {}};
        context.stopped = [] { return false; };
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };
        desktop::SharedInputAction action{Kind::Click, 800, 500, 3200, 2000, "left"};
        auto result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && result.input_released && events.size() == 3);
        MIRAGE_CHECK(events[0].x == 400 && events[0].y == 250);
        MIRAGE_CHECK(events[1].value == 272 && events[1].state == 1 && events[2].state == 0);
        for (const auto clicks : {2u, 3u, 5u, desktop::max_click_count}) {
            events.clear();
            action.click_count = clicks;
            result = dispatch_portal_input(context, action, {}, {});
            MIRAGE_CHECK(result.ok && result.input_released && events.size() == 2 * clicks + 1);
            for (std::size_t n = 1; n < events.size(); ++n)
                MIRAGE_CHECK(events[n].value == 272 && events[n].state == (n % 2));
        }
        for (const auto invalid : {0u, desktop::max_click_count + 1, UINT32_MAX}) {
            events.clear();
            action.click_count = invalid;
            result = dispatch_portal_input(context, action, {}, {});
            MIRAGE_CHECK(!result.ok && !result.side_effect_may_have_occurred && events.empty());
        }
        action.click_count = desktop::max_click_count;
        for (const auto interrupted_click : {3u, desktop::max_click_count}) {
            for (int mode = 0; mode < 7; ++mode) {
                events.clear();
                desktop::CancelToken cancel;
                desktop::InputLimits limits;
                if (mode == 3)
                    limits.timeout = std::chrono::milliseconds{1};
                context.notify = [&](const auto &event) {
                    events.push_back(event);
                    if (events.size() == 2 * interrupted_click - 1 && mode == 5)
                        cancel.request_cancel(); // stop between complete clicks
                    if (events.size() == 2 * interrupted_click) {
                        if (mode == 0)
                            return false;
                        if (mode == 1)
                            cancel.request_cancel();
                        if (mode == 2)
                            throw std::runtime_error("sequence press");
                        if (mode == 3)
                            (void)::poll(nullptr, 0, 10);
                        if (mode == 6)
                            context.stopped = [] { return true; };
                    }
                    return !(mode == 4 && events.size() == 2 * interrupted_click + 1);
                };
                result = dispatch_portal_input(context, action, limits, cancel);
                MIRAGE_CHECK(!result.ok && result.side_effect_may_have_occurred);
                MIRAGE_CHECK(events.size() == (mode == 5 ? 2 * interrupted_click - 1
                                                         : 2 * interrupted_click + 1) &&
                             events.back().state == 0);
                MIRAGE_CHECK(result.input_released == (mode != 4));
                context.stopped = [] { return false; };
            }
        }
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };
        action.click_count = 1;
        events.clear();
        for (const auto *button : {"left", "middle", "right"}) {
            action.kind = Kind::Click;
            action.value = button;
            events.clear();
            result = dispatch_portal_input(context, action, {}, {});
            const int code = action.value == "left" ? 272 : action.value == "middle" ? 274 : 273;
            MIRAGE_CHECK(result.ok && events[1].value == code && events.back().value == code);
            action.kind = Kind::Drag;
            action.destination_x = 1200;
            action.destination_y = 700;
            events.clear();
            result = dispatch_portal_input(context, action, {}, {});
            MIRAGE_CHECK(result.ok && result.input_released && events.size() == 19);
            MIRAGE_CHECK(events[1].value == code && events[1].state == 1 &&
                         events.back().state == 0);
            MIRAGE_CHECK(events[17].x == 600 && events[17].y == 350);
            for (std::size_t n = 2; n < 18; ++n)
                MIRAGE_CHECK(events[n].kind == PortalInputEvent::Kind::Motion);
        }
        for (int mode = 0; mode < 7; ++mode) {
            events.clear();
            desktop::CancelToken token;
            desktop::InputLimits budget;
            if (mode == 3)
                budget.timeout = std::chrono::milliseconds(20);
            context.notify = [&](const auto &event) {
                events.push_back(event);
                if (events.size() == 6) {
                    if (mode == 0)
                        return false;
                    if (mode == 1)
                        token.request_cancel();
                    if (mode == 2)
                        throw std::runtime_error("drag motion");
                    if (mode == 3)
                        (void)::poll(nullptr, 0, 30);
                    if (mode == 6)
                        context.stopped = [] { return true; };
                }
                return !(mode == 4 && event.kind == PortalInputEvent::Kind::Button && !event.state);
            };
            if (mode == 5)
                token.request_cancel();
            result = dispatch_portal_input(context, action, budget, token);
            MIRAGE_CHECK(!result.ok && result.input_released == (mode != 4));
            if (mode == 5)
                MIRAGE_CHECK(events.empty() && !result.side_effect_may_have_occurred);
            else
                MIRAGE_CHECK(events.back().kind == PortalInputEvent::Kind::Button &&
                             events.back().state == 0);
            context.stopped = [] { return false; };
        }
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };
        for (const auto destination : {-1., 3200., 1e30}) {
            events.clear();
            action.destination_x = destination;
            MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        }
        action.kind = Kind::Scroll;
        action.vertical_steps = 16;
        action.horizontal_steps = 1;
        events.clear();
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        action.vertical_steps = 0;
        action.horizontal_steps = -16;
        MIRAGE_CHECK(dispatch_portal_input(context, action, {}, {}).ok &&
                     events.back().value == -16);
        action.vertical_steps = action.horizontal_steps = 0;
        events.clear();
        action.kind = Kind::Key;
        action.value = "ctrl+shift+t";
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && events.size() == 6);
        MIRAGE_CHECK(events[0].value == 0xffe3 && events[1].value == 0xffe1);
        MIRAGE_CHECK(events[3].value == 't' && events[4].value == 0xffe1 &&
                     events[5].value == 0xffe3);
        events.clear();
        action.kind = Kind::Text;
        action.value = "a中😀";
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && result.error.code == "text_transport_unavailable" &&
                     !result.side_effect_may_have_occurred && events.empty());
        action.value = "https://example.invalid/";
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && events.size() == action.value.size() * 2);
        for (const auto &invalid : {"ctrl+unknown", "f0", "f13"}) {
            events.clear();
            action.kind = Kind::Key;
            action.value = invalid;
            MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        }
        events.clear();
        action.kind = Kind::Text;
        action.value = "hello\n";
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        action.value = std::string("\xc0\xaf", 2);
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        action.kind = Kind::Click;
        action.value = "left";
        action.x = 3200;
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        action.x = 800;
        action.image_width = 1600;
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        action.image_width = 3200;
        context.devices = 1;
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        context.devices = 3;
        desktop::CancelToken stop;
        stop.request_cancel();
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, stop).ok && events.empty());
        for (const auto mode : {0, 1, 2, 3, 4}) {
            events.clear();
            action.kind = Kind::Key;
            action.value = "ctrl+a";
            desktop::CancelToken cancel;
            context.notify = [&](const auto &event) {
                events.push_back(event);
                if (event.state == 1 && events.size() == 2) {
                    if (mode == 0)
                        return false; // failed press still releases
                    if (mode == 1)
                        cancel.request_cancel();
                    if (mode == 2)
                        throw std::runtime_error("press");
                    if (mode == 3)
                        (void)::poll(nullptr, 0, 10);
                }
                return !(mode == 4 && event.state == 0);
            };
            desktop::InputLimits limits;
            if (mode == 3)
                limits.timeout = std::chrono::milliseconds(1);
            result = dispatch_portal_input(context, action, limits, cancel);
            MIRAGE_CHECK(!result.ok && result.side_effect_may_have_occurred && events.size() == 4);
            MIRAGE_CHECK(events[2].value == 'a' && events[2].state == 0 &&
                         events[3].value == 0xffe3);
            MIRAGE_CHECK(result.input_released == (mode != 4));
        }
        events.clear();
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };
        action.kind = Kind::Scroll;
        action.vertical_steps = 2;
        action.horizontal_steps = -1;
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && events.size() == 3 && events[1].value == 2 &&
                     events[2].value == -1);
        events.clear();
        action.vertical_steps = 21;
        MIRAGE_CHECK(!dispatch_portal_input(context, action, {}, {}).ok && events.empty());
        // DEC-110 (M7-26): glide click on the shared Portal path. The fixture
        // keeps the 1600x1000 logical over 3200x2000 frame surface, so the
        // logical-coordinate contract of the glide travel is exercised at a
        // 2x scale factor: the pointer samples are dispatched in logical
        // coordinates exactly as the shared schedule generator produces them.
        context.has_pointer_origin = true;
        context.pointer_origin_x = 100.0;
        context.pointer_origin_y = 100.0;
        context.pointer_known = false;
        context.pointer_x = context.pointer_y = 0;
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };
        action.kind = Kind::Click;
        action.value = "left";
        action.x = 800;
        action.y = 500;
        action.image_width = 3200;
        action.image_height = 2000;
        action.click_count = 1;
        action.glide_ms = 100;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && result.input_released);
        const auto travel_schedule = desktop::build_smooth_move_schedule(
            {100, 100}, {400, 250}, std::chrono::milliseconds{100}, 0);
        MIRAGE_CHECK(travel_schedule.size() == 25); // 32 default points, 4 ms floor caps at 25
        std::size_t motions = 0;
        while (motions < events.size() && events[motions].kind == PortalInputEvent::Kind::Motion)
            ++motions;
        MIRAGE_CHECK(motions == travel_schedule.size());
        MIRAGE_CHECK(events.size() == motions + 2);
        MIRAGE_CHECK(events[motions].kind == PortalInputEvent::Kind::Button &&
                     events[motions].value == 272 && events[motions].state == 1);
        MIRAGE_CHECK(events[motions + 1].value == 272 && events[motions + 1].state == 0);
        for (std::size_t n = 0; n < motions; ++n)
            MIRAGE_CHECK(events[n].x == travel_schedule[n].x &&
                         events[n].y == travel_schedule[n].y);
        MIRAGE_CHECK(events[motions - 1].x == 400 && events[motions - 1].y == 250);
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 400 &&
                     context.pointer_y == 250);
        // No known same-origin start refuses the glide before any effect.
        context.has_pointer_origin = false;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && result.error.code == "pointer_origin_unknown" &&
                     !result.side_effect_may_have_occurred && events.empty());
        context.has_pointer_origin = true;
        // The frozen timing bounds refuse before any effect; the boundary
        // value 50 is accepted.
        for (const auto invalid_glide : {49u, 2001u}) {
            action.glide_ms = invalid_glide;
            events.clear();
            result = dispatch_portal_input(context, action, {}, {});
            MIRAGE_CHECK(!result.ok && result.error.code == "invalid_argument" &&
                         !result.side_effect_may_have_occurred && events.empty());
        }
        action.glide_ms = 50;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && result.input_released);
        // Glide is a Click-only feature: Drag and Scroll refuse it.
        action.kind = Kind::Drag;
        action.destination_x = 1200;
        action.destination_y = 700;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && result.error.code == "invalid_argument" &&
                     !result.side_effect_may_have_occurred && events.empty());
        action.kind = Kind::Scroll;
        action.vertical_steps = 2;
        action.horizontal_steps = 0;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && result.error.code == "invalid_argument" &&
                     !result.side_effect_may_have_occurred && events.empty());
        // A cancellation between glide samples stops the travel before the
        // press, reports cancelled with the partial motion effects, and the
        // pointer out-param stays on the last accepted sample.
        action.kind = Kind::Click;
        action.glide_ms = 200;
        context.pointer_origin_x = 0.0;
        context.pointer_origin_y = 0.0;
        desktop::CancelToken glide_cancel;
        events.clear();
        context.notify = [&](const auto &event) {
            events.push_back(event);
            if (events.size() == 3)
                glide_cancel.request_cancel();
            return true;
        };
        result = dispatch_portal_input(context, action, {}, glide_cancel);
        MIRAGE_CHECK(!result.ok && result.cancelled && result.error.code == "cancelled" &&
                     result.side_effect_may_have_occurred && result.input_released);
        MIRAGE_CHECK(events.size() == 3);
        for (const auto &event : events)
            MIRAGE_CHECK(event.kind == PortalInputEvent::Kind::Motion);
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == events.back().x &&
                     context.pointer_y == events.back().y);
        // Control: at scale 1 (logical == frame) the same dispatch sequence
        // lands on the target, isolating the coordinate-space failure to the
        // rescaling of already-logical schedule samples.
        context.logical_width = 1600;
        context.logical_height = 1000;
        context.frame_width = 1600;
        context.frame_height = 1000;
        action.image_width = 1600;
        action.image_height = 1000;
        context.pointer_origin_x = 100.0;
        context.pointer_origin_y = 100.0;
        action.glide_ms = 100;
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && result.input_released);
        const auto unit_schedule = desktop::build_smooth_move_schedule(
            {100, 100}, {800, 500}, std::chrono::milliseconds{100}, 0);
        MIRAGE_CHECK(!unit_schedule.empty());
        std::size_t unit_motions = 0;
        while (unit_motions < events.size() &&
               events[unit_motions].kind == PortalInputEvent::Kind::Motion)
            ++unit_motions;
        MIRAGE_CHECK(unit_motions == unit_schedule.size());
        for (std::size_t n = 0; n < unit_motions; ++n)
            MIRAGE_CHECK(events[n].x == unit_schedule[n].x &&
                         events[n].y == unit_schedule[n].y);
        MIRAGE_CHECK(events[unit_motions - 1].x == 800 && events[unit_motions - 1].y == 500);
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 800 &&
                     context.pointer_y == 500);
        context.frame_width = 3200;
        context.frame_height = 2000;
        action.image_width = 3200;
        action.image_height = 2000;
        context.pointer_origin_x = context.pointer_origin_y = 0.0;
        // Non-glide paths keep the single direct motion and also maintain the
        // pointer out-param (click and drag endpoints in logical coordinates).
        action.glide_ms = 0;
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && events.size() == 3);
        MIRAGE_CHECK(events[0].x == 400 && events[0].y == 250);
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 400 &&
                     context.pointer_y == 250);
        action.kind = Kind::Drag;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && events.size() == 19);
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 600 &&
                     context.pointer_y == 350);
    });
    work.get();
    executor.shutdown(true);
    return testing::finish("portal_input");
}
