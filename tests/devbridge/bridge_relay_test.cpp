#include "../support/ipc_io.hpp"

#include <bridge_loop.hpp>
#include <websocket.hpp>

#include <executor/blocking_io.hpp>
#include <executor/executor.hpp>
#include <executor/stop_token.hpp>

#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/stream.hpp>

#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// M1.5-03 integration tests for the dev bridge relay loop (DEC-012 decision
// 6). One DevBridgeLoop runs as the Executor's blocking I/O worker against a
// scripted echo upstream on a temporary Unix domain socket; the test main
// thread drives a raw-TCP WebSocket client (handshake, masked frames) and
// asserts the byte-exact, ordered pass-through of length-prefixed IPC frames,
// the close-code contract, session admission and the ordered shutdown. All
// concurrency is owned by the Executor (no std::thread/std::async in the
// test); everything else polls with bounded deadlines.

namespace {

namespace ws = mirage::devbridge::ws;
namespace ipc = mirage::runtime::ipc;
using mirage::devbridge::BridgeRunReport;
using mirage::devbridge::DevBridgeLoop;
using mirage::testing::TempDir;

constexpr auto kBudget = std::chrono::milliseconds{5000};
constexpr auto kStartupBudget = std::chrono::milliseconds{3000};
constexpr const char *kClientKey = "dGhlIHNhbXBsZSBub25jZQ==";
constexpr const char *kExpectedAccept = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";

/// Polls `predicate` on the calling thread until it holds or `budget` ends.
template <typename Predicate> bool wait_for(Predicate predicate, std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    return predicate();
}

/// Number of open descriptors of this process (also counts "." / ".." and
/// the listing directory itself; stable across calls).
int count_open_fds() {
    DIR *dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) {
        return -1;
    }
    int count = 0;
    while (::readdir(dir) != nullptr) {
        ++count;
    }
    ::closedir(dir);
    return count;
}

std::string to_lower(std::string text) {
    for (char &character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return text;
}

/// Case-insensitive single-header lookup in a raw HTTP response.
std::string header_value(const std::string &response, const std::string &name) {
    std::size_t position = 0;
    while (position < response.size()) {
        const std::size_t line_end = response.find("\r\n", position);
        if (line_end == std::string::npos) {
            break;
        }
        const std::string line = response.substr(position, line_end - position);
        position = line_end + 2;
        const std::size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        if (to_lower(line.substr(0, colon)) != name) {
            continue;
        }
        std::size_t begin = colon + 1;
        while (begin < line.size() && (line[begin] == ' ' || line[begin] == '\t')) {
            ++begin;
        }
        std::size_t end = line.size();
        while (end > begin && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
            --end;
        }
        return line.substr(begin, end - begin);
    }
    return "";
}

/// Builds one masked client-to-server WebSocket frame (RFC 6455: the client
/// side must mask; the fixed key is fine for tests).
std::string make_client_frame(std::uint8_t opcode, const std::string &payload, bool fin = true,
                              std::uint32_t mask = 0x6E4B1A07U) {
    std::string out;
    out.push_back(static_cast<char>((fin ? 0x80u : 0x00u) | opcode));
    const std::size_t size = payload.size();
    if (size <= 125) {
        out.push_back(static_cast<char>(0x80u | size));
    } else {
        out.push_back(static_cast<char>(0x80u | 126u));
        out.push_back(static_cast<char>((size >> 8) & 0xFFu));
        out.push_back(static_cast<char>(size & 0xFFu));
    }
    const unsigned char mask_bytes[4] = {static_cast<unsigned char>((mask >> 24) & 0xFFu),
                                         static_cast<unsigned char>((mask >> 16) & 0xFFu),
                                         static_cast<unsigned char>((mask >> 8) & 0xFFu),
                                         static_cast<unsigned char>(mask & 0xFFu)};
    for (const unsigned char mask_byte : mask_bytes) {
        out.push_back(static_cast<char>(mask_byte));
    }
    for (std::size_t index = 0; index < size; ++index) {
        out.push_back(payload[index] ^ static_cast<char>(mask_bytes[index % 4]));
    }
    return out;
}

/// One parsed server-to-client frame (server frames are unmasked).
struct ServerFrameParse {
    enum class Status { need_more, frame, violation };
    Status status = Status::need_more;
    std::size_t consumed = 0;
    std::uint8_t opcode = 0;
    bool fin = true;
    std::string payload;
};

ServerFrameParse parse_server_frame(const std::string &buffer) {
    ServerFrameParse parse;
    if (buffer.size() < 2) {
        return parse;
    }
    const auto byte0 = static_cast<unsigned char>(buffer[0]);
    const auto byte1 = static_cast<unsigned char>(buffer[1]);
    if ((byte0 & 0x70) != 0) {
        parse.status = ServerFrameParse::Status::violation;
        return parse;
    }
    // Server-to-client frames must NOT be masked (RFC 6455 section 5.1).
    if ((byte1 & 0x80) != 0) {
        parse.status = ServerFrameParse::Status::violation;
        return parse;
    }
    parse.fin = (byte0 & 0x80) != 0;
    parse.opcode = static_cast<std::uint8_t>(byte0 & 0x0Fu);
    std::size_t cursor = 2;
    std::uint64_t length = byte1 & 0x7Fu;
    if (length == 126) {
        if (buffer.size() < 4) {
            return parse;
        }
        length = (static_cast<std::uint64_t>(static_cast<unsigned char>(buffer[2])) << 8) |
                 static_cast<unsigned char>(buffer[3]);
        cursor = 4;
    } else if (length == 127) {
        if (buffer.size() < 10) {
            return parse;
        }
        length = 0;
        for (int index = 0; index < 8; ++index) {
            length = (length << 8) | static_cast<unsigned char>(buffer[2 + index]);
        }
        cursor = 10;
    }
    if (length > buffer.size() - cursor) {
        return parse;
    }
    parse.payload = buffer.substr(cursor, static_cast<std::size_t>(length));
    parse.consumed = cursor + static_cast<std::size_t>(length);
    parse.status = ServerFrameParse::Status::frame;
    return parse;
}

struct ServerEvent {
    enum class Kind { message, close, ping, pong };
    Kind kind = Kind::message;
    std::string payload;
    std::uint16_t close_code = 0;
    std::string close_reason;
};

/// Blocking-socket WebSocket client driven from the test main thread; every
/// wait is bounded by a deadline so a misbehaving bridge cannot hang the run.
class WsClient {
  public:
    WsClient() = default;
    ~WsClient() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    WsClient(const WsClient &) = delete;
    WsClient &operator=(const WsClient &) = delete;

    /// Performs the opening handshake exchange and keeps the raw response;
    /// false only on transport failure (any HTTP status is reported).
    bool exchange_handshake(int port, const std::string &key) {
        if (!connect(port)) {
            return false;
        }
        const std::string request =
            "GET /devbridge HTTP/1.1\r\nHost: 127.0.0.1:" + std::to_string(port) +
            "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key +
            "\r\nSec-WebSocket-Version: 13\r\n\r\n";
        if (!send_bytes(request)) {
            return false;
        }
        return read_http_response(kBudget);
    }

    const std::string &response() const { return response_; }

    bool response_is_101() const { return response_.rfind("HTTP/1.1 101", 0) == 0; }

    bool send_frame(std::uint8_t opcode, const std::string &payload, bool fin = true) {
        return send_bytes(make_client_frame(opcode, payload, fin));
    }

    bool send_close(std::uint16_t code, const std::string &reason = "") {
        std::string payload;
        payload.push_back(static_cast<char>((code >> 8) & 0xFFu));
        payload.push_back(static_cast<char>(code & 0xFFu));
        payload += reason;
        return send_frame(0x8, payload);
    }

    /// Pumps the socket until a matching event arrives or `budget` ends.
    bool wait_event(ServerEvent::Kind kind, ServerEvent &out, std::chrono::milliseconds budget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        for (;;) {
            parse_available();
            for (std::size_t index = 0; index < events_.size(); ++index) {
                if (events_[index].kind == kind) {
                    out = events_[index];
                    events_.erase(events_.begin() + static_cast<std::ptrdiff_t>(index));
                    return true;
                }
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) {
                return false;
            }
            ::pollfd descriptor{};
            descriptor.fd = fd_;
            descriptor.events = POLLIN;
            const auto slice = std::min(remaining, std::chrono::milliseconds{200});
            if (::poll(&descriptor, 1, static_cast<int>(slice.count())) <= 0) {
                continue;
            }
            char chunk[4096];
            errno = 0;
            const ssize_t received = ::recv(fd_, chunk, sizeof(chunk), 0);
            if (received > 0) {
                inbound_.append(chunk, static_cast<std::size_t>(received));
            } else if (received == 0) {
                return false; // EOF before the wanted event
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                return false;
            }
        }
    }

    /// True when the peer closes the TCP connection within `budget`.
    bool wait_eof(std::chrono::milliseconds budget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        for (;;) {
            char chunk[256];
            const ssize_t received = ::recv(fd_, chunk, sizeof(chunk), MSG_DONTWAIT);
            if (received == 0) {
                return true;
            }
            if (received > 0) {
                continue; // unexpected data; still waiting for the close
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return false;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) {
                return false;
            }
            ::pollfd descriptor{};
            descriptor.fd = fd_;
            descriptor.events = POLLIN;
            ::poll(&descriptor, 1,
                   static_cast<int>(std::min(remaining, std::chrono::milliseconds{100}).count()));
        }
    }

  private:
    bool connect(int port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd_ < 0) {
            return false;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(static_cast<std::uint16_t>(port));
        if (::connect(fd_, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }
        timeval timeout{};
        timeout.tv_sec = 2;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        return true;
    }

    bool send_bytes(const std::string &bytes) {
        std::size_t sent = 0;
        while (sent < bytes.size()) {
            const ssize_t written =
                ::send(fd_, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
            if (written > 0) {
                sent += static_cast<std::size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR) {
                continue;
            }
            return false;
        }
        return true;
    }

    bool read_http_response(std::chrono::milliseconds budget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        for (;;) {
            const std::size_t header_end = inbound_.find("\r\n\r\n");
            if (header_end != std::string::npos) {
                response_ = inbound_.substr(0, header_end + 4);
                inbound_.erase(0, header_end + 4);
                return true;
            }
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now());
            if (remaining.count() <= 0) {
                return false;
            }
            ::pollfd descriptor{};
            descriptor.fd = fd_;
            descriptor.events = POLLIN;
            if (::poll(&descriptor, 1,
                       static_cast<int>(
                           std::min(remaining, std::chrono::milliseconds{200}).count())) <= 0) {
                continue;
            }
            char chunk[4096];
            errno = 0;
            const ssize_t received = ::recv(fd_, chunk, sizeof(chunk), 0);
            if (received > 0) {
                inbound_.append(chunk, static_cast<std::size_t>(received));
            } else if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                return false;
            }
        }
    }

    void parse_available() {
        for (;;) {
            const ServerFrameParse parse = parse_server_frame(inbound_);
            if (parse.status == ServerFrameParse::Status::need_more) {
                break;
            }
            if (parse.status == ServerFrameParse::Status::violation) {
                inbound_.clear();
                return;
            }
            inbound_.erase(0, parse.consumed);
            ServerEvent event;
            if (parse.opcode >= 0x8) {
                event.payload = parse.payload;
                if (parse.opcode == 0x9) {
                    event.kind = ServerEvent::Kind::ping;
                } else if (parse.opcode == 0xA) {
                    event.kind = ServerEvent::Kind::pong;
                } else {
                    event.kind = ServerEvent::Kind::close;
                    if (parse.payload.size() >= 2) {
                        const auto high = static_cast<unsigned char>(parse.payload[0]);
                        const auto low = static_cast<unsigned char>(parse.payload[1]);
                        event.close_code = static_cast<std::uint16_t>((high << 8) | low);
                        event.close_reason = parse.payload.substr(2);
                    }
                }
            } else if (parse.opcode == 0x0) {
                pending_message_ += parse.payload;
                if (!parse.fin) {
                    continue;
                }
                event.kind = ServerEvent::Kind::message;
                event.payload = pending_message_;
                pending_message_.clear();
            } else if (!parse.fin) {
                pending_message_ = parse.payload;
                continue;
            } else {
                event.kind = ServerEvent::Kind::message;
                event.payload = parse.payload;
            }
            events_.push_back(std::move(event));
        }
    }

    int fd_ = -1;
    std::string inbound_;
    std::string response_;
    std::string pending_message_;
    std::vector<ServerEvent> events_;
};

/// Scripted echo upstream: accepts one connection on a UDS listener, echoes
/// every complete length-prefixed frame back byte-for-byte, and optionally
/// closes the connection after N echoes (upstream-exit scenario).
class FakeUpstream final : public executor::IBlockingIoWorker {
  public:
    struct Options {
        std::string socket_path;
        std::size_t close_after_echoes = 0; // 0 = never close
    };

    explicit FakeUpstream(Options options) : options_(std::move(options)) {}

    bool ready() const { return ready_.load(std::memory_order_acquire); }
    std::size_t echo_count() const { return echoes_.load(std::memory_order_acquire); }

    /// Echoed frame bytes; meaningful after echo_count() observed >= n.
    std::vector<std::string> echo_log() {
        std::lock_guard<std::mutex> lock(log_mutex_);
        return echo_log_;
    }

    void run(executor::StopToken stop_token) override {
        std::string diagnostic;
        ipc::IpcListener listener = ipc::IpcListener::bind(options_.socket_path, diagnostic);
        if (!listener.valid()) {
            ready_.store(true, std::memory_order_release);
            return;
        }
        ipc::IpcStream client;
        std::string inbound;
        std::string outbound;
        std::size_t outbound_sent = 0;
        std::size_t echoes = 0;
        bool close_pending = false;
        ready_.store(true, std::memory_order_release);
        while (!stop_token.stop_requested()) {
            ::pollfd descriptors[2] = {{-1, 0, 0}, {-1, 0, 0}};
            int count = 0;
            if (!client.valid()) {
                descriptors[count].fd = listener.handle();
                descriptors[count].events = POLLIN;
                ++count;
            } else if (!close_pending || outbound_sent < outbound.size()) {
                descriptors[count].fd = client.handle();
                descriptors[count].events = POLLIN;
                if (outbound_sent < outbound.size()) {
                    descriptors[count].events |= POLLOUT;
                }
                ++count;
            }
            if (count == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds{20});
                continue;
            }
            if (::poll(descriptors, static_cast<nfds_t>(count), 20) <= 0) {
                continue;
            }
            if (!client.valid() && (descriptors[0].revents & POLLIN) != 0) {
                client = listener.accept(diagnostic);
                if (client.valid()) {
                    // Fresh per-connection state; a stale close_pending from
                    // the previous session must not kill the new one.
                    close_pending = false;
                    inbound.clear();
                    outbound.clear();
                    outbound_sent = 0;
                }
            }
            if (client.valid() && !close_pending) {
                char chunk[4096];
                for (;;) {
                    const ipc::IoResult result = client.read_some(chunk, sizeof(chunk));
                    if (result.status == ipc::IoStatus::Ok) {
                        inbound.append(chunk, result.bytes);
                        continue;
                    }
                    if (result.status != ipc::IoStatus::WouldBlock) {
                        close_pending = true; // peer gone or broken
                    }
                    break;
                }
                // Echo complete frames verbatim (independent length-prefix
                // parser; no reuse of the implementation under test).
                for (;;) {
                    if (inbound.size() < ipc::kFrameHeaderBytes) {
                        break;
                    }
                    const std::uint32_t declared =
                        static_cast<std::uint32_t>(static_cast<unsigned char>(inbound[0])) |
                        (static_cast<std::uint32_t>(static_cast<unsigned char>(inbound[1])) << 8) |
                        (static_cast<std::uint32_t>(static_cast<unsigned char>(inbound[2])) << 16) |
                        (static_cast<std::uint32_t>(static_cast<unsigned char>(inbound[3])) << 24);
                    if (declared > ipc::kMaxFrameBytes) {
                        inbound.clear();
                        break;
                    }
                    if (inbound.size() < ipc::kFrameHeaderBytes + declared) {
                        break;
                    }
                    const std::string frame_bytes =
                        inbound.substr(0, ipc::kFrameHeaderBytes + declared);
                    inbound.erase(0, frame_bytes.size());
                    outbound += frame_bytes;
                    ++echoes;
                    echoes_.store(echoes, std::memory_order_release);
                    {
                        std::lock_guard<std::mutex> lock(log_mutex_);
                        echo_log_.push_back(frame_bytes);
                    }
                    if (options_.close_after_echoes != 0 && echoes >= options_.close_after_echoes) {
                        close_pending = true;
                    }
                }
            }
            while (client.valid() && outbound_sent < outbound.size()) {
                const ipc::IoResult result = client.write_some(outbound.data() + outbound_sent,
                                                               outbound.size() - outbound_sent);
                if (result.status == ipc::IoStatus::Ok) {
                    outbound_sent += result.bytes;
                    continue;
                }
                if (result.status != ipc::IoStatus::WouldBlock) {
                    close_pending = true;
                }
                break;
            }
            if (client.valid() && close_pending && outbound_sent == outbound.size()) {
                outbound.clear();
                outbound_sent = 0;
                inbound.clear();
                client.close();
            }
        }
    }

    void wakeup() noexcept override {
        // The run loop polls with a 20 ms timeout, so stop requests are seen
        // without a dedicated wake pipe.
    }

  private:
    Options options_;
    std::atomic<bool> ready_{false};
    std::atomic<std::size_t> echoes_{0};
    std::mutex log_mutex_;
    std::vector<std::string> echo_log_;
};

/// Owns one Executor instance plus the bridge listener/worker wiring; the
/// destructor performs the ordered stop (stop_serving, worker join, executor
/// shutdown from the non-worker main thread).
class BridgeFixture {
  public:
    BridgeFixture() { exit_future_ = exit_promise_.get_future(); }
    ~BridgeFixture() { teardown(); }
    BridgeFixture(const BridgeFixture &) = delete;
    BridgeFixture &operator=(const BridgeFixture &) = delete;

    bool initialize() {
        if (initialized_) {
            return true;
        }
        executor::ExecutorConfig config;
        config.max_threads = 2;
        initialized_ = executor_.initialize_ex(config).ok;
        return initialized_;
    }

    DevBridgeLoop *loop() { return loop_; }
    int port() const { return port_; }

    executor::WorkerHandle start_worker(std::unique_ptr<executor::IBlockingIoWorker> worker,
                                        const char *name) {
        executor::BlockingWorkerSpec spec;
        spec.name = name;
        spec.config.thread_name = name;
        spec.worker = std::move(worker);
        executor::WorkerHandle handle = executor_.start_worker(std::move(spec));
        if (handle.started()) {
            extra_workers_.push_back(handle);
        }
        return handle;
    }

    bool start_loop(const std::string &upstream_path, std::size_t max_sessions = 4,
                    std::size_t max_relay_buffer_bytes = 4u * 1024u * 1024u) {
        if (loop_ != nullptr || !initialized_) {
            return false;
        }
        listen_fd_ = bind_loopback(port_);
        if (listen_fd_ < 0) {
            return false;
        }
        DevBridgeLoop::Dependencies dependencies;
        dependencies.listen_fd = listen_fd_;
        dependencies.upstream_socket_path = upstream_path;
        dependencies.max_sessions = max_sessions;
        dependencies.poll_timeout_ms = 50;
        dependencies.max_relay_buffer_bytes = max_relay_buffer_bytes;
        dependencies.on_exit = [this](BridgeRunReport report) {
            exit_promise_.set_value(std::move(report));
        };
        auto owned = std::make_unique<DevBridgeLoop>(std::move(dependencies));
        loop_ = owned.get();
        loop_worker_ = start_worker(std::move(owned), "devbridge-loop");
        return loop_worker_.started();
    }

    bool wait_exit(BridgeRunReport &report) {
        if (exit_future_.wait_for(std::chrono::seconds{5}) != std::future_status::ready) {
            return false;
        }
        report = exit_future_.get();
        return true;
    }

    void teardown() {
        if (torn_down_) {
            return;
        }
        torn_down_ = true;
        if (loop_ != nullptr) {
            loop_->stop_serving();
        }
        if (loop_worker_.started()) {
            loop_worker_.stop();
        }
        for (executor::WorkerHandle &worker : extra_workers_) {
            if (worker.started()) {
                worker.stop();
            }
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
        if (initialized_) {
            executor_.shutdown(true);
        }
    }

  private:
    int bind_loopback(int &port_out) {
        const int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
        if (fd < 0) {
            return -1;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
            ::listen(fd, 16) != 0) {
            ::close(fd);
            return -1;
        }
        sockaddr_in resolved{};
        socklen_t resolved_size = sizeof(resolved);
        if (::getsockname(fd, reinterpret_cast<sockaddr *>(&resolved), &resolved_size) != 0) {
            ::close(fd);
            return -1;
        }
        port_out = ntohs(resolved.sin_port);
        return fd;
    }

    executor::Executor executor_;
    bool initialized_ = false;
    bool torn_down_ = false;
    int listen_fd_ = -1;
    int port_ = 0;
    DevBridgeLoop *loop_ = nullptr;
    executor::WorkerHandle loop_worker_;
    std::vector<executor::WorkerHandle> extra_workers_;
    std::promise<BridgeRunReport> exit_promise_;
    std::future<BridgeRunReport> exit_future_;
};

/// Starts one bridge against a fresh echo upstream in `dir`.
bool start_echo_relay(TempDir &dir, BridgeFixture &bridge, FakeUpstream *&fake,
                      std::size_t close_after_echoes = 0, std::size_t max_sessions = 4,
                      std::size_t max_relay_buffer_bytes = 4u * 1024u * 1024u) {
    if (!bridge.initialize()) {
        return false;
    }
    const std::string upstream_path = (dir.root() / "upstream.sock").string();
    auto worker =
        std::make_unique<FakeUpstream>(FakeUpstream::Options{upstream_path, close_after_echoes});
    fake = worker.get();
    if (!bridge.start_worker(std::move(worker), "fake-upstream").started()) {
        return false;
    }
    if (!bridge.start_loop(upstream_path, max_sessions, max_relay_buffer_bytes)) {
        return false;
    }
    return wait_for([&] { return fake->ready(); }, kStartupBudget);
}

// --- scenarios -------------------------------------------------------------------

/// Scenario 1+2: handshake accept value, byte-identical one-frame pass-through
/// and ordered multi-frame relay; plus ping/pong payload echo (scenario 3a).
void test_relay_round_trip_order_and_ping() {
    TempDir dir;
    BridgeFixture bridge;
    FakeUpstream *fake = nullptr;
    MIRAGE_CHECK(start_echo_relay(dir, bridge, fake));

    WsClient client;
    MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
    MIRAGE_CHECK(client.response_is_101());
    MIRAGE_CHECK(header_value(client.response(), "sec-websocket-accept") == kExpectedAccept);

    // One binary message carrying exactly one IPC frame, byte-identical both
    // ways.
    const std::string payload = "relay-payload-1";
    const std::string frame_bytes = ipc::make_frame(payload);
    MIRAGE_CHECK(client.send_frame(0x2, frame_bytes));
    MIRAGE_CHECK(wait_for([&] { return fake->echo_count() >= 1; }, kBudget));
    const std::vector<std::string> echoed = fake->echo_log();
    MIRAGE_CHECK(echoed.size() == 1);
    MIRAGE_CHECK(!echoed.empty() && echoed.front() == frame_bytes);
    ServerEvent event;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::message, event, kBudget));
    MIRAGE_CHECK(event.payload == frame_bytes);

    // Six frames out of order-safe FIFO: answers come back in order and
    // byte-identical.
    std::vector<std::string> sent;
    for (int index = 0; index < 6; ++index) {
        const std::string frame = ipc::make_frame("order-" + std::to_string(index));
        sent.push_back(frame);
        MIRAGE_CHECK(client.send_frame(0x2, frame));
    }
    for (int index = 0; index < 6; ++index) {
        ServerEvent answer;
        MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::message, answer, kBudget));
        MIRAGE_CHECK(answer.payload == sent[static_cast<std::size_t>(index)]);
    }
    MIRAGE_CHECK(fake->echo_count() == 7);

    // Ping is answered with a pong carrying the same payload.
    MIRAGE_CHECK(client.send_frame(0x9, "heartbeat"));
    ServerEvent pong;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::pong, pong, kBudget));
    MIRAGE_CHECK(pong.payload == "heartbeat");
}

/// Scenario 3 (b/c): text messages close with 1003; a binary message whose
/// length prefix does not match its size closes with 1002; a declared frame
/// above the 1 MiB IPC cap closes with 1009; a message shorter than a frame
/// header closes with 1002.
void test_framing_violations() {
    const auto expect_close = [](WsClient &client, std::uint16_t expected, const char *what) {
        ServerEvent close;
        if (!client.wait_event(ServerEvent::Kind::close, close, kBudget)) {
            std::fprintf(stderr, "violation[%s]: no close frame within budget\n", what);
            return false;
        }
        if (close.close_code != expected) {
            std::fprintf(stderr, "violation[%s]: got close %u reason='%s'\n", what,
                         close.close_code, close.close_reason.c_str());
        }
        return close.close_code == expected;
    };
    TempDir dir;
    BridgeFixture bridge;
    FakeUpstream *fake = nullptr;
    MIRAGE_CHECK(start_echo_relay(dir, bridge, fake));

    {
        WsClient client;
        MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
        MIRAGE_CHECK(client.response_is_101());
        MIRAGE_CHECK(client.send_frame(0x1, "not binary"));
        MIRAGE_CHECK(expect_close(client, 1003, "text"));
    }
    {
        WsClient client;
        MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
        MIRAGE_CHECK(client.response_is_101());
        std::string message;
        message.push_back(static_cast<char>(16));
        message.append("\0\0\0", 3);
        message.append(8, 'p'); // declared 16, actual 8
        MIRAGE_CHECK(client.send_frame(0x2, message));
        MIRAGE_CHECK(expect_close(client, 1002, "header-mismatch"));
    }
    {
        WsClient client;
        MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
        MIRAGE_CHECK(client.response_is_101());
        const std::uint32_t declared = static_cast<std::uint32_t>(ipc::kMaxFrameBytes) + 1;
        std::string message;
        message.push_back(static_cast<char>(declared & 0xFFu));
        message.push_back(static_cast<char>((declared >> 8) & 0xFFu));
        message.push_back(static_cast<char>((declared >> 16) & 0xFFu));
        message.push_back(static_cast<char>((declared >> 24) & 0xFFu));
        MIRAGE_CHECK(client.send_frame(0x2, message));
        MIRAGE_CHECK(expect_close(client, 1009, "declared-over-cap"));
    }
    {
        WsClient client;
        MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
        MIRAGE_CHECK(client.response_is_101());
        MIRAGE_CHECK(client.send_frame(0x2, "ab"));
        MIRAGE_CHECK(expect_close(client, 1002, "shorter-than-header"));
    }
}

/// Scenario 4: the client's close(1000) is echoed with the same code and the
/// bridge finishes the TCP connection. Also watches the process descriptor
/// count across two full session lifecycles: every dropped session must close
/// its sockets, not leak them.
void test_close_handshake_and_descriptor_budget() {
    TempDir dir;
    BridgeFixture bridge;
    FakeUpstream *fake = nullptr;
    MIRAGE_CHECK(start_echo_relay(dir, bridge, fake));

    const int fds_before = count_open_fds();
    MIRAGE_CHECK(fds_before > 0);
    for (int round = 0; round < 2; ++round) {
        WsClient client;
        MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
        MIRAGE_CHECK(client.response_is_101());
        MIRAGE_CHECK(client.send_close(1000));
        ServerEvent close;
        MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::close, close, kBudget));
        MIRAGE_CHECK(close.close_code == 1000);
        // RFC 6455 section 7.1.1: the server closes the TCP connection first
        // after echoing the close.
        MIRAGE_CHECK(client.wait_eof(std::chrono::milliseconds{2000}));
    }
    // The echo worker releases its accepted descriptor one poll cycle after
    // the bridge closed the session side, so the count needs a brief settle
    // window; a real descriptor leak never converges and still fails here.
    MIRAGE_CHECK(
        wait_for([&] { return count_open_fds() == fds_before; }, std::chrono::milliseconds{2000}));
    const int fds_after = count_open_fds();
    std::fprintf(stderr, "descriptor budget: before=%d after=%d\n", fds_before, fds_after);
    MIRAGE_CHECK(fds_after == fds_before);
}

/// Scenario 5: an unreachable upstream path lets the handshake succeed but
/// the first upstream connect fails, answered with close 1011.
void test_upstream_missing_closes_1011() {
    TempDir dir;
    const std::string absent_path = (dir.root() / "absent.sock").string();
    BridgeFixture bridge;
    MIRAGE_CHECK(bridge.initialize());
    MIRAGE_CHECK(bridge.start_loop(absent_path));

    WsClient client;
    MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
    MIRAGE_CHECK(client.response_is_101());
    ServerEvent close;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::close, close, kBudget));
    MIRAGE_CHECK(close.close_code == 1011);
}

/// Scenario 6: a per-direction relay budget below the frame size closes the
/// session explicitly with 1011 when the upstream answer would overflow the
/// browser-bound buffer (no crash, no silent drop).
void test_relay_budget_trips_1011() {
    TempDir dir;
    BridgeFixture bridge;
    FakeUpstream *fake = nullptr;
    // Message (1000 B) fits the budget; its echoed WS frame (1004 B) does not.
    MIRAGE_CHECK(start_echo_relay(dir, bridge, fake, 0, 4, 1002));

    WsClient client;
    MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
    MIRAGE_CHECK(client.response_is_101());
    const std::string frame = ipc::make_frame(std::string(996, 'e'));
    MIRAGE_CHECK(frame.size() == 1000);
    MIRAGE_CHECK(client.send_frame(0x2, frame));
    ServerEvent close;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::close, close, kBudget));
    MIRAGE_CHECK(close.close_code == 1011);
}

/// Scenario 8: above max_sessions the extra connection is answered with a
/// plain HTTP 400 rejection instead of a WebSocket upgrade, and the admitted
/// session keeps relaying.
void test_max_sessions_rejects_overflow() {
    TempDir dir;
    BridgeFixture bridge;
    FakeUpstream *fake = nullptr;
    MIRAGE_CHECK(start_echo_relay(dir, bridge, fake, 0, 1));

    WsClient first;
    MIRAGE_CHECK(first.exchange_handshake(bridge.port(), kClientKey));
    MIRAGE_CHECK(first.response_is_101());
    MIRAGE_CHECK(first.send_frame(0x2, ipc::make_frame("first-session")));
    MIRAGE_CHECK(wait_for([&] { return fake->echo_count() >= 1; }, kBudget));
    ServerEvent event;
    MIRAGE_CHECK(first.wait_event(ServerEvent::Kind::message, event, kBudget));
    MIRAGE_CHECK(event.payload == ipc::make_frame("first-session"));

    WsClient second;
    MIRAGE_CHECK(second.exchange_handshake(bridge.port(), kClientKey));
    MIRAGE_CHECK(!second.response_is_101());
    MIRAGE_CHECK(second.response().rfind("HTTP/1.1 400", 0) == 0);

    // The admitted session is unaffected by the rejection.
    MIRAGE_CHECK(first.send_frame(0x2, ipc::make_frame("still-alive")));
    ServerEvent after;
    MIRAGE_CHECK(first.wait_event(ServerEvent::Kind::message, after, kBudget));
    MIRAGE_CHECK(after.payload == ipc::make_frame("still-alive"));
}

/// Upstream exits mid-session: after the pending echo the bridge answers with
/// close 1001 "upstream closed".
void test_upstream_close_relays_1001() {
    TempDir dir;
    BridgeFixture bridge;
    FakeUpstream *fake = nullptr;
    MIRAGE_CHECK(start_echo_relay(dir, bridge, fake, 1));

    WsClient client;
    MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
    MIRAGE_CHECK(client.response_is_101());
    const std::string frame = ipc::make_frame("last-echo");
    MIRAGE_CHECK(client.send_frame(0x2, frame));
    ServerEvent event;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::message, event, kBudget));
    MIRAGE_CHECK(event.payload == frame);
    ServerEvent close;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::close, close, kBudget));
    MIRAGE_CHECK(close.close_code == 1001);
    MIRAGE_CHECK(close.close_reason == "upstream closed");
}

/// Scenario 7: stop_serving() ends run(), reports clean=true, flushes a
/// close 1001 "bridge shutting down" to the open session, and the executor
/// shutdown from the main thread returns.
void test_stop_serving_clean_exit() {
    TempDir dir;
    BridgeFixture bridge;
    FakeUpstream *fake = nullptr;
    MIRAGE_CHECK(start_echo_relay(dir, bridge, fake));

    WsClient client;
    MIRAGE_CHECK(client.exchange_handshake(bridge.port(), kClientKey));
    MIRAGE_CHECK(client.response_is_101());
    MIRAGE_CHECK(client.send_frame(0x2, ipc::make_frame("before-stop")));
    ServerEvent event;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::message, event, kBudget));
    MIRAGE_CHECK(event.payload == ipc::make_frame("before-stop"));

    bridge.loop()->stop_serving();
    BridgeRunReport report;
    MIRAGE_CHECK(bridge.wait_exit(report));
    MIRAGE_CHECK(report.clean);
    MIRAGE_CHECK(report.diagnostic.empty());

    // Ordered exit flushed the goodbye close frame to the live session.
    ServerEvent close;
    MIRAGE_CHECK(client.wait_event(ServerEvent::Kind::close, close, kBudget));
    MIRAGE_CHECK(close.close_code == 1001);
    MIRAGE_CHECK(close.close_reason == "bridge shutting down");

    // If worker stop or executor shutdown deadlocked, the test would time
    // out; run them here explicitly as part of the contract.
    bridge.teardown();
}

} // namespace

int main() {
    test_relay_round_trip_order_and_ping();
    test_framing_violations();
    test_close_handshake_and_descriptor_budget();
    test_upstream_missing_closes_1011();
    test_relay_budget_trips_1011();
    test_max_sessions_rejects_overflow();
    test_upstream_close_relays_1001();
    test_stop_serving_clean_exit();
    return mirage::testing::finish("bridge_relay_test");
}
