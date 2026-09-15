#pragma once

#include <chrono>
#include <cstddef>
#include <string>

#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Budget for one shell execution (RULE-07: every desktop operation carries
/// a capacity or budget cap). Both fields are enforced by the provider; the
/// full cancellation-path hardening is M1-05.
struct ProcessLimits {
    /// Wall-clock budget for the whole command. A command that exceeds it is
    /// killed and reported with ok=false and timed_out=true. Must be
    /// positive.
    std::chrono::milliseconds timeout{30000};
    /// Capture cap per stream; output beyond the cap is truncated and
    /// output_truncated is set instead of growing without bound. Must be
    /// positive.
    std::size_t max_output_bytes = 1u << 20;
};

/// Outcome of one shell execution. ok means the command ran to completion
/// within budget (its exit code is a structured result, not a provider
/// failure); a command killed by a signal or lost reports ok=false.
struct ProcessOutcome {
    bool ok = false;
    bool timed_out = false;
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
/// bounded: it blocks the calling thread no longer than the declared timeout
/// and tears the whole command process group down before returning, so no
/// descendant survives the call and no child is left unreaped. Command
/// budget scope and the cancellation path are hardened in M1-05; the Desktop
/// Permission gate for process.execute (RULE-05) lands with M1-06, so until
/// then this provider must only be bound in development and test topologies
/// (DEC-008).
class ProcessProvider {
public:
    virtual ~ProcessProvider() = default;

    virtual ProcessOutcome execute(const std::string& command,
                                   const ProcessLimits& limits) = 0;
};

} // namespace mirage::desktop
