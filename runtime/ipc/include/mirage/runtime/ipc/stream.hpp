#pragma once

#include <chrono>
#include <cstddef>
#include <string>

namespace mirage::runtime::ipc {

/// Result of one non-blocking read/write on an IPC socket.
enum class IoStatus {
    Ok,         ///< `bytes` transferred; call again for more
    WouldBlock, ///< nothing available / peer not ready; poll and retry
    Closed,     ///< orderly peer close (read) or the socket is shut down
    Error,      ///< transport failure; `os_error` carries errno semantics
};

struct IoResult {
    IoStatus status = IoStatus::Ok;
    std::size_t bytes = 0;
    int os_error = 0; ///< meaningful only for Error
};

/// One connected stream of the Local IPC transport (DEC-007). POSIX Unix
/// domain socket in M1; the Windows named pipe joins this contract with M4.
/// The stream is non-blocking: owners drive it from poll loops, so no
/// operation ever blocks its calling thread.
class IpcStream {
  public:
    IpcStream() = default;
    /// Takes ownership of an already connected, non-blocking socket fd.
    explicit IpcStream(int fd);
    ~IpcStream();
    IpcStream(IpcStream &&other) noexcept;
    IpcStream &operator=(IpcStream &&other) noexcept;
    IpcStream(const IpcStream &) = delete;
    IpcStream &operator=(const IpcStream &) = delete;

    bool valid() const { return fd_ >= 0; }
    /// Pollable handle; meaningful only while valid().
    int handle() const { return fd_; }
    void close();

    IoResult read_some(char *data, std::size_t size);
    IoResult write_some(const char *data, std::size_t size);

  private:
    int fd_ = -1;
};

/// Listening endpoint bound to a socket path. Binding fails closed when the
/// path cannot be prepared; stale-socket takeover is the caller's decision
/// (probe with endpoint_has_listener, then retry the bind).
class IpcListener {
  public:
    IpcListener() = default;
    ~IpcListener();
    IpcListener(IpcListener &&other) noexcept;
    IpcListener &operator=(IpcListener &&other) noexcept;
    IpcListener(const IpcListener &) = delete;
    IpcListener &operator=(IpcListener &) = delete;

    /// Creates the parent directory (0700), removes a leftover socket only
    /// when nothing is listening behind it, binds and listens. On failure
    /// `diagnostic` explains and the returned listener is invalid.
    static IpcListener bind(const std::string &socket_path, std::string &diagnostic);

    bool valid() const { return fd_ >= 0; }
    int handle() const { return fd_; }
    const std::string &socket_path() const { return path_; }
    void close();

    /// Non-blocking accept; an invalid stream with IoStatus::WouldBlock
    /// semantics (valid() == false) means nothing is pending.
    IpcStream accept(std::string &diagnostic);

  private:
    int fd_ = -1;
    std::string path_;
};

/// True when something is accepting connections at `socket_path` (used to
/// tell a live service from a stale socket file before takeover).
bool endpoint_has_listener(const std::string &socket_path, std::chrono::milliseconds probe_timeout);

/// Connects to `socket_path` with a bounded deadline. On failure the
/// returned stream is invalid and `diagnostic` explains (including the
/// "no service listening" case the CLI surfaces).
IpcStream connect_stream(const std::string &socket_path, std::chrono::milliseconds deadline,
                         std::string &diagnostic);

} // namespace mirage::runtime::ipc
