#pragma once

// Shared fixtures for the M1-04 IPC and runtime service tests: a unique
// temporary directory plus poll-driven frame helpers over the non-blocking
// IpcStream surface. Everything runs sequentially on the calling thread; the
// tests never spawn threads, mirroring the single-threaded CLI client shape
// (DEC-007). The service under test provides the concurrency.

#include "test.hpp"

#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/stream.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include <poll.h>
#include <unistd.h>

namespace mirage::testing {

namespace ipc = mirage::runtime::ipc;

/// Unique temporary directory under the system temp path; removed on scope
/// exit. Socket paths and provider fixtures live inside it.
class TempDir {
  public:
    TempDir() {
        std::error_code error;
        root_ = std::filesystem::temp_directory_path(error) /
                ("mirage-m1-04-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, error);
    }
    ~TempDir() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }
    TempDir(const TempDir &) = delete;
    TempDir &operator=(const TempDir &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

/// Waits until `fd` reports one of `events`; false on timeout or error.
inline bool poll_fd(int fd, short events, std::chrono::milliseconds budget) {
    if (budget <= std::chrono::milliseconds::zero()) {
        return false;
    }
    ::pollfd descriptor{};
    descriptor.fd = fd;
    descriptor.events = events;
    if (::poll(&descriptor, 1, static_cast<int>(budget.count())) != 1) {
        return false;
    }
    return (descriptor.revents & events) != 0;
}

/// Writes the whole buffer through the non-blocking stream; false when the
/// peer went away or the deadline elapsed.
inline bool write_all(ipc::IpcStream &stream, const std::string &data,
                      std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (!poll_fd(stream.handle(), POLLOUT, remaining)) {
            return false;
        }
        const ipc::IoResult result = stream.write_some(data.data() + sent, data.size() - sent);
        switch (result.status) {
        case ipc::IoStatus::Ok:
            sent += result.bytes;
            break;
        case ipc::IoStatus::WouldBlock:
            break;
        case ipc::IoStatus::Closed:
        case ipc::IoStatus::Error:
            return false;
        }
    }
    return true;
}

/// Outcome of one bounded frame read.
struct FrameRead {
    enum class Status { Message, Closed, Timeout, ProtocolError };
    Status status = Status::Closed;
    std::string message; ///< meaningful only for Message
    std::string reason;  ///< meaningful only for ProtocolError
};

/// Reads one complete length-prefixed frame from the non-blocking stream,
/// accumulating into `buffer` across calls. Distinguishes an orderly peer
/// close from a deadline from a framing violation.
inline FrameRead read_frame(ipc::IpcStream &stream, std::string &buffer,
                            std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
        if (extraction.status == ipc::FrameExtract::Message) {
            FrameRead read;
            read.status = FrameRead::Status::Message;
            read.message = extraction.message;
            return read;
        }
        if (extraction.status == ipc::FrameExtract::ProtocolError) {
            FrameRead read;
            read.status = FrameRead::Status::ProtocolError;
            read.reason = extraction.reason;
            return read;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero() ||
            !poll_fd(stream.handle(), POLLIN, remaining)) {
            FrameRead read;
            read.status = FrameRead::Status::Timeout;
            return read;
        }
        char chunk[4096];
        const ipc::IoResult result = stream.read_some(chunk, sizeof(chunk));
        switch (result.status) {
        case ipc::IoStatus::Ok:
            buffer.append(chunk, result.bytes);
            break;
        case ipc::IoStatus::WouldBlock:
            break;
        case ipc::IoStatus::Closed:
        case ipc::IoStatus::Error: {
            FrameRead read;
            read.status = FrameRead::Status::Closed;
            return read;
        }
        }
    }
}

/// True when `id` is 32 lowercase hexadecimal characters (the pinned
/// OperationRecord identity shape used by the task drivers).
inline bool is_32_lowercase_hex(const std::string &id) {
    if (id.size() != 32) {
        return false;
    }
    for (const char character : id) {
        const bool digit = character >= '0' && character <= '9';
        const bool lowercase = character >= 'a' && character <= 'f';
        if (!digit && !lowercase) {
            return false;
        }
    }
    return true;
}

/// Short unique token for marker files and shell payload assertions.
inline std::string unique_token() {
    return std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

} // namespace mirage::testing
