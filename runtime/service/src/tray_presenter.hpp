#pragma once
#include "event_hub.hpp"
#include <atomic>
#include <executor/blocking_io.hpp>
#include <executor/comm/channel.hpp>
#include <executor/comm/mailbox.hpp>
#include <executor/comm/phase_gate.hpp>
#include <functional>
#include <memory>
#include <mirage/desktop/tray_carrier.hpp>

namespace mirage::runtime::detail {
class TrayPresenter final : public executor::IBlockingIoWorker {
  public:
    struct Action {
        desktop::TrayAction kind;
        std::string task_id;
    };
    TrayPresenter(std::shared_ptr<desktop::TrayCarrier> carrier,
                  executor::comm::TopicSubscription<ipc::EventPayload> events,
                  std::function<void()> on_exit, std::string icon_path)
        : carrier_(std::move(carrier)), events_(std::move(events)), on_exit_(std::move(on_exit)),
          icon_path_(std::move(icon_path)) {}
    void run(executor::StopToken stop) override;
    void wakeup() noexcept override { carrier_->wakeup(); }
    bool wait_ready() { return static_cast<bool>(readiness_.wait_for(1, std::chrono::seconds{6})); }
    bool ready() const { return live_.load(); }
    void set_active_work(std::size_t count) { (void)active_work_.try_publish(count); }
    executor::comm::MpscChannel<Action> actions{{.capacity = 8, .name = "tray-actions"}};
    class ActionWorker final : public executor::IBlockingIoWorker {
      public:
        ActionWorker(TrayPresenter &presenter, std::function<void(Action)> handle)
            : presenter_(presenter), handle_(std::move(handle)) {}
        void run(executor::StopToken stop) override {
            while (!stop.stop_requested() && !presenter_.actions.is_closed()) {
                Action action{};
                if (presenter_.actions.receive_for(action, std::chrono::milliseconds{100}))
                    handle_(std::move(action));
            }
        }
        void wakeup() noexcept override { presenter_.actions.close(); }

      private:
        TrayPresenter &presenter_;
        std::function<void(Action)> handle_;
    };

  private:
    desktop::TrayState state(); // carrier-thread only, latest snapshot
    std::shared_ptr<desktop::TrayCarrier> carrier_;
    executor::comm::TopicSubscription<ipc::EventPayload> events_;
    std::function<void()> on_exit_;
    std::string icon_path_;
    executor::comm::PhaseGate readiness_{"tray-registration"};
    std::atomic_bool live_{false};
    std::string task_id_, goal_, progress_;
    executor::comm::LatestMailbox<std::size_t> active_work_{"tray-active-work"};
    std::uint64_t active_seq_ = 0;
    std::size_t active_count_ = 0;
};
} // namespace mirage::runtime::detail
