#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

#include <mirage/integration/mira_adapter.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/permission/permission.hpp>

namespace mirage::runtime {

/// Identity of the Mirage background runtime service (design doc section
/// 12); reported by the CLI --version surface.
struct ServiceInfo {
    std::string name;
    bool background_capable = false;
};

ServiceInfo runtime_service_info();

/// Configuration of the Mirage background runtime service (design doc
/// section 12, DEC-007). Every bound carries a capacity or budget: the
/// registry, the step count, the step wall clock and the per-step result
/// size all reject or truncate explicitly instead of growing without bound.
struct ServiceConfig {
    /// Local IPC endpoint. Empty selects the DEC-007 default
    /// ($XDG_RUNTIME_DIR/mirage/mirage-service.sock with a /tmp fallback).
    std::string socket_path;
    /// Product version reported by hello (set by the embedding executable).
    std::string mirage_version;
    HostConfig host;
    /// Executor async pool size for the service process; 0 lets the executor
    /// adapt. The service owns this process's only Executor instance
    /// (EXEC-01).
    std::size_t executor_threads = 2;
    std::size_t max_connections = 16;
    std::size_t max_steps_per_task = 64;
    std::size_t max_task_records = 256;
    /// Capacity of one connection's bounded event queue (DEC-012 decision
    /// 5, drop-oldest). Overflowing surfaces as an `events.overflow` marker
    /// instead of growing without bound; snapshots remain the source of
    /// truth.
    std::size_t event_queue_capacity = 256;
    /// Upper bound for one process.execute step (client-requested budgets
    /// are clamped to it). Filesystem reads are bounded by their provider
    /// contract, not by this value.
    std::chrono::milliseconds step_timeout{30000};
    /// Cap for one step's structured result carried back by task.inspect.
    std::size_t max_result_bytes = 8192;
    /// Upper bound for waiting on one serialized host command (for example
    /// in teardown cancels).
    std::chrono::milliseconds command_wait{4000};
    /// Desktop permission policy judged before every desktop action
    /// (RULE-05, DEC-010). Defaults keep the M1 development topology
    /// working: filesystem.read and process.execute allowed, everything
    /// else denied.
    permission::PermissionPolicy permission_policy;
    /// Confirmation hook for Confirm rules (DEC-010). Null selects the
    /// fail-closed DenyAllConfirmation; the service only keeps this handle,
    /// so the pointed-to handler must outlive every run() of this service.
    std::shared_ptr<permission::ConfirmationHandler> confirmation;
    /// Directory of the Runtime Recovery State file (design doc section 16,
    /// DEC-011, file name "task-recovery.json"). Empty selects the
    /// persistence module's default state directory.
    std::filesystem::path recovery_directory;
    /// Persist terminal task records across service restarts (M1-07). The
    /// file is written on every task settlement and at the end of the
    /// ordered teardown, and terminal records are hydrated back into the
    /// registry on start(). Tests that assert exact task-set contents
    /// should point recovery_directory at a fresh temp directory.
    bool persist_recovery_state = true;
};

/// Outcome of one service run(). `clean` mirrors the ordered-shutdown
/// report of the hosted Mira instance; transport teardown problems surface
/// in `diagnostic`.
struct ServiceRunReport {
    bool clean = false;
    std::string diagnostic; ///< meaningful only when clean is false
    HostShutdownReport host_shutdown;
};

/// Long-running host for the pinned Mira instance plus the Local IPC server
/// (design doc section 12, DEC-007).
///
/// Process shape: the service owns this process's only executor::Executor
/// instance (EXEC-01). The IPC accept/read/write loop runs on an Executor
/// blocking I/O worker; every MiraHost operation is serialized through one
/// Executor SerialExecutionContext (the single-owner discipline MiraHost
/// requires); M1 task drivers run as cancellable executor tasks that post
/// their host operations onto the same serial context.
///
/// Threading: start()/run() are called on the service's owning thread;
/// run() blocks it until shutdown is requested, then performs the ordered
/// teardown on that thread. request_shutdown() is safe from any thread.
/// Signal paths must not call it directly (not async-signal-safe); they use
/// register_shutdown_fd() with a self-pipe instead. After run() returns the
/// instance is terminal, like a shut-down MiraHost: a restart needs a new
/// instance.
class RuntimeService {
  public:
    explicit RuntimeService(ServiceConfig config = {});
    ~RuntimeService();
    RuntimeService(const RuntimeService &) = delete;
    RuntimeService &operator=(const RuntimeService &) = delete;

    /// Resolved IPC endpoint; valid after construction.
    const std::string &socket_path() const;

    /// Prepares the executor, binds the desktop environment through the
    /// hosted Mira instance, opens the IPC listener and begins serving.
    /// Fails closed on any error and records a terminal Failed state.
    HostOutcome start(std::shared_ptr<mirage::integration::DesktopEnvironmentBinding> binding);

    /// Serves until shutdown is requested (request_shutdown(), a
    /// registered shutdown fd or a service.shutdown IPC request), then
    /// performs the ordered teardown: stop accepting, cancel in-flight task
    /// drivers, drain the executor, shut the hosted Mira instance down and
    /// remove the socket file.
    ServiceRunReport run();

    /// Requests shutdown; safe from any thread, never from a signal handler.
    void request_shutdown();

    /// Registers a pipe (or similar) file descriptor whose readability
    /// triggers shutdown; the signal self-pipe path for mirage-service. The
    /// descriptor is not owned and must stay open until run() returns. Must
    /// be called before start().
    void register_shutdown_fd(int fd);

    /// Identity reported by hello requests.
    ipc::ServiceIdentity identity() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::runtime
