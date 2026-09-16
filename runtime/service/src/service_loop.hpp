#pragma once

#include <executor/blocking_io.hpp>
#include <executor/comm/channel.hpp>
#include <executor/stop_token.hpp>
#include <executor/types.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <unistd.h>

#include <mirage/runtime/ipc/stream.hpp>

namespace mirage::runtime::detail {

/// The Local IPC server event loop (DEC-007 item 7): one poll-driven loop on
/// an Executor blocking I/O worker owning every client connection. Complete
/// request frames are handed to `on_frame` (implemented by the service,
/// which serializes actual work onto its serial context and posts responses
/// back through post_response()). The loop thread itself only parses frames
/// and moves bytes, so it stays responsive to accept, wakeup and shutdown.
class ServiceLoop final : public executor::IBlockingIoWorker {
  public:
    struct Dependencies {
        int listen_fd = -1;
        std::size_t max_connections = 16;
        /// Upper bound of one poll wait; bounds shutdown and wakeup latency.
        int poll_timeout_ms = 200;
        /// Invoked on the loop thread for every complete request frame.
        std::function<void(std::uint64_t connection_id, std::string payload)> on_frame;
        /// Invoked once on the loop thread when run() exits (any reason).
        std::function<void()> on_exit;
    };

    explicit ServiceLoop(Dependencies dependencies);
    ~ServiceLoop() override;
    ServiceLoop(const ServiceLoop &) = delete;
    ServiceLoop &operator=(const ServiceLoop &) = delete;

    // executor::IBlockingIoWorker
    void run(executor::StopToken stop_token) override;
    void wakeup() noexcept override;

    /// Queues one response frame for a connection; thread-safe. When the
    /// outbound channel is full the loop fails loud: it drops every
    /// connection instead of silently losing a response.
    void post_response(std::uint64_t connection_id, std::string payload);

    /// Queues a response and closes the connection once it is written;
    /// used for protocol violations (DEC-007 framing rules).
    void post_response_and_close(std::uint64_t connection_id, std::string payload);

    /// Extra poll descriptor whose readability stops the loop (the signal
    /// self-pipe). Not owned. Must be called before run().
    void register_shutdown_fd(int fd);

    /// Requests the loop to stop serving and exit run() after a final
    /// best-effort response flush; thread-safe (IPC shutdown path).
    void stop_serving();

  private:
    struct Connection {
        mirage::runtime::ipc::IpcStream stream;
        std::string inbound;
        std::string outbound;
        std::size_t outbound_sent = 0;
        bool busy = false;              ///< one outstanding request
        bool close_after_write = false; ///< protocol violation / capacity
    };

    struct OutboundMessage {
        std::uint64_t connection_id = 0;
        std::string payload;
        bool close_after = false;
    };

    void drain_outbound();
    void accept_ready();
    void handle_wakeups(bool wake_ready, bool shutdown_ready);
    void close_connection(std::map<std::uint64_t, Connection>::iterator entry);
    /// One best-effort write pass over a connection's pending outbound;
    /// false when the transport failed or the peer is gone.
    bool flush_connection(Connection &connection);
    void close_all();

    Dependencies dependencies_;
    int wake_read_ = -1;
    int wake_write_ = -1;
    int shutdown_fd_ = -1;
    std::atomic<bool> stop_serving_{false};
    std::atomic<bool> overflow_{false};
    std::uint64_t next_connection_id_ = 1;
    std::map<std::uint64_t, Connection> connections_;
    executor::comm::MpscChannel<OutboundMessage> outbound_;
};

} // namespace mirage::runtime::detail
