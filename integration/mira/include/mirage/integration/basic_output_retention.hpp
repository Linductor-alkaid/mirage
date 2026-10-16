#pragma once

#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>

#include <memory>
#include <string>
#include <string_view>

namespace mirage::integration {

/// Terminal facts of one retained execution (M10-06, DEC-095 budget table):
/// refs are opaque session-scoped tokens for `read`'s output_ref argument —
/// never host file paths.
struct BasicOutputStreamFacts {
    std::string stdout_ref;
    std::string stderr_ref;
    std::uint64_t stdout_bytes = 0;
    std::uint64_t stderr_bytes = 0;
    bool stdout_truncated = false; ///< per-call retention cap reached
    bool stderr_truncated = false;
    /// True when retention failed for reasons other than the per-call cap
    /// (quota exhausted mid-flight, disk full); the drain never stopped.
    bool retention_failed = false;
};

/// Retention handle of one exec call (M10-06): mirrors the drained command
/// output (ProcessOutputSink seam) and fixes refs, sizes and expiry at the
/// command's terminal state. The runtime owner implements it; handlers
/// only hold it across one execute_request call.
class BasicOutputStreams : public mirage::desktop::ProcessOutputSink {
  public:
    ~BasicOutputStreams() override = default;
    /// Terminal state: stops retention, fixes the retention window and
    /// returns the facts the exec result reports.
    [[nodiscard]] virtual BasicOutputStreamFacts settle() = 0;
};

/// Runtime-owned bounded retention store seam (M10-06): the service wires
/// one implementation into DesktopToolContext; null keeps exec
/// preview-only (the M10-07 behavior). Implementations enforce the frozen
/// budgets — 4 MiB per stream, 32 MiB per session, 128 MiB global, refs
/// expire one hour after the command's terminal state — and refuse new
/// executions with a stable code when admission budgets are spent.
class BasicOutputRetention {
  public:
    virtual ~BasicOutputRetention() = default;
    /// Admission before spawn. Returns null on refusal and sets
    /// `refusal_code` ("capacity_exceeded"); the caller fails the exec
    /// without starting any process.
    [[nodiscard]] virtual std::shared_ptr<BasicOutputStreams>
    open(const std::string &session, std::string *refusal_code = nullptr) = 0;
    /// Pages one retained stream with the file-window contract (1-based
    /// lines, CRLF-normalized view, eof/next_offset facts). Every failed
    /// lookup — unknown id, expired, swept or another session's ref —
    /// reports "output_expired" without leaking which one it was.
    [[nodiscard]] virtual mirage::desktop::TextWindowOutcome
    read(const std::string &session, const std::string &ref, std::uint64_t skip_lines,
         const mirage::desktop::TextWindowLimits &limits) = 0;
};

} // namespace mirage::integration
