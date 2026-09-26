#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>

#include <executor/executor.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/session_client.hpp>

namespace mirage::desktop_shell {

namespace ipc = mirage::runtime::ipc;

/// The shell's Local IPC session owner (M5-02): one long-lived SessionClient
/// driven by an Executor blocking worker, lazily (re)connected on demand,
/// with service events and session loss surfaced to the shell's forwarders.
///
/// Executor discipline (AGENTS.md): the shell process owns the one
/// executor::Executor and passes it in; this class owns only the blocking
/// worker (the run loop) and borrows the pool for nothing. The forwarders
/// must not call back into this class (the run loop invokes them) — the
/// shell's forwarders only post to the CEF UI thread.
class ShellSession {
  public:
    /// Receives one encoded service event envelope on the worker thread.
    using EventFn = std::function<void(const std::string &event_json)>;
    /// Receives the session-loss diagnostic once per lost session (not on
    /// deliberate shutdown).
    using LostFn = std::function<void(const std::string &diagnostic)>;

    /// The connect budget for the lazy (re)connect: a local named pipe or
    /// Unix socket answers or refuses in milliseconds; the budget covers
    /// service restart windows, not network latencies.
    static constexpr std::chrono::milliseconds kConnectDeadline{5'000};

    ShellSession(executor::Executor &executor, std::string address, EventFn on_event,
                 LostFn on_lost);
    ~ShellSession();
    ShellSession(const ShellSession &) = delete;
    ShellSession &operator=(const ShellSession &) = delete;

    /// Replaces the forwarders (shell_main wiring: the CEF client is created
    /// after the session, so the forwarders land once it exists). No events
    /// can flow before that: they require a subscribed renderer query first.
    void set_forwarders(EventFn on_event, LostFn on_lost);

    /// Bounded connect + loop start; idempotent while live. False when the
    /// service is unreachable (calls refuse with "unavailable" meanwhile).
    bool ensure_connected();

    bool connected() const;

    /// Thread-safe service call through the live session, lazily (re)con
    /// necting first. The future resolves with the service response or a
    /// wire-shaped local error ("unavailable").
    std::future<ipc::Response> call(const ipc::Request &body, std::chrono::milliseconds timeout);

    /// Stops the run loop and tears the session down; the executor shutdown
    /// stays the shell main's responsibility (the unique external owner).
    void shutdown();

  private:
    void tear_down_locked();
    void handle_loop_exit(const std::string &diagnostic);

    executor::Executor &executor_;
    std::string address_;
    EventFn on_event_;
    LostFn on_lost_;
    mutable std::mutex mutex_;
    std::shared_ptr<ipc::SessionClient> session_;
    executor::WorkerHandle worker_;
    bool stopping_ = false;
};

} // namespace mirage::desktop_shell
