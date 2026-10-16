#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mutex>
#include <string>

namespace mirage::runtime::ipc {

/// Long-lived Local IPC session (DEC-007 / DEC-012): one connection, request
/// queueing with the frozen single-outstanding discipline, id-correlated
/// responses and the connection-scoped event stream. The GUI-side transport
/// (M5-02) and later the tray drive one of these through an Executor
/// blocking worker — this class owns no threads and no scheduling of its
/// own, it only defines the blocking `run()` loop the owner submits.
///
/// Threading contract:
/// - `connect()`, `run()` and the event sink are driven by the owner: connect
///   on the setup thread, run() on an Executor blocking worker, sink calls
///   arrive on that worker's thread.
/// - `call()` and `stop()` are thread-safe against all of the above.
///
/// Failure discipline: the session fails closed. A transport loss, a framing
/// or decode violation, an unexpected response id or a request that exhausts
/// its timeout all close the session (run() returns, pending calls resolve
/// with the same stable local error codes as IpcClient: "unavailable" for
/// reachability and timeouts, "internal" for framing and decode violations).
/// Recovery (reconnect, resubscribe, resync) is the owner's decision —
/// snapshots remain the source of truth (DEC-012).
class SessionClient {
  public:
    /// Receives decoded service events on the run() thread. Must not throw
    /// and must not call back into this client; long work belongs on the
    /// owner's own execution context.
    using EventSink = std::function<void(const Event &)>;

    explicit SessionClient(std::string address);
    ~SessionClient();
    SessionClient(const SessionClient &) = delete;
    SessionClient &operator=(const SessionClient &) = delete;

    /// Establishes the transport with a bounded deadline. False with a
    /// diagnostic when the service is unreachable (the same "no service
    /// listening" surface as connect_stream).
    bool connect(std::chrono::milliseconds deadline, std::string &diagnostic);
    bool connected() const;

    /// Installs the event sink (optional; without one, decoded events are
    /// dropped — the owner chose not to observe them). Call before run();
    /// a second call is a contract violation.
    void set_event_sink(EventSink sink);

    /// Thread-safe request submission honoring the DEC-012 single-outstanding
    /// rule: requests queue and are issued strictly one at a time, responses
    /// correlate by id, so callers may overlap freely. The future resolves
    /// with the matched response, or ok=false carrying a stable local error
    /// code when the session is not connected / dies / the call times out.
    /// A timeout also closes the session fail-closed (the frozen discipline
    /// forbids issuing the next request while one is unanswered).
    std::future<Response> call(const Request &body, std::chrono::milliseconds timeout);

    enum class RunExit {
        Stopped,        ///< stop() was requested by the owner
        ConnectionLost, ///< transport loss, protocol violation or the
                        ///< fail-closed close after a timed-out request
    };

    /// Drives the session until stop() or a fail-closed condition. Runs on
    /// the calling thread (the owner's Executor blocking worker) and blocks
    /// there for the session's lifetime; `diagnostic` explains the exit.
    /// The owner must let run() return before destroying the client.
    RunExit run(std::string &diagnostic);

    /// Thread-safe cooperative stop; run() returns Stopped within one
    /// readiness slice. Pending calls resolve with "unavailable".
    void stop();

    const std::string &address() const { return address_; }

  private:
    struct Pending {
        std::promise<Response> promise;
        std::chrono::steady_clock::time_point deadline;
    };

    void fail_pending_locked(const std::string &code, const std::string &message);
    void fail_call_locked(std::uint64_t id, const std::string &code, const std::string &message);
    /// Marks the session dead fail-closed and wakes run(); safe from any
    /// thread (the run loop performs the actual close).
    void close_fail_closed(const std::string &reason);

    std::string address_;
    mutable std::mutex mutex_;
    IpcStream stream_; ///< handed from connect() to run() under the mutex
    bool connected_ = false;
    bool dead_ = false;       ///< fail-closed: run() must exit, calls refuse
    std::string dead_reason_; ///< why the session died (run()'s diagnostic)
    bool stop_requested_ = false;
    std::uint64_t next_id_ = 1;      ///< correlation ids, strictly increasing
    std::deque<std::string> outbox_; ///< complete frames awaiting the wire
    std::map<std::uint64_t, Pending> pending_;
    EventSink sink_;
    bool sink_set_ = false;
};

} // namespace mirage::runtime::ipc
