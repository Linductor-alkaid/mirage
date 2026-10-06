#include "tray_presenter.hpp"
#include <iostream>

namespace mirage::runtime::detail {
desktop::TrayState TrayPresenter::state() {
    ipc::EventPayload event;
    for (unsigned i = 0; i != 128 && events_.try_receive(event); ++i) {
        if (const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event)) {
            if (task->progress == "Completed" || task->progress == "Failed" ||
                task->progress == "Cancelled") {
                if (task_id_ == task->task_id)
                    task_id_.clear();
            } else {
                task_id_ = task->task_id;
                goal_ = task->goal;
                progress_ = task->progress;
            }
        }
    }
    if (active_work_.try_load_newer_than(active_seq_, active_count_, active_seq_) &&
        active_count_ == 0)
        task_id_.clear();
    desktop::TrayState result;
    result.can_open_shell = live_.load();
    result.can_pause = !task_id_.empty() && progress_ == "Active";
    result.can_resume = !task_id_.empty() && progress_ == "Paused";
    result.status =
        task_id_.empty()
            ? (active_count_ ? "Mirage：" + std::to_string(active_count_) + " 项任务运行中"
                             : "Mirage：Agent 服务运行中")
            : "Mirage：" + goal_ + " — " + progress_;
    desktop::clamp_tray_status(result.status);
    return result;
}
void TrayPresenter::run(executor::StopToken stop) {
    desktop::TrayCarrierContext context;
    context.icon_path = icon_path_;
    context.on_ready = [this] {
        live_.store(true);
        (void)readiness_.advance_to(1);
    };
    context.load_state = [this] { return state(); };
    // SDK callback only validates and posts. Business handling runs on the
    // Runtime-owned ActionWorker and its serialized finite Executor task.
    context.on_action = [this](desktop::TrayAction kind) {
        if (!actions.try_send({kind, task_id_}))
            std::cerr << "mirage-tray: action rejected (queue full or closing)\n";
    };
    const auto report = carrier_->run(context, [&] { return stop.stop_requested(); });
    live_.store(false);
    (void)readiness_.close();
    if (!stop.stop_requested()) {
        std::cerr << "mirage-tray: carrier ended: " << report.diagnostic << '\n';
        on_exit_();
    }
}
} // namespace mirage::runtime::detail
