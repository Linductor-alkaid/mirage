#include "../../integration/mirador/src/continuous_recognition_host.hpp"
#include "../support/test.hpp"
#include <atomic>
#include <chrono>
#include <kairo/executor.hpp>
#include <mirage/integration/continuous_capture_host.hpp>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
using Host = mirage::integration::detail::ContinuousRecognitionHost;
using Clock = Host::Clock;
namespace desktop = mirage::desktop;
using Sample = mirage::integration::PerceptionSample;

template <class F> bool wait_until(F predicate) {
    const auto end = Clock::now() + 5s;
    while (!predicate()) {
        if (Clock::now() >= end)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}
struct Owner {
    kairo::Executor executor;
    Owner() {
        kairo::ExecutorConfig config;
        config.min_threads = 2;
        config.max_threads = 4;
        config.queue_capacity = 64;
        config.max_in_flight_tasks = 64;
        MIRAGE_CHECK(executor.initialize(config).ok);
    }
    ~Owner() { executor.shutdown(true); }
};
Host::Input input(std::uint64_t sequence, std::uint64_t generation = 1) {
    Host::Input in;
    in.image.width = in.image.height = 8;
    in.image.stride = 32;
    in.image.source_sequence = sequence;
    in.image.pixels.assign(256, static_cast<std::uint8_t>(sequence));
    in.captured_at = Clock::now();
    in.deadline = in.captured_at + 3s;
    in.generation = generation;
    return in;
}
Sample success(std::uint64_t sequence) {
    Sample out;
    out.ok = out.objects_complete = true;
    out.source_epoch = 1;
    out.frame_sequence = sequence;
    mirage::integration::PerceptionEvent event;
    event.kind = "appeared";
    event.text = "original";
    out.events.push_back(std::move(event));
    return out;
}
std::shared_ptr<const Host::Result> take(Host &host) {
    std::shared_ptr<const Host::Result> result;
    MIRAGE_CHECK(wait_until([&] { return bool(result = host.take()); }));
    return result;
}

void latest_does_not_starve_active_and_completion_applies_backpressure() {
    Owner owner;
    std::atomic<unsigned> calls{0};
    std::atomic<bool> release{false}, cancelled{false};
    std::atomic<Clock::duration::rep> seen_time{0};
    Host host(owner.executor, "latest",
              [&](const auto &in, const auto &source, const auto &cancel) {
                  ++calls;
                  if (in.image.source_sequence == 1)
                      seen_time = in.captured_at.time_since_epoch().count();
                  // Test harness intentionally checks exact source metadata below.
                  if (source != "latest" || !in.focus || in.deadline <= in.captured_at ||
                      in.captured_at == Clock::time_point{})
                      cancelled = true;
                  while (!release && !cancel.cancelled())
                      std::this_thread::sleep_for(1ms);
                  if (cancel.cancelled())
                      cancelled = true;
                  return success(in.image.source_sequence);
              });
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(owner.executor.get_blocking_io_worker_status("recognition-latest").is_running);
    auto first = input(1);
    first.focus = desktop::WindowGeometry{1, 1, 2, 2};
    const auto captured_at = first.captured_at;
    MIRAGE_CHECK(host.submit(std::move(first)).accepted);
    MIRAGE_CHECK(wait_until([&] { return calls == 1; }));
    std::uint64_t previous = 0;
    for (unsigned n = 2; n <= 30; ++n) {
        auto in = input(n);
        in.focus = desktop::WindowGeometry{1, 1, 2, 2};
        const auto admitted = host.submit(std::move(in));
        MIRAGE_CHECK(admitted.accepted);
        MIRAGE_CHECK(admitted.superseded == previous);
        previous = admitted.ticket;
        const auto state = host.stats();
        MIRAGE_CHECK(state.live_generations == 2);
        MIRAGE_CHECK(state.live_pixel_bytes == 512);
    }
    MIRAGE_CHECK(!cancelled);
    release = true;
    MIRAGE_CHECK(wait_until([&] { return host.stats().result_pending; }));
    MIRAGE_CHECK(calls == 1); // Unconsumed completion cannot lose its events.
    auto result = take(host);
    MIRAGE_CHECK(result && result->kind == Host::Result::Kind::completed);
    if (result) {
        MIRAGE_CHECK(result->input->image.source_sequence == 1);
        MIRAGE_CHECK(result->input->image.pixels.front() == 1);
        MIRAGE_CHECK(result->input->captured_at == captured_at);
        MIRAGE_CHECK(seen_time == captured_at.time_since_epoch().count());
        MIRAGE_CHECK(result->sample.frame_sequence == 1);
        MIRAGE_CHECK(result->sample.events.size() == 1);
    }
    MIRAGE_CHECK(wait_until([&] { return host.stats().result_pending; }));
    auto last = take(host);
    MIRAGE_CHECK(last && last->input->image.source_sequence == 30);
    MIRAGE_CHECK(calls == 2);
    // Consumer-held results retain both source generations, despite empty queues.
    auto denied = host.submit(input(31));
    MIRAGE_CHECK(!denied.accepted && denied.error == "consumer_frame_budget");
    result.reset();
    MIRAGE_CHECK(wait_until([&] { return host.stats().live_generations == 1; }));
    auto next = input(32);
    next.focus = desktop::WindowGeometry{1, 1, 2, 2};
    auto accepted = host.submit(std::move(next));
    MIRAGE_CHECK(accepted.accepted);
    auto final = take(host);
    MIRAGE_CHECK(final && final->input->image.source_sequence == 32);
    final.reset();
    last.reset();
    host.stop();
    const auto state = host.stats();
    MIRAGE_CHECK(state.admitted == 31 && state.superseded == 28);
    MIRAGE_CHECK(state.completed == 3 && state.rejected == 1);
    MIRAGE_CHECK(state.live_generations == 0 && !state.accepting);
    MIRAGE_CHECK(state.request_overwrites > 0 && state.publication_failures == 0);
    MIRAGE_CHECK(!cancelled);
    MIRAGE_CHECK(!owner.executor.get_blocking_io_worker_status("recognition-latest").is_running);
}
void generation_barrier_rejects_late_success_and_releases_callback() {
    Owner owner;
    std::atomic<unsigned> calls{0};
    std::atomic<bool> release{false};
    auto payload = std::make_shared<int>(7);
    std::weak_ptr<int> weak = payload;
    Host host(owner.executor, "generation",
              [&, held = payload](const auto &in, const auto &, const auto &) {
                  ++calls;
                  // Models a non-interruptible backend returning success after revocation.
                  while (!release)
                      std::this_thread::sleep_for(1ms);
                  return success(in.image.source_sequence + std::uint64_t(*held - 7));
              });
    payload.reset();
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.submit(input(1)).accepted);
    MIRAGE_CHECK(wait_until([&] { return calls == 1; }));
    MIRAGE_CHECK(host.submit(input(2)).accepted);
    const auto generation = host.invalidate();
    MIRAGE_CHECK(generation == 2);
    auto stale = host.submit(input(3));
    MIRAGE_CHECK(!stale.accepted && stale.error == "stale_generation");
    MIRAGE_CHECK(host.submit(input(4, generation)).accepted);
    release = true;
    auto result = take(host);
    MIRAGE_CHECK(result && result->input->generation == generation);
    MIRAGE_CHECK(result && result->input->image.source_sequence == 4);
    MIRAGE_CHECK(host.stats().cancelled == 2);
    MIRAGE_CHECK(host.stats().discarded_results == 1);
    MIRAGE_CHECK(calls == 2);
    // Ready but unconsumed results also disappear at a generation barrier.
    result.reset();
    MIRAGE_CHECK(host.submit(input(5, generation)).accepted);
    MIRAGE_CHECK(wait_until([&] { return host.stats().result_pending; }));
    (void)host.invalidate();
    MIRAGE_CHECK(!host.take());
    MIRAGE_CHECK(host.stats().discarded_results == 2);
    host.stop();
    MIRAGE_CHECK(weak.expired()); // Executor registration must not retain callback state.
    MIRAGE_CHECK(host.stats().live_generations == 0);
    MIRAGE_CHECK(!host.start(error));
    MIRAGE_CHECK(!host.submit(input(6, host.stats().generation)).accepted);
}
void failures_deadlines_and_cancel_clear_semantic_outputs() {
    Owner owner;
    std::atomic<int> mode{0};
    std::atomic<bool> entered{false};
    Host host(owner.executor, "failures", [&](const auto &in, const auto &, const auto &cancel) {
        entered = true;
        if (mode == 0)
            throw std::runtime_error("backend");
        if (mode == 1) {
            Sample failed;
            failed.error = {"backend_failure", "bad"};
            return failed;
        }
        while (!cancel.cancelled())
            std::this_thread::sleep_for(1ms);
        // A backend might return success on cancellation; discard all its data.
        return success(in.image.source_sequence);
    });
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.submit(input(1)).accepted);
    auto result = take(host);
    MIRAGE_CHECK(result && result->kind == Host::Result::Kind::failed);
    MIRAGE_CHECK(result && result->sample.error.code == "analysis_exception");
    result.reset();
    mode = 1;
    MIRAGE_CHECK(host.submit(input(2)).accepted);
    result = take(host);
    MIRAGE_CHECK(result && result->sample.error.code == "backend_failure");
    result.reset();
    mode = 2;
    auto deadline = input(3);
    deadline.deadline = Clock::now() + 30ms;
    MIRAGE_CHECK(host.submit(std::move(deadline)).accepted);
    result = take(host);
    MIRAGE_CHECK(result && result->kind == Host::Result::Kind::timed_out);
    MIRAGE_CHECK(result && result->sample.events.empty() && !result->sample.ok);
    result.reset();
    entered = false;
    auto cancelled = input(4);
    auto token = cancelled.cancel;
    MIRAGE_CHECK(host.submit(std::move(cancelled)).accepted);
    MIRAGE_CHECK(wait_until([&] { return entered.load(); }));
    token.request_cancel();
    MIRAGE_CHECK(wait_until([&] { return host.stats().cancelled == 1; }));
    // Authorization/caller cancellation after completion is checked again by take.
    MIRAGE_CHECK(!host.take());
    MIRAGE_CHECK(host.stats().discarded_results == 1);
    entered = false;
    MIRAGE_CHECK(host.submit(input(5)).accepted);
    MIRAGE_CHECK(wait_until([&] { return entered.load(); }));
    MIRAGE_CHECK(host.submit(input(6)).accepted);
    host.stop();
    MIRAGE_CHECK(host.stats().cancelled == 3);
    MIRAGE_CHECK(host.stats().failed == 2 && host.stats().timed_out == 1);
    MIRAGE_CHECK(host.stats().live_generations == 0);
}
void duplicate_registration_and_cancel_after_completion() {
    Owner owner;
    const auto observer = [](const auto &in, const auto &, const auto &) {
        return success(in.image.source_sequence);
    };
    Host host(owner.executor, "unique", observer);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    Host duplicate(owner.executor, "unique", observer);
    MIRAGE_CHECK(!duplicate.start(error));
    MIRAGE_CHECK(error == "recognition_worker_admission_rejected");
    auto in = input(1);
    const auto cancel = in.cancel;
    MIRAGE_CHECK(host.submit(std::move(in)).accepted);
    MIRAGE_CHECK(wait_until([&] { return host.stats().result_pending; }));
    auto revoked = cancel;
    revoked.request_cancel();
    MIRAGE_CHECK(!host.take());
    MIRAGE_CHECK(host.stats().completed == 1 && host.stats().discarded_results == 1);
    MIRAGE_CHECK(host.submit(input(2)).accepted);
    auto result = take(host);
    MIRAGE_CHECK(result && result->kind == Host::Result::Kind::completed);
    result.reset();
    host.stop();
}

void queued_deadline_and_result_budget_are_explicit() {
    Owner owner;
    std::atomic<unsigned> calls{0};
    std::atomic<bool> release{false};
    Host host(owner.executor, "deadline", [&](const auto &in, const auto &, const auto &cancel) {
        ++calls;
        if (in.image.source_sequence == 1)
            while (!release && !cancel.cancelled())
                std::this_thread::sleep_for(1ms);
        auto out = success(in.image.source_sequence);
        if (in.image.source_sequence == 3)
            out.events.reserve(10000); // Capacity, even with size==1, is charged.
        if (in.image.source_sequence == 4)
            out.events.front().text.assign(1024, 'x');
        if (in.image.source_sequence == 5) {
            while (!cancel.cancelled())
                std::this_thread::sleep_for(1ms);
            out.cancelled = true; // Deadline cancellation must remain timed_out.
        }
        return out;
    });
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.submit(input(1)).accepted);
    MIRAGE_CHECK(wait_until([&] { return calls == 1; }));
    auto queued = input(2);
    queued.deadline = Clock::now() + 20ms;
    const auto expiry = queued.deadline;
    MIRAGE_CHECK(host.submit(std::move(queued)).accepted);
    MIRAGE_CHECK(wait_until([&] { return Clock::now() >= expiry; }));
    release = true;
    auto result = take(host);
    result.reset();
    result = take(host);
    MIRAGE_CHECK(result && result->kind == Host::Result::Kind::timed_out);
    MIRAGE_CHECK(calls == 1); // Expired queued pixels never enter the observer.
    result.reset();
    for (unsigned n : {3u, 4u}) {
        MIRAGE_CHECK(host.submit(input(n)).accepted);
        result = take(host);
        MIRAGE_CHECK(result && result->kind == Host::Result::Kind::failed);
        MIRAGE_CHECK(result && result->sample.error.code == "recognition_result_budget");
        MIRAGE_CHECK(result && !result->sample.ok && result->sample.events.empty());
        result.reset();
    }
    auto end = input(5);
    end.deadline = Clock::now() + 20ms;
    MIRAGE_CHECK(host.submit(std::move(end)).accepted);
    result = take(host);
    MIRAGE_CHECK(result && result->kind == Host::Result::Kind::timed_out);
    result.reset();
    host.stop();
    MIRAGE_CHECK(host.stats().timed_out == 2 && host.stats().failed == 2);
}
void executor_shutdown_interrupts_active_and_queued_work() {
    Owner owner;
    std::atomic<bool> entered{false};
    Host host(owner.executor, "shutdown", [&](const auto &in, const auto &, const auto &cancel) {
        entered = true;
        while (!cancel.cancelled())
            std::this_thread::sleep_for(1ms);
        return success(in.image.source_sequence);
    });
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.submit(input(1)).accepted);
    MIRAGE_CHECK(wait_until([&] { return entered.load(); }));
    MIRAGE_CHECK(host.submit(input(2)).accepted);
    owner.executor.shutdown(true);
    MIRAGE_CHECK(!host.take());
    MIRAGE_CHECK(host.stats().cancelled == 2);
    MIRAGE_CHECK(host.stats().live_generations == 0);
    MIRAGE_CHECK(!host.stats().accepting);
    host.stop();
}
void producers_consumer_and_generation_changes_are_race_safe() {
    Owner owner;
    std::atomic<unsigned> called{0}, submitted{0};
    std::atomic<bool> valid{true};
    Host host(owner.executor, "race", [&](const auto &in, const auto &, const auto &cancel) {
        ++called;
        if (!cancel.cancelled())
            std::this_thread::sleep_for(200us);
        return success(in.image.source_sequence);
    });
    std::string error;
    MIRAGE_CHECK(host.start(error));
    struct Join {
        std::thread thread;
        ~Join() {
            if (thread.joinable())
                thread.join();
        }
    };
    const auto produce = [&] {
        for (unsigned i = 0; i < 150; ++i) {
            auto in = input(++submitted, host.stats().generation);
            auto admission = host.submit(std::move(in));
            if (!admission.accepted && admission.error != "consumer_frame_budget" &&
                admission.error != "stale_generation")
                valid = false;
            std::this_thread::sleep_for(100us);
        }
    };
    Join first{std::thread(produce)}, second{std::thread(produce)};
    for (unsigned i = 0; i < 60; ++i) {
        auto result = host.take();
        if (result && result->sample.ok &&
            result->sample.frame_sequence != result->input->image.source_sequence)
            valid = false;
        if (i % 5 == 0)
            (void)host.invalidate();
        if (host.stats().live_generations > 2)
            valid = false;
        std::this_thread::sleep_for(1ms);
    }
    first.thread.join();
    second.thread.join();
    host.stop();
    const auto state = host.stats();
    MIRAGE_CHECK(valid && called > 0);
    MIRAGE_CHECK(state.admitted + state.rejected == 300);
    MIRAGE_CHECK(state.admitted == state.completed + state.cancelled + state.timed_out +
                                       state.failed + state.superseded);
    MIRAGE_CHECK(state.live_generations == 0 && state.publication_failures == 0);
}

void validation_and_executor_rejection() {
    Owner owner;
    Host host(owner.executor, "validate", [](const auto &in, const auto &, const auto &) {
        return success(in.image.source_sequence);
    });
    std::string error;
    MIRAGE_CHECK(!host.submit(input(1)).accepted);
    MIRAGE_CHECK(host.start(error));
    auto bad = input(2);
    bad.image.stride = 1;
    MIRAGE_CHECK(host.submit(std::move(bad)).error == "invalid_frame_or_budget");
    bad = input(3);
    bad.image.pixels.reserve((10u << 20) + 1);
    MIRAGE_CHECK(host.submit(std::move(bad)).error == "invalid_frame_or_budget");
    bad = input(4);
    bad.focus = desktop::WindowGeometry{7, 7, 2, 2};
    MIRAGE_CHECK(host.submit(std::move(bad)).error == "invalid_focus");
    bad = input(5);
    bad.deadline = bad.captured_at + 4s;
    MIRAGE_CHECK(host.submit(std::move(bad)).error == "invalid_or_expired_deadline");
    bad = input(6);
    bad.captured_at = Clock::now() + 1s;
    MIRAGE_CHECK(host.submit(std::move(bad)).error == "invalid_or_expired_deadline");
    bad = input(7);
    bad.deadline = Clock::now() - 1ms;
    MIRAGE_CHECK(!host.submit(std::move(bad)).accepted);
    bad = input(70);
    bad.work.ocr = bad.work.detector = false;
    MIRAGE_CHECK(host.submit(std::move(bad)).error == "empty_recognition_work");
    bad = input(71);
    bad.work.roi = desktop::WindowGeometry{0, 0, 8, 8};
    MIRAGE_CHECK(host.submit(std::move(bad)).error == "invalid_recognition_roi");
    bad = input(72);
    bad.work.roi = desktop::WindowGeometry{0, 0, 2, 2};
    MIRAGE_CHECK(host.submit(std::move(bad)).accepted);
    auto mismatched = take(host); // Legacy full-result callback cannot claim regional work.
    MIRAGE_CHECK(mismatched && mismatched->kind == Host::Result::Kind::failed &&
                 mismatched->sample.error.code == "recognition_scope_mismatch");
    mismatched.reset();
    owner.executor.shutdown(true); // Owner shutting down first must wake/cancel.
    MIRAGE_CHECK(!host.stats().accepting);
    MIRAGE_CHECK(!host.submit(input(8)).accepted);
    host.stop();
    Host rejected(owner.executor, "rejected",
                  [](const auto &, const auto &, const auto &) { return Sample{}; });
    MIRAGE_CHECK(!rejected.start(error));
    MIRAGE_CHECK(error == "recognition_worker_admission_rejected");
}
void retained_cancellation_tokens_do_not_pin_source_pixels() {
    Owner owner;
    std::vector<desktop::CancelToken> retained;
    Host host(owner.executor, "retained-tokens",
              [&](const auto &in, const auto &, const auto &cancel) {
                  retained.push_back(cancel); // Like VisualSessionHost's settled request slots.
                  return success(in.image.source_sequence);
              });
    std::string error;
    MIRAGE_CHECK(host.start(error));
    for (unsigned n = 1; n <= 8; ++n) {
        const auto admitted = host.submit(input(n));
        MIRAGE_CHECK(admitted.accepted);
        if (!admitted.accepted)
            break;
        auto result = take(host);
        MIRAGE_CHECK(result && result->input->image.source_sequence == n);
        result.reset();
    }
    host.stop();
    MIRAGE_CHECK(retained.size() == 8);
    MIRAGE_CHECK(host.stats().live_generations == 0);
    for (const auto &cancel : retained)
        MIRAGE_CHECK(cancel.cancelled());
}

void actual_capture_proceeds_while_recognition_is_blocked() {
    Owner owner;
    std::atomic<unsigned> observed{0}, captured{0}, submitted{0};
    std::atomic<bool> release{false};
    Host recognition(owner.executor, "capture",
                     [&](const auto &in, const auto &, const auto &cancel) {
                         ++observed;
                         while (!release && !cancel.cancelled())
                             std::this_thread::sleep_for(1ms);
                         return success(in.image.source_sequence);
                     });
    std::string error;
    MIRAGE_CHECK(recognition.start(error));
    mirage::integration::ContinuousCaptureConfig config;
    config.sample_period = 100ms;
    config.max_frame_bytes = 512;
    mirage::integration::ContinuousCaptureHost capture(
        owner.executor, "capture",
        [&](const auto &, const auto &) {
            const auto sequence = ++captured;
            auto in = input(sequence);
            auto image = in.image;
            if (recognition.submit(std::move(in)).accepted)
                submitted = sequence; // Publish only after admission/stats have committed.
            return desktop::CaptureOutcome{true, false, std::move(image), {}};
        },
        config);
    MIRAGE_CHECK(capture.start(error));
    MIRAGE_CHECK(capture.set_demand(true, error));
    MIRAGE_CHECK(wait_until([&] { return observed == 1 && submitted >= 5; }));
    MIRAGE_CHECK(observed == 1);
    MIRAGE_CHECK(recognition.stats().superseded >= 3);
    MIRAGE_CHECK(recognition.stats().live_generations == 2);
    // Stop producers before recognition; a stopped backend cannot be fed more work.
    capture.stop();
    release = true;
    auto original = take(recognition);
    MIRAGE_CHECK(original && original->input->image.source_sequence == 1);
    auto latest = take(recognition);
    MIRAGE_CHECK(latest && latest->input->image.source_sequence == captured.load());
    MIRAGE_CHECK(observed == 2);
    original.reset();
    latest.reset();
    recognition.stop();
}
} // namespace
int main() {
    latest_does_not_starve_active_and_completion_applies_backpressure();
    generation_barrier_rejects_late_success_and_releases_callback();
    failures_deadlines_and_cancel_clear_semantic_outputs();
    duplicate_registration_and_cancel_after_completion();
    queued_deadline_and_result_budget_are_explicit();
    executor_shutdown_interrupts_active_and_queued_work();
    producers_consumer_and_generation_changes_are_race_safe();
    validation_and_executor_rejection();
    retained_cancellation_tokens_do_not_pin_source_pixels();
    actual_capture_proceeds_while_recognition_is_blocked();
    return mirage::testing::finish("continuous_recognition_test");
}
