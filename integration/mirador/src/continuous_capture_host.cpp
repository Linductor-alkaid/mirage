#include <mirage/integration/continuous_capture_host.hpp>

#include <kairo/blocking_io.hpp>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>

#include <array>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <utility>

namespace mirage::integration {
namespace {
using Stats = ContinuousCaptureStats;
using Frame = SharedCaptureFrame;
using Clock = std::chrono::steady_clock;

struct Core {
    Core(std::string id, ContinuousCaptureHost::Capture callback, ContinuousCaptureConfig options)
        : source_id(std::move(id)), capture(std::move(callback)), config(options) {}

    const std::string source_id;
    ContinuousCaptureHost::Capture capture;
    const ContinuousCaptureConfig config;
    // Ownership/lifecycle and wakeup handshake only. Cross-context payloads use
    // kairo mailboxes; these are not a private queue or scheduling mechanism.
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool started = false, stopped = false, active = false;
    std::uint64_t epoch = 0, next_sequence = 0;
    std::optional<desktop::CancelToken> in_flight;
    kairo::TimerHandle timer;
    kairo::comm::LatestMailbox<std::uint64_t> ticks{"continuous-capture-tick"};
    kairo::comm::LatestMailbox<std::weak_ptr<const Frame>> frames{"continuous-capture-frame"};
    // Weak output slots keep transport-internal old values from pinning pixels.
    // Only this explicit latest owner and consumers retain frame generations.
    std::shared_ptr<const Frame> current;
    struct Slot {
        std::weak_ptr<const Frame> frame;
        std::size_t bytes = 0;
    };
    std::array<Slot, 2> slots;
    Stats counters;
    kairo::comm::LatestMailbox<Stats> reports{"continuous-capture-report"};

    void report_locked() {
        counters.epoch = epoch;
        reports.publish(counters);
    }

    void invalidate_locked() {
        active = false;
        ++epoch;
        if (timer.valid())
            (void)timer.cancel();
        if (in_flight)
            in_flight->request_cancel();
        current.reset();
        frames.publish(std::weak_ptr<const Frame>{});
    }

    void fail_locked(Stats::State state, desktop::ProviderError error) {
        invalidate_locked();
        counters.state = state;
        // Copy the bounded prefix: resize alone would retain the provider's
        // potentially large string allocation in this long-lived owner.
        counters.error = {error.code.substr(0, 64), error.message.substr(0, 256)};
        report_locked();
    }

    void tick(std::uint64_t requested_epoch) {
        std::lock_guard lock(mutex);
        if (!active || stopped || epoch != requested_epoch)
            return;
        ticks.publish(requested_epoch);
        cv.notify_all();
    }
};

bool valid_frame(const desktop::ImageFrame &image, std::size_t limit) {
    return image.format == desktop::ImageFormat::Bgra8 && image.width > 0 && image.height > 0 &&
           image.width <= 65535 && image.height <= 65535 &&
           image.stride >= static_cast<std::size_t>(image.width) * 4 &&
           image.stride <= limit / static_cast<std::size_t>(image.height) &&
           image.pixels.size() == image.stride * static_cast<std::size_t>(image.height) &&
           image.pixels.capacity() <= limit;
}

class CaptureWorker final : public kairo::IBlockingIoWorker {
  public:
    explicit CaptureWorker(std::shared_ptr<Core> core) : core_(std::move(core)) {}

    void run(kairo::StopToken stop) override {
        try {
            loop(stop);
        } catch (...) {
            // Preserve the executor's failure event/status as the task-health source.
            core_->capture = {}; // Release callback-owned Backend state on its worker.
            std::lock_guard lock(core_->mutex);
            ++core_->counters.failed_frames;
            core_->in_flight.reset();
            core_->stopped = true;
            core_->fail_locked(Stats::State::Failed,
                               {"capture_exception", "capture worker raised an exception"});
            throw;
        }
        core_->capture = {};
        std::lock_guard lock(core_->mutex);
        if (!core_->stopped) {
            core_->stopped = true;
            core_->invalidate_locked();
            core_->counters.state = Stats::State::Stopped;
            core_->report_locked();
        }
    }

    void wakeup() noexcept override {
        std::lock_guard lock(core_->mutex);
        if (core_->in_flight)
            core_->in_flight->request_cancel();
        core_->cv.notify_all();
    }

  private:
    void loop(kairo::StopToken stop) {
        std::uint64_t seen = 0;
        for (;;) {
            std::unique_lock lock(core_->mutex);
            core_->cv.wait(lock,
                           [&] { return stop.stop_requested() || core_->ticks.sequence() > seen; });
            if (stop.stop_requested())
                return;
            std::uint64_t requested_epoch = 0;
            if (!core_->ticks.try_load_newer_than(seen, requested_epoch, seen))
                continue;
            if (!core_->active || core_->stopped || core_->epoch != requested_epoch)
                continue;
            std::size_t slot = 0;
            while (slot < core_->slots.size() && !core_->slots[slot].frame.expired())
                ++slot;
            if (slot == core_->slots.size()) {
                ++core_->counters.backpressure_count;
                core_->counters.state = Stats::State::Backpressured;
                core_->report_locked();
                continue; // Budget admission BEFORE provider work/allocation.
            }
            const auto deadline = Clock::now() + core_->config.capture_timeout;
            desktop::CancelToken cancel{
                [stop, deadline] { return stop.stop_requested() || Clock::now() >= deadline; }};
            core_->in_flight = cancel;
            ++core_->counters.capture_calls;
            core_->counters.state = Stats::State::Sampling;
            core_->report_locked();
            lock.unlock();
            auto result = core_->capture({core_->config.max_frame_bytes, 0}, cancel);
            const auto received_at = Clock::now();
            lock.lock();
            core_->in_flight.reset();
            if (stop.stop_requested() || !core_->active || core_->epoch != requested_epoch) {
                ++core_->counters.cancelled_frames;
                core_->report_locked();
                continue;
            }
            if (result.error.code == "capture_busy") {
                ++core_->counters.backpressure_count;
                core_->counters.state = Stats::State::Backpressured;
                core_->report_locked();
                continue; // Foreground owns the session; try the next admitted tick.
            }
            if (received_at >= deadline) {
                ++core_->counters.timed_out_frames;
                core_->fail_locked(
                    Stats::State::TimedOut,
                    {"capture_timeout", "capture exceeded its cooperative deadline"});
                continue;
            }
            if (!result.ok || result.cancelled || cancel.cancelled()) {
                if (result.cancelled || cancel.cancelled())
                    ++core_->counters.cancelled_frames;
                else
                    ++core_->counters.failed_frames;
                core_->fail_locked(Stats::State::Failed,
                                   result.error.code.empty()
                                       ? desktop::ProviderError{"capture_failed", "capture refused"}
                                       : std::move(result.error));
                continue;
            }
            if (!valid_frame(result.frame, core_->config.max_frame_bytes)) {
                ++core_->counters.failed_frames;
                core_->fail_locked(Stats::State::Failed,
                                   {"invalid_capture", "capture layout or pixel budget invalid"});
                continue;
            }
            auto frame = std::make_shared<const Frame>(Frame{std::move(result.frame),
                                                             core_->source_id, requested_epoch,
                                                             ++core_->next_sequence, received_at});
            core_->slots[slot] = {frame, frame->image.pixels.capacity()};
            core_->current = frame;
            core_->frames.publish(std::weak_ptr<const Frame>{frame});
            ++core_->counters.published_frames;
            core_->report_locked();
        }
    }

    std::shared_ptr<Core> core_;
};
} // namespace

struct ContinuousCaptureHost::Impl {
    Impl(kairo::Executor &owner, std::shared_ptr<Core> state)
        : executor(owner), core(std::move(state)) {}
    kairo::Executor &executor;
    std::shared_ptr<Core> core;
    kairo::WorkerHandle worker;
};

ContinuousCaptureHost::ContinuousCaptureHost(kairo::Executor &executor, std::string source_id,
                                             Capture capture, ContinuousCaptureConfig config)
    : impl_(std::make_unique<Impl>(
          executor, std::make_shared<Core>(std::move(source_id), std::move(capture), config))) {}

ContinuousCaptureHost::~ContinuousCaptureHost() { stop(); }

bool ContinuousCaptureHost::start(std::string &error) {
    auto &core = *impl_->core;
    std::unique_lock lock(core.mutex);
    error.clear();
    if (core.started || core.stopped || core.source_id.empty() || core.source_id.size() > 128 ||
        !core.capture || core.config.max_frame_bytes == 0 ||
        core.config.max_frame_bytes > (256u << 20) ||
        core.config.sample_period < std::chrono::milliseconds{10} ||
        core.config.sample_period > std::chrono::seconds{60} ||
        core.config.capture_timeout <= std::chrono::milliseconds{0} ||
        core.config.capture_timeout > std::chrono::seconds{60}) {
        error = "capture source/configuration invalid or host already started/stopped";
        return false;
    }
    // Do not hold the lifecycle lock across worker start: the worker can run
    // immediately. Lifecycle entrypoints are serialized by the external owner.
    // start_worker only admits; first pixels are separately observable.
    kairo::BlockingWorkerSpec spec;
    spec.name = "mirage-capture-" + core.source_id;
    spec.config.thread_name = spec.name;
    spec.worker = std::make_unique<CaptureWorker>(impl_->core);
    lock.unlock();
    impl_->worker = impl_->executor.start_worker(std::move(spec));
    lock.lock();
    if (!impl_->worker.started()) {
        error = "capture worker admission refused: " + impl_->worker.start_result().message;
        core.counters.state = Stats::State::Failed;
        core.counters.error = {"capture_admission_rejected", error};
        core.report_locked();
        return false;
    }
    core.started = true;
    core.counters.state = Stats::State::Idle;
    core.report_locked();
    return true;
}

bool ContinuousCaptureHost::set_demand(bool active, std::string &error) {
    auto &core = *impl_->core;
    std::lock_guard lock(core.mutex);
    error.clear();
    if (!core.started || core.stopped) {
        error = "capture host is not running";
        return false;
    }
    if (core.active == active)
        return true;
    core.invalidate_locked();
    core.counters.error = {};
    core.counters.state = active ? Stats::State::Sampling : Stats::State::Idle;
    if (active) {
        core.active = true;
        const auto epoch = core.epoch;
        core.timer = impl_->executor.submit_periodic(
            core.config.sample_period.count(), [weak = std::weak_ptr<Core>{impl_->core}, epoch] {
                if (const auto current = weak.lock())
                    current->tick(epoch);
            });
        if (!core.timer.valid()) {
            error = "capture timer admission refused";
            core.fail_locked(Stats::State::Failed, {"capture_admission_rejected", error});
            return false;
        }
        core.ticks.publish(epoch); // First request need not wait a sampling period.
        core.cv.notify_all();
    }
    core.report_locked();
    return true;
}

std::shared_ptr<const SharedCaptureFrame> ContinuousCaptureHost::latest() const {
    auto &core = *impl_->core;
    std::lock_guard lock(core.mutex); // epoch validation + ownership acquisition linearize revoke.
    std::weak_ptr<const Frame> weak;
    if (!core.active || core.stopped || !core.frames.try_load(weak))
        return {};
    auto frame = weak.lock();
    return frame && frame->epoch == core.epoch ? frame : nullptr;
}

ContinuousCaptureStats ContinuousCaptureHost::stats() const {
    auto &core = *impl_->core;
    std::lock_guard lock(core.mutex); // only budget-owner accounting outside the report channel.
    Stats stats;
    (void)core.reports.try_load(stats);
    stats.tick_overwrites = core.ticks.stats().overwritten_count;
    stats.frame_overwrites = core.frames.stats().overwritten_count;
    for (const auto &slot : core.slots) {
        if (!slot.frame.expired()) {
            ++stats.live_generations;
            stats.live_pixel_bytes += slot.bytes;
        }
    }
    if (core.in_flight) {
        ++stats.live_generations;
        stats.live_pixel_bytes += core.config.max_frame_bytes;
    }
    return stats;
}

void ContinuousCaptureHost::stop() {
    auto &core = *impl_->core;
    {
        std::lock_guard lock(core.mutex);
        if (!core.stopped) {
            core.stopped = true;
            core.invalidate_locked();
            core.counters.state = Stats::State::Stopped;
            core.report_locked();
        }
    }
    if (impl_->worker.started())
        impl_->worker.stop();
    else {
        // No worker ever admitted; no platform work ran.
        core.capture = {};
    }
}

} // namespace mirage::integration
