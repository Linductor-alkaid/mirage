#include "../support/test.hpp"
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <loader.hpp>

#include <atomic>
#include <stdexcept>

namespace ui = mirage::pet_ui;
namespace pet = mirage::runtime::pet;
using namespace std::chrono_literals;
using Clock = ui::ResourceLoader::Clock;

namespace {
bool decoded(const pet::Clip &clip, ui::DecodedClip &out, std::string &,
             const std::function<bool()> &) {
    out.clip = &clip;
    out.rgba_frames.assign(clip.frame_count,
                           std::make_shared<const std::vector<std::uint8_t>>(4, 0));
    return true;
}
bool await_update(ui::ResourceLoader &loader, ui::LoadUpdate &out) {
    kairo::comm::PhaseGate pace;
    const auto deadline = Clock::now() + 5s;
    while (Clock::now() < deadline) {
        if (loader.poll(out, Clock::now()))
            return true;
        (void)pace.wait_for(1, 1ms);
    }
    return false;
}
void initialize(kairo::Executor &executor) {
    kairo::ExecutorConfig config;
    config.min_threads = config.max_threads = 1;
    config.queue_capacity = config.max_in_flight_tasks = 4;
    MIRAGE_CHECK(executor.initialize(config));
}
} // namespace

int main() {
    const auto &a = pet::v5_clips()[0];
    const auto &b = pet::v5_clips()[1];
    const auto &c = pet::v5_clips()[2];
    {
        kairo::Executor executor;
        initialize(executor);
        ui::ResourceLoader loader(executor, decoded);
        const auto generation = loader.request(a, Clock::now());
        ui::LoadUpdate out;
        MIRAGE_CHECK(await_update(loader, out));
        MIRAGE_CHECK(out.state == ui::LoadState::Ready && out.generation == generation);
        MIRAGE_CHECK(out.decoded.clip == &a && out.decoded.rgba_frames.size() == a.frame_count);
        MIRAGE_CHECK(!loader.busy() && !loader.poll(out, Clock::now()));
        loader.stop();
        MIRAGE_CHECK(loader.request(b, Clock::now()) == 0);
        executor.shutdown(true);
    }
    {
        // A stays in flight while B and C arrive. Only C may be admitted after
        // A settles, even if the decoder ignores cancellation until it returns.
        kairo::Executor executor;
        initialize(executor);
        kairo::comm::PhaseGate entered, release;
        std::atomic_int calls{0};
        ui::ResourceLoader loader(executor,
                                  [&](const pet::Clip &clip, ui::DecodedClip &out, std::string &why,
                                      const std::function<bool()> &cancelled) {
                                      calls.fetch_add(1);
                                      if (&clip == &a) {
                                          (void)entered.advance_to(1);
                                          (void)release.wait_for(1, 5s);
                                      }
                                      return decoded(clip, out, why, cancelled);
                                  });
        loader.request(a, Clock::now());
        ui::LoadUpdate out;
        MIRAGE_CHECK(!loader.poll(out, Clock::now()));
        MIRAGE_CHECK(entered.wait_for(1, 5s));
        loader.request(b, Clock::now());
        const auto latest = loader.request(c, Clock::now());
        MIRAGE_CHECK(!loader.poll(out, Clock::now()) && calls.load() == 1);
        (void)release.advance_to(1);
        MIRAGE_CHECK(await_update(loader, out));
        MIRAGE_CHECK(out.state == ui::LoadState::Ready && out.generation == latest &&
                     out.decoded.clip == &c && calls.load() == 2);
        MIRAGE_CHECK(executor.get_cancellation_status().running_request_count == 1);
        loader.stop();
        executor.shutdown(true);
    }
    {
        kairo::Executor executor;
        initialize(executor);
        ui::ResourceLoader loader(executor, [](const auto &, auto &, auto &, const auto &) -> bool {
            throw std::runtime_error("injected decode failure");
        });
        loader.request(a, Clock::now());
        ui::LoadUpdate out;
        MIRAGE_CHECK(await_update(loader, out));
        MIRAGE_CHECK(out.state == ui::LoadState::Failed &&
                     out.diagnostic == "injected decode failure" && !out.decoded.clip);
        loader.stop();
        executor.shutdown(true);
        MIRAGE_CHECK(executor.get_failure_status().task_exception_count == 1);
    }
    {
        kairo::Executor executor;
        initialize(executor);
        // Exhaust the public cancellation registry: the loader must report
        // admission rejection, retain/consume its future, and avoid retry loops.
        executor.set_cancellation_registry_capacity(0);
        ui::ResourceLoader loader(executor, decoded);
        loader.request(a, Clock::now());
        ui::LoadUpdate out;
        MIRAGE_CHECK(await_update(loader, out));
        MIRAGE_CHECK(out.state == ui::LoadState::Rejected && !out.diagnostic.empty());
        MIRAGE_CHECK(!loader.busy());
        MIRAGE_CHECK(executor.get_failure_status().submit_rejected_count >= 1);
        loader.stop();
        executor.shutdown(true);
    }
    {
        kairo::Executor executor;
        initialize(executor);
        kairo::comm::PhaseGate entered, pace;
        ui::ResourceLoader loader(executor, [&](const auto &, auto &, auto &, const auto &stop) {
            (void)entered.advance_to(1);
            while (!stop())
                (void)pace.wait_for(1, 1ms);
            return false;
        });
        const auto now = Clock::now();
        const auto generation = loader.request(a, now);
        ui::LoadUpdate out;
        MIRAGE_CHECK(!loader.poll(out, now));
        MIRAGE_CHECK(entered.wait_for(1, 5s));
        MIRAGE_CHECK(loader.poll(out, now + 2s));
        MIRAGE_CHECK(out.state == ui::LoadState::TimedOut && out.generation == generation);
        loader.stop();
        MIRAGE_CHECK(!loader.busy() && !loader.poll(out, Clock::now()));
        executor.shutdown(true);
        MIRAGE_CHECK(executor.get_cancellation_status().running_request_count == 1);
    }
    {
        // Closing the UI requests running cancellation and consumes the future
        // before the process's external owner performs Executor shutdown.
        kairo::Executor executor;
        initialize(executor);
        kairo::comm::PhaseGate entered, pace;
        ui::ResourceLoader loader(executor, [&](const auto &, auto &, auto &, const auto &stop) {
            (void)entered.advance_to(1);
            while (!stop())
                (void)pace.wait_for(1, 1ms);
            return false;
        });
        loader.request(a, Clock::now());
        ui::LoadUpdate out;
        (void)loader.poll(out, Clock::now());
        MIRAGE_CHECK(entered.wait_for(1, 5s));
        loader.stop();
        loader.stop();
        MIRAGE_CHECK(!loader.busy());
        executor.shutdown(true);
        MIRAGE_CHECK(executor.get_cancellation_status().running_request_count == 1);
    }
    {
        // Window suspension abandons the generation. Resuming must not publish
        // the completed hidden load, and only the new request may become Ready.
        kairo::Executor executor;
        initialize(executor);
        ui::ResourceLoader loader(executor, decoded);
        loader.request(a, Clock::now());
        ui::LoadUpdate out;
        (void)loader.poll(out, Clock::now());
        loader.cancel();
        const auto resumed = loader.request(b, Clock::now());
        MIRAGE_CHECK(await_update(loader, out));
        MIRAGE_CHECK(out.state == ui::LoadState::Ready && out.generation == resumed &&
                     out.decoded.clip == &b);
        loader.stop();
        executor.shutdown(true);
    }
    {
        kairo::Executor executor;
        initialize(executor);
        executor.shutdown(true);
        ui::ResourceLoader loader(executor, decoded);
        loader.request(a, Clock::now());
        ui::LoadUpdate out;
        MIRAGE_CHECK(await_update(loader, out));
        MIRAGE_CHECK(out.state == ui::LoadState::Rejected && !out.diagnostic.empty());
    }
    return mirage::testing::finish("pet_resource_loader_test");
}
