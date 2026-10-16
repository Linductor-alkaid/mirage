#pragma once

#include <mirage/integration/basic_output_retention.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>

namespace mirage::runtime {

/// Frozen DEC-095 budget table for retained command output (M10-02).
struct BasicOutputStoreConfig {
    /// Controlled storage root; created on demand. Must be non-empty for a
    /// functional store. Contents do not survive process restarts (refs are
    /// runtime state; leftovers are removed at construction).
    std::filesystem::path directory;
    /// Retention cap per stream; the drain continues beyond it and the
    /// stream reports truncated.
    std::size_t max_stream_bytes = 4u << 20;
    /// Retention admission budget per session; exhausted sessions refuse
    /// new executions with capacity_exceeded before any process is created.
    std::size_t max_session_bytes = 32u << 20;
    /// Global admission budget across all sessions.
    std::size_t max_global_bytes = 128u << 20;
    /// Retention window that starts at the command's terminal state.
    std::chrono::milliseconds retention_after_terminal{3600 * 1000};
    /// Concurrent retained executions per session (bounded handles).
    std::size_t max_open_executions = 4;
};

/// Disk-backed implementation of the M10-06 retention seam (DEC-095:
/// bounded disk retention, session-scoped opaque refs, one-hour expiry
/// after the terminal state, admission before spawn). One instance per
/// service; all entry points are thread-safe and bounded.
class BasicOutputStore final : public mirage::integration::BasicOutputRetention {
  public:
    explicit BasicOutputStore(BasicOutputStoreConfig config);
    ~BasicOutputStore() override;

    BasicOutputStore(const BasicOutputStore &) = delete;
    BasicOutputStore &operator=(const BasicOutputStore &) = delete;

    [[nodiscard]] std::shared_ptr<mirage::integration::BasicOutputStreams>
    open(const std::string &session, std::string *refusal_code = nullptr) override;

    [[nodiscard]] mirage::desktop::TextWindowOutcome
    read(const std::string &session, const std::string &ref, std::uint64_t skip_lines,
         const mirage::desktop::TextWindowLimits &limits) override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mirage::runtime
