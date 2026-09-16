#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace mirage::runtime::persistence {

/// Schema version of the recovery document (DEC-011).
inline constexpr int kRecoverySchema = 1;

/// Decode-side bounds of the recovery document (DEC-011 item 3). The file
/// budget matches the pinned parser's default document limit; per-record
/// bounds are hostile-file guards well above what the service can produce
/// (results are capped at the service's max_result_bytes = 8 KiB).
inline constexpr std::size_t kMaxRecoveryFileBytes = 4 * 1024 * 1024;
inline constexpr std::size_t kMaxRecoveryTasks = 1024;
inline constexpr std::size_t kMaxRecoveryStepsPerTask = 256;
inline constexpr std::size_t kMaxRecoveryResultBytes = 1024 * 1024;

/// One settled step of a recovered task (M1-07). Field values use the
/// frozen stable vocabularies: `kind` is an ipc StepKind name
/// ("filesystem.read" / "process.execute"), `status` a step status name
/// ("pending" / "running" / "ok" / "failed" / "skipped" / "cancelled"),
/// `permission` a DEC-010 decision name ("" before judgment). The record
/// mirrors the task.inspect StepView surface so a recovered task renders
/// exactly like a live one.
struct RecoveryStep {
    std::string kind;
    std::string argument;
    std::string status;
    std::string operation_id;
    std::string permission;
    bool ok = false;
    int exit_code = -1;
    std::string result;
    bool result_truncated = false;
    std::string error;
};

/// One settled task record of a previous service run (DEC-011 item 4).
/// `progress` is the terminal progress name ("Completed" / "Failed" /
/// "Cancelled"); only settled tasks are persisted, so nothing else is
/// valid. `has_success`/`success` mirror the task.inspect semantics.
struct RecoveryTask {
    std::string id; ///< pinned task id (32 lowercase hex characters)
    std::string goal;
    std::string progress;
    bool has_success = false;
    bool success = false;
    std::vector<RecoveryStep> steps;
};

/// The Runtime Recovery State document (design doc section 16, DEC-011):
/// a full snapshot of the terminal task records of one service era.
struct RecoveryState {
    int schema = kRecoverySchema;
    std::string mirage_version; ///< informational; "" when unknown
    std::string saved_at;       ///< UTC ISO-8601; informational
    std::vector<RecoveryTask> tasks;
};

/// Current UTC time as an ISO-8601 string ("YYYY-MM-DDTHH:MM:SSZ"); the
/// clock source is wall time and the value is informational only.
std::string utc_timestamp_now();

/// Serializes the snapshot (compact JSON, schema field included).
std::string encode_recovery(const RecoveryState &state);

struct RecoveryDecode {
    bool ok = false;
    RecoveryState state;
    std::string error; ///< stable reason, meaningful when !ok
};

/// Strict decode: unknown members, unknown schema, out-of-vocabulary
/// progress/status/kind/permission strings, duplicate task ids and
/// over-bound counts or strings are rejected with a stable reason.
RecoveryDecode decode_recovery(std::string_view body);

} // namespace mirage::runtime::persistence
