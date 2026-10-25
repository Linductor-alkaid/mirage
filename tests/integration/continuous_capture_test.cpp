#include "../support/test.hpp"

#include <mirage/integration/continuous_capture_host.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_session_host.hpp>

#include <kairo/executor.hpp>

#include <atomic>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace desktop = mirage::desktop;
using namespace mirage::integration;
using State = ContinuousCaptureStats::State;

template <class Predicate> bool wait_until(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

desktop::CaptureOutcome pixels(std::uint64_t source_sequence = 0) {
    desktop::ImageFrame frame;
    frame.width = 8;
    frame.height = 8;
    frame.stride = 32;
    frame.pixels.assign(256, std::uint8_t{0x80});
    frame.source_sequence = source_sequence;
    return {true, false, std::move(frame), {}};
}

ContinuousCaptureConfig options(std::chrono::milliseconds period = 10ms) {
    ContinuousCaptureConfig config;
    config.max_frame_bytes = 256;
    config.sample_period = period;
    config.capture_timeout = 1s;
    return config;
}

struct ExecutorOwner {
    kairo::Executor executor;
    ExecutorOwner() {
        kairo::ExecutorConfig config;
        config.min_threads = 2;
        config.max_threads = 4;
        config.queue_capacity = 64;
        config.max_in_flight_tasks = 64;
        MIRAGE_CHECK(executor.initialize(config).ok);
    }
    ~ExecutorOwner() { executor.shutdown(true); }
};

void idle_and_shared_ownership() {
    ExecutorOwner owner;
    std::atomic_int calls{0};
    std::atomic<const std::uint8_t *> allocation{nullptr};
    ContinuousCaptureHost host(
        owner.executor, "shared",
        [&](const auto &limits, const auto &) {
            ++calls;
            auto result = pixels(47);
            if (limits.max_bytes != 256)
                throw std::runtime_error("byte budget not forwarded");
            allocation.store(result.frame.pixels.data());
            return result;
        },
        options(60s));
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(!host.start(error));
    std::this_thread::sleep_for(30ms);
    MIRAGE_CHECK(calls == 0);
    MIRAGE_CHECK(owner.executor.get_timer_status_summary().pending_count == 0);
    MIRAGE_CHECK(host.stats().state == State::Idle);
    MIRAGE_CHECK(host.set_demand(true, error));
    MIRAGE_CHECK(wait_until([&] { return bool(host.latest()); }));
    auto first = host.latest();
    auto second = host.latest();
    MIRAGE_CHECK(first && first == second);
    if (!first)
        return;
    MIRAGE_CHECK(first->image.pixels.data() == allocation.load());
    MIRAGE_CHECK(first->image.source_sequence == 47);
    MIRAGE_CHECK(first->source_id == "shared");
    MIRAGE_CHECK(first->sequence == 1 && first->epoch > 0);
    auto wrapped = to_mirador_frame(first);
    MIRAGE_CHECK(wrapped.ok());
    if (!wrapped.ok())
        return;
    auto view = wrapped.take_value();
    MIRAGE_CHECK(view.image.data == reinterpret_cast<const std::byte *>(allocation.load()));
    MIRAGE_CHECK(view.timestamp == first->received_at);
    MIRAGE_CHECK(view.sequence == first->sequence);
    MIRAGE_CHECK(host.set_demand(false, error));
    MIRAGE_CHECK(!host.latest());
    first.reset();
    second.reset();
    MIRAGE_CHECK(host.stats().live_generations == 1);
    MIRAGE_CHECK(view.image.data[0] == std::byte{0x80});
    view = {};
    MIRAGE_CHECK(host.stats().live_generations == 0);
    MIRAGE_CHECK(host.stats().live_pixel_bytes == 0);
    MIRAGE_CHECK(owner.executor.get_timer_status_summary().pending_count == 0);
    host.stop();
    const auto stopped_epoch = host.stats().epoch;
    host.stop();
    MIRAGE_CHECK(host.stats().epoch == stopped_epoch);
    MIRAGE_CHECK(!host.set_demand(true, error));
}

void retained_generations_backpressure_before_capture() {
    ExecutorOwner owner;
    std::atomic_int calls{0};
    ContinuousCaptureHost host(
        owner.executor, "bounded",
        [&](const auto &, const auto &) { return pixels(static_cast<std::uint64_t>(++calls)); },
        options());
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.set_demand(true, error));
    std::shared_ptr<const SharedCaptureFrame> first;
    MIRAGE_CHECK(wait_until([&] {
        first = host.latest();
        return bool(first);
    }));
    if (!first)
        return;
    std::shared_ptr<const SharedCaptureFrame> second;
    MIRAGE_CHECK(wait_until([&] {
        second = host.latest();
        return second && second->sequence > first->sequence;
    }));
    MIRAGE_CHECK(wait_until([&] { return host.stats().backpressure_count > 0; }));
    const auto count = calls.load();
    auto wrapped = to_mirador_frame(first);
    MIRAGE_CHECK(wrapped.ok());
    if (!wrapped.ok())
        return;
    auto analysis_view = wrapped.take_value();
    first.reset();
    std::this_thread::sleep_for(40ms);
    MIRAGE_CHECK(calls == count);
    MIRAGE_CHECK(host.stats().live_generations == 2);
    MIRAGE_CHECK(host.stats().live_pixel_bytes == 512);
    analysis_view = {};
    MIRAGE_CHECK(wait_until([&] { return calls > count; }));
    MIRAGE_CHECK(host.stats().live_generations <= 2);
    MIRAGE_CHECK(host.stats().live_pixel_bytes <= 512);
    MIRAGE_CHECK(host.set_demand(false, error));
    second.reset();
    MIRAGE_CHECK(wait_until([&] { return host.stats().live_generations == 0; }));
}

void slow_capture_coalesces_and_revoke_discards_late_result() {
    ExecutorOwner owner;
    std::atomic_int calls{0}, concurrent{0}, peak{0};
    std::atomic_bool release{false};
    ContinuousCaptureHost host(
        owner.executor, "late",
        [&](const auto &, const auto &) {
            const auto count = ++calls;
            const auto current = ++concurrent;
            auto prior = peak.load();
            while (prior < current && !peak.compare_exchange_weak(prior, current)) {
            }
            // Controlled race injection: first provider deliberately returns late.
            if (count == 1)
                (void)wait_until([&] { return release.load(); });
            --concurrent;
            return pixels(static_cast<std::uint64_t>(count));
        },
        options());
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.set_demand(true, error));
    MIRAGE_CHECK(wait_until([&] { return calls == 1; }));
    const auto old_epoch = host.stats().epoch;
    std::this_thread::sleep_for(50ms);
    MIRAGE_CHECK(calls == 1);
    MIRAGE_CHECK(host.stats().tick_overwrites > 0);
    MIRAGE_CHECK(host.set_demand(false, error));
    MIRAGE_CHECK(!host.latest());
    MIRAGE_CHECK(host.set_demand(true, error));
    release = true;
    std::shared_ptr<const SharedCaptureFrame> fresh;
    MIRAGE_CHECK(wait_until([&] {
        fresh = host.latest();
        return bool(fresh);
    }));
    MIRAGE_CHECK(fresh && fresh->epoch > old_epoch);
    MIRAGE_CHECK(fresh && fresh->image.source_sequence >= 2);
    MIRAGE_CHECK(host.stats().cancelled_frames == 1);
    MIRAGE_CHECK(peak == 1);
}

void frame_churn_keeps_pixel_storage_bounded() {
    ExecutorOwner owner;
    ContinuousCaptureHost host(
        owner.executor, "churn", [](const auto &, const auto &) { return pixels(); }, options());
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.set_demand(true, error));
    bool within_budget = true;
    MIRAGE_CHECK(wait_until([&] {
        // Repeated external reads must not turn the latest-value transport
        // into a history retaining all prior pixel owners.
        auto frame = host.latest();
        const auto stats = host.stats();
        within_budget =
            within_budget && stats.live_generations <= 2 && stats.live_pixel_bytes <= 512;
        return frame && stats.published_frames >= 64;
    }));
    MIRAGE_CHECK(within_budget);
    MIRAGE_CHECK(host.set_demand(false, error));
    MIRAGE_CHECK(wait_until([&] { return host.stats().live_generations == 0; }));
}

void timeout_pauses_until_explicit_demand() {
    ExecutorOwner owner;
    std::atomic_int calls{0};
    auto config = options();
    config.capture_timeout = 25ms;
    ContinuousCaptureHost host(
        owner.executor, "timeout",
        [&](const auto &, const auto &cancel) {
            if (++calls == 1) {
                (void)wait_until([&] { return cancel.cancelled(); });
                return desktop::CaptureOutcome{false, true, {}, {"cancelled", "deadline"}};
            }
            return pixels();
        },
        config);
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.set_demand(true, error));
    MIRAGE_CHECK(wait_until([&] { return host.stats().state == State::TimedOut; }));
    MIRAGE_CHECK(host.stats().timed_out_frames == 1);
    MIRAGE_CHECK(!host.latest());
    std::this_thread::sleep_for(30ms);
    MIRAGE_CHECK(calls == 1);
    MIRAGE_CHECK(host.set_demand(true, error));
    MIRAGE_CHECK(wait_until([&] { return bool(host.latest()); }));
}

void failure_and_invalid_pixels_pause() {
    ExecutorOwner owner;
    for (int mode = 0; mode < 5; ++mode) {
        ContinuousCaptureHost host(
            owner.executor, "invalid-" + std::to_string(mode),
            [mode](const auto &, const auto &) {
                if (mode == 0)
                    return desktop::CaptureOutcome{
                        false, false, {}, {"permission_denied", "revoked"}};
                auto result = pixels();
                if (mode == 1)
                    result.frame.stride = std::numeric_limits<std::size_t>::max();
                if (mode == 2)
                    result.frame.pixels.reserve(1024);
                if (mode == 3)
                    result.frame.pixels.resize(255);
                if (mode == 4)
                    result.frame.format = static_cast<desktop::ImageFormat>(99);
                return result;
            },
            options());
        std::string error;
        MIRAGE_CHECK(host.start(error));
        MIRAGE_CHECK(host.set_demand(true, error));
        MIRAGE_CHECK(wait_until([&] { return host.stats().state == State::Failed; }));
        MIRAGE_CHECK(!host.latest());
        MIRAGE_CHECK(host.stats().failed_frames == 1);
        MIRAGE_CHECK(host.stats().capture_calls == 1);
        MIRAGE_CHECK(host.stats().live_pixel_bytes == 0);
        MIRAGE_CHECK(host.stats().error.code ==
                     (mode == 0 ? "permission_denied" : "invalid_capture"));
    }
}

void exception_visible_to_executor() {
    ExecutorOwner owner;
    ContinuousCaptureHost host(
        owner.executor, "exception",
        [](const auto &, const auto &) -> desktop::CaptureOutcome {
            throw std::runtime_error("injected failure");
        },
        options());
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.set_demand(true, error));
    MIRAGE_CHECK(wait_until([&] { return host.stats().state == State::Failed; }));
    MIRAGE_CHECK(wait_until([&] {
        const auto status =
            owner.executor.get_blocking_io_worker_status("mirage-capture-exception");
        return !status.last_error_message.empty() && !status.is_running;
    }));
    MIRAGE_CHECK(host.stats().error.code == "capture_exception");
    MIRAGE_CHECK(!host.set_demand(true, error));
}

void stop_and_executor_shutdown_cancel_in_flight() {
    for (bool executor_first : {false, true}) {
        ExecutorOwner owner;
        std::atomic_bool entered{false}, cancelled{false};
        ContinuousCaptureHost host(
            owner.executor, "shutdown",
            [&](const auto &, const auto &cancel) {
                entered = true;
                (void)wait_until([&] { return cancel.cancelled(); });
                cancelled = cancel.cancelled();
                return pixels(); // Even successful late pixels may not publish.
            },
            options());
        std::string error;
        MIRAGE_CHECK(host.start(error));
        MIRAGE_CHECK(host.set_demand(true, error));
        MIRAGE_CHECK(wait_until([&] { return entered.load(); }));
        const auto before = std::chrono::steady_clock::now();
        if (executor_first)
            owner.executor.shutdown(true);
        host.stop();
        MIRAGE_CHECK(std::chrono::steady_clock::now() - before < 1s);
        MIRAGE_CHECK(cancelled.load());
        MIRAGE_CHECK(host.stats().state == State::Stopped);
        MIRAGE_CHECK(host.stats().published_frames == 0);
        MIRAGE_CHECK(host.stats().live_generations == 0);
        MIRAGE_CHECK(!host.latest());
    }
}

void admission_and_shared_view_validation() {
    ExecutorOwner owner;
    ContinuousCaptureHost occupied(owner.executor, "refused",
                                   [](const auto &, const auto &) { return pixels(); });
    std::string error;
    MIRAGE_CHECK(occupied.start(error));
    ContinuousCaptureHost refused(owner.executor, "refused",
                                  [](const auto &, const auto &) { return pixels(); });
    MIRAGE_CHECK(!refused.start(error));
    MIRAGE_CHECK(!error.empty());
    MIRAGE_CHECK(!refused.set_demand(true, error));
    auto config = options();
    config.max_frame_bytes = 0;
    ContinuousCaptureHost invalid(
        owner.executor, "bad-config", [](const auto &, const auto &) { return pixels(); }, config);
    MIRAGE_CHECK(!invalid.start(error));
    MIRAGE_CHECK(!to_mirador_frame(std::shared_ptr<const SharedCaptureFrame>{}).ok());
    auto frame = std::make_shared<SharedCaptureFrame>();
    frame->source_id = "invalid-view";
    frame->image = pixels().frame;
    frame->image.pixels.resize(1);
    MIRAGE_CHECK(!to_mirador_frame(frame).ok());
}

void callback_owned_state_released_on_worker_before_join() {
    ExecutorOwner owner;
    std::atomic_bool released_on_worker{false};
    const auto caller = std::this_thread::get_id();
    auto resource = std::shared_ptr<int>(new int{0}, [&](int *value) {
        released_on_worker = std::this_thread::get_id() != caller;
        delete value;
    });
    std::weak_ptr<int> weak = resource;
    ContinuousCaptureHost host(
        owner.executor, "release",
        [resource = std::move(resource)](const auto &, const auto &) {
            ++*resource; // Worker-owned state, never accessed by the external owner.
            return pixels();
        },
        options());
    std::string error;
    MIRAGE_CHECK(host.start(error));
    MIRAGE_CHECK(host.set_demand(true, error));
    MIRAGE_CHECK(wait_until([&] { return bool(host.latest()); }));
    host.stop();
    MIRAGE_CHECK(weak.expired());
    MIRAGE_CHECK(released_on_worker.load());
}

void shared_frames_feed_existing_change_gate() {
    ExecutorOwner owner;
    ContinuousCaptureHost capture(
        owner.executor, "pipeline", [](const auto &, const auto &) { return pixels(); }, options());
    FakeOcrBackend ocr;
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    VisualSessionHost analysis(owner.executor, "pipeline", config);
    std::string error;
    MIRAGE_CHECK(capture.start(error));
    MIRAGE_CHECK(analysis.start(error));
    MIRAGE_CHECK(capture.set_demand(true, error));
    std::uint64_t previous_sequence = 0;
    for (int iteration = 0; iteration < 2; ++iteration) {
        std::shared_ptr<const SharedCaptureFrame> frame;
        MIRAGE_CHECK(wait_until([&] {
            frame = capture.latest();
            return frame && frame->sequence > previous_sequence;
        }));
        auto wrapped = to_mirador_frame(frame);
        MIRAGE_CHECK(wrapped.ok());
        if (!wrapped.ok())
            break;
        VisualAnalysisRequest request;
        previous_sequence = frame->sequence;
        request.frame = wrapped.take_value();
        request.run_ocr = true;
        request.admission = VisualAnalysisAdmission::when_changed;
        request.analysis_policy_revision = frame->epoch;
        auto future = analysis.submit(std::move(request));
        MIRAGE_CHECK(future.wait_for(5s) == std::future_status::ready);
        const auto result = future.get();
        MIRAGE_CHECK(result.outcome.kind == (iteration == 0
                                                 ? VisualAnalysisOutcome::Kind::kCompleted
                                                 : VisualAnalysisOutcome::Kind::kSkippedUnchanged));
    }
    MIRAGE_CHECK(ocr.calls() == 1);
    analysis.stop();
}
} // namespace

int main() {
    idle_and_shared_ownership();
    retained_generations_backpressure_before_capture();
    slow_capture_coalesces_and_revoke_discards_late_result();
    frame_churn_keeps_pixel_storage_bounded();
    timeout_pauses_until_explicit_demand();
    failure_and_invalid_pixels_pause();
    exception_visible_to_executor();
    stop_and_executor_shutdown_cancel_in_flight();
    admission_and_shared_view_validation();
    callback_owned_state_released_on_worker_before_join();
    shared_frames_feed_existing_change_gate();
    return mirage::testing::finish("continuous_capture_test");
}
