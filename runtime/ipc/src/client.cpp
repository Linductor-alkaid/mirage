#include <mirage/runtime/ipc/client.hpp>

#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/stream.hpp>

#include <chrono>
#include <string>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#else
#include <poll.h>
#endif

namespace mirage::runtime::ipc {
namespace {

constexpr std::uint64_t kClientId = 1;

#ifdef _WIN32
// The named-pipe stream is zero-wait non-blocking (read_some reports
// WouldBlock instead of waiting), so the bounded waits here only pace the
// deadline loop: a bounded slice, then the operation itself decides.
constexpr std::chrono::milliseconds kClientWaitSlice{25};

bool wait_readable(const IpcStream &, std::chrono::milliseconds budget) {
    if (budget <= std::chrono::milliseconds::zero()) {
        return false;
    }
    ::Sleep(static_cast<DWORD>(std::min(budget, kClientWaitSlice).count()));
    return true;
}

bool wait_writable(const IpcStream &, std::chrono::milliseconds budget) {
    // Writes queue inside the stream; nothing to wait for.
    return budget > std::chrono::milliseconds::zero();
}
#else
bool wait_readable(const IpcStream &stream, std::chrono::milliseconds budget) {
    if (budget <= std::chrono::milliseconds::zero()) {
        return false;
    }
    pollfd descriptor{};
    descriptor.fd = static_cast<int>(stream.handle());
    descriptor.events = POLLIN;
    return ::poll(&descriptor, 1, static_cast<int>(budget.count())) == 1;
}

bool wait_writable(const IpcStream &stream, std::chrono::milliseconds budget) {
    if (budget <= std::chrono::milliseconds::zero()) {
        return false;
    }
    pollfd descriptor{};
    descriptor.fd = static_cast<int>(stream.handle());
    descriptor.events = POLLOUT;
    return ::poll(&descriptor, 1, static_cast<int>(budget.count())) == 1;
}
#endif

std::chrono::milliseconds remaining(std::chrono::steady_clock::time_point deadline) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
        return std::chrono::milliseconds::zero();
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
}

} // namespace

IpcClient::IpcClient(std::string socket_path) : socket_path_(std::move(socket_path)) {}

Response IpcClient::call(const Request &request, std::chrono::milliseconds timeout) {
    Response response;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    auto fail = [&response](std::string code, std::string message) {
        response.ok = false;
        response.error = {std::move(code), std::move(message)};
        return response;
    };
    std::string diagnostic;
    IpcStream stream = connect_stream(socket_path_, remaining(deadline), diagnostic);
    if (!stream.valid()) {
        return fail("unavailable", "cannot reach service at '" + socket_path_ + "': " + diagnostic);
    }
    const std::string frame = make_frame(encode_request(kClientId, request));
    std::size_t written = 0;
    while (written < frame.size()) {
        if (!wait_writable(stream, remaining(deadline))) {
            return fail("unavailable", "timed out writing the request");
        }
        const IoResult result = stream.write_some(frame.data() + written, frame.size() - written);
        switch (result.status) {
        case IoStatus::Ok:
            written += result.bytes;
            break;
        case IoStatus::WouldBlock:
            continue;
        case IoStatus::Closed:
            return fail("unavailable", "service closed the connection");
        case IoStatus::Error:
            return fail("internal",
                        "socket write failed (os error " + std::to_string(result.os_error) + ")");
        }
    }
    std::string buffer;
    for (;;) {
        const FrameExtraction extraction = try_extract_frame(buffer);
        if (extraction.status == FrameExtract::Message) {
            const ResponseDecode decoded = decode_response(extraction.message);
            if (!decoded.ok) {
                return fail("internal", "service sent an undecodable response: " + decoded.error);
            }
            return decoded.response;
        }
        if (extraction.status == FrameExtract::ProtocolError) {
            return fail("internal", "service violated the framing protocol: " + extraction.reason);
        }
        if (!wait_readable(stream, remaining(deadline))) {
            return fail("unavailable", "timed out waiting for the response");
        }
        char chunk[4096];
        const IoResult result = stream.read_some(chunk, sizeof(chunk));
        switch (result.status) {
        case IoStatus::Ok:
            buffer.append(chunk, result.bytes);
            break;
        case IoStatus::WouldBlock:
            break;
        case IoStatus::Closed:
            return fail("unavailable", "service closed the connection before "
                                       "answering");
        case IoStatus::Error:
            return fail("internal",
                        "socket read failed (os error " + std::to_string(result.os_error) + ")");
        }
    }
}

} // namespace mirage::runtime::ipc
