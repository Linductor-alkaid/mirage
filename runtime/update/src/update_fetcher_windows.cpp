// Windows implementation of the update fetch port (M5-12, DEC-032):
// WinSock2 TCP + the same hand-rolled HTTP/1.1 GET contract as the POSIX
// implementation. Platform boundary inside this module only; every Win32
// type stays here (RULE-01).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <mirage/runtime/update/update_fetcher.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace mirage::runtime::update {
namespace {

constexpr std::uint64_t kMaxBodyBytesHardCap = 2ull * 1024 * 1024 * 1024; // 2 GiB
constexpr int kIoSliceSeconds = 30;

struct WsaSession {
    bool started = false;
    ~WsaSession() {
        if (started) {
            WSACleanup();
        }
    }
};

bool ensure_wsa(WsaSession &session) {
    if (session.started) {
        return true;
    }
    WSADATA data{};
    session.started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    return session.started;
}

bool send_all(SOCKET socket, const std::string &bytes) {
    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const int n = ::send(socket, bytes.data() + static_cast<int>(sent),
                             static_cast<int>(bytes.size() - sent), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

bool read_head_and_body(SOCKET socket, std::string &head, std::string &body,
                        std::uint64_t max_bytes, const std::function<void(std::uint64_t)> &progress,
                        std::uint64_t &received) {
    static const std::string terminator = "\r\n\r\n";
    std::string buffer;
    char chunk[16 * 1024];
    std::size_t head_end = std::string::npos;
    while (head_end == std::string::npos) {
        const int n = ::recv(socket, chunk, sizeof(chunk), 0);
        if (n <= 0) {
            return false;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
        head_end = buffer.find(terminator);
        if (buffer.size() > (1u << 20)) {
            return false;
        }
    }
    head = buffer.substr(0, head_end);
    const std::size_t body_start = head_end + terminator.size();
    body.append(buffer, body_start, std::string::npos);
    received = body.size();
    if (progress) {
        progress(received);
    }
    while (received <= max_bytes) {
        const int n = ::recv(socket, chunk, sizeof(chunk), 0);
        if (n == 0) {
            return true;
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
            return false;
        }
    }
    return received <= max_bytes;
}

} // namespace

FetchResult http_get(const std::string &host, const std::string &path, std::uint64_t max_bytes,
                     const std::function<void(std::uint64_t)> &progress) {
    FetchResult result;
    static WsaSession session;
    if (!ensure_wsa(session)) {
        result.diagnostic = "WSAStartup failed";
        return result;
    }
    const std::uint64_t budget =
        max_bytes == 0 ? kMaxBodyBytesHardCap : std::min(max_bytes, kMaxBodyBytesHardCap);

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

    SOCKET socket_fd = INVALID_SOCKET;
    for (const addrinfo *entry = list; entry != nullptr && socket_fd == INVALID_SOCKET;
         entry = entry->ai_next) {
        socket_fd = ::WSASocketW(entry->ai_family, entry->ai_socktype, entry->ai_protocol, nullptr,
                                 0, WSA_FLAG_OVERLAPPED);
        if (socket_fd == INVALID_SOCKET) {
            socket_fd = ::socket(entry->ai_family, entry->ai_socktype, entry->ai_protocol);
        }
        if (socket_fd == INVALID_SOCKET) {
            continue;
        }
        DWORD timeout = kIoSliceSeconds * 1000;
        ::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout),
                     sizeof(timeout));
        ::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout),
                     sizeof(timeout));
        if (::connect(socket_fd, entry->ai_addr, static_cast<int>(entry->ai_addrlen)) != 0) {
            ::closesocket(socket_fd);
            socket_fd = INVALID_SOCKET;
        }
    }
    ::freeaddrinfo(list);
    if (socket_fd == INVALID_SOCKET) {
        result.diagnostic = "connect failed";
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
    if (!send_all(socket_fd, request)) {
        result.diagnostic = "request send failed";
        ::closesocket(socket_fd);
        return result;
    }

    std::string head;
    std::string body;
    std::uint64_t received = 0;
    const bool read_ok = read_head_and_body(socket_fd, head, body, budget, progress, received);
    ::closesocket(socket_fd);
    if (!read_ok) {
        result.diagnostic = "response read failed (or exceeded the byte budget)";
        return result;
    }

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
