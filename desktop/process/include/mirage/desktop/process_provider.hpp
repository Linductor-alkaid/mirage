#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Budget for one shell execution (RULE-07: every desktop operation carries
/// a capacity or budget cap). All fields are enforced by the provider.
struct ProcessLimits {
    /// Wall-clock budget for the whole command. A command that exceeds it is
    /// killed and reported with ok=false and timed_out=true. Must be
    /// positive.
    std::chrono::milliseconds timeout{30000};
    /// Capture cap per stream; output beyond the cap is truncated and
    /// output_truncated is set instead of growing without bound. Must be
    /// positive.
    std::size_t max_output_bytes = 1u << 20;
    /// Length cap for the command line itself; a longer command is refused
    /// before any process is created (M1-05, "refused execution"). Must be
    /// positive.
    std::size_t max_command_bytes = 64u << 10;
};

/// Outcome of one shell execution. ok means the command ran to completion
/// within budget (its exit code is a structured result, not a provider
/// failure); a command killed by a signal, cancelled or lost reports
/// ok=false.
///
/// M10-05 (DEC-095): `retained_truncated` was appended as the last member —
/// aggregate initializers in existing backends keep compiling and keep
/// their previous meaning.
struct ProcessOutcome {
    bool ok = false;
    bool timed_out = false;
    /// True when the CancelToken passed to execute() ended the command
    /// before its own budget expired (M1-05 cancellation path).
    bool cancelled = false;
    bool output_truncated = false;
    bool exited_normally = false;
    int exit_code = -1;
    std::string standard_output;
    std::string standard_error;
    ProviderError error; ///< meaningful only when ok is false
    /// True when the retention sink rejected or dropped chunks (quota/disk
    /// pressure); the in-result preview stays complete on its own terms and
    /// the drain never blocked the command. Only meaningful when a sink was
    /// supplied.
    bool retained_truncated = false;
};

/// Bounded output retention seam (DEC-095, M10-06): supplied by the runtime
/// owner, never by the provider. The provider drains both pipes fully and
/// mirrors every chunk, so retention pressure can neither block the command
/// nor stop the drain; a failed retain() marks retained_truncated and keeps
/// going. Implementations must be bounded, non-blocking and thread-confined
/// to the execute() caller.
class ProcessOutputSink {
  public:
    virtual ~ProcessOutputSink() = default;
    /// Mirrors one chunk of one stream. Returns false on retention failure
    /// (quota exhausted, disk full); the command and drain continue.
    virtual bool retain(bool standard_output, std::string_view chunk) = 0;
};

/// Additive execution request (DEC-095, M10-05): a command plus an explicit
/// working directory and an optional output retention sink. The historical
/// execute(command, limits, cancel) entry point keeps its exact semantics —
/// empty working directory behavior — and every backend that does not
/// override the request overload keeps compiling unchanged.
struct ProcessRequest {
    std::string command;
    /// Working directory for the command; must exist and be a directory.
    /// Empty keeps the caller's process cwd (legacy behavior).
    std::filesystem::path working_directory;
    /// Optional bounded retention sink; null keeps preview-only capture.
    ProcessOutputSink *output_sink = nullptr;
};

/// Shell execution capability (design doc section 5, "Process / Shell").
///
/// Commands run through the platform shell. The provider is synchronous and
/// bounded: it blocks the calling thread no longer than the declared timeout,
/// refuses over-budget commands before any process is created, and tears the
/// whole command process group down before returning, so no descendant
/// survives the call and no child is left unreaped. The provider itself
/// stays permission-agnostic: callers judge the process.execute capability
/// through the runtime permission gate (RULE-05, DEC-010) before invoking.
class ProcessProvider {
  public:
    virtual ~ProcessProvider() = default;

    /// Executes `command` within `limits`; a cancelled token ends the
    /// command cooperatively (whole process group torn down, cancelled=true)
    /// at the provider's observation granularity.
    virtual ProcessOutcome execute(const std::string &command, const ProcessLimits &limits,
                                   const CancelToken &cancel) = 0;

    /// Same execution without cancellation.
    ProcessOutcome execute(const std::string &command, const ProcessLimits &limits) {
        return execute(command, limits, CancelToken{});
    }

    /// True when this backend honors ProcessRequest::working_directory;
    /// false means only the legacy no-cwd entry point works and callers must
    /// not expose a workdir argument.
    [[nodiscard]] virtual bool supports_working_directory() const { return false; }

    /// True when this backend mirrors output into a ProcessOutputSink.
    [[nodiscard]] virtual bool supports_output_retention() const { return false; }

    /// Additive request entry point (DEC-095). Deliberately not an execute()
    /// overload: a second overload would hide the legacy override in every
    /// subclass (-Woverloaded-virtual) and each backend would need a using
    /// declaration. The default keeps every existing backend
    /// source-compatible: a cwd-less, sink-less request behaves exactly like
    /// the legacy call; a request that needs cwd or retention fails closed
    /// on a backend that cannot deliver it.
    virtual ProcessOutcome execute_request(const ProcessRequest &request,
                                           const ProcessLimits &limits, const CancelToken &cancel) {
        if (!request.working_directory.empty() || request.output_sink != nullptr) {
            ProcessOutcome outcome;
            outcome.error = {"unsupported_platform",
                             "working directory / output retention is not available on this "
                             "provider"};
            return outcome;
        }
        return execute(request.command, limits, cancel);
    }
};

} // namespace mirage::desktop
