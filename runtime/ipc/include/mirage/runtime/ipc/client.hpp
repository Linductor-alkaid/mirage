#pragma once

#include <chrono>
#include <string>

#include <mirage/runtime/ipc/protocol.hpp>

namespace mirage::runtime::ipc {

/// Blocking one-shot Local IPC client (DEC-007): connect, exchange exactly
/// one request/response pair, disconnect. Sized for the CLI and for tests;
/// long-lived multiplexing clients arrive with the GUI (M5) on the same
/// protocol. The calling thread blocks no longer than the declared timeout.
class IpcClient {
public:
    explicit IpcClient(std::string socket_path);

    /// Performs one call. Transport failures (no service, timeout, framing
    /// violation) come back as ok=false with a stable error code
    /// ("unavailable" for connect failures, "internal" for others) — the
    /// wire-level error of a decoded response is carried in the response's
    /// own IpcError instead.
    Response call(const Request& request, std::chrono::milliseconds timeout);

private:
    std::string socket_path_;
};

} // namespace mirage::runtime::ipc
