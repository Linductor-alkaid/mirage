#pragma once

#include <chrono>
#include <functional>
#include <future>
#include <string>

#include <mirage/runtime/ipc/protocol.hpp>

namespace mirage::desktop_shell {

namespace ipc = mirage::runtime::ipc;

/// The controlled renderer bridge core (M5-02; design doc section 17 /
/// DEC-006: the IPC contract is the only coupling face between the UI and
/// C++). Maps protocol-v1 request envelopes from the renderer onto one
/// service call and the response back, restoring the renderer's correlation
/// id. Service events do not pass through here: they flow from the session
/// worker to the shell's forwarder directly. The core is CEF-free — the CEF
/// layer only passes strings in and out, so this mapping is unit-testable
/// without a browser.
class BridgeCore {
  public:
    /// Performs one service call through the live session (the shell
    /// supervisor's thread-safe facade). The returned future resolves with
    /// the service response or a wire-shaped local error.
    using CallFn =
        std::function<std::future<ipc::Response>(const ipc::Request &, std::chrono::milliseconds)>;

    /// One request/response exchange is bounded by this timeout, mirroring
    /// the UI-side request bound (ws-transport default) so a stalled service
    /// unblocks both sides of the bridge at the same pace.
    static constexpr std::chrono::milliseconds kCallTimeout{30'000};

    explicit BridgeCore(CallFn call) : call_(std::move(call)) {}

    /// Handles one renderer request; invokes `respond` exactly once with a
    /// response envelope JSON — the service's own envelope with the
    /// renderer's correlation id restored, or a wire-shaped local error
    /// ("protocol_error" for undecodable requests, "unavailable" for a dead
    /// session). Runs on the caller's context; the shell hops to the
    /// Executor before entering here (AGENTS.md third-party-thread rule).
    void handle_request(const std::string &request_json,
                        const std::function<void(const std::string &)> &respond);

  private:
    CallFn call_;
};

} // namespace mirage::desktop_shell
