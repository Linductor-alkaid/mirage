#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mirage::runtime::ipc {

/// Wire protocol version (DEC-007). Requests and responses both carry it;
/// a mismatch is a protocol error, never a best-effort decode.
inline constexpr int kProtocolVersion = 1;

// ---------------------------------------------------------------------------
// Requests
// ---------------------------------------------------------------------------

/// One scripted desktop action of an M1 task (DEC-007 item 5). The M1
/// service-side driver loop executes steps in order, bracketing each with
/// the host operation surface; the step set mirrors the M1 desktop
/// capabilities (DEC-008).
enum class StepKind {
    FilesystemRead, ///< `argument` is the file path
    ProcessExecute, ///< `argument` is the shell command line
};

struct TaskStep {
    StepKind kind = StepKind::FilesystemRead;
    std::string argument;
};

struct HelloRequest {};

struct SubmitTaskRequest {
    std::string goal;
    std::vector<TaskStep> steps;
    /// Optional wall-clock budget for one process.execute step; clamped by
    /// the service's own cap. Filesystem reads are bounded by the provider
    /// contract, not by this field.
    std::optional<std::chrono::milliseconds> step_timeout;
};

struct ListTasksRequest {};

struct InspectTaskRequest {
    std::string task_id;
};

struct ShutdownRequest {};

using Request =
    std::variant<HelloRequest, SubmitTaskRequest, ListTasksRequest,
                 InspectTaskRequest, ShutdownRequest>;

// ---------------------------------------------------------------------------
// Responses
// ---------------------------------------------------------------------------

struct ServiceIdentity {
    std::string name;
    std::string mirage_version;
    std::string mira_core_version;
    std::string host_status;
    int protocol = kProtocolVersion;
};

struct TaskSubmitted {
    std::string task_id;
};

struct TaskSummary {
    std::string id;
    std::string goal;
    std::string progress;
};

struct TaskList {
    std::vector<TaskSummary> tasks;
};

/// One executed (or pending) step of a task as reported by task.inspect.
/// `operation_id` is the pinned OperationRecord identity the driver's
/// begin_operation returned, tying the step result to the control plane
/// trace (DEC-007 item 5). Structured results are carried in `result`,
/// capped by the service; `result_truncated` marks the cap.
struct StepView {
    int index = 0;
    std::string kind; ///< "filesystem.read" or "process.execute"
    std::string status; ///< "pending" / "running" / "ok" / "failed" / "skipped"
    std::string operation_id;
    bool ok = false;
    int exit_code = -1;         ///< process.execute only; -1 otherwise
    std::string result;         ///< file content or captured output, capped
    bool result_truncated = false;
    std::string error;          ///< stable failure summary, safe for UI
};

struct InspectTask {
    std::string id;
    std::string goal;
    std::string progress;
    /// Meaningful only once the task reached a terminal progress state.
    bool has_success = false;
    bool success = false;
    std::vector<StepView> steps;
};

struct ShutdownAccepted {};

using ResponsePayload = std::variant<ServiceIdentity, TaskSubmitted, TaskList,
                                     InspectTask, ShutdownAccepted>;

/// Stable error surface (DEC-007 item 4). `code` is from the mirage.ipc
/// domain ("protocol_error", "unsupported", "invalid_argument", "not_found",
/// "invalid_state", "unavailable", "internal"); `message` is safe for logs
/// and UI.
struct IpcError {
    std::string code;
    std::string message;
};

struct Response {
    bool ok = false;
    std::uint64_t id = 0; ///< correlation id echoed from the request
    ResponsePayload payload;
    IpcError error; ///< meaningful only when ok is false
};

// ---------------------------------------------------------------------------
// Codec
// ---------------------------------------------------------------------------

/// Encodes one request envelope (protocol version + correlation id + body)
/// as a wire payload (no framing prefix; framing.hpp adds that).
std::string encode_request(std::uint64_t id, const Request& body);

struct RequestDecode {
    bool ok = false;
    std::uint64_t id = 0;   ///< correlation id, meaningful when ok
    Request body = HelloRequest{};
    std::string error;      ///< stable reason, meaningful when !ok
};

/// Strict decode of one request payload; any deviation (bad JSON, wrong
/// version, unknown op, malformed fields) fails with a stable reason.
RequestDecode decode_request(std::string_view payload);

/// Encodes one response envelope as a wire payload.
std::string encode_response(const Response& response);

struct ResponseDecode {
    bool ok = false;
    Response response;
    std::string error; ///< stable reason, meaningful when !ok
};

/// Strict decode of one response payload.
ResponseDecode decode_response(std::string_view payload);

/// Stable string form of a StepKind ("filesystem.read" / "process.execute").
const char* step_kind_name(StepKind kind);

} // namespace mirage::runtime::ipc
