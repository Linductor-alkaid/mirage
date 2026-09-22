#include "service_loop.hpp"

#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>

#include <cstring>
#include <utility>
#include <vector>

#ifdef _WIN32
// The Win32 surface is included macro-neutral (DEC-017 decision 5): the
// wake object is an auto-reset event, waits are bounded slices, and every
// transport operation goes through the zero-wait named-pipe stream.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#else
#include <cerrno>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace mirage::runtime::detail {
namespace {

/// Per-connection inbound guard: a frame header declares at most
/// kMaxFrameBytes, so anything beyond header + cap cannot be legitimate.
constexpr std::size_t kMaxInboundBytes = ipc::kMaxFrameBytes + ipc::kFrameHeaderBytes;

std::string error_payload(std::string code, std::string message) {
    ipc::Response response;
    response.ok = false;
    response.id = 0;
    response.error = {std::move(code), std::move(message)};
    return ipc::make_frame(ipc::encode_response(response));
}

#ifdef _WIN32
/// The Windows loop's bounded wait: the wake event shortens it, the poll
/// timeout bounds shutdown and wakeup latency exactly like on POSIX.
constexpr long kMaxWaitSliceMs = 25;
#endif

} // namespace

ServiceLoop::ServiceLoop(Dependencies dependencies)
    : dependencies_(std::move(dependencies)),
      outbound_(executor::comm::ChannelOptions{
          .capacity = 1024,
          .drop_policy = executor::comm::DropPolicy::RejectNewest,
          .enable_stats = true,
          .name = "mirage.ipc.outbound",
      }) {
#ifdef _WIN32
    wake_event_ = ::CreateEventW(nullptr, /*bManualReset=*/FALSE, /*bInitialState=*/FALSE, nullptr);
#else
    int fds[2] = {-1, -1};
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0) {
        wake_read_ = fds[0];
        wake_write_ = fds[1];
    }
#endif
}

ServiceLoop::~ServiceLoop() {
#ifdef _WIN32
    if (wake_event_ != nullptr) {
        ::CloseHandle(wake_event_);
        wake_event_ = nullptr;
    }
#else
    if (wake_read_ >= 0) {
        ::close(wake_read_);
    }
    if (wake_write_ >= 0) {
        ::close(wake_write_);
    }
#endif
}

void ServiceLoop::wakeup() noexcept {
#ifdef _WIN32
    if (wake_event_ != nullptr) {
        ::SetEvent(wake_event_);
    }
#else
    if (wake_write_ >= 0) {
        const char token = 'w';
        // Best-effort by design; consuming the result is what silences
        // glibc's warn_unused_result without relying on cast semantics.
        const ssize_t written = ::write(wake_write_, &token, 1);
        (void)written;
    }
#endif
}

void ServiceLoop::register_shutdown_fd(int fd) {
#ifdef _WIN32
    // The named-pipe transport has no signal self-pipe: Windows shutdown
    // reaches the loop through stop_serving() (the console ctrl handler
    // calls RuntimeService::request_shutdown()), so the descriptor is
    // deliberately not consumed here.
    (void)fd;
#else
    shutdown_fd_ = fd;
#endif
}

void ServiceLoop::stop_serving() {
    stop_serving_.store(true, std::memory_order_release);
    wakeup();
}

void ServiceLoop::post_response(std::uint64_t connection_id, std::string payload) {
    OutboundMessage message;
    message.connection_id = connection_id;
    message.payload = std::move(payload);
    if (!outbound_.try_send(std::move(message))) {
        // A dropped response can never be recovered inside the protocol;
        // fail loud and let the loop tear the transport down (RULE-07).
        overflow_.store(true, std::memory_order_release);
        wakeup();
    }
}

void ServiceLoop::post_response_and_close(std::uint64_t connection_id, std::string payload) {
    OutboundMessage message;
    message.kind = OutboundMessage::Kind::Frame;
    message.connection_id = connection_id;
    message.payload = std::move(payload);
    message.close_after = true;
    if (!outbound_.try_send(std::move(message))) {
        overflow_.store(true, std::memory_order_release);
        wakeup();
    }
}

void ServiceLoop::post_attach_events(
    std::uint64_t connection_id, executor::comm::TopicSubscription<ipc::EventPayload> subscription,
    std::optional<ipc::EventPayload> seed) {
    OutboundMessage message;
    message.kind = OutboundMessage::Kind::AttachEvents;
    message.connection_id = connection_id;
    message.seed = std::move(seed);
    message.subscription = std::move(subscription);
    if (!outbound_.try_send(std::move(message))) {
        // Losing an attach would strand the subscription on the topic and
        // leak events into a queue nobody drains; fail loud like a lost
        // response (RULE-07).
        overflow_.store(true, std::memory_order_release);
        wakeup();
    }
}

void ServiceLoop::post_detach_events(std::uint64_t connection_id) {
    OutboundMessage message;
    message.kind = OutboundMessage::Kind::DetachEvents;
    message.connection_id = connection_id;
    if (!outbound_.try_send(std::move(message))) {
        overflow_.store(true, std::memory_order_release);
        wakeup();
    }
}

void ServiceLoop::append_event(Connection &connection, const ipc::Event &event) {
    if (connection.outbound_sent == connection.outbound.size()) {
        connection.outbound.clear();
        connection.outbound_sent = 0;
    }
    connection.outbound += ipc::make_frame(ipc::encode_event(event));
}

void ServiceLoop::drain_events() {
    for (auto &entry : connections_) {
        Connection &connection = entry.second;
        if (!connection.events) {
            continue;
        }
        // Responses of this pass were already appended by drain_outbound();
        // events follow them in the same buffer, so response frames go out
        // before the connection's queued events (DEC-012 write-out rule).
        ipc::EventPayload payload;
        while (connection.events->try_receive(payload)) {
            ipc::Event event;
            event.seq = ++connection.event_seq;
            event.payload = payload;
            append_event(connection, event);
        }
        const auto stats = connection.events->stats();
        if (stats.dropped_count > connection.overflow_reported) {
            // The drop-oldest queue discarded events; surface the loss as
            // the synthetic marker instead of a silent seq hole (RULE-07).
            ipc::EventsOverflowEvent overflow;
            overflow.dropped = stats.dropped_count - connection.overflow_reported;
            connection.overflow_reported = stats.dropped_count;
            ipc::Event event;
            event.seq = ++connection.event_seq;
            event.payload = overflow;
            append_event(connection, event);
        }
    }
}

void ServiceLoop::drain_outbound() {
    OutboundMessage message;
    while (outbound_.try_receive(message)) {
        auto entry = connections_.find(message.connection_id);
        if (message.kind != OutboundMessage::Kind::Frame && entry == connections_.end()) {
            continue; // connection already gone; its subscription dies here
        }
        if (message.kind == OutboundMessage::Kind::AttachEvents) {
            Connection &connection = entry->second;
            connection.events.emplace(std::move(message.subscription));
            if (message.seed.has_value()) {
                // Seed first (seq 1): the current host status at subscribe
                // time, then everything the topic queued meanwhile.
                ipc::Event event;
                event.seq = ++connection.event_seq;
                event.payload = std::move(*message.seed);
                append_event(connection, event);
            }
            continue;
        }
        if (message.kind == OutboundMessage::Kind::DetachEvents) {
            entry->second.events.reset();
            continue;
        }
        Connection &connection = entry->second;
        if (connection.outbound_sent == connection.outbound.size()) {
            connection.outbound.clear();
            connection.outbound_sent = 0;
        }
        connection.outbound += ipc::make_frame(message.payload);
        if (message.close_after) {
            connection.close_after_write = true;
        }
    }
}

#ifndef _WIN32
void ServiceLoop::accept_ready() {
    for (;;) {
        std::string diagnostic;
        ipc::IpcStream stream = dependencies_.listener->accept(diagnostic);
        if (!stream.valid()) {
            return; // EAGAIN: nothing pending; other errors surface as closed
        }
        if (connections_.size() >= dependencies_.max_connections) {
            const std::string frame = error_payload("unavailable", "connection capacity exceeded");
            // Best-effort refusal, then the stream destructs and closes.
            std::size_t sent = 0;
            while (sent < frame.size()) {
                const ipc::IoResult result =
                    stream.write_some(frame.data() + sent, frame.size() - sent);
                if (result.status != ipc::IoStatus::Ok) {
                    break;
                }
                sent += result.bytes;
            }
            continue;
        }
        Connection connection;
        connection.stream = std::move(stream);
        connections_.emplace(next_connection_id_++, std::move(connection));
    }
}

void ServiceLoop::handle_wakeups(bool wake_ready, bool shutdown_ready) {
    // Both descriptors are drained only when poll reported them ready; a
    // registered shutdown fd (signal self-pipe) may legitimately be a
    // blocking pipe, so reading it without an event would hang the loop.
    char buffer[64];
    if (wake_ready && wake_read_ >= 0) {
        while (::read(wake_read_, buffer, sizeof(buffer)) > 0) {
        }
    }
    if (shutdown_ready && shutdown_fd_ >= 0) {
        while (::read(shutdown_fd_, buffer, sizeof(buffer)) > 0) {
        }
        stop_serving_.store(true, std::memory_order_release);
    }
}
#else
void ServiceLoop::accept_ready() {
    for (;;) {
        std::string diagnostic;
        ipc::IpcStream stream = dependencies_.listener->accept(diagnostic);
        if (!stream.valid()) {
            return; // nothing pending; recycle diagnostics are carried by the caller's logs
        }
        if (connections_.size() >= dependencies_.max_connections) {
            const std::string frame = error_payload("unavailable", "connection capacity exceeded");
            // Best-effort refusal, then the stream destructs and closes.
            std::size_t sent = 0;
            while (sent < frame.size()) {
                const ipc::IoResult result =
                    stream.write_some(frame.data() + sent, frame.size() - sent);
                if (result.status != ipc::IoStatus::Ok) {
                    break;
                }
                sent += result.bytes;
            }
            continue;
        }
        Connection connection;
        connection.stream = std::move(stream);
        connections_.emplace(next_connection_id_++, std::move(connection));
    }
}
#endif

void ServiceLoop::close_connection(std::map<std::uint64_t, Connection>::iterator entry) {
    connections_.erase(entry);
}

bool ServiceLoop::flush_connection(Connection &connection) {
    while (connection.outbound_sent < connection.outbound.size()) {
        const ipc::IoResult result =
            connection.stream.write_some(connection.outbound.data() + connection.outbound_sent,
                                         connection.outbound.size() - connection.outbound_sent);
        switch (result.status) {
        case ipc::IoStatus::Ok:
            connection.outbound_sent += result.bytes;
            break;
        case ipc::IoStatus::WouldBlock:
            return true;
        case ipc::IoStatus::Closed:
        case ipc::IoStatus::Error:
            return false;
        }
    }
    return true;
}

bool ServiceLoop::ingest_connection(std::map<std::uint64_t, Connection>::iterator entry) {
    Connection &connection = entry->second;
    bool transport_failed = false;
    bool violation = false;
    bool peer_closed = false;
    char chunk[4096];
    for (;;) {
        const ipc::IoResult result = connection.stream.read_some(chunk, sizeof(chunk));
        if (result.status == ipc::IoStatus::Ok) {
            connection.inbound.append(chunk, result.bytes);
            if (connection.inbound.size() > kMaxInboundBytes) {
                connection.outbound =
                    error_payload("protocol_error", "inbound exceeds the frame cap");
                connection.outbound_sent = 0;
                connection.close_after_write = true;
                // Deliver the error frame, then close.
                violation = true;
                break;
            }
            continue;
        }
        if (result.status == ipc::IoStatus::WouldBlock) {
            break;
        }
        if (result.status == ipc::IoStatus::Closed) {
            peer_closed = true;
        } else {
            transport_failed = true;
        }
        break;
    }
    // Extract every complete frame that just arrived, even after a peer
    // half-close: the request may already sit in the buffer and still
    // deserves its response.
    while (!transport_failed && !violation) {
        const ipc::FrameExtraction extraction = ipc::try_extract_frame(connection.inbound);
        if (extraction.status == ipc::FrameExtract::NeedMoreData) {
            break;
        }
        if (extraction.status == ipc::FrameExtract::ProtocolError) {
            connection.outbound = error_payload("protocol_error", extraction.reason);
            connection.outbound_sent = 0;
            connection.close_after_write = true;
            violation = true;
            break;
        }
        if (connection.busy) {
            // DEC-007: one outstanding request per connection; a second
            // frame in the same read is pipelining.
            connection.outbound =
                error_payload("protocol_error", "pipelined request before the previous response");
            connection.outbound_sent = 0;
            connection.close_after_write = true;
            violation = true;
            break;
        }
        connection.busy = true;
        // The handler serializes through the service's serial context and
        // posts the response; bounded by the host command wait, so the loop
        // stays responsive enough.
        dependencies_.on_frame(entry->first, std::move(extraction.message));
    }
    if (transport_failed) {
        return false;
    }
    if (peer_closed && !connection.busy && !connection.close_after_write &&
        connection.outbound_sent >= connection.outbound.size()) {
        // Nothing was in flight and nothing will be answered.
        return false;
    }
    if (peer_closed) {
        // Deliver whatever is owed, then close.
        connection.close_after_write = true;
    }
    return true;
}

void ServiceLoop::flush_and_settle() {
    for (auto entry = connections_.begin(); entry != connections_.end();) {
        Connection &connection = entry->second;
        if (connection.outbound_sent < connection.outbound.size()) {
            if (!flush_connection(connection)) {
                entry = connections_.erase(entry);
                continue;
            }
        }
        if (connection.outbound_sent == connection.outbound.size() &&
            !connection.outbound.empty()) {
            connection.outbound.clear();
            connection.outbound_sent = 0;
            if (connection.close_after_write) {
                entry = connections_.erase(entry);
                continue;
            }
            connection.busy = false; // ready for the next request
        }
        ++entry;
    }
}

void ServiceLoop::close_all() { connections_.clear(); }

#ifndef _WIN32

void ServiceLoop::run(executor::StopToken stop_token) {
    std::vector<pollfd> descriptors;
    std::vector<std::map<std::uint64_t, Connection>::iterator> owners;

    while (!stop_token.stop_requested() && !stop_serving_.load(std::memory_order_acquire)) {
        descriptors.clear();
        owners.clear();
        if (dependencies_.listener != nullptr && dependencies_.listener->valid()) {
            descriptors.push_back({static_cast<int>(dependencies_.listener->handle()), POLLIN, 0});
            owners.push_back(connections_.end()); // not a connection slot
        }
        if (wake_read_ >= 0) {
            descriptors.push_back({wake_read_, POLLIN, 0});
            owners.push_back(connections_.end());
        }
        if (shutdown_fd_ >= 0) {
            descriptors.push_back({shutdown_fd_, POLLIN, 0});
            owners.push_back(connections_.end());
        }
        for (auto entry = connections_.begin(); entry != connections_.end(); ++entry) {
            Connection &connection = entry->second;
            short events = 0;
            if (!connection.busy && connection.inbound.size() <= kMaxInboundBytes) {
                events |= POLLIN;
            }
            if (connection.outbound_sent < connection.outbound.size()) {
                events |= POLLOUT;
            }
            if (events != 0) {
                descriptors.push_back({static_cast<int>(connection.stream.handle()), events, 0});
                owners.push_back(entry);
            }
        }

        const int ready =
            ::poll(descriptors.data(), descriptors.size(), dependencies_.poll_timeout_ms);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            break; // poll itself failed; fail loud instead of spinning
        }

        std::size_t remaining = static_cast<std::size_t>(ready);
        for (std::size_t index = 0; index < descriptors.size() && remaining > 0; ++index) {
            const short revents = descriptors[index].revents;
            if (revents == 0) {
                continue;
            }
            --remaining;
            const bool is_connection = owners[index] != connections_.end();

            if (!is_connection) {
                handle_wakeups(/*wake_ready=*/descriptors[index].fd == wake_read_,
                               /*shutdown_ready=*/descriptors[index].fd == shutdown_fd_);
                if (dependencies_.listener != nullptr &&
                    descriptors[index].fd == static_cast<int>(dependencies_.listener->handle()) &&
                    (revents & POLLIN) != 0) {
                    accept_ready();
                }
                continue;
            }

            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (revents & POLLIN) == 0) {
                close_connection(owners[index]);
                continue;
            }

            if ((revents & POLLIN) != 0 && !owners[index]->second.busy) {
                if (!ingest_connection(owners[index])) {
                    close_connection(owners[index]);
                    continue;
                }
            }

            Connection &connection = owners[index]->second;
            if (connection.outbound_sent < connection.outbound.size() ||
                connection.close_after_write) {
                if (!flush_connection(connection)) {
                    close_connection(owners[index]);
                    continue;
                }
                if (connection.outbound_sent == connection.outbound.size()) {
                    connection.outbound.clear();
                    connection.outbound_sent = 0;
                    if (connection.close_after_write) {
                        close_connection(owners[index]);
                        continue;
                    }
                    connection.busy = false; // ready for the next request
                }
            }
        }

        drain_outbound();
        drain_events();
        // Responses produced by handlers during this iteration go out in the
        // same iteration when the socket takes them.
        flush_and_settle();
        if (overflow_.load(std::memory_order_acquire)) {
            overflow_.store(false, std::memory_order_release);
            close_all();
        }
    }

    // Ordered exit: best-effort delivery of already-produced responses and
    // queued events, then close everything. No new requests are read after
    // stop.
    drain_outbound();
    drain_events();
    for (auto &entry : connections_) {
        (void)flush_connection(entry.second);
    }
    close_all();
    if (dependencies_.on_exit) {
        dependencies_.on_exit();
    }
}

#else

void ServiceLoop::run(executor::StopToken stop_token) {
    // The named-pipe stream is zero-wait non-blocking on both directions,
    // so this loop is level-driven: every pass reads, flushes, drains and
    // settles every connection, then bounds its wait with the wake event /
    // the poll timeout slice. Wakeup latency and shutdown latency stay
    // bounded by the same numbers as the POSIX loop; the per-pass cost is
    // one non-blocking probe per connection.
    while (!stop_token.stop_requested() && !stop_serving_.load(std::memory_order_acquire)) {
        if (dependencies_.listener != nullptr && dependencies_.listener->valid()) {
            accept_ready();
        }

        for (auto entry = connections_.begin(); entry != connections_.end();) {
            Connection &connection = entry->second;
            if (!connection.busy && connection.inbound.size() <= kMaxInboundBytes) {
                if (!ingest_connection(entry)) {
                    entry = connections_.erase(entry);
                    continue;
                }
            }
            if (connection.outbound_sent < connection.outbound.size() ||
                connection.close_after_write) {
                if (!flush_connection(connection)) {
                    entry = connections_.erase(entry);
                    continue;
                }
                if (connection.outbound_sent == connection.outbound.size()) {
                    connection.outbound.clear();
                    connection.outbound_sent = 0;
                    if (connection.close_after_write) {
                        entry = connections_.erase(entry);
                        continue;
                    }
                    connection.busy = false; // ready for the next request
                }
            }
            ++entry;
        }

        drain_outbound();
        drain_events();
        // Responses produced by handlers during this iteration go out in
        // the same iteration when the pipe takes them.
        flush_and_settle();
        if (overflow_.load(std::memory_order_acquire)) {
            overflow_.store(false, std::memory_order_release);
            close_all();
        }

        // The bounded wait: the auto-reset wake event fires on
        // wakeup()/stop_serving(); the slice keeps the loop responsive even
        // without it (new clients connect outside any event).
        const long wait_ms = static_cast<long>(dependencies_.poll_timeout_ms);
        if (wake_event_ != nullptr) {
            ::WaitForSingleObject(wake_event_, wait_ms);
        } else {
            ::Sleep(static_cast<DWORD>(wait_ms));
        }
    }

    // Ordered exit: best-effort delivery of already-produced responses and
    // queued events, then close everything. No new requests are read after
    // stop.
    drain_outbound();
    drain_events();
    for (auto &entry : connections_) {
        (void)flush_connection(entry.second);
    }
    close_all();
    if (dependencies_.on_exit) {
        dependencies_.on_exit();
    }
}

#endif

} // namespace mirage::runtime::detail
