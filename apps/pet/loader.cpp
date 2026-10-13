#include "loader.hpp"

#include <atomic>
#include <future>
#include <kairo/executor.hpp>
#include <stdexcept>

namespace mirage::pet_ui {
struct ResourceLoader::Impl {
    Impl(kairo::Executor &owner, Decoder decode, std::chrono::milliseconds budget)
        : executor(owner), decoder(std::move(decode)), timeout(budget) {}
    kairo::Executor &executor;
    Decoder decoder;
    std::chrono::milliseconds timeout;
    const runtime::pet::Clip *desired = nullptr;
    std::uint64_t generation = 0, submitted_generation = 0;
    Clock::time_point requested{};
    kairo::TaskSubmission<DecodedClip> task;
    std::shared_ptr<std::atomic_bool> began;
    bool pending = false, reported = false, stopped = false;

    void cancel() {
        if (task.future.valid())
            (void)executor.request_task_cancel(task.handle);
    }
    void submit() {
        const auto *clip = desired;
        submitted_generation = generation;
        pending = false;
        began = std::make_shared<std::atomic_bool>(false);
        task = executor.submit_cancellable(
            [decode = decoder, clip, started = began](kairo::StopToken token) {
                started->store(true);
                const auto cancelled = [token] { return token.stop_requested(); };
                DecodedClip result;
                std::string diagnostic;
                if (cancelled())
                    throw kairo::TaskCancelled(kairo::TaskCancellationReason::Explicit,
                                               "pet asset load cancelled");
                const bool ok = decode(*clip, result, diagnostic, cancelled);
                if (cancelled())
                    throw kairo::TaskCancelled(kairo::TaskCancellationReason::Explicit,
                                               "pet asset load cancelled");
                if (!ok || result.clip != clip || result.rgba_frames.size() != clip->frame_count)
                    throw std::runtime_error(diagnostic.empty() ? "pet decoded clip mismatch"
                                                                : diagnostic);
                return result;
            });
    }
};
ResourceLoader::ResourceLoader(kairo::Executor &executor, Decoder decoder,
                               std::chrono::milliseconds timeout)
    : impl_(std::make_unique<Impl>(executor, std::move(decoder), timeout)) {
    if (timeout <= std::chrono::milliseconds::zero() || !impl_->decoder)
        throw std::invalid_argument("pet loader requires decoder and positive timeout");
}
ResourceLoader::~ResourceLoader() { stop(); }
std::uint64_t ResourceLoader::request(const runtime::pet::Clip &clip, Clock::time_point now) {
    auto &p = *impl_;
    if (p.stopped)
        return 0;
    p.cancel();
    p.desired = &clip;
    ++p.generation;
    p.requested = now;
    p.pending = true;
    p.reported = false;
    return p.generation;
}
bool ResourceLoader::poll(LoadUpdate &out, Clock::time_point now) {
    auto &p = *impl_;
    out = {};
    if (p.stopped)
        return false;
    // The request deadline covers waiting for an obsolete load, queueing, and
    // decoding. It requests cooperative stop; it does not forcibly end a task.
    if (!p.reported && p.desired && now - p.requested >= p.timeout) {
        p.cancel();
        p.pending = false;
        p.reported = true;
        out = {p.generation, LoadState::TimedOut, {}, "pet asset load deadline exceeded"};
        return true;
    }
    if (p.task.future.valid()) {
        if (p.task.future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready)
            return false;
        out.generation = p.submitted_generation;
        try {
            out.decoded = p.task.future.get();
            out.state = LoadState::Ready;
        } catch (const kairo::TaskCancelled &error) {
            out.state = LoadState::Cancelled;
            out.diagnostic = error.what();
        } catch (const kairo::TimedOutException &error) {
            out.state = LoadState::TimedOut;
            out.diagnostic = error.what();
        } catch (const std::exception &error) {
            out.state = p.began->load() ? LoadState::Failed : LoadState::Rejected;
            out.diagnostic = error.what();
        } catch (...) {
            out.state = LoadState::Failed;
            out.diagnostic = "pet asset load unknown exception";
        }
        if (out.generation == p.generation && !p.reported) {
            p.reported = true;
            return true;
        }
        out = {}; // release obsolete frames before admitting the latest request
    }
    if (p.pending) {
        try {
            p.submit();
        } catch (const std::exception &error) {
            p.reported = true;
            out = {p.generation, LoadState::Rejected, {}, error.what()};
            return true;
        }
    }
    return false;
}
bool ResourceLoader::busy() const { return impl_->pending || impl_->task.future.valid(); }
void ResourceLoader::cancel() {
    auto &p = *impl_;
    p.cancel();
    ++p.generation;
    p.desired = nullptr;
    p.pending = false;
    p.reported = true;
}
void ResourceLoader::stop() {
    if (!impl_ || impl_->stopped)
        return;
    auto &p = *impl_;
    p.stopped = true;
    p.pending = false;
    p.cancel();
    if (p.task.future.valid()) {
        try {
            (void)p.task.future.get();
        } catch (...) {
            // Already settled in Executor's failure/cancellation facilities;
            // teardown consumes the result, without publishing stale UI data.
        }
    }
}
} // namespace mirage::pet_ui
