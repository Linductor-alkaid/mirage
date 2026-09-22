#include <mirage/runtime/ipc/stream.hpp>

#include <mirage/runtime/ipc/endpoint.hpp>

// The Win32 surface is included macro-neutral (DEC-017 decision 5): the
// named pipe APIs live in windows.h / shellapi-free core, all string APIs
// are the explicit W forms.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace mirage::runtime::ipc {
namespace {

/// Per-direction pipe buffer (DEC-007): plenty for the protocol's frames;
/// writes that would exceed the free buffer space are queued inside the
/// stream instead of blocking the loop.
constexpr DWORD kPipeBufferBytes = 64 * 1024;

constexpr wchar_t kPipePrefix[] = L"\\\\.\\pipe\\";

std::optional<std::wstring> wide_from_utf8(const std::string &text) {
    if (text.empty()) {
        return std::wstring{};
    }
    const int size = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                           static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return std::nullopt;
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                              static_cast<int>(text.size()), out.data(), size);
    if (written <= 0) {
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// Per-stream overlapped I/O state (IpcStream::state_ on Windows): the two
/// completion events. Neither direction ever leaves an operation behind —
/// reads are gated by PeekNamedPipe, and an operation that queues is
/// cancelled and reaped inside the very same call, reporting exactly the
/// bytes that were truly transferred (the POSIX WouldBlock contract; the
/// caller always resumes from the reported cursor).
struct StreamState {
    HANDLE read_event = nullptr;  // auto-reset; the read completion
    HANDLE write_event = nullptr; // auto-reset; the write completion
};

/// Aborts an in-flight overlapped operation and reaps it. A cancelled
/// operation completes promptly on a local pipe; the wait keeps the
/// OVERLAPPED (whose buffer belongs to this call's stack) alive until it
/// did, so partial progress is reported, never lost and never duplicated.
void cancel_and_reap(HANDLE handle, OVERLAPPED *overlapped) {
    ::CancelIoEx(handle, overlapped);
    DWORD abandoned = 0;
    ::GetOverlappedResult(handle, overlapped, &abandoned, TRUE);
}

/// Issues one overlapped write. Returns ERROR_SUCCESS when it completed
/// synchronously (read the count with GetOverlappedResult), ERROR_IO_PENDING
/// when it queued, or the immediate failure code.
DWORD issue_write(HANDLE handle, OVERLAPPED *overlapped, HANDLE write_event, const char *data,
                  std::size_t size) {
    overlapped->hEvent = write_event;
    if (::WriteFile(handle, data, static_cast<DWORD>(size), nullptr, overlapped) != 0) {
        return ERROR_SUCCESS;
    }
    return ::GetLastError();
}

/// Issues one overlapped read. Returns ERROR_SUCCESS on synchronous
/// completion, ERROR_IO_PENDING when queued (the caller cancels), or the
/// immediate failure code.
DWORD issue_read(HANDLE handle, OVERLAPPED *overlapped, HANDLE read_event, char *data,
                 std::size_t size) {
    overlapped->hEvent = read_event;
    if (::ReadFile(handle, data, static_cast<DWORD>(size), nullptr, overlapped) != 0) {
        return ERROR_SUCCESS;
    }
    return ::GetLastError();
}

/// Creates one named-pipe server instance. `first_instance` carries the
/// takeover discipline: only the very first bind of a pipe name may claim
/// it, so a second service on the same address fails access-denied.
HANDLE create_pipe_instance(const std::wstring &name, bool first_instance) {
    SECURITY_ATTRIBUTES inherit_none{};
    inherit_none.nLength = sizeof(inherit_none);
    inherit_none.bInheritHandle = FALSE;
    return ::CreateNamedPipeW(name.c_str(),
                              PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED |
                                  (first_instance ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0),
                              PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                              PIPE_UNLIMITED_INSTANCES, kPipeBufferBytes, kPipeBufferBytes, 0,
                              &inherit_none);
}

/// Per-listener state (IpcListener::state_ on Windows): the one idle
/// listening instance with its queued ConnectNamedPipe, recycled after
/// every accept.
struct ListenerState {
    std::wstring name;
    HANDLE listen_pipe = nullptr;
    HANDLE connect_event = nullptr; // auto-reset; the queued connect
    OVERLAPPED connect_overlapped{};
    bool connect_pending = false;
    bool connect_done = false; // ERROR_PIPE_CONNECTED race: a client is attached
};

/// Arms a fresh listening instance for the next client (the accept/recycle
/// cycle). On failure `error` carries the reason and the listener stays
/// closed-out.
bool arm_listener(ListenerState &state, std::string &error) {
    state.listen_pipe = create_pipe_instance(state.name, /*first_instance=*/false);
    if (state.listen_pipe == INVALID_HANDLE_VALUE || state.listen_pipe == nullptr) {
        error = "cannot create the named pipe instance (Win32 error " +
                std::to_string(static_cast<unsigned long>(::GetLastError())) + ")";
        return false;
    }
    state.connect_overlapped = OVERLAPPED{};
    state.connect_overlapped.hEvent = state.connect_event;
    if (::ConnectNamedPipe(state.listen_pipe, &state.connect_overlapped) != 0) {
        state.connect_done = true; // synchronous completion (unexpected but legal)
        return true;
    }
    const DWORD connect_error = ::GetLastError();
    if (connect_error == ERROR_IO_PENDING) {
        state.connect_pending = true;
        return true;
    }
    if (connect_error == ERROR_PIPE_CONNECTED) {
        // A client connected between CreateNamedPipe and ConnectNamedPipe.
        state.connect_done = true;
        return true;
    }
    error = "ConnectNamedPipe failed (Win32 error " +
            std::to_string(static_cast<unsigned long>(connect_error)) + ")";
    ::CloseHandle(state.listen_pipe);
    state.listen_pipe = nullptr;
    return false;
}

void close_listener_state(ListenerState &state) {
    if (state.listen_pipe != nullptr && state.connect_pending) {
        cancel_and_reap(state.listen_pipe, &state.connect_overlapped);
    }
    if (state.listen_pipe != nullptr) {
        ::DisconnectNamedPipe(state.listen_pipe);
        ::CloseHandle(state.listen_pipe);
        state.listen_pipe = nullptr;
    }
    if (state.connect_event != nullptr) {
        ::CloseHandle(state.connect_event);
        state.connect_event = nullptr;
    }
    state.connect_pending = false;
    state.connect_done = false;
}

} // namespace

IpcStream::~IpcStream() { close(); }

IpcStream::IpcStream(IpcStream &&other) noexcept
    : state_(std::exchange(other.state_, nullptr)),
      native_(std::exchange(other.native_, kInvalidTransport)) {}

IpcStream &IpcStream::operator=(IpcStream &&other) noexcept {
    if (this != &other) {
        close();
        native_ = std::exchange(other.native_, kInvalidTransport);
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}

void IpcStream::close() {
    if (native_ != kInvalidTransport) {
        if (state_ != nullptr) {
            auto *state = static_cast<StreamState *>(state_);
            if (state->read_event != nullptr) {
                ::CloseHandle(state->read_event);
                state->read_event = nullptr;
            }
            if (state->write_event != nullptr) {
                ::CloseHandle(state->write_event);
                state->write_event = nullptr;
            }
            delete state;
            state_ = nullptr;
        }
        // Closing the handle ends the connection on both transports: the
        // peer's next read reports broken pipe (the Closed status).
        ::CloseHandle(reinterpret_cast<HANDLE>(native_));
        native_ = kInvalidTransport;
    }
}

/// Allocates the stream's overlapped I/O state. Round-1 verification D1: a
/// stream without this state degrades every operation to a null result —
/// the factories are the only place it can be created, and every one of
/// them goes through here.
IpcStream IpcStream::make_stream(std::intptr_t native) {
    if (native == kInvalidTransport) {
        return IpcStream{};
    }
    auto *state = new StreamState();
    state->read_event =
        ::CreateEventW(nullptr, /*bManualReset=*/FALSE, /*bInitialState=*/FALSE, nullptr);
    state->write_event =
        ::CreateEventW(nullptr, /*bManualReset=*/FALSE, /*bInitialState=*/FALSE, nullptr);
    if (state->read_event == nullptr || state->write_event == nullptr) {
        const DWORD error = ::GetLastError();
        if (state->read_event != nullptr) {
            ::CloseHandle(state->read_event);
        }
        if (state->write_event != nullptr) {
            ::CloseHandle(state->write_event);
        }
        delete state;
        ::CloseHandle(reinterpret_cast<HANDLE>(native));
        std::fprintf(stderr, "[win32-ipc] stream state allocation failed (Win32 error %lu)\n",
                     static_cast<unsigned long>(error));
        std::fflush(stderr);
        return IpcStream{};
    }
    IpcStream stream(native);
    stream.state_ = state;
    return stream;
}

IpcStream IpcStream::adopt_native(std::intptr_t native) { return make_stream(native); }

IoResult IpcStream::read_some(char *data, std::size_t size) {
    IoResult result;
    if (!valid() || size == 0 || state_ == nullptr) {
        return result;
    }
    const HANDLE handle = reinterpret_cast<HANDLE>(native_);
    auto *state = static_cast<StreamState *>(state_);
    // Zero-wait fast path: the pipe reports inbound availability without
    // queueing a read, so WouldBlock never leaves an operation behind.
    DWORD available = 0;
    if (::PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr) == 0) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA) {
            result.status = IoStatus::Closed;
        } else {
            result.status = IoStatus::Error;
            result.os_error = static_cast<int>(error);
        }
        return result;
    }
    if (available == 0) {
        result.status = IoStatus::WouldBlock;
        return result;
    }
    OVERLAPPED overlapped{};
    const DWORD issued = issue_read(handle, &overlapped, state->read_event, data, size);
    if (issued == ERROR_IO_PENDING) {
        // The zero wait loses to scheduling far more often than not; the
        // queued read is aborted and reaped with the caller's buffer still
        // in scope. The transfer count is authoritative even for an aborted
        // operation — GetOverlappedResult reports FALSE with
        // ERROR_OPERATION_ABORTED while `reaped` still carries the bytes
        // that already landed in the caller's buffer — so any positive
        // count is delivered as Ok, never dropped.
        if (::WaitForSingleObject(overlapped.hEvent, 0) != WAIT_OBJECT_0) {
            ::CancelIoEx(handle, &overlapped);
            DWORD reaped = 0;
            const BOOL completed = ::GetOverlappedResult(handle, &overlapped, &reaped, TRUE);
            const DWORD reap_error = completed ? ERROR_SUCCESS : ::GetLastError();
            if (reaped > 0) {
                result.bytes = reaped;
                result.status = IoStatus::Ok;
                return result;
            }
            if (reap_error == ERROR_OPERATION_ABORTED) {
                result.status = IoStatus::WouldBlock;
            } else if (reap_error == ERROR_BROKEN_PIPE) {
                result.status = IoStatus::Closed;
            } else {
                result.status = IoStatus::Error;
                result.os_error = static_cast<int>(reap_error);
            }
            return result;
        }
    } else if (issued != ERROR_SUCCESS) {
        if (issued == ERROR_BROKEN_PIPE) {
            result.status = IoStatus::Closed;
        } else {
            result.status = IoStatus::Error;
            result.os_error = static_cast<int>(issued);
        }
        return result;
    }
    DWORD read_bytes = 0;
    if (::GetOverlappedResult(handle, &overlapped, &read_bytes, FALSE) == 0) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_BROKEN_PIPE && read_bytes > 0) {
            result.bytes = read_bytes; // deliver the tail before the close
            return result;
        }
        if (error == ERROR_BROKEN_PIPE || error == ERROR_OPERATION_ABORTED) {
            result.status = IoStatus::Closed;
        } else {
            result.status = IoStatus::Error;
            result.os_error = static_cast<int>(error);
        }
        return result;
    }
    if (read_bytes == 0) {
        result.status = IoStatus::Closed;
        return result;
    }
    result.bytes = read_bytes;
    return result;
}

IoResult IpcStream::write_some(const char *data, std::size_t size) {
    IoResult result;
    if (!valid() || size == 0 || state_ == nullptr) {
        return result;
    }
    const HANDLE handle = reinterpret_cast<HANDLE>(native_);
    auto *state = static_cast<StreamState *>(state_);
    // The same discipline as read_some: an operation the pipe cannot take
    // immediately is cancelled and reaped inside this call, reporting
    // exactly the bytes that were truly transferred. The caller therefore
    // always resumes from the reported cursor — nothing is queued behind
    // the call and nothing can be written twice.
    OVERLAPPED overlapped{};
    const DWORD issued = issue_write(handle, &overlapped, state->write_event, data, size);
    if (issued == ERROR_IO_PENDING) {
        // Mirror of the read path: the transfer count is authoritative even
        // for an aborted operation — bytes already on the wire are reported
        // as Ok so the caller resumes from the true cursor; nothing enters
        // the pipe twice and nothing is dropped.
        if (::WaitForSingleObject(overlapped.hEvent, 0) != WAIT_OBJECT_0) {
            cancel_and_reap(handle, &overlapped);
            DWORD written = 0;
            const BOOL completed = ::GetOverlappedResult(handle, &overlapped, &written, TRUE);
            const DWORD reap_error = completed ? ERROR_SUCCESS : ::GetLastError();
            if (written > 0) {
                result.bytes = written;
                result.status = IoStatus::Ok;
                return result;
            }
            if (reap_error == ERROR_OPERATION_ABORTED) {
                result.status = IoStatus::WouldBlock;
            } else if (reap_error == ERROR_BROKEN_PIPE) {
                result.status = IoStatus::Closed;
            } else {
                result.status = IoStatus::Error;
                result.os_error = static_cast<int>(reap_error);
            }
            return result;
        }
    } else if (issued != ERROR_SUCCESS) {
        if (issued == ERROR_BROKEN_PIPE) {
            result.status = IoStatus::Closed;
        } else {
            result.status = IoStatus::Error;
            result.os_error = static_cast<int>(issued);
        }
        return result;
    }
    DWORD written = 0;
    if (::GetOverlappedResult(handle, &overlapped, &written, FALSE) == 0) {
        const DWORD error = ::GetLastError();
        if (error == ERROR_BROKEN_PIPE) {
            result.status = IoStatus::Closed;
        } else {
            result.status = IoStatus::Error;
            result.os_error = static_cast<int>(error);
        }
        return result;
    }
    if (written == 0) {
        result.status = IoStatus::WouldBlock;
        return result;
    }
    result.bytes = written;
    result.status = IoStatus::Ok;
    return result;
}

IpcListener::~IpcListener() { close(); }

IpcListener::IpcListener(IpcListener &&other) noexcept
    : state_(std::exchange(other.state_, nullptr)), address_(std::move(other.address_)) {}

IpcListener &IpcListener::operator=(IpcListener &&other) noexcept {
    if (this != &other) {
        close();
        state_ = std::exchange(other.state_, nullptr);
        address_ = std::move(other.address_);
    }
    return *this;
}

bool IpcListener::valid() const {
    return state_ != nullptr && static_cast<const ListenerState *>(state_)->listen_pipe != nullptr;
}

std::intptr_t IpcListener::handle() const {
    if (state_ == nullptr) {
        return 0; // the null-HANDLE invalid token
    }
    return reinterpret_cast<std::intptr_t>(static_cast<const ListenerState *>(state_)->listen_pipe);
}

void IpcListener::close() {
    if (state_ != nullptr) {
        auto *state = static_cast<ListenerState *>(state_);
        close_listener_state(*state);
        delete state;
        state_ = nullptr;
    }
    address_.clear();
}

IpcListener IpcListener::bind(const std::string &address, std::string &diagnostic) {
    IpcListener listener;
    const std::optional<std::wstring> name = wide_from_utf8(address);
    if (!name.has_value() || name->rfind(kPipePrefix, 0) != 0 ||
        name->size() <= std::size(kPipePrefix)) {
        diagnostic = "address must be a named pipe path of the form \\\\.\\pipe\\<name> (got '" +
                     address + "')";
        return listener;
    }
    auto *state = new ListenerState();
    state->name = *name;
    state->connect_event =
        ::CreateEventW(nullptr, /*bManualReset=*/FALSE, /*bInitialState=*/FALSE, nullptr);
    if (state->connect_event == nullptr) {
        diagnostic = "cannot create the connect event (Win32 error " +
                     std::to_string(static_cast<unsigned long>(::GetLastError())) + ")";
        delete state;
        return listener;
    }
    // The first instance carries the takeover flag: a second listener on
    // the same pipe name fails access-denied (the leftover-endpoint
    // discipline; named pipes leave no stale file behind).
    state->listen_pipe = create_pipe_instance(*name, /*first_instance=*/true);
    if (state->listen_pipe == INVALID_HANDLE_VALUE || state->listen_pipe == nullptr) {
        const DWORD error = ::GetLastError();
        diagnostic = error == ERROR_ACCESS_DENIED
                         ? "another service is already listening at '" + address + "'"
                         : "cannot create the named pipe (Win32 error " +
                               std::to_string(static_cast<unsigned long>(error)) + ")";
        ::CloseHandle(state->connect_event);
        delete state;
        return listener;
    }
    state->connect_overlapped = OVERLAPPED{};
    state->connect_overlapped.hEvent = state->connect_event;
    if (::ConnectNamedPipe(state->listen_pipe, &state->connect_overlapped) != 0) {
        state->connect_done = true; // synchronous completion (legal, rare)
    } else {
        const DWORD connect_error = ::GetLastError();
        if (connect_error == ERROR_IO_PENDING) {
            state->connect_pending = true;
        } else if (connect_error == ERROR_PIPE_CONNECTED) {
            state->connect_done = true; // a client raced the connect
        } else {
            diagnostic = "ConnectNamedPipe failed (Win32 error " +
                         std::to_string(static_cast<unsigned long>(connect_error)) + ")";
            ::CloseHandle(state->listen_pipe);
            ::CloseHandle(state->connect_event);
            delete state;
            return listener;
        }
    }
    listener.state_ = state;
    listener.address_ = address;
    return listener;
}

IpcStream IpcListener::accept(std::string &diagnostic) {
    if (state_ == nullptr) {
        return IpcStream{};
    }
    auto *state = static_cast<ListenerState *>(state_);
    if (!state->connect_pending && !state->connect_done) {
        return IpcStream{}; // nothing armed (unexpected; the listener recycles eagerly)
    }
    // HasOverlappedIoCompleted is a macro: no leading ::.
    if (state->connect_pending && HasOverlappedIoCompleted(&state->connect_overlapped) == 0) {
        return IpcStream{}; // the queued connect is still waiting for a client
    }
    DWORD transferred = 0;
    if (state->connect_pending &&
        ::GetOverlappedResult(state->listen_pipe, &state->connect_overlapped, &transferred,
                              FALSE) == 0) {
        const DWORD error = ::GetLastError();
        if (error != ERROR_BROKEN_PIPE && error != ERROR_OPERATION_ABORTED) {
            diagnostic = "ConnectNamedPipe completion failed (Win32 error " +
                         std::to_string(static_cast<unsigned long>(error)) + ")";
        }
        // A client that connected and vanished still leaves the instance
        // attached: hand it out so the first read reports the Closed status
        // through the normal path.
    }
    // Detach the connected instance from the listener and recycle a fresh
    // listening instance immediately, so the service never stops accepting.
    const HANDLE connected = state->listen_pipe;
    state->listen_pipe = nullptr;
    state->connect_pending = false;
    state->connect_done = false;
    std::string recycle_error;
    if (!arm_listener(*state, recycle_error)) {
        // A failed recycle is loud: the service keeps its connected clients
        // but cannot take new ones (RULE-07 visibility).
        diagnostic = "listener recycle failed: " + recycle_error;
    }
    return IpcStream::make_stream(reinterpret_cast<std::intptr_t>(connected));
}

bool endpoint_has_listener(const std::string &address, std::chrono::milliseconds) {
    const std::optional<std::wstring> name = wide_from_utf8(address);
    if (!name.has_value()) {
        return true; // unparseable address: keep any existing endpoint in place
    }
    HANDLE probe = ::CreateFileW(name->c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                 OPEN_EXISTING, 0, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        ::CloseHandle(probe);
        return true;
    }
    const DWORD error = ::GetLastError();
    // Optimistic-conservative split, the POSIX rule: an absent endpoint is
    // free to take over, everything else (including a busy live listener)
    // stays in place.
    return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
}

IpcStream connect_stream(const std::string &address, std::chrono::milliseconds deadline,
                         std::string &diagnostic) {
    const std::optional<std::wstring> name = wide_from_utf8(address);
    if (!name.has_value() || name->rfind(kPipePrefix, 0) != 0) {
        diagnostic = "address must be a named pipe path of the form \\\\.\\pipe\\<name> (got '" +
                     address + "')";
        return IpcStream{};
    }
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        HANDLE handle = ::CreateFileW(name->c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                      OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            return IpcStream::make_stream(reinterpret_cast<std::intptr_t>(handle));
        }
        const DWORD error = ::GetLastError();
        if (error == ERROR_PIPE_BUSY) {
            // The listener exists but every instance is busy; wait for a
            // free slot within the deadline.
            ::WaitNamedPipeW(name->c_str(), 100);
        } else if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) {
            // The accept/recycle window keeps zero instances alive for an
            // instant; within the deadline that is retryable, not a refusal.
            if (std::chrono::steady_clock::now() - start >= deadline) {
                diagnostic = "connection refused: no such pipe at '" + address + "'";
                return IpcStream{};
            }
        } else {
            diagnostic = "connect('" + address + "') failed (Win32 error " +
                         std::to_string(static_cast<unsigned long>(error)) + ")";
            return IpcStream{};
        }
        if (std::chrono::steady_clock::now() - start >= deadline) {
            diagnostic = "connect('" + address + "') timed out";
            return IpcStream{};
        }
        ::Sleep(10);
    }
}

} // namespace mirage::runtime::ipc
