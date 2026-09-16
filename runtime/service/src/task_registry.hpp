#pragma once

// Internal service module surface (not installed, never included from
// public headers): the in-memory task registry, the shared service core and
// the loop/driver building blocks. Pinned executor types are confined to
// these internal headers and their translation units.

#include <cstddef>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/runtime/ipc/protocol.hpp>

namespace mirage::runtime::detail {

/// Lifecycle of one scripted step inside the service-side driver.
namespace step_status {
inline constexpr const char* kPending = "pending";
inline constexpr const char* kRunning = "running";
inline constexpr const char* kOk = "ok";
inline constexpr const char* kFailed = "failed";
inline constexpr const char* kSkipped = "skipped";
/// The step was interrupted by a task cancellation (M1-05 cancellation
/// path): the desktop action did not finish, but the task did not fail.
inline constexpr const char* kCancelled = "cancelled";
} // namespace step_status

struct StepRecord {
    ipc::TaskStep spec;
    const char* status = step_status::kPending;
    std::string operation_id;
    bool ok = false;
    int exit_code = -1;
    std::string result;
    bool result_truncated = false;
    std::string error;
};

struct TaskRecord {
    std::string id;
    std::string goal;
    std::vector<StepRecord> steps;
    /// Wall-clock budget for one process.execute step, already clamped by
    /// the service cap at submit time (DEC-007 item 5).
    std::chrono::milliseconds step_timeout{30000};
    /// Cooperative cancellation flag the driver hands to every desktop
    /// action; the task.cancel handler and the ordered teardown request it
    /// so an in-flight provider operation ends promptly (M1-05).
    mirage::desktop::CancelToken cancel;
    /// True once the driver settled the task (or gave up on it).
    bool driver_done = false;
    /// Meaningful only once driver_done and the task reached a terminal
    /// pinned state; false for cancelled/failed settlements.
    bool has_success = false;
    bool success = false;
};

/// In-memory task registry (M1; persistence is M1-07). Explicitly bounded:
/// insert() refuses when capacity is reached instead of evicting silently
/// (RULE-07). All access happens under `mutex`.
struct TaskRegistry {
    std::mutex mutex;
    std::map<std::string, TaskRecord> tasks;
    std::size_t capacity = 256;

    bool full() const { return tasks.size() >= capacity; }
    std::vector<std::string> ids() const {
        std::vector<std::string> result;
        result.reserve(tasks.size());
        for (const auto& entry : tasks) {
            result.push_back(entry.first);
        }
        return result;
    }
};

/// Stable name of a MiraHost task progress state for IPC payloads.
const char* progress_name(TaskProgress progress);

} // namespace mirage::runtime::detail
