#include "continuous_recognition_host.hpp"
#include <array>
#include <condition_variable>
#include <kairo/blocking_io.hpp>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <mutex>

namespace mirage::integration::detail {
namespace {
using Host = ContinuousRecognitionHost;
using Clock = Host::Clock;
using Kind = Host::Result::Kind;
constexpr std::size_t max_pixels = 10u << 20;
struct Job {
    std::uint64_t ticket = 0;
    std::shared_ptr<const Host::Input> input;
    desktop::CancelToken cancel;
};
struct Core {
    Core(std::string id, Host::Observer callback)
        : source(std::move(id)), observer(std::move(callback)) {}
    const std::string source;
    Host::Observer observer; // worker only, released on exit
    // Admission/ownership and blocking-worker wake handshake only. Payloads
    // cross contexts through kairo mailboxes; weak slots cannot pin old pixels.
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool started = false, stopped = false;
    Host::Stats counters;
    std::uint64_t next = 0;
    std::shared_ptr<Job> queued, active;
    std::shared_ptr<const Host::Result> completion;
    std::array<std::weak_ptr<const Host::Input>, 2> slots;
    kairo::comm::LatestMailbox<std::weak_ptr<Job>> requests{"recognition-latest"};
    kairo::comm::LatestMailbox<std::weak_ptr<const Host::Result>> results{"recognition-result"};

    void revoke_locked() {
        ++counters.generation;
        if (queued) {
            ++counters.cancelled;
            queued.reset();
        }
        if (active)
            active->cancel.request_cancel();
        if (completion) {
            ++counters.discarded_results;
            completion.reset();
        }
        // Empty weak values do not retain pixels in historical version slots.
        if (!requests.try_publish(std::weak_ptr<Job>{}))
            ++counters.publication_failures;
        if (!results.try_publish(std::weak_ptr<const Host::Result>{}))
            ++counters.publication_failures;
        cv.notify_all();
    }
};

std::string invalid(const Host::Input &in) {
    const auto &im = in.image;
    if (im.format != desktop::ImageFormat::Bgra8 || im.width <= 0 || im.height <= 0 ||
        im.width > 1600 || im.height > 1600 || im.stride < std::size_t(im.width) * 4 ||
        im.stride > max_pixels / std::size_t(im.height) ||
        im.pixels.size() != im.stride * std::size_t(im.height) || im.pixels.capacity() > max_pixels)
        return "invalid_frame_or_budget";
    if ((in.source_width != 0 || in.source_height != 0) &&
        (in.source_width < im.width || in.source_height < im.height || in.source_width > 65535 ||
         in.source_height > 65535))
        return "invalid_source_extent";
    if (!in.work.ocr && !in.work.detector)
        return "empty_recognition_work";
    if (in.work.roi) {
        const auto &r = *in.work.roi;
        if (r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
            std::int64_t(r.x) + r.width > im.width || std::int64_t(r.y) + r.height > im.height ||
            std::int64_t(r.width) * r.height > std::int64_t(im.width) * im.height / 4)
            return "invalid_recognition_roi";
    }
    const auto now = Clock::now();
    if (in.captured_at == Clock::time_point{} || in.captured_at > now || in.deadline <= now ||
        in.deadline > in.captured_at + std::chrono::seconds(3))
        return "invalid_or_expired_deadline";
    if (in.cancel.cancelled())
        return "cancelled";
    if (in.focus) {
        const auto &r = *in.focus;
        if (r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
            std::int64_t(r.x) + r.width > im.width || std::int64_t(r.y) + r.height > im.height)
            return "invalid_focus";
    }
    return {};
}

// Bound retained callback metadata as well as pixel generations. Oversized
// vectors/strings are rejected, not truncated into apparent semantic coverage.
bool result_fits(const PerceptionSample &sample) {
    std::size_t remaining = 512u << 10;
    const auto reserve = [&](std::size_t n) {
        if (n > remaining)
            return false;
        remaining -= n;
        return true;
    };
    const auto text = [&](const std::string &s, std::size_t max) {
        return s.size() <= max && reserve(s.capacity() + 1);
    };
    if (sample.events.size() > 256 || sample.objects.size() > 256 ||
        sample.events.capacity() > remaining / sizeof(PerceptionEvent) ||
        !reserve(sample.events.capacity() * sizeof(PerceptionEvent)) ||
        sample.objects.capacity() > remaining / sizeof(PerceptionObject) ||
        !reserve(sample.objects.capacity() * sizeof(PerceptionObject)) ||
        !text(sample.error.code, 64) || !text(sample.error.message, 256) ||
        !text(sample.recognition_reason, 64))
        return false;
    for (const auto &e : sample.events)
        if (!text(e.kind, 64) || !text(e.text, 256) || !text(e.frame_id, 128))
            return false;
    for (const auto &o : sample.objects)
        if (!text(o.object.text, 4096) || !text(o.object.label, 1024))
            return false;
    if (sample.tracking) {
        const auto &t = *sample.tracking;
        if (t.tracks.size() > 8 || t.candidates.size() > 8 ||
            t.tracks.capacity() > remaining / sizeof(PerceptionTrack) ||
            !reserve(t.tracks.capacity() * sizeof(PerceptionTrack)) ||
            t.candidates.capacity() > remaining / sizeof(desktop::WindowGeometry) ||
            !reserve(t.candidates.capacity() * sizeof(desktop::WindowGeometry)) ||
            !text(t.error, 64))
            return false;
        for (const auto &track : t.tracks)
            if (!text(track.state, 32) || !text(track.evidence, 64))
                return false;
    }
    return true;
}

bool matches_work(const Host::Input &input, const PerceptionSample &sample) {
    if (!input.work.roi)
        return !sample.recognition || !sample.recognition->roi;
    if (!sample.recognition || !sample.recognition->roi || sample.objects_complete ||
        !sample.objects.empty() || sample.events.size() > 32)
        return false;
    const auto &actual = *sample.recognition;
    const auto &a = *actual.roi;
    const auto &b = *input.work.roi;
    return (actual.ocr || actual.detector) && (!actual.ocr || input.work.ocr) &&
           (!actual.detector || input.work.detector) && a.x == b.x && a.y == b.y &&
           a.width == b.width && a.height == b.height;
}

class Worker final : public kairo::IBlockingIoWorker {
    std::shared_ptr<Core> core_;
    void loop(kairo::StopToken stop) {
        auto &c = *core_;
        for (;;) {
            std::shared_ptr<Job> job;
            {
                std::unique_lock lock(c.mutex);
                c.cv.wait_for(lock, std::chrono::milliseconds(50), [&] {
                    return stop.stop_requested() || c.stopped || (!c.completion && bool(c.queued));
                });
                if (stop.stop_requested() || c.stopped)
                    return;
                if (c.completion || !c.queued)
                    continue;
                std::weak_ptr<Job> latest;
                if (!c.requests.try_load(latest) || !(job = latest.lock()) || job != c.queued)
                    continue;
                c.active = job;
                c.queued.reset();
            }
            auto output = std::make_shared<Host::Result>();
            output->ticket = job->ticket;
            output->input = job->input;
            const auto &in = *job->input;
            // The analysis host may retain a settled request/token in its
            // mailbox. A token must not keep this dispatcher's pixel slot alive.
            // The worker/active owner keeps Job valid for the synchronous call.
            desktop::CancelToken cancel([weak = std::weak_ptr<Job>(job), stop] {
                auto active = weak.lock();
                return !active || stop.stop_requested() || active->cancel.cancelled() ||
                       active->input->cancel.cancelled() || Clock::now() >= active->input->deadline;
            });
            if (!cancel.cancelled()) {
                try {
                    output->sample = c.observer(in, c.source, cancel);
                    output->kind = output->sample.ok ? Kind::completed : Kind::failed;
                    if (!result_fits(output->sample)) {
                        output->sample = {};
                        output->sample.error = {"recognition_result_budget",
                                                "callback output exceeds retention budget"};
                        output->kind = Kind::failed;
                    } else if (output->sample.ok && !output->sample.skipped &&
                               !matches_work(in, output->sample)) {
                        output->sample = {};
                        output->sample.error = {"recognition_scope_mismatch",
                                                "callback output does not match requested work"};
                        output->kind = Kind::failed;
                    }
                } catch (...) {
                    output->sample.error = {"analysis_exception", "recognition observer threw"};
                }
            }
            {
                std::lock_guard lock(c.mutex);
                const bool retired = c.stopped || in.generation != c.counters.generation;
                if (retired || stop.stop_requested() || job->cancel.cancelled() ||
                    in.cancel.cancelled())
                    output->kind = Kind::cancelled;
                else if (Clock::now() >= in.deadline)
                    output->kind = Kind::timed_out;
                else if (output->sample.cancelled)
                    output->kind = Kind::cancelled;
                if (output->kind != Kind::completed) {
                    auto error =
                        desktop::ProviderError{output->sample.error.code.substr(0, 64),
                                               output->sample.error.message.substr(0, 256)};
                    output->sample = {};
                    output->sample.error = {error.code.substr(0, 64), error.message.substr(0, 256)};
                    output->sample.cancelled = output->kind == Kind::cancelled;
                    if (output->sample.error.code.empty())
                        output->sample.error = {
                            output->kind == Kind::timed_out   ? "deadline_exceeded"
                            : output->kind == Kind::cancelled ? "cancelled"
                                                              : "analysis_failed",
                            "recognition did not complete"};
                }
                switch (output->kind) {
                case Kind::completed:
                    ++c.counters.completed;
                    break;
                case Kind::cancelled:
                    ++c.counters.cancelled;
                    break;
                case Kind::timed_out:
                    ++c.counters.timed_out;
                    break;
                case Kind::failed:
                    ++c.counters.failed;
                    break;
                }
                if (!retired && !stop.stop_requested()) {
                    if (c.results.try_publish(std::weak_ptr<const Host::Result>(output)))
                        c.completion = output;
                    else {
                        ++c.counters.publication_failures;
                        c.counters.accepting = false;
                        c.stopped = true;
                        c.revoke_locked();
                    }
                } else
                    ++c.counters.discarded_results;
                c.active.reset();
            }
        }
    }

  public:
    explicit Worker(std::shared_ptr<Core> core) : core_(std::move(core)) {}
    void run(kairo::StopToken stop) override {
        try {
            loop(stop);
        } catch (...) {
            std::lock_guard lock(core_->mutex);
            ++core_->counters.failed;
            core_->counters.accepting = false;
            core_->stopped = true;
            core_->revoke_locked();
            core_->active.reset();
            core_->observer = {};
            throw; // Executor remains the worker-failure fact source.
        }
        std::lock_guard lock(core_->mutex);
        // A worker can observe Executor's stop token and exit before wakeup
        // is needed. Close admission and settle queued ownership on every exit.
        if (!core_->stopped) {
            core_->stopped = true;
            core_->counters.accepting = false;
            core_->revoke_locked();
        }
        core_->observer = {};
    }
    void wakeup() noexcept override {
        std::lock_guard lock(core_->mutex);
        core_->stopped = true;
        core_->counters.accepting = false;
        core_->revoke_locked();
    }
};
} // namespace
struct ContinuousRecognitionHost::Impl {
    Impl(kairo::Executor &e, std::string id, Host::Observer observer)
        : executor(e), core(std::make_shared<Core>(std::move(id), std::move(observer))) {}
    kairo::Executor &executor;
    std::shared_ptr<Core> core;
    kairo::WorkerHandle worker;
};
ContinuousRecognitionHost::ContinuousRecognitionHost(kairo::Executor &e, std::string source,
                                                     Host::Observer observer)
    : impl_(std::make_unique<Impl>(e, std::move(source), std::move(observer))) {}
ContinuousRecognitionHost::~ContinuousRecognitionHost() { stop(); }
bool ContinuousRecognitionHost::start(std::string &error) {
    error.clear();
    auto &c = *impl_->core;
    {
        std::lock_guard lock(c.mutex);
        if (c.started || c.stopped || c.source.empty() || c.source.size() > 128 || !c.observer) {
            error = "invalid_or_closed_recognition_host";
            return false;
        }
        c.started = true;
    }
    // Executor start/stop can call wakeup while holding its lifecycle lock.
    // Never hold our admission lock across an Executor lifecycle operation.
    kairo::BlockingWorkerSpec spec;
    spec.name = "recognition-" + c.source;
    spec.config.thread_name = "recognition";
    spec.worker = std::make_unique<Worker>(impl_->core);
    impl_->worker = impl_->executor.start_worker(std::move(spec));
    std::lock_guard lock(c.mutex);
    c.counters.accepting = impl_->worker.started() && !c.stopped;
    if (!c.counters.accepting) {
        c.stopped = true;
        c.observer = {};
        error = "recognition_worker_admission_rejected";
    }
    return c.counters.accepting;
}
ContinuousRecognitionHost::Admission ContinuousRecognitionHost::submit(Input input) {
    auto &c = *impl_->core;
    Admission out;
    out.error = invalid(input);
    std::lock_guard lock(c.mutex);
    if (out.error.empty() && (!c.counters.accepting || c.stopped))
        out.error = "recognition_admission_closed";
    if (out.error.empty() && input.generation != c.counters.generation)
        out.error = "stale_generation";
    std::size_t slot = c.slots.size();
    for (std::size_t i = 0; i < c.slots.size(); ++i)
        if (c.slots[i].expired() || (c.queued && c.slots[i].lock() == c.queued->input)) {
            slot = i;
            break;
        }
    if (out.error.empty() && slot == c.slots.size())
        out.error = "consumer_frame_budget";
    if (!out.error.empty()) {
        ++c.counters.rejected;
        return out;
    }
    auto job = std::make_shared<Job>();
    job->ticket = ++c.next;
    job->input = std::make_shared<const Input>(std::move(input));
    if (!c.requests.try_publish(std::weak_ptr<Job>(job))) {
        ++c.counters.rejected;
        out.error = "recognition_mailbox_rejected";
        return out;
    }
    if (c.queued) {
        out.superseded = c.queued->ticket;
        ++c.counters.superseded;
    }
    c.queued = job;
    c.slots[slot] = job->input;
    out.accepted = true;
    out.ticket = job->ticket;
    ++c.counters.admitted;
    c.cv.notify_all();
    return out;
}
std::shared_ptr<const ContinuousRecognitionHost::Result> ContinuousRecognitionHost::take() {
    auto &c = *impl_->core;
    std::lock_guard lock(c.mutex);
    std::weak_ptr<const Result> weak;
    if (!c.results.try_load(weak))
        return {};
    auto result = weak.lock();
    if (!result || result != c.completion)
        return {};
    c.completion.reset();
    c.cv.notify_all();
    if (c.stopped || result->input->generation != c.counters.generation ||
        result->input->cancel.cancelled()) {
        ++c.counters.discarded_results;
        return {};
    }
    return result;
}
std::uint64_t ContinuousRecognitionHost::invalidate() {
    auto &c = *impl_->core;
    std::lock_guard lock(c.mutex);
    c.revoke_locked();
    return c.counters.generation;
}
ContinuousRecognitionHost::Stats ContinuousRecognitionHost::stats() const {
    auto &c = *impl_->core;
    std::lock_guard lock(c.mutex);
    auto out = c.counters;
    out.running = bool(c.active);
    out.result_pending = bool(c.completion);
    out.queued_ticket = c.queued ? c.queued->ticket : 0;
    out.active_ticket = c.active ? c.active->ticket : 0;
    out.request_overwrites = c.requests.stats().overwritten_count;
    for (const auto &slot : c.slots)
        if (auto input = slot.lock()) {
            ++out.live_generations;
            out.live_pixel_bytes += input->image.pixels.capacity();
        }
    return out;
}
void ContinuousRecognitionHost::stop() {
    auto &c = *impl_->core;
    {
        std::lock_guard lock(c.mutex);
        if (!c.stopped) {
            c.stopped = true;
            c.counters.accepting = false;
            c.revoke_locked();
        }
    }
    if (impl_->worker.started())
        impl_->worker.stop();
    std::lock_guard lock(c.mutex);
    c.observer = {};
}
} // namespace mirage::integration::detail
