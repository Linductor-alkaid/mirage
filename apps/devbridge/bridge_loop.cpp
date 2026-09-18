#include "bridge_loop.hpp"

#include <mirage/runtime/ipc/framing.hpp>

#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace mirage::devbridge {
namespace {

using mirage::runtime::ipc::IoResult;
using mirage::runtime::ipc::IoStatus;

/// Per-session inbound guards: a frame or WebSocket message can never
/// legitimately exceed one maximal IPC frame plus transport overhead.
constexpr std::size_t kMaxUpstreamInboundBytes =
    2 * (mirage::runtime::ipc::kMaxFrameBytes + mirage::runtime::ipc::kFrameHeaderBytes + 4096);
constexpr std::size_t kMaxWsInboundBytes = ws::kMaxMessageBytes + 4096;

IoResult tcp_recv_some(int fd, char *data, std::size_t size) {
    const ssize_t received = ::recv(fd, data, size, 0);
    if (received > 0) {
        return {IoStatus::Ok, static_cast<std::size_t>(received), 0};
    }
    if (received == 0) {
        return {IoStatus::Closed, 0, 0};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {IoStatus::WouldBlock, 0, 0};
    }
    return {IoStatus::Error, 0, errno};
}

IoResult tcp_send_some(int fd, const char *data, std::size_t size) {
    const ssize_t sent = ::send(fd, data, size, MSG_NOSIGNAL);
    if (sent >= 0) {
        return {IoStatus::Ok, static_cast<std::size_t>(sent), 0};
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return {IoStatus::WouldBlock, 0, 0};
    }
    return {IoStatus::Error, 0, errno};
}

/// Poll descriptor owner tag; sessions are addressed by their ws_fd key so
/// an entry erased earlier in the same poll pass is detected on lookup.
struct Owner {
    enum class Kind { listen, wake, shutdown, session_ws, session_upstream };
    Kind kind = Kind::listen;
    int session_key = 0;
};

} // namespace

DevBridgeLoop::DevBridgeLoop(Dependencies dependencies) : dependencies_(std::move(dependencies)) {
    int fds[2] = {-1, -1};
    if (::pipe2(fds, O_NONBLOCK | O_CLOEXEC) == 0) {
        wake_read_ = fds[0];
        wake_write_ = fds[1];
    }
}

DevBridgeLoop::~DevBridgeLoop() {
    // Session descriptors close with the map elements; only the wake pipe
    // is owned directly.
    if (wake_read_ >= 0) {
        ::close(wake_read_);
    }
    if (wake_write_ >= 0) {
        ::close(wake_write_);
    }
}

void DevBridgeLoop::wakeup() noexcept {
    if (wake_write_ >= 0) {
        const char token = 'w';
        // Best-effort by design; consuming the result is what silences
        // glibc's warn_unused_result without relying on cast semantics.
        const ssize_t written = ::write(wake_write_, &token, 1);
        (void)written;
    }
}

void DevBridgeLoop::register_shutdown_fd(int fd) { shutdown_fd_ = fd; }

void DevBridgeLoop::stop_serving() {
    stop_serving_.store(true, std::memory_order_release);
    wakeup();
}

void DevBridgeLoop::queue_ws_frame(std::map<int, Session>::iterator entry,
                                   const std::string &frame) {
    Session &session = entry->second;
    if (session.ws_outbound_sent == session.ws_outbound.size()) {
        session.ws_outbound.clear();
        session.ws_outbound_sent = 0;
    }
    session.ws_outbound.append(frame);
}

bool DevBridgeLoop::queue_ws_binary(std::map<int, Session>::iterator entry,
                                    const std::string &payload) {
    const std::string frame = ws::make_frame(ws::Opcode::binary, payload);
    const std::size_t pending = entry->second.ws_outbound.size() - entry->second.ws_outbound_sent;
    if (pending + frame.size() > dependencies_.max_relay_buffer_bytes) {
        close_session(entry, ws::kCloseInternalError,
                      "relay budget exceeded; the browser is not draining");
        return false;
    }
    queue_ws_frame(entry, frame);
    return true;
}

void DevBridgeLoop::queue_ws_close(std::map<int, Session>::iterator entry, std::uint16_t code,
                                   const std::string &reason) {
    queue_ws_frame(entry, ws::make_close_frame(code, reason));
    entry->second.closing = true;
}

void DevBridgeLoop::drop_session(std::map<int, Session>::iterator entry) { sessions_.erase(entry); }

void DevBridgeLoop::close_session(std::map<int, Session>::iterator entry, std::uint16_t code,
                                  const std::string &reason) {
    queue_ws_close(entry, code, reason);
}

bool DevBridgeLoop::flush_ws(std::map<int, Session>::iterator entry) {
    Session &session = entry->second;
    while (session.ws_outbound_sent < session.ws_outbound.size()) {
        const IoResult result =
            tcp_send_some(session.ws_fd, session.ws_outbound.data() + session.ws_outbound_sent,
                          session.ws_outbound.size() - session.ws_outbound_sent);
        switch (result.status) {
        case IoStatus::Ok:
            session.ws_outbound_sent += result.bytes;
            break;
        case IoStatus::WouldBlock:
            return true;
        case IoStatus::Closed:
        case IoStatus::Error:
            return false;
        }
    }
    session.ws_outbound.clear();
    session.ws_outbound_sent = 0;
    if (session.closing) {
        drop_session(entry);
        return false;
    }
    return true;
}

bool DevBridgeLoop::flush_upstream(std::map<int, Session>::iterator entry) {
    Session &session = entry->second;
    if (!session.upstream.valid()) {
        return true;
    }
    while (session.upstream_outbound_sent < session.upstream_outbound.size()) {
        const IoResult result = session.upstream.write_some(
            session.upstream_outbound.data() + session.upstream_outbound_sent,
            session.upstream_outbound.size() - session.upstream_outbound_sent);
        switch (result.status) {
        case IoStatus::Ok:
            session.upstream_outbound_sent += result.bytes;
            break;
        case IoStatus::WouldBlock:
            return true;
        case IoStatus::Closed:
        case IoStatus::Error:
            return false;
        }
    }
    session.upstream_outbound.clear();
    session.upstream_outbound_sent = 0;
    return true;
}

void DevBridgeLoop::accept_ready() {
    for (;;) {
        const int fd =
            ::accept4(dependencies_.listen_fd, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0) {
            return; // EAGAIN: nothing pending; other errors surface as closed
        }
        if (sessions_.size() >= dependencies_.max_sessions) {
            const std::string rejection = ws::make_http_rejection();
            (void)::send(fd, rejection.data(), rejection.size(), MSG_NOSIGNAL);
            ::close(fd);
            continue;
        }
        Session session;
        session.ws_fd = fd;
        sessions_.emplace(fd, std::move(session));
    }
}

bool DevBridgeLoop::handle_ws_readable(std::map<int, Session>::iterator entry) {
    Session &session = entry->second;
    char chunk[4096];
    bool peer_closed = false;
    bool transport_failed = false;
    for (;;) {
        const IoResult result = tcp_recv_some(session.ws_fd, chunk, sizeof(chunk));
        if (result.status == IoStatus::Ok) {
            session.ws_inbound.append(chunk, result.bytes);
            if (session.ws_inbound.size() > kMaxWsInboundBytes) {
                if (session.phase == Phase::handshake) {
                    const std::string rejection = ws::make_http_rejection();
                    (void)::send(session.ws_fd, rejection.data(), rejection.size(), MSG_NOSIGNAL);
                    drop_session(entry);
                    return false;
                }
                close_session(entry, ws::kCloseMessageTooBig, "inbound exceeds the message cap");
                return true;
            }
            continue;
        }
        if (result.status == IoStatus::WouldBlock) {
            break;
        }
        if (result.status == IoStatus::Closed) {
            peer_closed = true;
        } else {
            transport_failed = true;
        }
        break;
    }
    if (transport_failed) {
        drop_session(entry);
        return false;
    }

    if (session.phase == Phase::handshake) {
        ws::Handshake handshake = ws::try_parse_handshake(session.ws_inbound);
        if (handshake.status == ws::Handshake::Status::need_more_data) {
            return true;
        }
        if (handshake.status == ws::Handshake::Status::reject) {
            const std::string rejection = ws::make_http_rejection();
            (void)::send(session.ws_fd, rejection.data(), rejection.size(), MSG_NOSIGNAL);
            drop_session(entry);
            return false;
        }
        const std::string response = ws::make_handshake_response(handshake.accept_value);
        // The handshake answer is small; a fresh TCP buffer always takes it.
        (void)::send(session.ws_fd, response.data(), response.size(), MSG_NOSIGNAL);
        // Leftover pipelined bytes stay in ws_inbound and are parsed below.

        const int upstream_fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (upstream_fd < 0) {
            drop_session(entry);
            return false;
        }
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (dependencies_.upstream_socket_path.size() >= sizeof(address.sun_path)) {
            ::close(upstream_fd);
            close_session(entry, ws::kCloseInternalError, "upstream socket path too long");
            return true;
        }
        std::memcpy(address.sun_path, dependencies_.upstream_socket_path.c_str(),
                    dependencies_.upstream_socket_path.size() + 1);
        if (::connect(upstream_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ==
            0) {
            session.upstream = mirage::runtime::ipc::IpcStream(upstream_fd);
            session.phase = Phase::relaying;
        } else if (errno == EINPROGRESS) {
            session.connecting_fd = upstream_fd;
            session.phase = Phase::connecting_upstream;
        } else {
            ::close(upstream_fd);
            close_session(entry, ws::kCloseInternalError, "upstream connect failed");
            return true;
        }
    }

    // Frame parsing; a protocol violation or the close handshake finishes
    // the session after its queued bytes flush.
    while (!session.closing) {
        const ws::ParsedFrame parsed = ws::try_parse_frame(session.ws_inbound);
        if (parsed.status == ws::ParsedFrame::Status::need_more_data) {
            break;
        }
        if (parsed.status == ws::ParsedFrame::Status::protocol_error) {
            close_session(entry, parsed.close_code, parsed.reason);
            return true;
        }
        const ws::Frame &frame = parsed.frame;
        if (ws::is_control(frame.opcode)) {
            if (frame.opcode == ws::Opcode::ping) {
                queue_ws_frame(entry, ws::make_frame(ws::Opcode::pong, frame.payload));
                continue;
            }
            if (frame.opcode == ws::Opcode::pong) {
                continue;
            }
            // close: validate the body (0 bytes or code + optional reason),
            // echo the peer's code and close after the flush.
            if (frame.payload.size() == 1) {
                close_session(entry, ws::kCloseProtocolError, "close frame body of one byte");
                return true;
            }
            std::uint16_t code = ws::kCloseNormal;
            if (frame.payload.size() >= 2) {
                const auto high = static_cast<unsigned char>(frame.payload[0]);
                const auto low = static_cast<unsigned char>(frame.payload[1]);
                code = static_cast<std::uint16_t>((high << 8) | low);
            }
            session.peer_close_received = true;
            queue_ws_close(entry, code, "");
            return true;
        }
        const ws::MessageAssembler::Result message = session.assembler.consume(frame);
        if (message.status == ws::MessageAssembler::Result::Status::need_more_data) {
            continue;
        }
        if (message.status == ws::MessageAssembler::Result::Status::protocol_error) {
            close_session(entry, message.close_code, message.reason);
            return true;
        }
        if (message.opcode == ws::Opcode::text) {
            close_session(entry, ws::kCloseUnsupportedData,
                          "the mirage IPC contract is binary-only");
            return true;
        }
        // One WebSocket binary message must be exactly one length-prefixed
        // IPC frame (DEC-012): validate the header against the actual size,
        // then pass the bytes through verbatim.
        if (message.payload.size() < mirage::runtime::ipc::kFrameHeaderBytes) {
            close_session(entry, ws::kCloseProtocolError, "message shorter than a frame header");
            return true;
        }
        const auto b0 = static_cast<unsigned char>(message.payload[0]);
        const auto b1 = static_cast<unsigned char>(message.payload[1]);
        const auto b2 = static_cast<unsigned char>(message.payload[2]);
        const auto b3 = static_cast<unsigned char>(message.payload[3]);
        const std::uint64_t declared =
            static_cast<std::uint64_t>(b0) | (static_cast<std::uint64_t>(b1) << 8) |
            (static_cast<std::uint64_t>(b2) << 16) | (static_cast<std::uint64_t>(b3) << 24);
        if (declared > mirage::runtime::ipc::kMaxFrameBytes) {
            close_session(entry, ws::kCloseMessageTooBig,
                          "declared frame length exceeds the IPC cap");
            return true;
        }
        if (declared != message.payload.size() - mirage::runtime::ipc::kFrameHeaderBytes) {
            close_session(entry, ws::kCloseProtocolError,
                          "frame header does not match the message size");
            return true;
        }
        const std::size_t pending =
            session.upstream_outbound.size() - session.upstream_outbound_sent;
        if (pending + message.payload.size() > dependencies_.max_relay_buffer_bytes) {
            close_session(entry, ws::kCloseInternalError,
                          "relay budget exceeded; the upstream is not draining");
            return true;
        }
        if (session.upstream_outbound_sent == session.upstream_outbound.size()) {
            session.upstream_outbound.clear();
            session.upstream_outbound_sent = 0;
        }
        session.upstream_outbound.append(message.payload);
    }

    if (peer_closed) {
        // The browser finished (or abandoned) the connection; nothing it
        // will still read justifies flushing.
        drop_session(entry);
        return false;
    }
    return true;
}

bool DevBridgeLoop::finish_upstream_connect(std::map<int, Session>::iterator entry) {
    Session &session = entry->second;
    int error = 0;
    socklen_t error_size = sizeof(error);
    if (::getsockopt(session.connecting_fd, SOL_SOCKET, SO_ERROR, &error, &error_size) != 0 ||
        error != 0) {
        const std::string reason =
            "upstream connect failed: " + std::string(std::strerror(error != 0 ? error : errno));
        ::close(session.connecting_fd);
        session.connecting_fd = -1;
        close_session(entry, ws::kCloseInternalError, reason);
        return true;
    }
    const int fd = session.connecting_fd;
    session.connecting_fd = -1;
    session.upstream = mirage::runtime::ipc::IpcStream(fd);
    session.phase = Phase::relaying;
    return true;
}

bool DevBridgeLoop::handle_upstream_readable(std::map<int, Session>::iterator entry) {
    Session &session = entry->second;
    char chunk[4096];
    bool upstream_closed = false;
    bool transport_failed = false;
    for (;;) {
        const IoResult result = session.upstream.read_some(chunk, sizeof(chunk));
        if (result.status == IoStatus::Ok) {
            session.upstream_inbound.append(chunk, result.bytes);
            if (session.upstream_inbound.size() > kMaxUpstreamInboundBytes) {
                close_session(entry, ws::kCloseInternalError, "upstream framing violation");
                return true;
            }
            continue;
        }
        if (result.status == IoStatus::WouldBlock) {
            break;
        }
        if (result.status == IoStatus::Closed) {
            upstream_closed = true;
        } else {
            transport_failed = true;
        }
        break;
    }
    if (transport_failed) {
        close_session(entry, ws::kCloseInternalError, "upstream transport failure");
        return true;
    }
    // Pass every complete frame through; ordering is the upstream's FIFO.
    for (;;) {
        const mirage::runtime::ipc::FrameExtraction extraction =
            mirage::runtime::ipc::try_extract_frame(session.upstream_inbound);
        if (extraction.status == mirage::runtime::ipc::FrameExtract::NeedMoreData) {
            break;
        }
        if (extraction.status == mirage::runtime::ipc::FrameExtract::ProtocolError) {
            close_session(entry, ws::kCloseInternalError, "upstream framing violation");
            return true;
        }
        // make_frame re-renders the 4-byte header byte-identically; the
        // payload itself is untouched (DEC-012 passthrough).
        if (!queue_ws_binary(entry, mirage::runtime::ipc::make_frame(extraction.message))) {
            return false;
        }
    }
    if (upstream_closed) {
        close_session(entry, ws::kCloseGoingAway, "upstream closed");
        return true;
    }
    return true;
}

void DevBridgeLoop::shutdown_flush(Session &session) {
    // Single best-effort pass: deliver what the TCP buffer takes (ideally
    // including the queued close frame), then both transports close with
    // the session map erase in run().
    if (session.phase != Phase::handshake &&
        session.ws_outbound_sent < session.ws_outbound.size()) {
        while (session.ws_outbound_sent < session.ws_outbound.size()) {
            const IoResult result =
                tcp_send_some(session.ws_fd, session.ws_outbound.data() + session.ws_outbound_sent,
                              session.ws_outbound.size() - session.ws_outbound_sent);
            if (result.status != IoStatus::Ok) {
                break;
            }
            session.ws_outbound_sent += result.bytes;
        }
    }
}

void DevBridgeLoop::run(executor::StopToken stop_token) {
    std::vector<pollfd> descriptors;
    std::vector<Owner> owners;

    while (!stop_token.stop_requested() && !stop_serving_.load(std::memory_order_acquire)) {
        descriptors.clear();
        owners.clear();
        if (dependencies_.listen_fd >= 0) {
            descriptors.push_back({dependencies_.listen_fd, POLLIN, 0});
            owners.push_back({Owner::Kind::listen, 0});
        }
        if (wake_read_ >= 0) {
            descriptors.push_back({wake_read_, POLLIN, 0});
            owners.push_back({Owner::Kind::wake, 0});
        }
        if (shutdown_fd_ >= 0) {
            descriptors.push_back({shutdown_fd_, POLLIN, 0});
            owners.push_back({Owner::Kind::shutdown, 0});
        }
        for (auto &entry : sessions_) {
            Session &session = entry.second;
            const int key = entry.first;
            short ws_events = 0;
            if (!session.closing) {
                ws_events |= POLLIN;
            }
            if (session.ws_outbound_sent < session.ws_outbound.size()) {
                ws_events |= POLLOUT;
            }
            if (ws_events != 0) {
                descriptors.push_back({session.ws_fd, ws_events, 0});
                owners.push_back({Owner::Kind::session_ws, key});
            }
            const int upstream_fd =
                session.upstream.valid() ? session.upstream.handle() : session.connecting_fd;
            if (upstream_fd >= 0 && session.phase == Phase::connecting_upstream) {
                descriptors.push_back({upstream_fd, POLLOUT, 0});
                owners.push_back({Owner::Kind::session_upstream, key});
            } else if (upstream_fd >= 0 && !session.closing) {
                short upstream_events = POLLIN;
                if (session.upstream_outbound_sent < session.upstream_outbound.size()) {
                    upstream_events |= POLLOUT;
                }
                descriptors.push_back({upstream_fd, upstream_events, 0});
                owners.push_back({Owner::Kind::session_upstream, key});
            }
        }

        const int ready = ::poll(descriptors.data(), descriptors.size(),
                                 static_cast<int>(dependencies_.poll_timeout_ms));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            report_.clean = false;
            report_.diagnostic = std::string("poll failed: ") + std::strerror(errno);
            break; // poll itself failed; fail loud instead of spinning
        }

        std::size_t remaining = static_cast<std::size_t>(ready);
        for (std::size_t index = 0; index < descriptors.size() && remaining > 0; ++index) {
            const short revents = descriptors[index].revents;
            if (revents == 0) {
                continue;
            }
            --remaining;
            const Owner &owner = owners[index];

            if (owner.kind == Owner::Kind::wake) {
                char buffer[64];
                while (::read(wake_read_, buffer, sizeof(buffer)) > 0) {
                }
                continue;
            }
            if (owner.kind == Owner::Kind::shutdown) {
                char buffer[64];
                while (::read(shutdown_fd_, buffer, sizeof(buffer)) > 0) {
                }
                stop_serving_.store(true, std::memory_order_release);
                continue;
            }
            if (owner.kind == Owner::Kind::listen) {
                if ((revents & POLLIN) != 0) {
                    accept_ready();
                }
                continue;
            }

            auto entry = sessions_.find(owner.session_key);
            if (entry == sessions_.end()) {
                continue; // closed earlier in this pass
            }
            if ((revents & (POLLERR | POLLHUP | POLLNVAL)) != 0 && (revents & POLLIN) == 0 &&
                (revents & POLLOUT) == 0) {
                drop_session(entry);
                continue;
            }
            if ((revents & POLLIN) != 0 && owner.kind == Owner::Kind::session_ws) {
                if (!handle_ws_readable(entry)) {
                    continue;
                }
            }
            if ((revents & POLLOUT) != 0 && owner.kind == Owner::Kind::session_upstream) {
                entry = sessions_.find(owner.session_key);
                if (entry == sessions_.end()) {
                    continue;
                }
                if (entry->second.phase == Phase::connecting_upstream) {
                    if (!finish_upstream_connect(entry)) {
                        continue;
                    }
                }
            }
            if ((revents & POLLIN) != 0 && owner.kind == Owner::Kind::session_upstream) {
                entry = sessions_.find(owner.session_key);
                if (entry == sessions_.end()) {
                    continue;
                }
                if (!handle_upstream_readable(entry)) {
                    continue;
                }
            }
            if ((revents & POLLOUT) != 0 && owner.kind == Owner::Kind::session_ws) {
                entry = sessions_.find(owner.session_key);
                if (entry == sessions_.end()) {
                    continue;
                }
                if (!flush_ws(entry)) {
                    continue;
                }
            }
            // A readable+writable pass may have produced upstream-bound
            // bytes (or freed space); try one immediate flush so a whole
            // request/response round-trip fits into this iteration.
            entry = sessions_.find(owner.session_key);
            if (entry != sessions_.end() && entry->second.upstream.valid()) {
                if (!flush_upstream(entry)) {
                    continue;
                }
            }
            entry = sessions_.find(owner.session_key);
            if (entry != sessions_.end()) {
                (void)flush_ws(entry);
            }
        }
    }

    // Ordered exit: no new data is read; every relaying session gets its
    // close frame and one best-effort flush, then everything closes.
    for (auto entry = sessions_.begin(); entry != sessions_.end(); ++entry) {
        Session &session = entry->second;
        if (session.phase == Phase::connecting_upstream && session.connecting_fd >= 0) {
            ::close(session.connecting_fd);
            session.connecting_fd = -1;
        }
        if (session.phase != Phase::handshake && !session.closing) {
            queue_ws_close(entry, ws::kCloseGoingAway, "bridge shutting down");
        }
        shutdown_flush(session);
    }
    sessions_.clear();
    if (dependencies_.on_exit) {
        dependencies_.on_exit(report_);
    }
}

} // namespace mirage::devbridge
