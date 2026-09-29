#include "tray_core.hpp"

#include <mirage/desktop/tray_carrier.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <unistd.h>
#endif

namespace mirage::tray {
namespace {

constexpr auto kConnectDeadline = std::chrono::milliseconds{2000};
constexpr auto kCallBudget = std::chrono::milliseconds{5000};
constexpr auto kReconnectDelay = std::chrono::milliseconds{2000};

/// The tray's tracked-task policy (DEC-030 decision 6): the most recently
/// active or paused task wins; terminal tasks release the tracking. The
/// task.list / task.inspect snapshots stay the source of truth — the tray
/// resyncs from the snapshot on (re)connect and trusts events in between.
bool tracks(const std::string &progress) {
    return progress == "Active" || progress == "Paused" || progress == "Cancelling" ||
           progress == "Idle";
}

} // namespace

TrayCore::TrayCore(Dependencies dependencies, executor::Executor &executor)
    : dependencies_(std::move(dependencies)), executor_(executor) {
    state_.can_open_shell = !dependencies_.shell_path.empty();
    state_.status = "Mirage：正在连接 mirage-service …";
}

TrayCore::~TrayCore() = default;

bool TrayCore::start() {
    bool connected = false;
    {
        std::lock_guard lock(mutex_);
        connected = ensure_connected_locked();
    }
    // The snapshot resync issues a request and must never run under the
    // state mutex (the session worker's event sink takes the same mutex).
    if (connected) {
        resync_from_snapshot();
    }
    return connected;
}

bool TrayCore::ensure_connected_locked() {
    if (stopping_) {
        return false;
    }
    if (session_ && session_worker_.started() && session_->connected()) {
        return true;
    }
    auto next = std::make_shared<ipc::SessionClient>(dependencies_.socket_path);
    std::string diagnostic;
    if (!next->connect(kConnectDeadline, diagnostic)) {
        std::cerr << "mirage-tray: mirage-service unreachable (" << dependencies_.socket_path
                  << "): " << diagnostic << "; the tray keeps retrying\n";
        return false;
    }
    session_ = next;
    // Events arrive on the session worker thread: fold them into the state
    // (post-only discipline — never re-enter this class under the lock).
    next->set_event_sink([this](const ipc::Event &event) { handle_event(event); });
    executor::BlockingWorkerSpec spec;
    spec.name = "mirage-tray-ipc";
    spec.config.thread_name = "mirage-tray-ipc";
    spec.worker = std::make_unique<SessionLoopWorker>(
        next, [this](const std::string &reason) { handle_lost(reason); });
    session_worker_ = executor_.start_worker(std::move(spec));
    if (!session_worker_.started()) {
        // Admission is not execution (the Executor integration discipline).
        std::cerr << "mirage-tray: session worker start failed: "
                  << session_worker_.start_result().message << '\n';
        session_.reset();
        return false;
    }
    connected_ = true;
    refresh_locked();
    return true;
}

void TrayCore::resync_from_snapshot() {
    // The snapshot is the source of truth (DEC-012): on every (re)connect
    // the tray re-reads task.list and tracks the most recent non-terminal
    // task, so a tray that joined a running service shows the running work
    // without waiting for a new event.
    std::shared_ptr<ipc::SessionClient> session;
    {
        std::lock_guard lock(mutex_);
        session = session_;
    }
    if (session == nullptr) {
        return;
    }
    auto future = session->call(ipc::ListTasksRequest{}, kCallBudget);
    try {
        const ipc::Response response = future.get();
        const auto *list = std::get_if<ipc::TaskList>(&response.payload);
        if (!response.ok || list == nullptr) {
            return;
        }
        std::lock_guard lock(mutex_);
        if (!tracked_.id.empty() && tracks(tracked_.progress)) {
            return; // already tracking a live task
        }
        for (const auto &task : list->tasks) {
            if (tracks(task.progress)) {
                tracked_.id = task.id;
                tracked_.goal = task.goal;
                tracked_.progress = task.progress;
                break;
            }
        }
        refresh_locked();
    } catch (const std::exception &) {
        // Admission rejected during teardown: nothing to resync for.
    }
}

mirage::desktop::TrayState TrayCore::state() {
    std::lock_guard lock(mutex_);
    return state_;
}

void TrayCore::on_action(mirage::desktop::TrayAction action) {
    switch (action) {
    case mirage::desktop::TrayAction::Pause:
        deliver_command(/*pause=*/true);
        break;
    case mirage::desktop::TrayAction::Resume:
        deliver_command(/*pause=*/false);
        break;
    case mirage::desktop::TrayAction::OpenShell:
        open_shell();
        break;
    case mirage::desktop::TrayAction::Quit:
        if (dependencies_.on_quit != nullptr) {
            dependencies_.on_quit();
        }
        break;
    }
}

void TrayCore::handle_event(const ipc::Event &event) {
    std::lock_guard lock(mutex_);
    if (const auto *status = std::get_if<ipc::HostStatusEvent>(&event.payload)) {
        state_.status =
            std::string("Mirage 服务：") + status->status +
            (tracked_.id.empty() ? std::string("（空闲）")
                                 : "\n任务 " + tracked_.goal + " — " + tracked_.progress);
        // TrayState.status is a single line (clamped by clamp_tray_status);
        // collapse the two lines onto one.
        std::replace(state_.status.begin(), state_.status.end(), '\n', ' ');
        refresh_locked();
        return;
    }
    if (const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload)) {
        if (tracks(task->progress)) {
            tracked_.id = task->task_id;
            tracked_.goal = task->goal;
            tracked_.progress = task->progress;
        } else if (tracked_.id == task->task_id) {
            tracked_ = TrackedTask{}; // the tracked task settled
        }
        refresh_locked();
        return;
    }
    // Other event kinds (session.*, workflow.*, permission.*) do not drive
    // the indicator; the workspace remains their face (DEC-030 decision 6).
}

void TrayCore::handle_lost(const std::string &diagnostic) {
    std::lock_guard lock(mutex_);
    connected_ = false;
    session_.reset();
    session_worker_ = executor::WorkerHandle{};
    tracked_ = TrackedTask{};
    state_.status = "与 mirage-service 失联，正在重试 …";
    state_.can_pause = false;
    state_.can_resume = false;
    refresh_locked();
    std::cerr << "mirage-tray: session lost: " << diagnostic << "; retrying every "
              << std::chrono::duration_cast<std::chrono::seconds>(kReconnectDelay).count() << "s\n";
    schedule_reconnect();
}

void TrayCore::schedule_reconnect() {
    if (reconnect_scheduled_ || stopping_) {
        return;
    }
    reconnect_scheduled_ = true;
    // Self-rescheduling delayed task (the Executor timer capability, rule
    // 5): one retry per cadence, rescheduled only while disconnected.
    const auto reconnect_delay_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(kReconnectDelay).count();
    auto scheduled = executor_.submit_delayed(reconnect_delay_ms, [this] {
        bool connected = false;
        {
            std::lock_guard lock(mutex_);
            reconnect_scheduled_ = false;
            if (stopping_ || connected_) {
                return;
            }
            connected = ensure_connected_locked();
        }
        if (connected) {
            resync_from_snapshot();
            return;
        }
        std::lock_guard lock(mutex_);
        schedule_reconnect();
    });
    try {
        if (scheduled.valid()) {
            scheduled.get(); // the delayed task only schedules state work
        }
    } catch (const std::exception &) {
        // Teardown rejected the retry: the shutdown path owns the process.
    }
}

void TrayCore::refresh_locked() {
    state_.can_pause = connected_ && !tracked_.id.empty() && tracked_.progress == "Active";
    state_.can_resume = connected_ && !tracked_.id.empty() && tracked_.progress == "Paused";
    state_.can_open_shell = !dependencies_.shell_path.empty();
    if (!tracked_.id.empty()) {
        const char *verb = tracked_.progress == "Paused" ? "任务已暂停" : "任务运行中";
        state_.status =
            std::string("Mirage：") + verb + "：" + tracked_.goal + " — " + tracked_.progress;
    } else if (connected_) {
        state_.status = "Mirage：空闲";
    }
    mirage::desktop::clamp_tray_status(state_.status);
}

void TrayCore::deliver_command(bool pause) {
    std::shared_ptr<ipc::SessionClient> session;
    std::string task_id;
    {
        std::lock_guard lock(mutex_);
        session = session_;
        task_id = tracked_.id;
    }
    if (session == nullptr || task_id.empty()) {
        return; // nothing tracked: the menu entry was disabled anyway
    }
    auto future = session->call(pause ? ipc::Request{ipc::PauseTaskRequest{task_id}}
                                      : ipc::Request{ipc::ResumeTaskRequest{task_id}},
                                kCallBudget);
    // The future is consumed on the executor (admission discipline): a
    // failure surfaces loudly; the task.updated event converges the state.
    auto consume = executor_.submit_auto([future = std::move(future), pause]() mutable {
        try {
            const ipc::Response response = future.get();
            if (!response.ok) {
                std::cerr << "mirage-tray: task." << (pause ? "pause" : "resume")
                          << " refused: " << response.error.code << ": " << response.error.message
                          << '\n';
            }
        } catch (const std::exception &error) {
            std::cerr << "mirage-tray: task." << (pause ? "pause" : "resume")
                      << " transport failure: " << error.what() << '\n';
        }
    });
    try {
        if (consume.valid()) {
            consume.get();
        }
    } catch (const std::exception &) {
    }
}

void TrayCore::open_shell() {
    if (dependencies_.shell_path.empty()) {
        std::cerr << "mirage-tray: no desktop shell binary known; cannot open Mirage\n";
        return;
    }
#ifdef _WIN32
    const std::wstring wide(dependencies_.shell_path.begin(), dependencies_.shell_path.end());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (::CreateProcessW(wide.c_str(), nullptr, nullptr, nullptr, FALSE,
                         DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup,
                         &process) == 0) {
        std::cerr << "mirage-tray: shell launch failed (Win32 error " << ::GetLastError() << ")\n";
        return;
    }
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
#else
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::setsid();
        execl(dependencies_.shell_path.c_str(), dependencies_.shell_path.c_str(),
              static_cast<char *>(nullptr));
        _exit(127); // exec failed in the child
    }
    if (pid < 0) {
        std::cerr << "mirage-tray: shell launch failed (fork)\n";
        return;
    }
    // The child is reaped by the init chain (setsid'd daemon shape); the
    // tray does not wait.
#endif
}

void TrayCore::shutdown() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        connected_ = false;
    }
    if (session_worker_.started()) {
        session_worker_.stop(); // requests stop, wakes, joins
    }
    session_worker_ = executor::WorkerHandle{};
    session_.reset();
}

} // namespace mirage::tray
