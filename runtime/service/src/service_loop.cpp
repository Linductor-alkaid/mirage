#include "service_loop.hpp"

#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>

#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>

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

} // namespace

ServiceLoop::ServiceLoop(Dependencies dependencies)
    : dependencies_(std::move(dependencies)),
      outbound_(executor::comm::ChannelOptions{
          .capacity = 1024,
          .drop_policy = executor::comm::DropPolicy::RejectNewest,
          .enable_stats = true,
          .name = "mirage.ipc.outbound",
      }) {
    int fds[2] = {-1, -1};
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0) {
        wake_read_ = fds[0];
        wake_write_ = fds[1];
    }
}

ServiceLoop::~ServiceLoop() {
    if (wake_read_ >= 0) {
        ::close(wake_read_);
    }
    if (wake_write_ >= 0) {
        ::close(wake_write_);
    }
}

void ServiceLoop::wakeup() noexcept {
    if (wake_write_ >= 0) {
        const char token = 'w';
        // Best-effort by design; consuming the result is what silences
        // glibc's warn_unused_result without relying on cast semantics.
        const ssize_t written = ::write(wake_write_, &token, 1);
        (void)written;
    }
}

void ServiceLoop::register_shutdown_fd(int fd) { shutdown_fd_ = fd; }

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
    message.connection_id = connection_id;
    message.payload = std::move(payload);
    message.close_after = true;
    if (!outbound_.try_send(std::move(message))) {
        overflow_.store(true, std::memory_order_release);
        wakeup();
    }
}

void ServiceLoop::drain_outbound() {
    OutboundMessage message;
    while (outbound_.try_receive(message)) {
        auto entry = connections_.find(message.connection_id);
        if (entry == connections_.end()) {
            continue; // connection already gone; the response dies with it
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

void ServiceLoop::accept_ready() {
    for (;;) {
        const int fd =
            ::accept4(dependencies_.listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            return; // EAGAIN: nothing pending; other errors surface as closed
        }
        if (connections_.size() >= dependencies_.max_connections) {
            const std::string frame = error_payload("unavailable", "connection capacity exceeded");
            const ssize_t written = ::write(fd, frame.data(), frame.size());
            (void)written; // best effort: nothing accepts this connection
            ::close(fd);
            continue;
        }
        Connection connection;
        connection.stream = ipc::IpcStream{fd};
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

void ServiceLoop::close_all() { connections_.clear(); }

void ServiceLoop::run(executor::StopToken stop_token) {
    std::vector<pollfd> descriptors;
    std::vector<std::map<std::uint64_t, Connection>::iterator> owners;

    while (!stop_token.stop_requested() && !stop_serving_.load(std::memory_order_acquire)) {
        descriptors.clear();
        owners.clear();
        if (dependencies_.listen_fd >= 0) {
            descriptors.push_back({dependencies_.listen_fd, POLLIN, 0});
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
                descriptors.push_back({connection.stream.handle(), events, 0});
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
                if (descriptors[index].fd == dependencies_.listen_fd && (revents & POLLIN) != 0) {
                    accept_ready();
                }
                continue;
            }

            const std::uint64_t connection_id = owners[index]->first;
            Connection &connection = owners[index]->second;
            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (revents & POLLIN) == 0) {
                close_connection(owners[index]);
                continue;
            }

            bool transport_failed = false;
            bool violation = false;
            bool peer_closed = false;
            if ((revents & POLLIN) != 0 && !connection.busy) {
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
                // Extract every complete frame that just arrived, even after
                // a peer half-close: the request may already sit in the
                // buffer and still deserves its response.
                while (!transport_failed && !violation) {
                    const ipc::FrameExtraction extraction =
                        ipc::try_extract_frame(connection.inbound);
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
                        // DEC-007: one outstanding request per connection;
                        // a second frame in the same read is pipelining.
                        connection.outbound = error_payload(
                            "protocol_error", "pipelined request before the previous response");
                        connection.outbound_sent = 0;
                        connection.close_after_write = true;
                        violation = true;
                        break;
                    }
                    connection.busy = true;
                    // The handler serializes through the service's serial
                    // context and posts the response; bounded by the host
                    // command wait, so the loop stays responsive enough.
                    dependencies_.on_frame(connection_id, std::move(extraction.message));
                }
                if (transport_failed) {
                    close_connection(owners[index]);
                    continue;
                }
                if (peer_closed && !connection.busy && !connection.close_after_write &&
                    connection.outbound_sent >= connection.outbound.size()) {
                    // Nothing was in flight and nothing will be answered.
                    close_connection(owners[index]);
                    continue;
                }
                if (peer_closed) {
                    // Deliver whatever is owed, then close.
                    connection.close_after_write = true;
                }
            }

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
        // Responses produced by handlers during this iteration go out in the
        // same iteration when the socket takes them.
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
                connection.busy = false;
            }
            ++entry;
        }
        if (overflow_.load(std::memory_order_acquire)) {
            overflow_.store(false, std::memory_order_release);
            close_all();
        }
    }

    // Ordered exit: best-effort delivery of already-produced responses, then
    // close everything. No new requests are read after stop.
    drain_outbound();
    for (auto &entry : connections_) {
        (void)flush_connection(entry.second);
    }
    close_all();
    if (dependencies_.on_exit) {
        dependencies_.on_exit();
    }
}

} // namespace mirage::runtime::detail
