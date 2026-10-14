#pragma once

#include <chrono>
#include <functional>
#include <kairo/comm.hpp>
#include <kairo/executor.hpp>
#include <mutex>

namespace mirage::runtime::detail {

// Runtime-owned presentation dispatch: one latest request and one tracked
// serial task. Pool workers never wait for another pool worker to publish it.
// stop() runs on the external owner; retain this object through Executor shutdown
// so cancelled timer callbacks cannot touch a destroyed adapter.
class PetProjectionRefresh {
  public:
    PetProjectionRefresh(kairo::Executor &executor, kairo::SerialExecutionContext &serial,
                         std::function<void()> refresh,
                         std::function<void(const std::exception &)> failure)
        : executor_(executor), serial_(serial), refresh_(std::move(refresh)),
          failure_(std::move(failure)) {}

    bool start() {
        timer_ = executor_.submit_periodic(25, [this] { poll(); });
        return timer_.valid();
    }
    void request() {
        post();
        poll();
    }
    void post() {
        if (stopping_.load())
            return;
        requests_.publish(true); // transport callbacks only post; timer owns dispatch
    }
    void poll() {
        std::lock_guard lock(mutex_); // ownership of the single task/future, not a message queue
        if (stopping_.load())
            return;
        if (pending_.future.valid()) {
            if (pending_.future.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready)
                return;
            try {
                pending_.future.get();
            } catch (const std::exception &error) {
                failure_(error); // Executor already records the task's failure/rejection
            }
        }
        bool refresh = false;
        std::uint64_t sequence = 0;
        if (!requests_.try_load_newer_than(consumed_, refresh, sequence))
            return;
        consumed_ = sequence;
        pending_ = executor_.submit_on_with_handle(serial_, [this] {
            if (!stopping_.load())
                refresh_();
        });
    }
    void stop() {
        stopping_.store(true);
        if (timer_.valid())
            (void)timer_.cancel();
        kairo::TaskSubmission<void> pending;
        {
            std::lock_guard lock(mutex_);
            pending = std::move(pending_);
        }
        if (pending.future.valid()) {
            (void)executor_.request_task_cancel(pending.handle);
            try {
                pending.future.get();
            } catch (const kairo::TaskCancelled &) {
                // Cooperative stop is visible through Executor's cancellation lifecycle.
            } catch (const kairo::ExecutorStopping &) {
            } catch (const std::exception &error) {
                failure_(error);
            }
        }
    }

  private:
    kairo::Executor &executor_;
    kairo::SerialExecutionContext &serial_;
    std::function<void()> refresh_;
    std::function<void(const std::exception &)> failure_;
    kairo::comm::LatestMailbox<bool> requests_{"pet-projection-refresh"};
    std::mutex mutex_;
    kairo::TaskSubmission<void> pending_;
    std::uint64_t consumed_ = 0;
    std::atomic_bool stopping_{false};
    kairo::TimerHandle timer_;
};
} // namespace mirage::runtime::detail
