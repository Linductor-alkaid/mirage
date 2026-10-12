#include "../../platform/linux/src/portal_input.hpp"
#include "../support/test.hpp"
#include <kairo/executor.hpp>
#include <mirage/desktop/action_coordinator.hpp>
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
        PortalInputContext context{3, 1600, 1000, 3200, 2000, {}, {}};
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
    });
    work.get();
    executor.shutdown(true);
    return testing::finish("portal_input");
}
