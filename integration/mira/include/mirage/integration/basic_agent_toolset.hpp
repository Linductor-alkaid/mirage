#pragma once

#include <mira/tool_executor.hpp>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/integration/desktop_tool_context.hpp>

#include <filesystem>
#include <memory>
#include <optional>

namespace mirage::integration {

/// Session-scoped observation record of the basic agent tools (DEC-095
/// decision 3): the host holds what the model actually saw — version facts
/// plus the observed byte ranges — so a write/edit presents provider-
/// verifiable authorization instead of model-claimed "already read" state.
/// Bounded to kMaxEntries files per session (least recently used evicted;
/// eviction simply forces a re-read).
class BasicToolSessionState {
  public:
    static constexpr std::size_t kMaxEntries = 128;

    BasicToolSessionState();
    ~BasicToolSessionState();
    BasicToolSessionState(const BasicToolSessionState &) = delete;
    BasicToolSessionState &operator=(const BasicToolSessionState &) = delete;

    /// Replaces the record for `path` with a full observation of freshly
    /// committed content (DEC-095: a successful write/edit updates the
    /// observation without a mechanical re-read).
    void remember_full(const std::filesystem::path &path,
                       const mirage::desktop::FileVersion &version);

    /// Folds one windowed read into the record: a version change resets the
    /// ranges (the file was replaced under us), a matching version extends
    /// them. `full` marks an eof window over a digestable file.
    void record_window(const std::filesystem::path &path,
                       const mirage::desktop::FileVersion &version, bool full,
                       const mirage::desktop::ObservedByteRange &range);

    /// The record for `path`, or nullopt; touches the LRU clock.
    [[nodiscard]] std::optional<mirage::desktop::FileObservation>
    observation_of(const std::filesystem::path &path) const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// M10-07 (DEC-095): registers `exec`, `read`, `write` and `edit` on a
/// session registry. Capability honesty governs registration: `read` needs a
/// windowed-read provider, `write`/`edit` need write/edit capabilities (an
/// empty write scope reports no capability at all), `exec` needs a process
/// provider with working-directory support plus an enabled workspace.
/// Permission judgement rides the same gate seam as the desktop atoms
/// (filesystem.read / filesystem.write / process.execute); the short ASCII
/// names pass the pinned hosted-name check and the alias provider
/// unchanged, so history and replay stay canonical. `cancel` is the
/// owning turn's cancellation (M10-08): exec tears its process group down
/// and file commits observe it at their pre-commit points.
/// Registration failure throws; callers settle it as a failed turn.
void register_basic_agent_tools(mira::BuiltinToolRegistry &registry,
                                const DesktopToolContext &desktop,
                                BasicToolSessionState &session,
                                const BasicAgentToolsOptions &options,
                                const mirage::desktop::CancelToken &cancel,
                                const Decorator &decorate);

} // namespace mirage::integration
