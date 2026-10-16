#pragma once

#include <chrono>
#include <cstddef>
#include <string>

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
};

} // namespace mirage::desktop
