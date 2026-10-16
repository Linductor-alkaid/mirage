// POSIX implementation of the update fetch port (M5-12, DEC-032):
// blocking TCP sockets + a hand-rolled HTTP/1.1 GET. Windows compiles the
// sibling implementation over WinSock (update_fetcher_windows.cpp) — same
// contract, platform boundary inside this module only.

#include <mirage/runtime/update/update_fetcher.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace mirage::runtime::update {
namespace {

constexpr std::uint64_t kMaxBodyBytesHardCap = 2ull * 1024 * 1024 * 1024; // 2 GiB
constexpr int kIoSliceSeconds = 30;

// connect with a bounded timeout (per-address); -1 on failure.
int connect_bounded(const addrinfo *entry) {
    const int fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
    if (fd < 0) {
        return -1;
    }
    // Bounded connect via non-blocking + poll: a dead host must not hang
    // the update for minutes (fail closed within slices).
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    const int rc = ::connect(fd, entry->ai_addr, entry->ai_addrlen);
    if (rc != 0) {
        if (errno != EINPROGRESS) {
            ::close(fd);
            return -1;
        }
        pollfd pfd{fd, POLLOUT, 0};
        if (::poll(&pfd, 1, 10'000) != 1) {
            ::close(fd);
            return -1;
        }
        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0 || so_error != 0) {
            ::close(fd);
            return -1;
        }
    }
    ::fcntl(fd, F_SETFL, flags); // back to blocking for the bounded I/O
    timeval tv{kIoSliceSeconds, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    tv = timeval{kIoSliceSeconds, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    return fd;
}

bool send_all(int fd, const std::string &bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const ssize_t n = ::send(fd, bytes.data() + sent, bytes.size() - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

/// Reads until the head terminator (\r\n\r\n); appends the remainder to
/// body. False on transport failure/overrun.
bool read_head_and_body(int fd, std::string &head, std::string &body, std::uint64_t max_bytes,
                        const std::function<void(std::uint64_t)> &progress,
                        std::uint64_t &received) {
    static const std::string terminator = "\r\n\r\n";
    std::string buffer;
    char chunk[16 * 1024];
    std::size_t head_end = std::string::npos;
    while (head_end == std::string::npos) {
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
        head_end = buffer.find(terminator);
        if (buffer.size() > 1 << 20) { // a head larger than 1 MiB is hostile
            return false;
        }
    }
    if (head_end == std::string::npos) {
        return false;
    }
    head = buffer.substr(0, head_end);
    const std::size_t body_start = head_end + terminator.size();
    body.append(buffer, body_start, std::string::npos);
    received = body.size();
    if (progress) {
        progress(received);
    }
    while (received <= max_bytes) {
        const ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
        if (n == 0) {
            return true; // clean close: body complete
        }
        if (n < 0) {
            return false;
        }
        body.append(chunk, static_cast<std::size_t>(n));
        received = body.size();
        if (progress) {
            progress(received);
        }
        if (received > max_bytes) {
            return false; // fail closed over the budget
        }
    }
    // Trailing bytes beyond the budget are a failure even at clean close.
    return received <= max_bytes;
}

} // namespace

FetchResult http_get(const std::string &host, const std::string &path, std::uint64_t max_bytes,
                     const std::function<void(std::uint64_t)> &progress) {
    FetchResult result;
    const std::uint64_t budget =
        max_bytes == 0 ? kMaxBodyBytesHardCap : std::min(max_bytes, kMaxBodyBytesHardCap);

    // Parse "host" / "host:port".
    std::string node = host;
    std::string service = "80";
    const auto colon = host.rfind(':');
    if (colon != std::string::npos && host.find(':') == colon) {
        node = host.substr(0, colon);
        service = host.substr(colon + 1);
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *list = nullptr;
    if (::getaddrinfo(node.c_str(), service.c_str(), &hints, &list) != 0 || list == nullptr) {
        result.diagnostic = "name resolution failed for " + node;
        return result;
    }

    int fd = -1;
    std::string connect_diagnostic;
    for (const addrinfo *entry = list; entry != nullptr && fd < 0; entry = entry->ai_next) {
        fd = connect_bounded(entry);
        if (fd < 0) {
            connect_diagnostic = "connect failed";
        }
    }
    ::freeaddrinfo(list);
    if (fd < 0) {
        result.diagnostic = connect_diagnostic.empty() ? "connect failed" : connect_diagnostic;
        return result;
    }

    const std::string request = "GET " + path +
                                " HTTP/1.1\r\n"
                                "Host: " +
                                host +
                                "\r\n"
                                "User-Agent: mirage-updater\r\n"
                                "Accept: */*\r\n"
                                "Connection: close\r\n"
                                "\r\n";
    if (!send_all(fd, request)) {
        result.diagnostic = "request send failed";
        ::close(fd);
        return result;
    }

    std::string head;
    std::string body;
    std::uint64_t received = 0;
    const bool read_ok = read_head_and_body(fd, head, body, budget, progress, received);
    ::close(fd);
    if (!read_ok) {
        result.diagnostic = "response read failed (or exceeded the byte budget)";
        return result;
    }

    // Status line: "HTTP/1.x NNN ...".
    if (head.rfind("HTTP/", 0) != 0) {
        result.diagnostic = "malformed HTTP status line";
        return result;
    }
    const auto space1 = head.find(' ');
    const auto space2 = head.find(' ', space1 == std::string::npos ? 0 : space1 + 1);
    if (space1 == std::string::npos || space2 == std::string::npos) {
        result.diagnostic = "malformed HTTP status line";
        return result;
    }
    result.http_status = std::atoi(head.substr(space1 + 1, space2 - space1 - 1).c_str());
    if (result.http_status == 0) {
        result.diagnostic = "malformed HTTP status code";
        return result;
    }
    if (result.http_status / 100 == 3) {
        result.diagnostic = "update channel redirects are not followed (fail closed)";
        return result;
    }
    if (result.http_status != 200) {
        result.diagnostic = "update channel returned HTTP " + std::to_string(result.http_status);
        return result;
    }
    result.ok = true;
    result.body = std::move(body);
    return result;
}

} // namespace mirage::runtime::update
