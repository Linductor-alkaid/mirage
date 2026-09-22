#pragma once

#include <executor/blocking_io.hpp>
#include <executor/comm/channel.hpp>
#include <executor/stop_token.hpp>
#include <executor/types.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include <executor/comm/topic.hpp>

#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>

namespace mirage::runtime::detail {

/// The Local IPC server event loop (DEC-007 item 7): one loop on an
/// Executor blocking I/O worker owning every client connection. Complete
/// request frames are handed to `on_frame` (implemented by the service,
/// which serializes actual work onto its serial context and posts responses
/// back through post_response()). The loop thread itself only parses frames
/// and moves bytes, so it stays responsive to accept, wakeup and shutdown.
/// The frame processing, the outbound queue and the event fan-out are
/// shared; only the readiness mechanism is platform-selected (poll over
/// Unix sockets on Linux, zero-wait named-pipe passes with a bounded wait
/// slice on Windows, M4-06).
class ServiceLoop final : public executor::IBlockingIoWorker {
  public:
    struct Dependencies {
        /// The bound transport endpoint (not owned; must outlive run()).
        ipc::IpcListener *listener = nullptr;
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

    /// Attaches an event subscription to a connection (M1.5-02, DEC-012):
    /// the loop drains the subscription's bounded queue and writes its
    /// events after the responses of the same pass. `seed`, when engaged,
    /// becomes the connection's first event (seq 1) — the current host
    /// status at subscribe time. A subscription for a connection that has
    /// died in the meantime is dropped here.
    void post_attach_events(std::uint64_t connection_id,
                            executor::comm::TopicSubscription<ipc::EventPayload> subscription,
                            std::optional<ipc::EventPayload> seed);

    /// Detaches a connection's event subscription; idempotent. Queued
    /// events already written into the connection buffer still flush.
    void post_detach_events(std::uint64_t connection_id);

#ifndef _WIN32
    /// Extra poll descriptor whose readability stops the loop (the signal
    /// self-pipe). Not owned. Must be called before run(). POSIX
    /// transports only: the Windows service stops through
    /// RuntimeService::request_shutdown().
    void register_shutdown_fd(int fd);
#endif

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
        /// Event stream state (DEC-012); loop-thread only. The
        /// subscription is the bounded drop-oldest per-connection queue.
        std::optional<executor::comm::TopicSubscription<ipc::EventPayload>> events;
        std::uint64_t event_seq = 0;         ///< last written event's seq
        std::uint64_t overflow_reported = 0; ///< drops already surfaced
    };

    struct OutboundMessage {
        enum class Kind { Frame, AttachEvents, DetachEvents };
        Kind kind = Kind::Frame;
        std::uint64_t connection_id = 0;
        std::string payload;
        bool close_after = false;
        /// AttachEvents only: first event delivered on the new
        /// subscription (current host status at subscribe time).
        std::optional<ipc::EventPayload> seed;
        /// AttachEvents only: the per-connection bounded queue.
        executor::comm::TopicSubscription<ipc::EventPayload> subscription;
    };

    void drain_outbound();
    void drain_events();
    /// Appends one encoded event frame to the connection buffer with the
    /// next per-connection seq.
    void append_event(Connection &connection, const ipc::Event &event);
    void accept_ready();
    void handle_wakeups(bool wake_ready, bool shutdown_ready);
    void close_connection(std::map<std::uint64_t, Connection>::iterator entry);
    /// One best-effort write pass over a connection's pending outbound;
    /// false when the transport failed or the peer is gone.
    bool flush_connection(Connection &connection);
    void close_all();

    /// One read pass + frame extraction over a connection (shared by both
    /// transports); returns false when the connection must be torn down.
    bool ingest_connection(std::map<std::uint64_t, Connection>::iterator entry);
    /// End-of-iteration flush: writes every pending outbound, settles the
    /// busy/close flags and erases finished connections.
    void flush_and_settle();

    Dependencies dependencies_;
#ifdef _WIN32
    void *wake_event_ = nullptr; // auto-reset event; wakeup() sets it
#else
    int wake_read_ = -1;
    int wake_write_ = -1;
    int shutdown_fd_ = -1;
#endif
    std::atomic<bool> stop_serving_{false};
    std::atomic<bool> overflow_{false};
    std::uint64_t next_connection_id_ = 1;
    std::map<std::uint64_t, Connection> connections_;
    executor::comm::MpscChannel<OutboundMessage> outbound_;
};

} // namespace mirage::runtime::detail
