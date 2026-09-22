#include <mirage/runtime/ipc/stream.hpp>

#include <mirage/runtime/ipc/endpoint.hpp>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>

namespace mirage::runtime::ipc {
namespace {

constexpr std::size_t kMaxSunPath = sizeof(sockaddr_un::sun_path);

IoStatus status_from_errno(int error, bool writing) {
    if (error == EAGAIN || error == EWOULDBLOCK) {
        return IoStatus::WouldBlock;
    }
    if (error == EPIPE || error == ECONNRESET) {
        return IoStatus::Closed;
    }
    if (!writing && error == ECONNREFUSED) {
        return IoStatus::Closed;
    }
    return IoStatus::Error;
}

} // namespace

IpcStream IpcStream::adopt_native(std::intptr_t native) { return IpcStream(native); }

IpcStream::~IpcStream() { close(); }

IpcStream::IpcStream(IpcStream &&other) noexcept
    : native_(std::exchange(other.native_, kInvalidTransport)) {}

IpcStream &IpcStream::operator=(IpcStream &&other) noexcept {
    if (this != &other) {
        close();
        native_ = std::exchange(other.native_, kInvalidTransport);
    }
    return *this;
}

void IpcStream::close() {
    if (native_ != kInvalidTransport) {
        ::close(static_cast<int>(native_));
        native_ = kInvalidTransport;
    }
}

IoResult IpcStream::read_some(char *data, std::size_t size) {
    IoResult result;
    if (!valid() || size == 0) {
        return result;
    }
    const ssize_t read_bytes = ::read(static_cast<int>(native_), data, size);
    if (read_bytes > 0) {
        result.bytes = static_cast<std::size_t>(read_bytes);
        return result;
    }
    if (read_bytes == 0) {
        result.status = IoStatus::Closed;
        return result;
    }
    result.status = status_from_errno(errno, false);
    result.os_error = errno;
    return result;
}

IoResult IpcStream::write_some(const char *data, std::size_t size) {
    IoResult result;
    if (!valid() || size == 0) {
        return result;
    }
    // send(MSG_NOSIGNAL), not write(): a peer that vanished between the
    // poll pass and this write must surface as a Closed status here, never
    // as a process-killing SIGPIPE in embedders that did not ignore it.
    const ssize_t written = ::send(static_cast<int>(native_), data, size, MSG_NOSIGNAL);
    if (written >= 0) {
        result.bytes = static_cast<std::size_t>(written);
        return result;
    }
    result.status = status_from_errno(errno, true);
    result.os_error = errno;
    return result;
}

IpcListener::~IpcListener() { close(); }

IpcListener::IpcListener(IpcListener &&other) noexcept
    : fd_(std::exchange(other.fd_, -1)), address_(std::move(other.address_)) {}

IpcListener &IpcListener::operator=(IpcListener &&other) noexcept {
    if (this != &other) {
        close();
        fd_ = std::exchange(other.fd_, -1);
        address_ = std::move(other.address_);
    }
    return *this;
}

bool IpcListener::valid() const { return fd_ >= 0; }

std::intptr_t IpcListener::handle() const { return static_cast<std::intptr_t>(fd_); }

void IpcListener::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    // Ordered shutdown removes the socket file so the next start does not
    // mistake this dead endpoint for a live one.
    if (!address_.empty()) {
        ::unlink(address_.c_str());
        address_.clear();
    }
}

IpcListener IpcListener::bind(const std::string &socket_path, std::string &diagnostic) {
    IpcListener listener;
    if (socket_path.empty() || socket_path.size() >= kMaxSunPath) {
        diagnostic =
            "socket path is empty or exceeds " + std::to_string(kMaxSunPath - 1) + " bytes";
        return listener;
    }
    const std::string directory = socket_directory(socket_path);
    if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        diagnostic = "cannot create socket directory '" + directory + "': " + std::strerror(errno);
        return listener;
    }
    // A leftover socket file from a crashed process is only taken over when
    // nothing answers behind it (DEC-007).
    struct stat stat_buffer {};
    if (::stat(socket_path.c_str(), &stat_buffer) == 0) {
        if (endpoint_has_listener(socket_path, std::chrono::milliseconds{500})) {
            diagnostic = "another service is already listening at '" + socket_path + "'";
            return listener;
        }
        ::unlink(socket_path.c_str());
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        diagnostic = std::string("socket() failed: ") + std::strerror(errno);
        return listener;
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), kMaxSunPath - 1);
    if (::bind(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        diagnostic = "bind('" + socket_path + "') failed: " + std::strerror(errno);
        ::close(fd);
        return listener;
    }
    // Defense in depth next to the 0700 directory: connect() requires write
    // access to the socket file itself.
    (void)::chmod(socket_path.c_str(), 0600);
    if (::listen(fd, 16) != 0) {
        diagnostic = "listen('" + socket_path + "') failed: " + std::strerror(errno);
        ::close(fd);
        ::unlink(socket_path.c_str());
        return listener;
    }
    listener.fd_ = fd;
    listener.address_ = socket_path;
    return listener;
}

IpcStream IpcListener::accept(std::string &diagnostic) {
    const int fd = ::accept4(fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (fd >= 0) {
        return IpcStream{static_cast<std::intptr_t>(fd)};
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        diagnostic = std::string("accept() failed: ") + std::strerror(errno);
    }
    return IpcStream{};
}

bool endpoint_has_listener(const std::string &socket_path,
                           std::chrono::milliseconds probe_timeout) {
    std::string diagnostic;
    IpcStream probe = connect_stream(socket_path, probe_timeout, diagnostic);
    if (probe.valid()) {
        // The probe connection is dropped immediately; the listener only
        // sees a connect and is built to tolerate that.
        probe.close();
        return true;
    }
    // Optimistic-conservative split: a refused or absent endpoint is free to
    // take over; anything else (permission, filesystem trouble) keeps the
    // file in place.
    return diagnostic.find("connection refused") == std::string::npos &&
           diagnostic.find("no such file") == std::string::npos;
}

IpcStream connect_stream(const std::string &socket_path, std::chrono::milliseconds deadline,
                         std::string &diagnostic) {
    if (socket_path.empty() || socket_path.size() >= kMaxSunPath) {
        diagnostic =
            "socket path is empty or exceeds " + std::to_string(kMaxSunPath - 1) + " bytes";
        return IpcStream{};
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        diagnostic = std::string("socket() failed: ") + std::strerror(errno);
        return IpcStream{};
    }
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::strncpy(address.sun_path, socket_path.c_str(), kMaxSunPath - 1);
    if (::connect(fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 &&
        errno != EINPROGRESS) {
        if (errno == ECONNREFUSED || errno == ENOENT) {
            diagnostic = std::string("connection refused: ") + std::strerror(errno);
        } else {
            diagnostic =
                std::string("connect('") + socket_path + "') failed: " + std::strerror(errno);
        }
        ::close(fd);
        return IpcStream{};
    }
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = POLLOUT;
        const int ready = ::poll(&descriptor, 1, 50);
        if (ready > 0) {
            int pending_error = 0;
            socklen_t error_size = sizeof(pending_error);
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &pending_error, &error_size) != 0 ||
                pending_error != 0) {
                diagnostic = std::string("connect('") + socket_path + "') failed: " +
                             std::strerror(pending_error != 0 ? pending_error : errno);
                ::close(fd);
                return IpcStream{};
            }
            return IpcStream{static_cast<std::intptr_t>(fd)};
        }
        if (std::chrono::steady_clock::now() - start >= deadline) {
            diagnostic = "connect('" + socket_path + "') timed out";
            ::close(fd);
            return IpcStream{};
        }
    }
}

} // namespace mirage::runtime::ipc
