#pragma once

// The tray process core (M5-10, DEC-030): one SessionClient driven on an
// Executor blocking worker (the shell's ShellSession consumption shape),
// the tray carrier driven on another, and the presentation state between
// them. EXEC-02: the tray talks to mirage-service exclusively over Local
// IPC; the process owns exactly one Executor instance (EXEC-01) and creates
// no threads of its own.

#include <executor/blocking_io.hpp>
#include <executor/executor.hpp>
#include <executor/stop_token.hpp>
#include <executor/types.hpp>

#include <mirage/desktop/tray_carrier.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/session_client.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>

namespace mirage::tray {

namespace ipc = mirage::runtime::ipc;

/// Drives the SessionClient until stop or a fail-closed condition; the
/// carrier's presentation loop runs on its own blocking worker (owned by
/// the embedding).
class SessionLoopWorker final : public executor::IBlockingIoWorker {
  public:
    SessionLoopWorker(std::shared_ptr<ipc::SessionClient> session,
                      std::function<void(const std::string &)> on_exit)
        : session_(std::move(session)), on_exit_(std::move(on_exit)) {}

    void run(executor::StopToken /*stop_token*/) override {
        std::string diagnostic;
        (void)session_->run(diagnostic);
        if (on_exit_) {
            on_exit_(diagnostic);
        }
    }

    void wakeup() noexcept override { session_->stop(); }

  private:
    std::shared_ptr<ipc::SessionClient> session_;
    std::function<void(const std::string &)> on_exit_;
};

/// The carrier pump: the Executor blocking-worker adapter around the tray
/// carrier's presentation loop (the OverlayPumpWorker precedent, DEC-029) —
/// the executor owns the adapter; the embedding owns the core and the
/// carrier.
class TrayPumpWorker final : public executor::IBlockingIoWorker {
  public:
    TrayPumpWorker(mirage::desktop::TrayCarrier *carrier, std::function<bool()> stop_requested)
        : carrier_(carrier), stop_requested_(std::move(stop_requested)) {}

    void run(executor::StopToken /*stop_token*/) override {
        if (carrier_ == nullptr) {
            return;
        }
        mirage::desktop::TrayCarrierContext context;
        context.load_state = [this] { return load_state(); };
        context.on_action = [this](const mirage::desktop::TrayAction action) {
            if (on_action) {
                on_action(action);
            }
        };
        // The presentation loop's exit — clean (stop) or not (indicator
        // registration refused, host vanished) — is never silent: the
        // embedding decides (a tray without a surface exits, DEC-030).
        const mirage::desktop::TrayCarrier::RunReport report =
            carrier_->run(context, [this] { return stop_requested_(); });
        if (on_exit) {
            on_exit(report);
        }
    }

    void wakeup() noexcept override {
        if (carrier_ != nullptr) {
            carrier_->wakeup();
        }
    }

    /// Wiring owned by the embedding (state pull and action delivery land
    /// in TrayCore); must be set before start_worker().
    std::function<mirage::desktop::TrayState()> load_state;
    std::function<void(mirage::desktop::TrayAction)> on_action;
    std::function<void(const mirage::desktop::TrayCarrier::RunReport &)> on_exit;

  private:
    mirage::desktop::TrayCarrier *carrier_;
    std::function<bool()> stop_requested_;
};

/// The tray's presentation state machine and IPC face. Thread model: the
/// session worker's event sink mutates the state under the state mutex; the
/// carrier pump pulls the newest TrayState through load_state (same mutex)
/// and delivers actions, which turn into queued SessionClient calls whose
/// futures are consumed on the executor (admission discipline, AGENTS rule
/// 3). A lost connection self-reschedules a bounded-cadence reconnect as an
/// executor delayed task.
class TrayCore final {
  public:
    struct Dependencies {
        std::string socket_path;
        /// The desktop shell binary to spawn for 快速进入 Mirage (resolved
        /// by the embedding; empty keeps the menu entry disabled).
        std::string shell_path;
        mirage::desktop::TrayCarrier *carrier = nullptr; ///< not owned
        std::function<bool()> quit_requested;            ///< any-thread stop probe
        /// Invoked for the tray's Quit menu entry (the embedding owns the
        /// quit flag the carrier's stop probe reads).
        std::function<void()> on_quit;
    };

    TrayCore(Dependencies dependencies, executor::Executor &executor);
    ~TrayCore();
    TrayCore(const TrayCore &) = delete;
    TrayCore &operator=(const TrayCore &) = delete;

    /// Establishes the first connection; false (with the diagnostic on
    /// stderr) keeps the tray presenting the degraded state while the
    /// reconnect task keeps trying.
    bool start();

    /// The newest presentation state (carrier pump thread; the same mutex
    /// the event sink writes under).
    mirage::desktop::TrayState state();

    /// One tray action delivered by the carrier pump (menu click).
    void on_action(mirage::desktop::TrayAction action);

    /// Stops the session loop and the reconnect cadence (the carrier's
    /// loop is owned by the embedding, which stops it separately).
    void shutdown();

  private:
    struct TrackedTask {
        std::string id;
        std::string goal;
        std::string progress;
    };

    bool ensure_connected();
    void handle_event(const ipc::Event &event);
    void handle_lost(const std::string &diagnostic);
    void schedule_reconnect();
    void resync_from_snapshot();
    void refresh_locked();
    void deliver_command(bool pause);
    void open_shell();

    Dependencies dependencies_;
    executor::Executor &executor_;
    std::shared_ptr<ipc::SessionClient> session_;
    executor::WorkerHandle session_worker_;

    std::mutex mutex_;
    mirage::desktop::TrayState state_;
    TrackedTask tracked_;
    bool connected_ = false;
    bool reconnect_scheduled_ = false;
    bool stopping_ = false;
};

} // namespace mirage::tray
