#pragma once
#include "event_hub.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <kairo/blocking_io.hpp>
#include <kairo/comm/channel.hpp>
#include <kairo/comm/mailbox.hpp>
#include <kairo/comm/phase_gate.hpp>
#include <memory>
#include <mirage/desktop/tray_carrier.hpp>

namespace mirage::runtime::detail {
class TrayPresenter final : public kairo::IBlockingIoWorker {
  public:
    struct Action {
        desktop::TrayAction kind;
        std::string task_id;
    };
    TrayPresenter(std::shared_ptr<desktop::TrayCarrier> carrier,
                  kairo::comm::TopicSubscription<ipc::EventPayload> events,
                  std::function<void()> on_exit, std::string icon_path)
        : carrier_(std::move(carrier)), events_(std::move(events)), on_exit_(std::move(on_exit)),
          icon_path_(std::move(icon_path)) {}
    void run(kairo::StopToken stop) override;
    void wakeup() noexcept override { carrier_->wakeup(); }
    bool wait_ready() { return static_cast<bool>(readiness_.wait_for(1, std::chrono::seconds{6})); }
    bool ready() const { return live_.load(); }
    void set_active_work(std::size_t count) { (void)active_work_.try_publish(count); }
    struct PetState {
        std::string state;
        bool present = false;
    };
    void set_pet_state(std::string state, bool present) {
        (void)pet_state_.try_publish(PetState{std::move(state), present});
        carrier_->wakeup();
    }
    kairo::comm::MpscChannel<Action> actions{{.capacity = 8, .name = "tray-actions"}};
    /// Producer side (carrier SDK thread). The payload moves only through the
    /// channel; the mutex exists solely to order the consumer's empty-check
    /// against the wakeup so a tray click can never miss its park.
    void push_action(Action action);
    void wake_actions() noexcept {
        std::lock_guard lock(action_mutex_);
        action_ready_.notify_all();
    }
    class ActionWorker final : public kairo::IBlockingIoWorker {
      public:
        ActionWorker(TrayPresenter &presenter, std::function<void(Action)> handle)
            : presenter_(presenter), handle_(std::move(handle)) {}
        void run(kairo::StopToken stop) override {
            // MpscChannel::receive_for is kairo's yield-poll adapter: polling
            // it from a blocking worker pins a core for the service lifetime.
            // Park on the sleep mutex instead; the bounded slice also covers
            // spurious wakeups and stop without a notifier.
            std::unique_lock lock(presenter_.action_mutex_);
            while (!stop.stop_requested() && !presenter_.actions.is_closed()) {
                Action action{};
                if (presenter_.actions.try_receive(action)) {
                    lock.unlock();
                    handle_(std::move(action));
                    lock.lock();
                    continue;
                }
                presenter_.action_ready_.wait_for(lock, std::chrono::seconds{5});
            }
        }
        void wakeup() noexcept override {
            presenter_.actions.close();
            presenter_.wake_actions();
        }

      private:
        TrayPresenter &presenter_;
        std::function<void(Action)> handle_;
    };

  private:
    desktop::TrayState state(); // carrier-thread only, latest snapshot
    std::shared_ptr<desktop::TrayCarrier> carrier_;
    kairo::comm::TopicSubscription<ipc::EventPayload> events_;
    std::function<void()> on_exit_;
    std::string icon_path_;
    kairo::comm::PhaseGate readiness_{"tray-registration"};
    std::atomic_bool live_{false};
    std::string task_id_, goal_, progress_;
    kairo::comm::LatestMailbox<std::size_t> active_work_{"tray-active-work"};
    kairo::comm::LatestMailbox<PetState> pet_state_{"tray-pet-state"};
    std::mutex action_mutex_;
    std::condition_variable action_ready_;
    std::uint64_t pet_seq_ = 0;
    PetState pet_current_;
    std::uint64_t active_seq_ = 0;
    std::size_t active_count_ = 0;
};
} // namespace mirage::runtime::detail
