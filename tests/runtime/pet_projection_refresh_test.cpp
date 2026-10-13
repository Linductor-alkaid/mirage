#include "../../runtime/service/src/pet_projection_refresh.hpp"
#include "../support/test.hpp"
#include <poll.h>

using namespace std::chrono_literals;
using mirage::runtime::detail::PetProjectionRefresh;

template <typename Predicate> bool until(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < end) {
        if (predicate())
            return true;
        (void)::poll(nullptr, 0, 1);
    }
    return false;
}

int main() {
    {
        kairo::Executor owner;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 4;
        MIRAGE_CHECK(owner.initialize(config).ok);
        kairo::SerialExecutionContext serial;
        kairo::comm::PhaseGate entered, release;
        std::atomic_int calls{0}, failures{0};
        PetProjectionRefresh refresh(
            owner, serial,
            [&] {
                if (++calls == 1) {
                    (void)entered.advance();
                    (void)release.wait_for(1, 2s);
                }
            },
            [&](const std::exception &) { ++failures; });
        MIRAGE_CHECK(refresh.start());
        refresh.request();
        MIRAGE_CHECK(entered.wait_for(1, 2s));
        std::vector<std::future<void>> producers;
        for (int i = 0; i < 4; ++i)
            producers.push_back(owner.submit_auto([&] {
                for (int j = 0; j < 20; ++j)
                    refresh.request();
            }));
        // Every pool worker can request presentation without waiting on its
        // serial dispatch; the existing callback is still intentionally held.
        for (auto &producer : producers) {
            MIRAGE_CHECK(producer.wait_for(1s) == std::future_status::ready);
            producer.get();
        }
        MIRAGE_CHECK(calls == 1);
        (void)release.advance();
        MIRAGE_CHECK(until([&] { return calls == 2; })); // all 80 requests coalesce
        refresh.stop();
        refresh.request();
        MIRAGE_CHECK(calls == 2 && failures == 0);
        owner.shutdown(true);
    }
    {
        kairo::Executor owner;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 2;
        MIRAGE_CHECK(owner.initialize(config).ok);
        kairo::SerialExecutionContext serial;
        std::atomic_int failures{0};
        PetProjectionRefresh refresh(
            owner, serial, [] { throw std::runtime_error("projection"); },
            [&](const std::exception &) { ++failures; });
        MIRAGE_CHECK(refresh.start());
        refresh.request();
        MIRAGE_CHECK(until([&] { return failures == 1; }));
        refresh.stop();
        owner.shutdown(true);
    }
    for (const auto mode : {"reject", "cancel", "timeout"}) {
        kairo::Executor owner;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 1;
        if (std::string_view(mode) == "reject")
            config.max_in_flight_tasks = 1;
        if (std::string_view(mode) == "timeout")
            config.task_timeout_ms = 20;
        MIRAGE_CHECK(owner.initialize(config).ok);
        kairo::SerialExecutionContext serial;
        kairo::comm::PhaseGate entered, release;
        auto blocker = owner.submit_auto([&] {
            (void)entered.advance();
            (void)release.wait_for(1, 2s);
        });
        MIRAGE_CHECK(entered.wait_for(1, 2s));
        std::atomic_int calls{0}, failures{0};
        PetProjectionRefresh refresh(
            owner, serial, [&] { ++calls; }, [&](const std::exception &) { ++failures; });
        refresh.request();
        if (std::string_view(mode) == "timeout") {
            // Queued soft timeout is checked when the worker can dequeue it;
            // it never forcibly interrupts the currently running blocker.
            (void)::poll(nullptr, 0, 50);
            (void)release.advance();
            blocker.get();
        }
        if (std::string_view(mode) != "cancel")
            MIRAGE_CHECK(until([&] {
                refresh.poll();
                return failures == 1;
            }));
        refresh.stop(); // queued cancellation must settle without freeing the blocker first
        MIRAGE_CHECK(calls == 0);
        (void)release.advance();
        if (blocker.valid())
            blocker.get();
        owner.shutdown(true);
    }
    {
        kairo::Executor owner;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 2;
        MIRAGE_CHECK(owner.initialize(config).ok);
        kairo::SerialExecutionContext serial;
        kairo::comm::PhaseGate entered;
        PetProjectionRefresh refresh(
            owner, serial,
            [&] {
                (void)entered.advance();
                (void)::poll(nullptr, 0, 50); // finite snapshot work already executing
            },
            [](const std::exception &) {});
        refresh.request();
        MIRAGE_CHECK(entered.wait_for(1, 2s));
        refresh.stop();
        const auto cancellation = owner.get_cancellation_status();
        MIRAGE_CHECK(cancellation.running_request_count == 1 &&
                     cancellation.completed_after_request_count == 1);
        owner.shutdown(true);
    }
    return mirage::testing::finish("pet_projection_refresh_test");
}
