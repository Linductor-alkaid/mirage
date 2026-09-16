#pragma once

// Internal service module surface (not installed, never included from
// public headers): the M1-07 recovery persistence writer. Pinned types
// never appear here; the persistence module's public surface is pure std.

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

#include <mirage/runtime/persistence/recovery.hpp>
#include <mirage/runtime/persistence/store.hpp>

#include "task_registry.hpp"

namespace mirage::runtime::detail {

/// Writes and hydrates the Runtime Recovery State file (DEC-011 item 4).
///
/// Threading: persist() is called from driver settlement paths (executor
/// tasks) and from the teardown sequence; a full snapshot runs under the
/// writer mutex so the last writer always holds the freshest complete
/// state. persist() never throws and never blocks on the registry lock
/// while writing: the snapshot copy happens under the registry mutex, the
/// file I/O happens after releasing it.
class RecoveryWriter {
  public:
    /// Points the writer at `store`. The version lands in the document for
    /// diagnostics only; `max_result_bytes` mirrors the service's per-step
    /// result cap for the decode-side sanity bound.
    void enable(persistence::LocalStateStore store, std::string mirage_version,
                std::size_t max_result_bytes);

    bool enabled() const;

    /// Snapshots every settled task (driver_done with a final_progress
    /// name) and writes the file. Failures are recorded in last_error()
    /// and reported once; they never propagate to the settlement path —
    /// a broken recovery file must not take tasks down with it.
    void persist(TaskRegistry &registry);

    /// Loads the recovery file and injects its terminal task records into
    /// `registry` (up to `capacity`; beyond-capacity entries are dropped
    /// and counted). A corrupt or over-budget file is a loud degradation:
    /// the reason lands in last_error(), the service starts without
    /// recovery, and the file is never touched. Returns the number of
    /// hydrated tasks.
    std::size_t hydrate(TaskRegistry &registry, std::size_t capacity);

    /// Last failure reason, empty when everything worked. Guarded by the
    /// writer mutex.
    std::string last_error();

  private:
    void record_error(const std::string &reason);

    mutable std::mutex mutex_;
    std::optional<persistence::LocalStateStore> store_;
    std::string mirage_version_;
    std::size_t max_result_bytes_ = 8192;
    std::string last_error_;
    bool error_reported_ = false;
};

} // namespace mirage::runtime::detail
