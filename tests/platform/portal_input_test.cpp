#include "../../platform/linux/src/portal_input.hpp"
#include "../support/test.hpp"
#include <chrono>
#include <kairo/executor.hpp>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/desktop/input_recovery.hpp>
#include <mirage/desktop/input_session.hpp>
#include <poll.h>
#include <stdexcept>
#include <vector>
int main() {
    using namespace mirage;
    using namespace platform::linux_backend;
    using Kind = desktop::SharedInputAction::Kind;
    desktop::InputRecovery recovery;
    const desktop::InputOutcome lag{false, false, {"input_lag", "fixture"}, true, true};
    for (unsigned episode = 0; episode < 3; ++episode) {
        recovery.failed(lag, 10 + episode);
        MIRAGE_CHECK(recovery.blocked());
        recovery.observe(10 + episode);
        MIRAGE_CHECK(recovery.blocked());
        recovery.observe(11 + episode);
        MIRAGE_CHECK(recovery.blocked() == (episode == 2));
    }
    MIRAGE_CHECK(std::string(recovery.reason()) == "input_recovery_budget_exhausted");
    desktop::InputRecovery reset;
    for (unsigned episode = 0; episode < 8; ++episode) {
        reset.failed(lag, episode + 1);
        reset.observe(episode + 2);
        MIRAGE_CHECK(!reset.blocked());
        reset.completed(); // successful full input resets only this failure episode
    }
    for (const auto *code : {"input_release_failed", "portal_input_failed"}) {
        desktop::InputRecovery unavailable;
        unavailable.failed({false, false, {code, "fixture"}, true, false}, 1);
        unavailable.observe(100);
        MIRAGE_CHECK(unavailable.blocked() && !unavailable.awaiting_frame());
    }
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
        PortalInputContext context{3, 1600, 1000, 3200, 2000, false, 0, 0, false, 0, 0, {}, {}};
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
                            (void)::poll(nullptr, 0, 1100);
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
            MIRAGE_CHECK(result.ok && result.input_released && events.size() == 35);
            MIRAGE_CHECK(events[1].value == code && events[1].state == 1 &&
                         events.back().state == 0);
            MIRAGE_CHECK(events[33].x == 600 && events[33].y == 350);
            for (std::size_t n = 2; n < 34; ++n)
                MIRAGE_CHECK(events[n].kind == PortalInputEvent::Kind::Motion);
        }
        for (int mode = 0; mode < 7; ++mode) {
            events.clear();
            desktop::CancelToken token;
            desktop::InputLimits budget;
            if (mode == 3)
                budget.timeout = std::chrono::milliseconds(1000);
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
                        (void)::poll(nullptr, 0, 1100);
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
        desktop::InputLimits wheel_budget;
        wheel_budget.timeout = std::chrono::milliseconds{3000};
        MIRAGE_CHECK(dispatch_portal_input(context, action, wheel_budget, {}).ok &&
                     events.back().value == -1 && events.size() == 17);
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
        for (const auto *key : {"-", "=", "1", "3", "9"}) {
            events.clear();
            action.kind = Kind::Key;
            action.value = key;
            result = dispatch_portal_input(context, action, {}, {});
            MIRAGE_CHECK(result.ok && result.input_released && events.size() == 2);
            if (events.size() == 2)
                MIRAGE_CHECK(events[0].value == key[0] && events[0].state == 1 &&
                             events[1].value == key[0] && events[1].state == 0);
        }
        events.clear();
        action.kind = Kind::Text;
        action.value = "a中😀";
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && result.error.code == "text_transport_unavailable" &&
                     !result.side_effect_may_have_occurred && events.empty());
        action.value = "https://example.invalid/";
        desktop::InputLimits text_budget;
        text_budget.timeout = std::chrono::milliseconds{15000};
        result = dispatch_portal_input(context, action, text_budget, {});
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
                        (void)::poll(nullptr, 0, 1100);
                }
                return !(mode == 4 && event.state == 0);
            };
            desktop::InputLimits limits;
            if (mode == 3)
                limits.timeout = std::chrono::milliseconds(1000);
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
        MIRAGE_CHECK(result.ok && events.size() == 4 && events[1].value == 1 &&
                     events[2].value == 1 && events[3].value == -1);
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
        // Internal approach completes its local trajectory without pressing.
        action.kind = Kind::Move;
        action.target_width = 40;
        action.glide_ms = 0;
        events.clear();
        const auto began = std::chrono::steady_clock::now();
        result = dispatch_portal_input(context, action, {}, {});
        const auto automatic = desktop::automatic_pointer_duration({100, 100}, {400, 250}, 20);
        MIRAGE_CHECK(result.ok && result.input_released && events.size() > 8);
        MIRAGE_CHECK(std::chrono::steady_clock::now() - began >= automatic * 88 / 100);
        for (const auto &event : events)
            MIRAGE_CHECK(event.kind == PortalInputEvent::Kind::Motion);
        MIRAGE_CHECK(events.back().x == 400 && events.back().y == 250);
        MIRAGE_CHECK(std::any_of(events.begin(), events.end(), [](const auto &event) {
            return std::abs(300 * (event.y - 100) - 150 * (event.x - 100)) > 600;
        }));
        // A narrow edge target must remain inside the authorized stream even
        // when a seeded bend or terminal overshoot would leave its pixel bounds.
        action.x = 0;
        action.y = 1998;
        action.target_width = 1;
        context.pointer_origin_x = 0;
        context.pointer_origin_y = 100;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && events.back().x == 0 && events.back().y == 999);
        for (const auto &event : events)
            MIRAGE_CHECK(event.kind == PortalInputEvent::Kind::Motion && event.x >= 0 &&
                         event.x < 1600 && event.y >= 0 && event.y < 1000);
        action.x = 800;
        action.y = 500;
        context.pointer_origin_x = 100;
        context.pointer_origin_y = 100;
        context.has_pointer_origin = false;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && !result.side_effect_may_have_occurred && events.empty());
        context.has_pointer_origin = true;
        action.kind = Kind::Click;
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
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 400 && context.pointer_y == 250);
        // No known same-origin start refuses the glide before any effect.
        context.has_pointer_origin = false;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && result.error.code == "pointer_origin_unknown" &&
                     !result.side_effect_may_have_occurred && events.empty());
        // The refusal travels into the model's rejection receipt: it names
        // the bounded recovery instead of leaving a bare code to guess at.
        MIRAGE_CHECK(result.error.message.find("direct click") == std::string::npos);
        MIRAGE_CHECK(result.error.message.find("cursor") != std::string::npos);
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
        // A slow Portal acknowledgement crosses the unchanged 250 ms guard.
        // No press is sent after travel falls behind; diagnostics retain the cause.
        events.clear();
        context.notify = [&](const auto &event) {
            events.push_back(event);
            if (events.size() == 1)
                (void)::poll(nullptr, 0, 350);
            return true;
        };
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(!result.ok && result.error.code == "input_lag");
        MIRAGE_CHECK(result.side_effect_may_have_occurred && result.input_released);
        MIRAGE_CHECK(events.size() == 1 && events.front().kind == PortalInputEvent::Kind::Motion);
        MIRAGE_CHECK(result.error.message.find("phase=travel") != std::string::npos);
        MIRAGE_CHECK(result.error.message.find("notify_max_ms=") != std::string::npos);
        MIRAGE_CHECK(result.error.message.size() < 224);
        context.notify = [&](const auto &event) {
            events.push_back(event);
            return true;
        };

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
            MIRAGE_CHECK(events[n].x == unit_schedule[n].x && events[n].y == unit_schedule[n].y);
        MIRAGE_CHECK(events[unit_motions - 1].x == 800 && events[unit_motions - 1].y == 500);
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 800 && context.pointer_y == 500);
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
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 400 && context.pointer_y == 250);
        action.kind = Kind::Drag;
        events.clear();
        result = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(result.ok && events.size() == 35);
        MIRAGE_CHECK(context.pointer_known && context.pointer_x == 600 && context.pointer_y == 350);
        // 2026-10-20: the pointer origin now lives on the PortalScreenBackend
        // (PortalPointerState, process lifetime) instead of a CastSession, so
        // cast recreation and turn boundaries stop forgetting it. The backend
        // persists exactly the dispatch out-params (pointer_known/x/y) and
        // reloads them into the next session's PortalInputContext as
        // has_pointer_origin/pointer_origin_*; this block exercises that
        // cross-context contract through the same dispatch entry the real
        // PortalObservation::input path uses. (Driving two real portal
        // sessions needs a live cast with a PipeWire stream; no scripted
        // fixture exists for that, so the shared_input() plumbing itself is
        // covered only by the fixture-driven portal_screen suite.)
        {
            struct PersistedOrigin {
                bool known = false;
                double x = 0, y = 0;
            } origin;
            const auto load = [&](PortalInputContext &fresh) {
                fresh.devices = 3;
                fresh.logical_width = 1600;
                fresh.logical_height = 1000;
                fresh.frame_width = 3200;
                fresh.frame_height = 2000;
                fresh.has_pointer_origin = origin.known;
                fresh.pointer_origin_x = origin.x;
                fresh.pointer_origin_y = origin.y;
                fresh.pointer_known = false;
                fresh.pointer_x = fresh.pointer_y = 0;
                fresh.stopped = [] { return false; };
            };
            const auto store = [&](const PortalInputContext &used) {
                origin.known = used.pointer_known;
                origin.x = used.pointer_x;
                origin.y = used.pointer_y;
            };
            const auto swallow = [&](const PortalInputEvent &event) {
                events.push_back(event);
                return true;
            };
            action.kind = Kind::Click;
            action.value = "left";
            action.image_width = 3200;
            action.image_height = 2000;
            action.click_count = 1;
            // Session A on a brand-new backend state: the first glide is
            // refused before any effect and the persisted origin stays
            // unknown.
            PortalInputContext session_a;
            load(session_a);
            session_a.notify = swallow;
            action.x = 800;
            action.y = 500;
            action.glide_ms = 120;
            events.clear();
            auto out = dispatch_portal_input(session_a, action, {}, {});
            MIRAGE_CHECK(!out.ok && out.error.code == "pointer_origin_unknown" && events.empty());
            MIRAGE_CHECK(out.error.message.find("direct click") == std::string::npos);
            store(session_a);
            MIRAGE_CHECK(!origin.known);
            // A direct click dispatches and writes the origin into the state.
            action.glide_ms = 0;
            out = dispatch_portal_input(session_a, action, {}, {});
            MIRAGE_CHECK(out.ok && out.input_released && events.size() == 3);
            store(session_a);
            MIRAGE_CHECK(origin.known && origin.x == 400 && origin.y == 250);
            // Session A dies; session B rebuilds from the same backend state
            // and the glide now works, travelling from the persisted origin.
            PortalInputContext session_b;
            load(session_b);
            session_b.notify = swallow;
            action.x = 1600;
            action.y = 1000;
            action.glide_ms = 120;
            events.clear();
            const auto rebuilt_schedule = desktop::build_smooth_move_schedule(
                {400, 250}, {800, 500}, std::chrono::milliseconds{120}, 0);
            out = dispatch_portal_input(session_b, action, {}, {});
            MIRAGE_CHECK(out.ok && out.input_released && !rebuilt_schedule.empty());
            std::size_t rebuilt_motions = 0;
            while (rebuilt_motions < events.size() &&
                   events[rebuilt_motions].kind == PortalInputEvent::Kind::Motion)
                ++rebuilt_motions;
            MIRAGE_CHECK(rebuilt_motions == rebuilt_schedule.size() &&
                         events.size() == rebuilt_motions + 2);
            for (std::size_t n = 0; n < rebuilt_motions; ++n)
                MIRAGE_CHECK(events[n].x == rebuilt_schedule[n].x &&
                             events[n].y == rebuilt_schedule[n].y);
            MIRAGE_CHECK(events[rebuilt_motions].kind == PortalInputEvent::Kind::Button &&
                         events[rebuilt_motions].value == 272 &&
                         events[rebuilt_motions].state == 1 &&
                         events[rebuilt_motions + 1].state == 0);
            store(session_b);
            MIRAGE_CHECK(origin.known && origin.x == 800 && origin.y == 500);
            // A third session chains from the latest position without any
            // direct click in between.
            PortalInputContext session_c;
            load(session_c);
            session_c.notify = swallow;
            action.x = 800;
            action.y = 500;
            events.clear();
            const auto chained_schedule = desktop::build_smooth_move_schedule(
                {800, 500}, {400, 250}, std::chrono::milliseconds{120}, 0);
            out = dispatch_portal_input(session_c, action, {}, {});
            MIRAGE_CHECK(out.ok && !chained_schedule.empty());
            std::size_t chained_motions = 0;
            while (chained_motions < events.size() &&
                   events[chained_motions].kind == PortalInputEvent::Kind::Motion)
                ++chained_motions;
            MIRAGE_CHECK(chained_motions == chained_schedule.size());
            for (std::size_t n = 0; n < chained_motions; ++n)
                MIRAGE_CHECK(events[n].x == chained_schedule[n].x &&
                             events[n].y == chained_schedule[n].y);
            MIRAGE_CHECK(events[chained_motions - 1].x == 400 &&
                         events[chained_motions - 1].y == 250);
            store(session_c);
            MIRAGE_CHECK(origin.known && origin.x == 400 && origin.y == 250);
        }
    });
    work.get();
    auto timed = executor.submit_auto([] {
        PortalInputContext context{3, 1600, 1000, 3200, 2000, false, 0, 0, false, 0, 0, {}, {}};
        context.stopped = [] { return false; };
        std::vector<PortalInputEvent> events;
        std::vector<std::chrono::steady_clock::time_point> times;
        context.notify = [&](const auto &event) {
            events.push_back(event);
            times.push_back(std::chrono::steady_clock::now());
            return true;
        };
        const auto elapsed = [&](std::size_t a, std::size_t b) {
            return std::chrono::duration_cast<std::chrono::milliseconds>(times[b] - times[a])
                .count();
        };
        desktop::InputLimits budget;
        budget.timeout = std::chrono::milliseconds{3000};
        desktop::SharedInputAction action{Kind::Key, 800, 500, 3200, 2000, "ctrl+shift+t"};
        auto out = dispatch_portal_input(context, action, budget, {});
        MIRAGE_CHECK(out.ok && events.size() == 6);
        if (events.size() == 6) {
            MIRAGE_CHECK(elapsed(0, 1) >= 11 && elapsed(1, 2) >= 11);
            MIRAGE_CHECK(elapsed(2, 3) >= 44);
            MIRAGE_CHECK(elapsed(3, 4) >= 7 && elapsed(4, 5) >= 7);
        }
        events.clear();
        times.clear();
        action.value = "shift";
        out = dispatch_portal_input(context, action, budget, {});
        MIRAGE_CHECK(out.ok && out.input_released && events.size() == 2);
        if (events.size() == 2) {
            MIRAGE_CHECK(events[0].value == 0xffe1 && events[0].state == 1);
            MIRAGE_CHECK(events[1].value == 0xffe1 && events[1].state == 0);
            MIRAGE_CHECK(elapsed(0, 1) >= 44);
        }
        events.clear();
        times.clear();
        action.kind = Kind::Text;
        action.value = "ab";
        out = dispatch_portal_input(context, action, budget, {});
        MIRAGE_CHECK(out.ok && events.size() == 4);
        if (events.size() == 4)
            MIRAGE_CHECK(elapsed(0, 1) >= 44 && elapsed(1, 2) >= 34 && elapsed(2, 3) >= 44);
        events.clear();
        times.clear();
        action.kind = Kind::Scroll;
        action.horizontal_steps = -2;
        action.vertical_steps = 3;
        out = dispatch_portal_input(context, action, budget, {});
        MIRAGE_CHECK(out.ok && events.size() == 6);
        int horizontal = 0, vertical = 0;
        for (std::size_t i = 1; i < events.size(); ++i) {
            MIRAGE_CHECK(events[i].kind == PortalInputEvent::Kind::Axis &&
                         std::abs(events[i].value) == 1);
            (events[i].state ? horizontal : vertical) += events[i].value;
            if (i > 1)
                MIRAGE_CHECK(elapsed(i - 1, i) >= 37);
        }
        MIRAGE_CHECK(horizontal == -2 && vertical == 3);
        events.clear();
        times.clear();
        action.kind = Kind::Drag;
        action.value = "right";
        action.destination_x = 1200;
        action.destination_y = 900;
        out = dispatch_portal_input(context, action, budget, {});
        MIRAGE_CHECK(out.ok && out.input_released && events.size() == 35);
        if (events.size() == 35) {
            MIRAGE_CHECK(events[1].kind == PortalInputEvent::Kind::Button && events[1].state == 1);
            MIRAGE_CHECK(elapsed(1, 2) >= 44 && elapsed(2, 33) >= 180 && elapsed(33, 34) >= 34);
            MIRAGE_CHECK(events[33].x == 600 && events[33].y == 450 &&
                         events[34].kind == PortalInputEvent::Kind::Button && !events[34].state);
        }
        // Full-plan refusal is effect-free; it is different from a timeout
        // injected after down, which the existing failure matrix exercises.
        for (const auto kind : {Kind::Key, Kind::Text, Kind::Scroll, Kind::Drag}) {
            events.clear();
            times.clear();
            action.kind = kind;
            action.value = kind == Kind::Key ? "ctrl+a" : kind == Kind::Text ? "ab" : "left";
            desktop::InputLimits tiny;
            tiny.timeout = std::chrono::milliseconds{1};
            out = dispatch_portal_input(context, action, tiny, {});
            MIRAGE_CHECK(!out.ok && !out.side_effect_may_have_occurred && out.input_released &&
                         events.empty());
        }
        action.kind = Kind::Text;
        action.value = std::string(4096, 'a');
        budget.timeout = std::chrono::milliseconds{15000};
        out = dispatch_portal_input(context, action, budget, {});
        MIRAGE_CHECK(!out.ok && out.error.code == "input_timeout" && events.empty());
        action.value = "abc";
        int planning_probes = 0;
        desktop::CancelToken planning_cancel([&] { return ++planning_probes >= 2; });
        out = dispatch_portal_input(context, action, budget, planning_cancel);
        MIRAGE_CHECK(out.cancelled && !out.side_effect_may_have_occurred && events.empty());
        action.kind = Kind::Move;
        context.has_pointer_origin = true;
        context.pointer_origin_x = 400;
        context.pointer_origin_y = 250;
        out = dispatch_portal_input(context, action, {}, {});
        MIRAGE_CHECK(out.ok && !out.side_effect_may_have_occurred && events.empty());
        // A physical-state-like probe ceases being true after release. The
        // receipt must preserve the cancellation that caused that cleanup.
        bool held = false;
        context.notify = [&](const auto &event) {
            events.push_back(event);
            if (event.kind == PortalInputEvent::Kind::Key)
                held = event.state != 0;
            return true;
        };
        desktop::CancelToken cancel([&] { return held; });
        action.kind = Kind::Text;
        action.value = "ab";
        out = dispatch_portal_input(context, action, budget, cancel);
        MIRAGE_CHECK(!out.ok && out.cancelled && out.input_released && !held && events.size() == 2);
        events.clear();
        action.kind = Kind::Key;
        action.value = "shift";
        out = dispatch_portal_input(context, action, budget, cancel);
        MIRAGE_CHECK(!out.ok && out.error.code == "invalid_argument" &&
                     !out.side_effect_may_have_occurred && events.empty());
        budget.timeout = std::chrono::milliseconds{3000};
        out = dispatch_portal_input(context, action, budget, cancel);
        MIRAGE_CHECK(!out.ok && out.cancelled && out.input_released && !held && events.size() == 2);
        if (events.size() == 2)
            MIRAGE_CHECK(events[0].value == 0xffe1 && events[1].value == 0xffe1);
    });
    timed.get();
    auto long_work = executor.submit_auto([] {
        for (const auto kind : {Kind::Click, Kind::Key}) {
            std::vector<PortalInputEvent> events;
            PortalInputContext context;
            context.devices = 3;
            context.stopped = [] { return false; };
            context.logical_width = context.frame_width = 200;
            context.logical_height = context.frame_height = 100;
            context.notify = [&](const auto &event) {
                events.push_back(event);
                return true;
            };
            desktop::SharedInputAction action;
            action.kind = kind;
            action.value = kind == Kind::Click ? "left" : "shift";
            action.image_width = 200;
            action.image_height = 100;
            action.x = 40;
            action.y = 40;
            action.long_press = true;
            desktop::InputLimits limits;
            limits.timeout = std::chrono::milliseconds{3000};
            auto began = std::chrono::steady_clock::now();
            auto result = dispatch_portal_input(context, action, limits, {});
            MIRAGE_CHECK(result.ok && result.input_released &&
                         result.side_effect_may_have_occurred);
            MIRAGE_CHECK(std::chrono::steady_clock::now() - began >=
                         std::chrono::milliseconds{1079});
            MIRAGE_CHECK(events.size() == (kind == Kind::Click ? 3u : 2u) &&
                         events.back().state == 0);
            events.clear();
            limits.timeout = std::chrono::milliseconds{100};
            result = dispatch_portal_input(context, action, limits, {});
            MIRAGE_CHECK(!result.ok && !result.side_effect_may_have_occurred && events.empty());
            desktop::CancelToken cancel;
            limits.timeout = std::chrono::milliseconds{3000};
            context.notify = [&](const auto &event) {
                events.push_back(event);
                if (event.state == 1)
                    cancel.request_cancel();
                return true;
            };
            result = dispatch_portal_input(context, action, limits, cancel);
            MIRAGE_CHECK(!result.ok && result.cancelled && result.input_released &&
                         events.back().state == 0);
            events.clear();
            action.click_count = 2;
            if (kind == Kind::Click) {
                result = dispatch_portal_input(context, action, limits, {});
                MIRAGE_CHECK(!result.ok && !result.side_effect_may_have_occurred && events.empty());
            }
        }
    });
    long_work.get();
    executor.shutdown(true);
    return testing::finish("portal_input");
}
