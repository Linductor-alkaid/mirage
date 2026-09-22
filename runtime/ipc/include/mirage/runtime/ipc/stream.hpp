#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mirage::runtime::ipc {

/// Result of one non-blocking read/write on an IPC connection.
enum class IoStatus {
    Ok,         ///< `bytes` transferred; call again for more
    WouldBlock, ///< nothing available / peer not ready; poll and retry
    Closed,     ///< orderly peer close (read) or the connection is shut down
    Error,      ///< transport failure; `os_error` carries OS semantics
};

struct IoResult {
    IoStatus status = IoStatus::Ok;
    std::size_t bytes = 0;
    int os_error = 0; ///< meaningful only for Error
};

/// The Local IPC transport (DEC-007): a POSIX Unix domain socket stream on
/// Linux (stream_posix.cpp), a named pipe stream on Windows
/// (stream_windows.cpp). Both realize the same contract: operations are
/// non-blocking and never wait on the peer — a read/write that would block
/// reports WouldBlock instead, so owners drive readiness from their own
/// loops (poll on POSIX, PeekNamedPipe / overlapped zero-waits on Windows).
///
/// `handle()` is an opaque transport token (the fd bits on POSIX, the pipe
/// HANDLE bits on Windows). It is not valid to interpret, duplicate or
/// close it through anything but this class; it exists so a platform loop
/// can correlate readiness with a connection without reaching inside.
class IpcStream {
  public:
    IpcStream() = default;
    ~IpcStream();
    IpcStream(IpcStream &&other) noexcept;
    IpcStream &operator=(IpcStream &&other) noexcept;
    IpcStream(const IpcStream &) = delete;
    IpcStream &operator=(const IpcStream &) = delete;

    bool valid() const { return native_ != kInvalidTransport; }
    /// Opaque transport token; meaningful only while valid().
    std::intptr_t handle() const { return native_; }
    void close();

    IoResult read_some(char *data, std::size_t size);
    IoResult write_some(const char *data, std::size_t size);

  private:
    friend class IpcListener;
    friend IpcStream connect_stream(const std::string &address, std::chrono::milliseconds deadline,
                                    std::string &diagnostic);
    /// Builds a connected stream around a transport-native handle (the fd
    /// on POSIX, the pipe HANDLE on Windows). Only the transport factories
    /// (accept / connect_stream) construct streams.
    explicit IpcStream(std::intptr_t native) : native_(native) {}

#ifdef _WIN32
    static constexpr std::intptr_t kInvalidTransport = 0; // null HANDLE
    /// Windows only: the stream's overlapped I/O state (completion events,
    /// the at-most-one in-flight write and its owned buffer). POSIX keeps
    /// no per-stream state beyond the fd. Declaration order matters: the
    /// Windows state precedes the native handle.
    void *state_ = nullptr;
    std::intptr_t native_ = kInvalidTransport;
#else
    static constexpr std::intptr_t kInvalidTransport = -1; // no fd
    std::intptr_t native_ = kInvalidTransport;
#endif
};

/// Listening endpoint bound to an IPC address. Binding fails closed when
/// the address cannot be prepared; takeover of a leftover endpoint is the
/// caller's decision (probe with endpoint_has_listener, then retry the
/// bind). On Windows the listener owns the one idle named-pipe instance and
/// recycles a fresh one per accepted connection.
class IpcListener {
  public:
    IpcListener() = default;
    ~IpcListener();
    IpcListener(IpcListener &&other) noexcept;
    IpcListener &operator=(IpcListener &&other) noexcept;
    IpcListener(const IpcListener &) = delete;
    IpcListener &operator=(IpcListener &) = delete;

    /// Creates the transport endpoint. On POSIX the parent directory is
    /// created (0700) and a leftover socket file is removed only when
    /// nothing is listening behind it; on Windows the address must be a
    /// named pipe path (`\\.\pipe\...`) and the first-instance flag makes a
    /// second listener on the same name fail. On failure `diagnostic`
    /// explains and the returned listener is invalid.
    static IpcListener bind(const std::string &address, std::string &diagnostic);

    bool valid() const;
    /// Opaque transport token; meaningful only while valid().
    std::intptr_t handle() const;
    const std::string &socket_path() const { return address_; }
    void close();

    /// Non-blocking accept; an invalid stream means nothing is pending.
    IpcStream accept(std::string &diagnostic);

  private:
#ifdef _WIN32
    void *state_ = nullptr; // owns the listener's window-of-implementation state
#else
    int fd_ = -1;
#endif
    std::string address_;
};

/// True when something is accepting connections at `address` (used to tell
/// a live service from a leftover endpoint before takeover).
bool endpoint_has_listener(const std::string &address, std::chrono::milliseconds probe_timeout);

/// Connects to `address` with a bounded deadline. On failure the returned
/// stream is invalid and `diagnostic` explains (including the "no service
/// listening" case the CLI surfaces).
IpcStream connect_stream(const std::string &address, std::chrono::milliseconds deadline,
                         std::string &diagnostic);

} // namespace mirage::runtime::ipc
