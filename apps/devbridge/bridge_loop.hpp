#pragma once

#include <executor/blocking_io.hpp>
#include <executor/stop_token.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unistd.h>
#include <utility>

#include <mirage/runtime/ipc/stream.hpp>

#include "websocket.hpp"

namespace mirage::devbridge {

/// Report of one bridge run(); `clean` is the exit-code-facing summary.
struct BridgeRunReport {
    bool clean = true;
    std::string diagnostic; ///< meaningful only when clean is false
};

/// The development bridge event loop (M1.5-03, DEC-012 decision 6): one
/// poll-driven loop on an Executor blocking I/O worker owning every browser
/// WebSocket session and its 1:1 upstream Local IPC connection.
///
/// Relay rules: one WebSocket binary message carries exactly one
/// length-prefixed IPC frame, payload bytes pass through verbatim in both
/// directions (DEC-012: no payload rewriting). Each direction is a FIFO on
/// this single loop thread, so ordering is structural. Every buffer carries
/// a byte budget; exceeding one closes the session explicitly instead of
/// growing or dropping silently (RULE-07).
class DevBridgeLoop final : public executor::IBlockingIoWorker {
  public:
    struct Dependencies {
        /// WebSocket TCP listener; not owned and not closed here.
        int listen_fd = -1;
        /// Upstream Local IPC endpoint (a mirage-service socket path).
        std::string upstream_socket_path;
        std::size_t max_sessions = 4;
        /// Upper bound of one poll wait; bounds shutdown latency.
        int poll_timeout_ms = 200;
        /// Byte budget of relayed frames buffered per direction per session;
        /// a slower peer makes the sender's budget trip and the session
        /// closes with an explicit close frame.
        std::size_t max_relay_buffer_bytes = 4 * 1024 * 1024;
        /// Invoked once on the loop thread when run() exits (any reason).
        std::function<void(BridgeRunReport)> on_exit;
    };

    explicit DevBridgeLoop(Dependencies dependencies);
    ~DevBridgeLoop() override;
    DevBridgeLoop(const DevBridgeLoop &) = delete;
    DevBridgeLoop &operator=(const DevBridgeLoop &) = delete;

    // executor::IBlockingIoWorker
    void run(executor::StopToken stop_token) override;
    void wakeup() noexcept override;

    /// Extra poll descriptor whose readability stops the loop (the signal
    /// self-pipe). Not owned. Must be called before run().
    void register_shutdown_fd(int fd);

    /// Requests the loop to stop serving and exit run(); thread-safe.
    void stop_serving();

  private:
    enum class Phase {
        handshake,           ///< HTTP opening handshake not finished
        connecting_upstream, ///< handshake done; UDS connect in progress
        relaying             ///< upstream connected; frames pass through
    };

    struct Session {
        Session() = default;
        /// Owns both raw descriptors; erasing a session closes them.
        ~Session() {
            if (ws_fd >= 0) {
                ::close(ws_fd);
            }
            if (connecting_fd >= 0) {
                ::close(connecting_fd);
            }
        }
        Session(Session &&other) noexcept
            : ws_fd(std::exchange(other.ws_fd, -1)), upstream(std::move(other.upstream)),
              connecting_fd(std::exchange(other.connecting_fd, -1)), phase(other.phase),
              assembler(std::move(other.assembler)), ws_inbound(std::move(other.ws_inbound)),
              ws_outbound(std::move(other.ws_outbound)), ws_outbound_sent(other.ws_outbound_sent),
              upstream_inbound(std::move(other.upstream_inbound)),
              upstream_outbound(std::move(other.upstream_outbound)),
              upstream_outbound_sent(other.upstream_outbound_sent), closing(other.closing),
              peer_close_received(other.peer_close_received) {}
        Session(const Session &) = delete;
        Session &operator=(const Session &) = delete;

        int ws_fd = -1;
        mirage::runtime::ipc::IpcStream upstream; ///< valid once connected
        int connecting_fd = -1;                   ///< owned until connected
        Phase phase = Phase::handshake;
        ws::MessageAssembler assembler{ws::kMaxMessageBytes};
        /// Raw TCP bytes not yet parsed into WebSocket frames.
        std::string ws_inbound;
        /// Encoded WebSocket frames awaiting the TCP write.
        std::string ws_outbound;
        std::size_t ws_outbound_sent = 0;
        /// Raw upstream bytes not yet extracted into IPC frames.
        std::string upstream_inbound;
        /// Length-prefixed frames awaiting the upstream write.
        std::string upstream_outbound;
        std::size_t upstream_outbound_sent = 0;
        /// A close frame has been queued (last one out); flush then close.
        bool closing = false;
        /// True when the browser initiated the close handshake.
        bool peer_close_received = false;
    };

    void accept_ready();
    /// Readable WebSocket bytes of one session; false when the session died.
    bool handle_ws_readable(std::map<int, Session>::iterator entry);
    /// Readable upstream bytes of one session; false when the session died.
    bool handle_upstream_readable(std::map<int, Session>::iterator entry);
    /// Connect finished on a session's UDS socket; false when it failed.
    bool finish_upstream_connect(std::map<int, Session>::iterator entry);

    bool flush_ws(std::map<int, Session>::iterator entry);
    bool flush_upstream(std::map<int, Session>::iterator entry);
    /// Queues `payload` as one binary message; false (session must close)
    /// when the session's relay budget would be exceeded.
    bool queue_ws_binary(std::map<int, Session>::iterator entry, const std::string &payload);
    void queue_ws_frame(std::map<int, Session>::iterator entry, const std::string &frame);
    void queue_ws_close(std::map<int, Session>::iterator entry, std::uint16_t code,
                        const std::string &reason);
    /// Drops the session: closes both transports right away (used before
    /// the WebSocket handshake completed, where there is no close channel).
    void drop_session(std::map<int, Session>::iterator entry);
    /// Queues a close frame and marks the session closing; after the queued
    /// bytes flush, both transports close.
    void close_session(std::map<int, Session>::iterator entry, std::uint16_t code,
                       const std::string &reason);
    /// Ordered-exit flush of one session (best effort, single pass).
    void shutdown_flush(Session &session);

    Dependencies dependencies_;
    int wake_read_ = -1;
    int wake_write_ = -1;
    int shutdown_fd_ = -1;
    std::atomic<bool> stop_serving_{false};
    BridgeRunReport report_;          ///< loop-thread only
    std::map<int, Session> sessions_; ///< keyed by ws_fd
};

} // namespace mirage::devbridge
