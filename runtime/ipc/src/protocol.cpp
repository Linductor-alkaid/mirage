#include <mirage/runtime/ipc/protocol.hpp>

#include <mira/json.hpp>

#include <algorithm>
#include <string>
#include <type_traits>
#include <variant>

namespace mirage::runtime::ipc {
namespace {

constexpr const char *kOpHello = "hello";
constexpr const char *kOpSubmit = "task.submit";
constexpr const char *kOpList = "task.list";
constexpr const char *kOpInspect = "task.inspect";
constexpr const char *kOpCancel = "task.cancel";
constexpr const char *kOpPause = "task.pause";
constexpr const char *kOpResume = "task.resume";
constexpr const char *kOpShutdown = "service.shutdown";
constexpr const char *kOpSubscribe = "events.subscribe";
constexpr const char *kOpUnsubscribe = "events.unsubscribe";
constexpr const char *kOpPermissionRespond = "permission.respond";
constexpr const char *kOpPermissionList = "permission.list";
constexpr const char *kOpSessionList = "session.list";
constexpr const char *kOpSessionDelete = "session.delete";
constexpr const char *kOpSessionOpen = "session.open";
constexpr const char *kOpSessionClose = "session.close";
constexpr const char *kOpSessionChat = "session.chat";
constexpr const char *kOpSessionChatHistory = "session.chat.history";
constexpr const char *kOpPolicyGet = "policy.get";
constexpr const char *kOpPolicySet = "policy.set";
constexpr const char *kOpSessionHistory = "session.history";
constexpr const char *kOpWorkflowList = "workflow.list";
constexpr const char *kOpWorkflowSave = "workflow.save";
constexpr const char *kOpWorkflowPublish = "workflow.publish";
constexpr const char *kOpWorkflowDelete = "workflow.delete";
constexpr const char *kOpWorkflowAtomCatalog = "workflow.atom.catalog";
constexpr const char *kOpWorkflowRuns = "workflow.runs";
constexpr const char *kOpWorkflowRun = "workflow.run";
constexpr const char *kOpWorkflowCancel = "workflow.cancel";
constexpr const char *kOpWorkflowGet = "workflow.get";
constexpr const char *kOpDesktopObserve = "desktop.observe";

constexpr const char *kStepRead = "filesystem.read";
constexpr const char *kStepExecute = "process.execute";

constexpr const char *kEventTaskUpdated = "task.updated";
constexpr const char *kEventHostStatus = "host.status";
constexpr const char *kEventOverflow = "events.overflow";
constexpr const char *kEventPermissionRequest = "permission.request";
constexpr const char *kEventSessionUpdated = "session.updated";
constexpr const char *kEventSessionMessage = "session.message";
constexpr const char *kEventSessionTurn = "session.turn";
constexpr const char *kEventSessionOutput = "session.output";
constexpr const char *kEventWorkflowRunUpdated = "workflow.run_updated";
constexpr const char *kEventChatPreview = "session.chat_preview";
constexpr const char *kEventChatTurnUpdated = "session.chat_updated";

/// Closed Capability vocabulary (DEC-010 / DEC-020) carried by
/// permission.request events and permission.list entries. Kept local so the
/// ipc layer stays independent of the permission module; the golden vectors
/// pin the set on both ends.
constexpr const char *kCapabilityNames[] = {
    "filesystem.read",    "filesystem.write",      "process.execute",   "window.activate",
    "screen.capture",     "input.inject",          "clipboard.read",    "clipboard.write",
    "application.launch", "application.terminate", "notification.post",
};

/// Closed rule vocabulary (DEC-010) carried by policy.set rules values
/// (M5-07). The golden vectors pin the set on both ends.
constexpr const char *kRuleNames[] = {"allow", "confirm", "deny"};

/// Closed product progress projection carried by task.updated events (schema
/// doc 6.2). Kept local so the ipc layer stays independent of mira_host;
/// the golden vectors pin the set on both ends.
constexpr const char *kProgressNames[] = {"Idle",      "Active", "Paused",    "Cancelling",
                                          "Completed", "Failed", "Cancelled", "Unknown"};

/// Closed Mira Host five-state set (DEC-004) carried by host.status events.
constexpr const char *kHostStatusNames[] = {"stopped", "starting", "running", "stopping", "failed"};

/// Closed session state projection (DEC-021): the pinned SessionState set in
/// stable lowercase form, carried by session.list entries and session.updated
/// events. The golden vectors pin the set on both ends.
constexpr const char *kSessionStateNames[] = {"opening",          "autonomous", "takeover_pending",
                                              "human_controlled", "resuming",   "closing",
                                              "closed",           "failed"};

/// Closed conversation-entry vocabulary (DEC-021): "user" marks a task goal
/// landing in the session, "outcome" a task settlement summary.
constexpr const char *kSessionMessageKinds[] = {"user", "outcome"};

/// Closed settled-step status vocabulary (DEC-021) carried by session.turn
/// events; turns publish on settlement only, so the in-flight names are
/// absent.
constexpr const char *kTurnStatusNames[] = {"ok", "failed", "cancelled", "skipped"};

/// Closed workflow run state vocabulary (DEC-023): the pinned
/// WorkflowRunState set in stable lowercase form, carried by workflow.runs
/// entries, workflow.run_updated events and the workflow.cancel reply. The
/// golden vectors pin the set on both ends.
constexpr const char *kWorkflowRunStateNames[] = {"created",      "running",       "paused",
                                                  "waiting_user", "waiting_agent", "completed",
                                                  "failed",       "cancelled"};

/// Closed workflow validation vocabulary (DEC-023): the pinned
/// WorkflowValidationResult set in stable lowercase form, carried by
/// workflow.list head entries.
constexpr const char *kWorkflowValidationNames[] = {"not_validated", "dry_run_passed", "validated",
                                                    "rejected"};

/// Closed execution policy vocabulary (DEC-023): the pinned WorkflowPolicy
/// set in stable lowercase form, carried by the optional workflow.run
/// `policy` member; empty means the definition default.
constexpr const char *kWorkflowPolicyNames[] = {"strict", "recoverable", "agent_assisted",
                                                "interactive", "dry_run"};

/// Closed visual-region provenance vocabulary (DEC-026): the desktop layer's
/// VisualRegionSource set in stable lowercase form, carried by the
/// observation projection's visual regions. The golden vectors pin the set
/// on both ends.
constexpr const char *kObservationRegionSources[] = {"ocr", "detector", "template", "geometry"};

/// Closed dialog turn-status vocabulary (DEC-027): "pending" marks an
/// accepted turn whose model call is in flight, "ok" a settled turn with a
/// reply, "failed" a settled turn with a stable error.
constexpr const char *kDialogTurnStatuses[] = {"pending", "ok", "failed"};

bool in_stable_set(const std::string &value, const char *const *set, std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (value == set[index]) {
            return true;
        }
    }
    return false;
}

mira::JsonValue make_object() { return mira::JsonValue{mira::JsonValue::Object{}}; }

void put(mira::JsonValue &object, std::string key, mira::JsonValue value) {
    object.set(std::move(key), std::move(value));
}

const mira::JsonValue *member(const mira::JsonValue &object, std::string_view key) {
    return object.find(key);
}

std::optional<std::int64_t> integer_member(const mira::JsonValue &object, std::string_view key) {
    const auto *value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    return value->as_integer();
}

std::optional<std::string> string_member(const mira::JsonValue &object, std::string_view key) {
    const auto *value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (const auto *text = value->as_string(); text != nullptr) {
        return *text;
    }
    return std::nullopt;
}

/// Embeds a stored serialized-JSON member (workflow definitions, run
/// parameters, tool schemas; DEC-023). The decode side only ever stores
/// canonical serializations of validated JSON values, so re-parsing is
/// lossless; a non-parsing text is a caller bug and degrades to a JSON
/// string member rather than invalid wire bytes.
mira::JsonValue embedded_json(const std::string &text) {
    if (auto parsed = mira::parse_json(text)) {
        return std::move(parsed.value());
    }
    return mira::JsonValue{text};
}

/// Captures a JSON-object member into its canonical serialization for the
/// pinned-free request/response structs; the object shape is checked by the
/// caller. Nullopt when the member is absent.
std::optional<std::string> object_member_text(const mira::JsonValue &object, std::string_view key) {
    const auto *value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    return mira::to_json_string(*value);
}

std::optional<bool> boolean_member(const mira::JsonValue &object, std::string_view key) {
    const auto *value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    return value->as_boolean();
}

// --- observation projection helpers (DEC-026) ------------------------------

mira::JsonValue encode_geometry(const ObservationGeometry &geometry) {
    auto object = make_object();
    put(object, "x", static_cast<std::int64_t>(geometry.x));
    put(object, "y", static_cast<std::int64_t>(geometry.y));
    put(object, "width", static_cast<std::int64_t>(geometry.width));
    put(object, "height", static_cast<std::int64_t>(geometry.height));
    return object;
}

std::optional<ObservationGeometry> decode_geometry(const mira::JsonValue &value,
                                                   std::string &error) {
    if (!value.is_object()) {
        error = "observation geometry must be an object";
        return std::nullopt;
    }
    const auto x = integer_member(value, "x");
    const auto y = integer_member(value, "y");
    const auto width = integer_member(value, "width");
    const auto height = integer_member(value, "height");
    if (!x || !y || !width || !height) {
        error = "observation geometry requires 'x', 'y', 'width' and 'height'";
        return std::nullopt;
    }
    ObservationGeometry geometry;
    geometry.x = static_cast<std::int32_t>(*x);
    geometry.y = static_cast<std::int32_t>(*y);
    geometry.width = static_cast<std::int32_t>(*width);
    geometry.height = static_cast<std::int32_t>(*height);
    return geometry;
}

mira::JsonValue encode_node(const ObservationNode &node) {
    auto object = make_object();
    put(object, "ref", node.ref);
    put(object, "role", node.role);
    put(object, "name", node.name);
    put(object, "description", node.description);
    put(object, "parent", node.parent);
    put(object, "geometry", encode_geometry(node.geometry));
    put(object, "focused", node.focused);
    put(object, "enabled", node.enabled);
    return object;
}

std::optional<ObservationNode> decode_node(const mira::JsonValue &value, std::string &error) {
    if (!value.is_object()) {
        error = "observation semantic nodes must be objects";
        return std::nullopt;
    }
    ObservationNode node;
    auto ref = string_member(value, "ref");
    auto role = string_member(value, "role");
    auto name = string_member(value, "name");
    auto description = string_member(value, "description");
    const auto parent = integer_member(value, "parent");
    const auto focused = boolean_member(value, "focused");
    const auto enabled = boolean_member(value, "enabled");
    if (!ref || ref->empty() || !role || !name || !description || !parent || *parent < -1 ||
        !focused || !enabled) {
        error = "observation semantic nodes require 'ref', 'role', 'name', 'description', a "
                "'parent' index (>= -1), 'focused' and 'enabled'";
        return std::nullopt;
    }
    const auto *geometry_value = member(value, "geometry");
    if (geometry_value == nullptr) {
        error = "observation semantic nodes require a 'geometry' object";
        return std::nullopt;
    }
    auto geometry = decode_geometry(*geometry_value, error);
    if (!geometry) {
        return std::nullopt;
    }
    node.ref = std::move(*ref);
    node.role = std::move(*role);
    node.name = std::move(*name);
    node.description = std::move(*description);
    node.parent = *parent;
    node.geometry = std::move(*geometry);
    node.focused = *focused;
    node.enabled = *enabled;
    return node;
}

mira::JsonValue encode_region(const ObservationRegion &region) {
    auto object = make_object();
    put(object, "ref", region.ref);
    put(object, "source", region.source);
    put(object, "geometry", encode_geometry(region.geometry));
    put(object, "text", region.text);
    put(object, "template_id", region.template_id);
    return object;
}

std::optional<ObservationRegion> decode_region(const mira::JsonValue &value, std::string &error) {
    if (!value.is_object()) {
        error = "observation visual regions must be objects";
        return std::nullopt;
    }
    ObservationRegion region;
    auto ref = string_member(value, "ref");
    auto source = string_member(value, "source");
    auto text = string_member(value, "text");
    auto template_id = string_member(value, "template_id");
    if (!ref || ref->empty() || !source || !text || !template_id) {
        error = "observation visual regions require 'ref', 'source', 'geometry', 'text' and "
                "'template_id'";
        return std::nullopt;
    }
    if (!in_stable_set(*source, kObservationRegionSources,
                       sizeof(kObservationRegionSources) / sizeof(kObservationRegionSources[0]))) {
        error = "observation visual region 'source' is not a known region source";
        return std::nullopt;
    }
    const auto *geometry_value = member(value, "geometry");
    if (geometry_value == nullptr) {
        error = "observation visual regions require a 'geometry' object";
        return std::nullopt;
    }
    auto geometry = decode_geometry(*geometry_value, error);
    if (!geometry) {
        return std::nullopt;
    }
    region.ref = std::move(*ref);
    region.source = std::move(*source);
    region.geometry = std::move(*geometry);
    region.text = std::move(*text);
    region.template_id = std::move(*template_id);
    return region;
}

mira::JsonValue encode_context_usage(const ContextUsage &usage) {
    auto object = make_object();
    put(object, "input_tokens", static_cast<std::int64_t>(usage.input_tokens));
    put(object, "window_tokens", static_cast<std::int64_t>(usage.window_tokens));
    put(object, "model", usage.model);
    return object;
}

bool decode_context_usage(const mira::JsonValue &object, const std::string &status,
                          std::optional<ContextUsage> &usage, std::string &error) {
    const auto *value = member(object, "context_usage");
    if (!value)
        return true;
    const auto input = integer_member(*value, "input_tokens");
    const auto window = integer_member(*value, "window_tokens");
    const auto model = string_member(*value, "model");
    if (status != "ok" || !value->is_object() || !input || *input < 0 || *input > 2000000000 ||
        !window || (*window != 0 && (*window < 2048 || *window > 2000000)) || !model ||
        model->empty() || model->size() > 1024) {
        error = "invalid context_usage on dialog turn";
        return false;
    }
    usage = ContextUsage{static_cast<std::uint64_t>(*input), static_cast<std::uint64_t>(*window),
                         *model};
    return true;
}

std::optional<DialogTurnEntry> decode_dialog_turn(const mira::JsonValue &value,
                                                  std::string &error) {
    if (!value.is_object()) {
        error = "session.chat.history turns must be objects";
        return std::nullopt;
    }
    DialogTurnEntry turn;
    auto turn_id = string_member(value, "turn_id");
    auto status = string_member(value, "status");
    auto user_text = string_member(value, "user_text");
    const auto sequence = integer_member(value, "sequence");
    const auto recorded = integer_member(value, "recorded_at_ms");
    if (!turn_id || turn_id->empty() || !status || !user_text || !sequence || *sequence < 1 ||
        !recorded || *recorded < 0) {
        error = "session.chat.history turns require 'turn_id', 'status', 'user_text', a positive "
                "'sequence' and a non-negative 'recorded_at_ms'";
        return std::nullopt;
    }
    if (!in_stable_set(*status, kDialogTurnStatuses,
                       sizeof(kDialogTurnStatuses) / sizeof(kDialogTurnStatuses[0]))) {
        error = "session.chat.history turn 'status' is not a known turn status";
        return std::nullopt;
    }
    turn.turn_id = std::move(*turn_id);
    turn.status = std::move(*status);
    turn.user_text = std::move(*user_text);
    turn.sequence = static_cast<std::uint64_t>(*sequence);
    turn.recorded_at_ms = *recorded;
    if (const auto *reply = member(value, "reply_text"); reply != nullptr) {
        const auto text = reply->as_string();
        if (!text) {
            error = "session.chat.history turn 'reply_text' must be a string";
            return std::nullopt;
        }
        if (turn.status != "ok") {
            error = "session.chat.history 'reply_text' is present exactly when status is 'ok'";
            return std::nullopt;
        }
        turn.reply_text = *text;
        turn.has_reply = true;
    } else if (turn.status == "ok") {
        error = "session.chat.history 'reply_text' is present exactly when status is 'ok'";
        return std::nullopt;
    }
    if (const auto *failure = member(value, "error"); failure != nullptr) {
        const auto text = failure->as_string();
        if (!text) {
            error = "session.chat.history turn 'error' must be a string";
            return std::nullopt;
        }
        if (turn.status != "failed") {
            error = "session.chat.history 'error' is present exactly when status is 'failed'";
            return std::nullopt;
        }
        turn.error = *text;
        turn.has_error = true;
    } else if (turn.status == "failed") {
        error = "session.chat.history 'error' is present exactly when status is 'failed'";
        return std::nullopt;
    }
    if (!decode_context_usage(value, turn.status, turn.context_usage, error))
        return std::nullopt;
    return turn;
}

std::optional<TaskStep> decode_step(const mira::JsonValue &value, std::string &error) {
    if (!value.is_object()) {
        error = "task.submit steps must be objects";
        return std::nullopt;
    }
    const auto kind_text = string_member(value, "op");
    if (!kind_text) {
        error = "task.submit step is missing 'op'";
        return std::nullopt;
    }
    TaskStep step;
    if (*kind_text == kStepRead) {
        step.kind = StepKind::FilesystemRead;
    } else if (*kind_text == kStepExecute) {
        step.kind = StepKind::ProcessExecute;
    } else {
        error = "task.submit step has unsupported op '" + *kind_text + "'";
        return std::nullopt;
    }
    auto argument = string_member(value, "arg");
    if (!argument || argument->empty()) {
        error = "task.submit step requires a non-empty 'arg'";
        return std::nullopt;
    }
    step.argument = std::move(*argument);
    return step;
}

mira::JsonValue encode_step_view(const StepView &step) {
    auto object = make_object();
    put(object, "index", static_cast<std::int64_t>(step.index));
    put(object, "kind", step.kind);
    put(object, "status", step.status);
    put(object, "operation_id", step.operation_id);
    put(object, "permission", step.permission);
    put(object, "ok", step.ok);
    put(object, "exit_code", static_cast<std::int64_t>(step.exit_code));
    put(object, "result", step.result);
    put(object, "result_truncated", step.result_truncated);
    put(object, "error", step.error);
    return object;
}

std::optional<StepView> decode_step_view(const mira::JsonValue &value, std::string &error) {
    if (!value.is_object()) {
        error = "task inspect steps must be objects";
        return std::nullopt;
    }
    StepView step;
    if (const auto index = integer_member(value, "index")) {
        step.index = static_cast<int>(*index);
    }
    if (auto text = string_member(value, "kind")) {
        step.kind = std::move(*text);
    }
    if (auto text = string_member(value, "status")) {
        step.status = std::move(*text);
    }
    if (auto text = string_member(value, "operation_id")) {
        step.operation_id = std::move(*text);
    }
    if (auto text = string_member(value, "permission")) {
        step.permission = std::move(*text);
    }
    if (const auto *flag = member(value, "ok"); flag != nullptr) {
        if (const auto boolean = flag->as_boolean()) {
            step.ok = *boolean;
        }
    }
    if (const auto code = integer_member(value, "exit_code")) {
        step.exit_code = static_cast<int>(*code);
    }
    if (auto text = string_member(value, "result")) {
        step.result = std::move(*text);
    }
    if (const auto *flag = member(value, "result_truncated"); flag != nullptr) {
        if (const auto boolean = flag->as_boolean()) {
            step.result_truncated = *boolean;
        }
    }
    if (auto text = string_member(value, "error")) {
        step.error = std::move(*text);
    }
    return step;
}

mira::JsonValue encode_inspect(const InspectTask &task) {
    auto object = make_object();
    put(object, "id", task.id);
    put(object, "goal", task.goal);
    put(object, "progress", task.progress);
    if (task.has_success) {
        put(object, "success", task.success);
    }
    mira::JsonValue::Array entries;
    for (const auto &step : task.steps) {
        entries.emplace_back(encode_step_view(step));
    }
    put(object, "steps", mira::JsonValue{std::move(entries)});
    return object;
}

mira::JsonValue encode_payload(const ResponsePayload &payload) {
    auto object = make_object();
    std::visit(
        [&object](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ServiceIdentity>) {
                put(object, "service", value.name);
                put(object, "mirage_version", value.mirage_version);
                put(object, "mira_core_version", value.mira_core_version);
                put(object, "host_status", value.host_status);
                put(object, "protocol", static_cast<std::int64_t>(value.protocol));
                if (value.events.has_value()) {
                    put(object, "events", *value.events);
                }
                if (value.permissions.has_value()) {
                    put(object, "permissions", *value.permissions);
                }
                if (value.sessions.has_value()) {
                    put(object, "sessions", *value.sessions);
                }
                if (value.workflows.has_value()) {
                    put(object, "workflows", *value.workflows);
                }
                if (value.observation.has_value()) {
                    put(object, "observation", *value.observation);
                }
                if (value.chat.has_value()) {
                    put(object, "chat", *value.chat);
                }
                if (value.policy.has_value()) {
                    put(object, "policy", *value.policy);
                }
                if (value.policy.has_value()) {
                    put(object, "policy", *value.policy);
                }
            } else if constexpr (std::is_same_v<T, TaskSubmitted>) {
                put(object, "task_id", value.task_id);
                if (value.session_id) {
                    put(object, "session_id", *value.session_id);
                }
            } else if constexpr (std::is_same_v<T, TaskList>) {
                mira::JsonValue::Array entries;
                for (const auto &task : value.tasks) {
                    auto entry = make_object();
                    put(entry, "id", task.id);
                    put(entry, "goal", task.goal);
                    put(entry, "progress", task.progress);
                    entries.emplace_back(std::move(entry));
                }
                put(object, "tasks", mira::JsonValue{std::move(entries)});
            } else if constexpr (std::is_same_v<T, InspectTask>) {
                put(object, "task", encode_inspect(value));
            } else if constexpr (std::is_same_v<T, TaskCancelled>) {
                auto cancelled = make_object();
                put(cancelled, "task_id", value.task_id);
                put(cancelled, "progress", value.progress);
                put(object, "task_cancelled", std::move(cancelled));
            } else if constexpr (std::is_same_v<T, TaskPaused>) {
                auto paused = make_object();
                put(paused, "task_id", value.task_id);
                put(paused, "progress", value.progress);
                put(object, "task_paused", std::move(paused));
            } else if constexpr (std::is_same_v<T, TaskResumed>) {
                auto resumed = make_object();
                put(resumed, "task_id", value.task_id);
                put(resumed, "progress", value.progress);
                put(object, "task_resumed", std::move(resumed));
            } else if constexpr (std::is_same_v<T, PermissionResponded>) {
                put(object, "request_id", value.request_id);
            } else if constexpr (std::is_same_v<T, PermissionPendingList>) {
                mira::JsonValue::Array entries;
                for (const auto &entry : value.pending) {
                    auto pending = make_object();
                    put(pending, "request_id", entry.request_id);
                    put(pending, "capability", entry.capability);
                    put(pending, "resource", entry.resource);
                    put(pending, "task_id", entry.task_id);
                    put(pending, "timeout_ms", entry.timeout_ms);
                    entries.emplace_back(std::move(pending));
                }
                put(object, "pending", mira::JsonValue{std::move(entries)});
            } else if constexpr (std::is_same_v<T, SessionList>) {
                mira::JsonValue::Array entries;
                for (const auto &session : value.sessions) {
                    auto entry = make_object();
                    put(entry, "id", session.id);
                    put(entry, "state", session.state);
                    put(entry, "created_at_ms", session.created_at_ms);
                    entries.emplace_back(std::move(entry));
                }
                put(object, "sessions", mira::JsonValue{std::move(entries)});
            } else if constexpr (std::is_same_v<T, SessionOpened>) {
                put(object, "session_id", value.session_id);
            } else if constexpr (std::is_same_v<T, SessionDeleted>) {
                put(object, "session_id", value.session_id);
                put(object, "deleted", true);
            } else if constexpr (std::is_same_v<T, SessionClosed>) {
                put(object, "session_id", value.session_id);
                put(object, "state", value.state);
            } else if constexpr (std::is_same_v<T, DialogTurnAccepted>) {
                put(object, "turn_id", value.turn_id);
                if (!value.replaces_turn_id.empty())
                    put(object, "replaces_turn_id", value.replaces_turn_id);
            } else if constexpr (std::is_same_v<T, DialogHistory>) {
                put(object, "session_id", value.session_id);
                mira::JsonValue::Array turns;
                for (const auto &turn : value.turns) {
                    auto entry = make_object();
                    put(entry, "turn_id", turn.turn_id);
                    put(entry, "status", turn.status);
                    put(entry, "user_text", turn.user_text);
                    if (turn.has_reply) {
                        put(entry, "reply_text", turn.reply_text);
                    }
                    if (turn.has_error) {
                        put(entry, "error", turn.error);
                    }
                    put(entry, "sequence", static_cast<std::int64_t>(turn.sequence));
                    put(entry, "recorded_at_ms", turn.recorded_at_ms);
                    if (turn.context_usage)
                        put(entry, "context_usage", encode_context_usage(*turn.context_usage));
                    turns.emplace_back(std::move(entry));
                }
                put(object, "turns", mira::JsonValue{std::move(turns)});
                put(object, "truncated", value.truncated);
            } else if constexpr (std::is_same_v<T, SessionHistory>) {
                put(object, "session_id", value.session_id);
                mira::JsonValue::Array entries;
                for (const auto &entry : value.entries) {
                    auto item = make_object();
                    put(item, "kind", entry.kind);
                    put(item, "text", entry.text);
                    put(item, "sequence", static_cast<std::int64_t>(entry.sequence));
                    put(item, "recorded_at_ms", entry.recorded_at_ms);
                    entries.emplace_back(std::move(item));
                }
                put(object, "entries", mira::JsonValue{std::move(entries)});
                put(object, "truncated", value.truncated);
            } else if constexpr (std::is_same_v<T, WorkflowList>) {
                mira::JsonValue::Array entries;
                for (const auto &workflow : value.workflows) {
                    auto entry = make_object();
                    put(entry, "workflow_id", workflow.workflow_id);
                    put(entry, "name", workflow.name);
                    put(entry, "head_digest", workflow.head_digest);
                    put(entry, "validation", workflow.validation);
                    put(entry, "runnable", workflow.runnable);
                    put(entry, "updated_at_ms", workflow.updated_at_ms);
                    entries.emplace_back(std::move(entry));
                }
                put(object, "workflows", mira::JsonValue{std::move(entries)});
            } else if constexpr (std::is_same_v<T, WorkflowSaved>) {
                put(object, "workflow_id", value.workflow_id);
                put(object, "digest", value.digest);
            } else if constexpr (std::is_same_v<T, WorkflowPublished>) {
                put(object, "workflow_id", value.workflow_id);
                put(object, "digest", value.digest);
                put(object, "dry_run_id", value.dry_run_id);
                put(object, "idempotent", value.idempotent);
            } else if constexpr (std::is_same_v<T, WorkflowDeleted>) {
                put(object, "workflow_id", value.workflow_id);
            } else if constexpr (std::is_same_v<T, WorkflowAtomCatalog>) {
                mira::JsonValue::Array entries;
                for (const auto &tool : value.tools) {
                    auto entry = make_object();
                    put(entry, "wire_name", tool.wire_name);
                    put(entry, "version", tool.version);
                    put(entry, "description", tool.description);
                    put(entry, "has_side_effects", tool.has_side_effects);
                    put(entry, "parameters_schema", embedded_json(tool.parameters_schema_json));
                    entries.emplace_back(std::move(entry));
                }
                put(object, "tools", mira::JsonValue{std::move(entries)});
            } else if constexpr (std::is_same_v<T, WorkflowRunList>) {
                mira::JsonValue::Array entries;
                for (const auto &run : value.runs) {
                    auto entry = make_object();
                    put(entry, "run_id", run.run_id);
                    put(entry, "workflow_id", run.workflow_id);
                    put(entry, "state", run.state);
                    put(entry, "run_epoch", static_cast<std::int64_t>(run.run_epoch));
                    put(entry, "created_at_ms", run.created_at_ms);
                    entries.emplace_back(std::move(entry));
                }
                put(object, "runs", mira::JsonValue{std::move(entries)});
            } else if constexpr (std::is_same_v<T, WorkflowRunStarted>) {
                put(object, "run_id", value.run_id);
            } else if constexpr (std::is_same_v<T, WorkflowRunCancelled>) {
                put(object, "run_id", value.run_id);
                put(object, "state", value.state);
            } else if constexpr (std::is_same_v<T, WorkflowDefinitionView>) {
                put(object, "workflow_id", value.workflow_id);
                put(object, "digest", value.digest);
                put(object, "definition", embedded_json(value.definition_json));
            } else if constexpr (std::is_same_v<T, ModelConfiguration>) {
                put(object, "model_settings", value.settings_json);
                if (!value.warning.empty())
                    put(object, "warning", value.warning);
            } else if constexpr (std::is_same_v<T, PolicyView>) {
                mira::JsonValue rules = make_object();
                for (const auto &[capability, rule] : value.rules) {
                    put(rules, capability, rule);
                }
                put(object, "rules", std::move(rules));
                mira::JsonValue::Array roots;
                for (const auto &root : value.read_roots) {
                    roots.emplace_back(root);
                }
                put(object, "read_roots", mira::JsonValue{std::move(roots)});
            } else if constexpr (std::is_same_v<T, ObservationView>) {
                put(object, "active_application", value.active_application);
                put(object, "active_window", value.active_window);
                put(object, "window_geometry", encode_geometry(value.window_geometry));
                put(object, "window_focused", value.window_focused);
                put(object, "focused_element", value.focused_element);
                put(object, "pointer_x", static_cast<std::int64_t>(value.pointer_x));
                put(object, "pointer_y", static_cast<std::int64_t>(value.pointer_y));
                put(object, "environment_state", value.environment_state);
                if (value.semantic.has_value()) {
                    auto semantic = make_object();
                    put(semantic, "application", value.semantic->application);
                    put(semantic, "window_title", value.semantic->window_title);
                    mira::JsonValue::Array nodes;
                    for (const auto &node : value.semantic->nodes) {
                        nodes.emplace_back(encode_node(node));
                    }
                    put(semantic, "nodes", mira::JsonValue{std::move(nodes)});
                    put(semantic, "truncated", value.semantic->truncated);
                    put(object, "semantic", std::move(semantic));
                }
                if (value.visual_snapshot_ref.has_value()) {
                    put(object, "visual_snapshot_ref", *value.visual_snapshot_ref);
                }
                if (value.visual_regions.has_value()) {
                    mira::JsonValue::Array regions;
                    for (const auto &region : *value.visual_regions) {
                        regions.emplace_back(encode_region(region));
                    }
                    put(object, "visual_regions", mira::JsonValue{std::move(regions)});
                }
            } else if constexpr (std::is_same_v<T, ShutdownAccepted>) {
                // No payload members beyond the ok envelope.
            }
        },
        payload);
    return object;
}

} // namespace

const char *step_kind_name(StepKind kind) {
    return kind == StepKind::FilesystemRead ? kStepRead : kStepExecute;
}

std::optional<StepKind> step_kind_from_name(std::string_view name) {
    if (name == kStepRead) {
        return StepKind::FilesystemRead;
    }
    if (name == kStepExecute) {
        return StepKind::ProcessExecute;
    }
    return std::nullopt;
}

std::string encode_request(std::uint64_t id, const Request &body) {
    auto object = make_object();
    put(object, "v", static_cast<std::int64_t>(kProtocolVersion));
    put(object, "id", static_cast<std::int64_t>(id));
    std::visit(
        [&object](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HelloRequest>) {
                put(object, "op", kOpHello);
            } else if constexpr (std::is_same_v<T, SubmitTaskRequest>) {
                put(object, "op", kOpSubmit);
                put(object, "goal", value.goal);
                mira::JsonValue::Array entries;
                for (const auto &step : value.steps) {
                    auto entry = make_object();
                    put(entry, "op", step_kind_name(step.kind));
                    put(entry, "arg", step.argument);
                    entries.emplace_back(std::move(entry));
                }
                put(object, "steps", mira::JsonValue{std::move(entries)});
                if (value.step_timeout) {
                    put(object, "step_timeout_ms",
                        static_cast<std::int64_t>(value.step_timeout->count()));
                }
                if (value.session_id) {
                    put(object, "session_id", *value.session_id);
                }
            } else if constexpr (std::is_same_v<T, ListTasksRequest>) {
                put(object, "op", kOpList);
            } else if constexpr (std::is_same_v<T, InspectTaskRequest>) {
                put(object, "op", kOpInspect);
                put(object, "task_id", value.task_id);
            } else if constexpr (std::is_same_v<T, CancelTaskRequest>) {
                put(object, "op", kOpCancel);
                put(object, "task_id", value.task_id);
            } else if constexpr (std::is_same_v<T, PauseTaskRequest>) {
                put(object, "op", kOpPause);
                put(object, "task_id", value.task_id);
            } else if constexpr (std::is_same_v<T, ResumeTaskRequest>) {
                put(object, "op", kOpResume);
                put(object, "task_id", value.task_id);
            } else if constexpr (std::is_same_v<T, ShutdownRequest>) {
                put(object, "op", kOpShutdown);
            } else if constexpr (std::is_same_v<T, SubscribeEventsRequest>) {
                put(object, "op", kOpSubscribe);
                if (value.chat_preview)
                    put(object, "chat_preview", true);
            } else if constexpr (std::is_same_v<T, UnsubscribeEventsRequest>) {
                put(object, "op", kOpUnsubscribe);
            } else if constexpr (std::is_same_v<T, RespondPermissionRequest>) {
                put(object, "op", kOpPermissionRespond);
                put(object, "request_id", value.request_id);
                put(object, "approved", value.approved);
            } else if constexpr (std::is_same_v<T, ListPermissionsRequest>) {
                put(object, "op", kOpPermissionList);
            } else if constexpr (std::is_same_v<T, ListSessionsRequest>) {
                put(object, "op", kOpSessionList);
            } else if constexpr (std::is_same_v<T, OpenSessionRequest>) {
                put(object, "op", kOpSessionOpen);
            } else if constexpr (std::is_same_v<T, DeleteSessionRequest>) {
                put(object, "op", kOpSessionDelete);
                put(object, "session_id", value.session_id);
            } else if constexpr (std::is_same_v<T, CloseSessionRequest>) {
                put(object, "op", kOpSessionClose);
                put(object, "session_id", value.session_id);
            } else if constexpr (std::is_same_v<T, SessionChatRequest>) {
                put(object, "op", kOpSessionChat);
                put(object, "session_id", value.session_id);
                put(object, "text", value.text);
                if (value.agent)
                    put(object, "agent", true);
                if (value.access != "default")
                    put(object, "access", value.access);
                if (!value.reasoning.empty())
                    put(object, "reasoning", value.reasoning);
                if (!value.replace_turn_id.empty())
                    put(object, "replace_turn_id", value.replace_turn_id);
            } else if constexpr (std::is_same_v<T, GetModelRequest>) {
                put(object, "op", "model.get");
            } else if constexpr (std::is_same_v<T, SetModelRequest>) {
                put(object, "op", "model.set");
                put(object, "settings", value.settings_json);
                if (value.api_key)
                    put(object, "api_key", *value.api_key);
            } else if constexpr (std::is_same_v<T, CancelChatRequest>) {
                put(object, "op", "session.chat.cancel");
                put(object, "session_id", value.session_id);
            } else if constexpr (std::is_same_v<T, ChatHistoryRequest>) {
                put(object, "op", kOpSessionChatHistory);
                put(object, "session_id", value.session_id);
                if (value.limit) {
                    put(object, "limit", static_cast<std::int64_t>(*value.limit));
                }
            } else if constexpr (std::is_same_v<T, SessionHistoryRequest>) {
                put(object, "op", kOpSessionHistory);
                put(object, "session_id", value.session_id);
                if (value.limit) {
                    put(object, "limit", static_cast<std::int64_t>(*value.limit));
                }
            } else if constexpr (std::is_same_v<T, WorkflowListRequest>) {
                put(object, "op", kOpWorkflowList);
            } else if constexpr (std::is_same_v<T, WorkflowSaveRequest>) {
                put(object, "op", kOpWorkflowSave);
                put(object, "definition", embedded_json(value.definition_json));
            } else if constexpr (std::is_same_v<T, WorkflowPublishRequest>) {
                put(object, "op", kOpWorkflowPublish);
                put(object, "definition", embedded_json(value.definition_json));
            } else if constexpr (std::is_same_v<T, WorkflowDeleteRequest>) {
                put(object, "op", kOpWorkflowDelete);
                put(object, "workflow_id", value.workflow_id);
            } else if constexpr (std::is_same_v<T, WorkflowAtomCatalogRequest>) {
                put(object, "op", kOpWorkflowAtomCatalog);
            } else if constexpr (std::is_same_v<T, WorkflowRunsRequest>) {
                put(object, "op", kOpWorkflowRuns);
            } else if constexpr (std::is_same_v<T, WorkflowRunRequest>) {
                put(object, "op", kOpWorkflowRun);
                put(object, "workflow_id", value.workflow_id);
                if (!value.digest.empty()) {
                    put(object, "digest", value.digest);
                }
                if (!value.parameters_json.empty()) {
                    put(object, "parameters", embedded_json(value.parameters_json));
                }
                if (!value.policy.empty()) {
                    put(object, "policy", value.policy);
                }
            } else if constexpr (std::is_same_v<T, WorkflowCancelRunRequest>) {
                put(object, "op", kOpWorkflowCancel);
                put(object, "run_id", value.run_id);
            } else if constexpr (std::is_same_v<T, WorkflowGetRequest>) {
                put(object, "op", kOpWorkflowGet);
                put(object, "workflow_id", value.workflow_id);
            } else if constexpr (std::is_same_v<T, GetPolicyRequest>) {
                put(object, "op", kOpPolicyGet);
            } else if constexpr (std::is_same_v<T, SetPolicyRequest>) {
                put(object, "op", kOpPolicySet);
                mira::JsonValue rules = make_object();
                for (const auto &[capability, rule] : value.rules) {
                    put(rules, capability, rule);
                }
                put(object, "rules", std::move(rules));
                if (value.has_read_roots) {
                    mira::JsonValue::Array roots;
                    for (const auto &root : value.read_roots) {
                        roots.emplace_back(root);
                    }
                    put(object, "read_roots", mira::JsonValue{std::move(roots)});
                }
            } else if constexpr (std::is_same_v<T, DesktopObserveRequest>) {
                put(object, "op", kOpDesktopObserve);
                // Both flags always write: the defaults (semantic on, visual
                // off) are part of the request's pinned canonical form, and
                // the explicit form keeps the golden vectors unambiguous.
                put(object, "semantic", value.semantic);
                put(object, "visual", value.visual);
            }
        },
        body);
    return mira::to_json_string(object);
}

RequestDecode decode_request(std::string_view payload) {
    RequestDecode result;
    auto parsed = mira::parse_json(payload);
    if (!parsed) {
        result.error = "payload is not valid JSON: " + parsed.error().safe_message;
        return result;
    }
    if (!parsed.value().is_object()) {
        result.error = "request payload must be a JSON object";
        return result;
    }
    const auto &object = parsed.value();
    if (const auto version = integer_member(object, "v");
        !version || *version != kProtocolVersion) {
        result.error = "unsupported protocol version";
        return result;
    }
    const auto id = integer_member(object, "id");
    if (!id || *id < 0) {
        result.error = "request is missing a non-negative 'id'";
        return result;
    }
    const auto op = string_member(object, "op");
    if (!op) {
        result.error = "request is missing 'op'";
        return result;
    }
    result.id = static_cast<std::uint64_t>(*id);
    if (*op == kOpHello) {
        result.body = HelloRequest{};
    } else if (*op == kOpList) {
        result.body = ListTasksRequest{};
    } else if (*op == kOpShutdown) {
        result.body = ShutdownRequest{};
    } else if (*op == kOpSubscribe) {
        SubscribeEventsRequest subscribe;
        if (member(object, "chat_preview")) {
            const auto flag = boolean_member(object, "chat_preview");
            if (!flag) {
                result.error = "events.subscribe chat_preview must be boolean";
                return result;
            }
            subscribe.chat_preview = *flag;
        }
        result.body = subscribe;
    } else if (*op == kOpUnsubscribe) {
        result.body = UnsubscribeEventsRequest{};
    } else if (*op == kOpSubmit) {
        SubmitTaskRequest submit;
        const auto goal = string_member(object, "goal");
        if (!goal) {
            // Emptiness is a semantic rejection (invalid_argument at the
            // service); the protocol layer only fixes presence and type.
            result.error = "task.submit requires a 'goal' string";
            return result;
        }
        submit.goal = *goal;
        if (const auto *steps = member(object, "steps"); steps != nullptr) {
            if (!steps->is_array()) {
                result.error = "task.submit 'steps' must be an array";
                return result;
            }
            for (const auto &entry : *steps->as_array()) {
                std::string step_error;
                auto step = decode_step(entry, step_error);
                if (!step) {
                    result.error = step_error;
                    return result;
                }
                submit.steps.push_back(std::move(*step));
            }
        }
        if (const auto timeout = integer_member(object, "step_timeout_ms")) {
            if (*timeout <= 0) {
                result.error = "task.submit 'step_timeout_ms' must be positive";
                return result;
            }
            submit.step_timeout = std::chrono::milliseconds(*timeout);
        }
        if (const auto session = string_member(object, "session_id")) {
            if (session->empty()) {
                result.error = "task.submit 'session_id' must be non-empty";
                return result;
            }
            submit.session_id = std::move(*session);
        }
        result.body = std::move(submit);
    } else if (*op == kOpInspect) {
        InspectTaskRequest inspect;
        const auto task_id = string_member(object, "task_id");
        if (!task_id || task_id->empty()) {
            result.error = "task.inspect requires a non-empty 'task_id'";
            return result;
        }
        inspect.task_id = *task_id;
        result.body = std::move(inspect);
    } else if (*op == kOpCancel) {
        CancelTaskRequest cancel;
        const auto task_id = string_member(object, "task_id");
        if (!task_id || task_id->empty()) {
            result.error = "task.cancel requires a non-empty 'task_id'";
            return result;
        }
        cancel.task_id = *task_id;
        result.body = std::move(cancel);
    } else if (*op == kOpPause) {
        PauseTaskRequest pause;
        const auto task_id = string_member(object, "task_id");
        if (!task_id || task_id->empty()) {
            result.error = "task.pause requires a non-empty 'task_id'";
            return result;
        }
        pause.task_id = *task_id;
        result.body = std::move(pause);
    } else if (*op == kOpResume) {
        ResumeTaskRequest resume;
        const auto task_id = string_member(object, "task_id");
        if (!task_id || task_id->empty()) {
            result.error = "task.resume requires a non-empty 'task_id'";
            return result;
        }
        resume.task_id = *task_id;
        result.body = std::move(resume);
    } else if (*op == kOpPermissionRespond) {
        RespondPermissionRequest respond;
        const auto request_id = string_member(object, "request_id");
        if (!request_id || request_id->empty()) {
            result.error = "permission.respond requires a non-empty 'request_id'";
            return result;
        }
        respond.request_id = *request_id;
        const auto *approved = member(object, "approved");
        const auto approved_flag = approved == nullptr ? std::nullopt : approved->as_boolean();
        if (!approved_flag) {
            result.error = "permission.respond requires an 'approved' boolean";
            return result;
        }
        respond.approved = *approved_flag;
        result.body = std::move(respond);
    } else if (*op == kOpPermissionList) {
        result.body = ListPermissionsRequest{};
    } else if (*op == kOpSessionList) {
        result.body = ListSessionsRequest{};
    } else if (*op == kOpSessionOpen) {
        result.body = OpenSessionRequest{};
    } else if (*op == kOpSessionDelete) {
        const auto deleted_id = string_member(object, "session_id");
        if (!deleted_id || deleted_id->empty()) {
            result.error = "session.delete requires session_id";
            return result;
        }
        result.body = DeleteSessionRequest{*deleted_id};
    } else if (*op == kOpSessionClose) {
        CloseSessionRequest close;
        const auto session_id = string_member(object, "session_id");
        if (!session_id || session_id->empty()) {
            result.error = "session.close requires a non-empty 'session_id'";
            return result;
        }
        close.session_id = *session_id;
        result.body = std::move(close);
    } else if (*op == kOpSessionChat) {
        SessionChatRequest chat;
        const auto session_id = string_member(object, "session_id");
        if (!session_id || session_id->empty()) {
            result.error = "session.chat requires a non-empty 'session_id'";
            return result;
        }
        chat.session_id = *session_id;
        const auto text = string_member(object, "text");
        if (!text || text->empty()) {
            result.error = "session.chat requires a non-empty 'text'";
            return result;
        }
        chat.text = *text;
        if (const auto *flag = member(object, "agent")) {
            if (!flag->is_boolean()) {
                result.error = "agent must be boolean";
                return result;
            }
            chat.agent = *flag->as_boolean();
        }
        if (const auto *value = member(object, "access")) {
            const auto *text_value = value->as_string();
            if (!text_value || (*text_value != "default" && *text_value != "read_only")) {
                result.error = "invalid access mode";
                return result;
            }
            chat.access = *text_value;
        }
        if (const auto *value = member(object, "reasoning")) {
            const auto *text_value = value->as_string();
            if (!text_value ||
                (!text_value->empty() && *text_value != "minimal" && *text_value != "low" &&
                 *text_value != "medium" && *text_value != "high")) {
                result.error = "invalid reasoning level";
                return result;
            }
            chat.reasoning = *text_value;
        }
        if (const auto *value = member(object, "replace_turn_id")) {
            const auto *replacement = value->as_string();
            if (!replacement || replacement->empty() || replacement->size() > 128) {
                result.error = "replace_turn_id must be a non-empty bounded string";
                return result;
            }
            chat.replace_turn_id = *replacement;
        }
        result.body = std::move(chat);
    } else if (*op == "model.get") {
        result.body = GetModelRequest{};
    } else if (*op == "model.set") {
        const auto settings = string_member(object, "settings");
        if (!settings || settings->size() > 65536) {
            result.error = "model.set requires bounded settings";
            return result;
        }
        SetModelRequest request{*settings};
        if (const auto *value = member(object, "api_key")) {
            const auto *key = value->as_string();
            if (!key || key->size() > 2048 ||
                std::any_of(key->begin(), key->end(),
                            [](unsigned char c) { return c < 33 || c > 126; })) {
                result.error = "invalid API Key";
                return result;
            }
            request.api_key = *key;
        }
        result.body = std::move(request);
    } else if (*op == "session.chat.cancel") {
        const auto session = string_member(object, "session_id");
        if (!session || session->empty()) {
            result.error = "cancel requires session_id";
            return result;
        }
        result.body = CancelChatRequest{*session};
    } else if (*op == kOpSessionChatHistory) {
        ChatHistoryRequest history;
        const auto session_id = string_member(object, "session_id");
        if (!session_id || session_id->empty()) {
            result.error = "session.chat.history requires a non-empty 'session_id'";
            return result;
        }
        history.session_id = *session_id;
        if (const auto limit = integer_member(object, "limit")) {
            if (*limit <= 0) {
                result.error = "session.chat.history 'limit' must be positive";
                return result;
            }
            history.limit = static_cast<int>(*limit);
        }
        result.body = std::move(history);
    } else if (*op == kOpSessionHistory) {
        SessionHistoryRequest history;
        const auto session_id = string_member(object, "session_id");
        if (!session_id || session_id->empty()) {
            result.error = "session.history requires a non-empty 'session_id'";
            return result;
        }
        history.session_id = *session_id;
        if (const auto limit = integer_member(object, "limit")) {
            if (*limit <= 0) {
                result.error = "session.history 'limit' must be positive";
                return result;
            }
            history.limit = static_cast<int>(*limit);
        }
        result.body = std::move(history);
    } else if (*op == kOpWorkflowList) {
        result.body = WorkflowListRequest{};
    } else if (*op == kOpWorkflowSave) {
        WorkflowSaveRequest save;
        auto definition = object_member_text(object, "definition");
        if (!definition || !member(object, "definition")->is_object()) {
            result.error = "workflow.save requires a 'definition' object";
            return result;
        }
        save.definition_json = std::move(*definition);
        result.body = std::move(save);
    } else if (*op == kOpWorkflowPublish) {
        WorkflowPublishRequest publish;
        auto definition = object_member_text(object, "definition");
        if (!definition || !member(object, "definition")->is_object()) {
            result.error = "workflow.publish requires a 'definition' object";
            return result;
        }
        publish.definition_json = std::move(*definition);
        result.body = std::move(publish);
    } else if (*op == kOpWorkflowDelete) {
        WorkflowDeleteRequest remove;
        const auto workflow_id = string_member(object, "workflow_id");
        if (!workflow_id || workflow_id->empty()) {
            result.error = "workflow.delete requires a non-empty 'workflow_id'";
            return result;
        }
        remove.workflow_id = *workflow_id;
        result.body = std::move(remove);
    } else if (*op == kOpWorkflowAtomCatalog) {
        result.body = WorkflowAtomCatalogRequest{};
    } else if (*op == kOpWorkflowRuns) {
        result.body = WorkflowRunsRequest{};
    } else if (*op == kOpWorkflowRun) {
        WorkflowRunRequest run;
        const auto workflow_id = string_member(object, "workflow_id");
        if (!workflow_id || workflow_id->empty()) {
            result.error = "workflow.run requires a non-empty 'workflow_id'";
            return result;
        }
        run.workflow_id = *workflow_id;
        if (const auto digest = string_member(object, "digest")) {
            if (digest->empty()) {
                result.error = "workflow.run 'digest' must be non-empty";
                return result;
            }
            run.digest = *digest;
        }
        auto parameters = object_member_text(object, "parameters");
        if (parameters.has_value() && !member(object, "parameters")->is_object()) {
            result.error = "workflow.run 'parameters' must be an object";
            return result;
        }
        if (parameters) {
            run.parameters_json = std::move(*parameters);
        }
        if (const auto policy = string_member(object, "policy")) {
            if (!in_stable_set(*policy, kWorkflowPolicyNames,
                               sizeof(kWorkflowPolicyNames) / sizeof(kWorkflowPolicyNames[0]))) {
                result.error = "workflow.run 'policy' is not a known policy name";
                return result;
            }
            run.policy = *policy;
        }
        result.body = std::move(run);
    } else if (*op == kOpWorkflowCancel) {
        WorkflowCancelRunRequest cancel;
        const auto run_id = string_member(object, "run_id");
        if (!run_id || run_id->empty()) {
            result.error = "workflow.cancel requires a non-empty 'run_id'";
            return result;
        }
        cancel.run_id = *run_id;
        result.body = std::move(cancel);
    } else if (*op == kOpWorkflowGet) {
        WorkflowGetRequest get;
        const auto workflow_id = string_member(object, "workflow_id");
        if (!workflow_id || workflow_id->empty()) {
            result.error = "workflow.get requires a non-empty 'workflow_id'";
            return result;
        }
        get.workflow_id = *workflow_id;
        result.body = std::move(get);
    } else if (*op == kOpPolicyGet) {
        result.body = GetPolicyRequest{};
    } else if (*op == kOpPolicySet) {
        SetPolicyRequest set_policy;
        const auto *rules = member(object, "rules");
        if (rules == nullptr || !rules->is_object() || rules->as_object() == nullptr) {
            result.error = "policy.set requires a 'rules' object";
            return result;
        }
        for (const auto &[capability, value] : *rules->as_object()) {
            if (!in_stable_set(capability, kCapabilityNames,
                               sizeof(kCapabilityNames) / sizeof(kCapabilityNames[0]))) {
                result.error = "policy.set 'rules' keys must be DEC-010 capability names";
                return result;
            }
            const auto *text = value.as_string();
            if (text == nullptr ||
                !in_stable_set(*text, kRuleNames, sizeof(kRuleNames) / sizeof(kRuleNames[0]))) {
                result.error = "policy.set rule values must be \"allow\", \"confirm\" or "
                               "\"deny\"";
                return result;
            }
            set_policy.rules.insert_or_assign(capability, *text);
        }
        if (const auto *roots = member(object, "read_roots"); roots != nullptr) {
            if (!roots->is_array() || roots->as_array()->size() > 64) {
                result.error = "policy.set 'read_roots' must be an array of at most 64 strings";
                return result;
            }
            for (const auto &entry : *roots->as_array()) {
                const auto *text = entry.as_string();
                if (text == nullptr || text->empty() || text->size() > 4096) {
                    result.error = "policy.set 'read_roots' entries must be strings of at most "
                                   "4096 bytes";
                    return result;
                }
                set_policy.read_roots.push_back(*text);
            }
            set_policy.has_read_roots = true;
        }
        result.body = std::move(set_policy);
    } else if (*op == kOpDesktopObserve) {
        DesktopObserveRequest observe;
        // Both flags are optional on the wire (absent keeps the default:
        // semantic on, visual off) and must be booleans when present.
        if (const auto *semantic = member(object, "semantic"); semantic != nullptr) {
            const auto flag = semantic->as_boolean();
            if (!flag) {
                result.error = "desktop.observe 'semantic' must be a boolean";
                return result;
            }
            observe.semantic = *flag;
        }
        if (const auto *visual = member(object, "visual"); visual != nullptr) {
            const auto flag = visual->as_boolean();
            if (!flag) {
                result.error = "desktop.observe 'visual' must be a boolean";
                return result;
            }
            observe.visual = *flag;
        }
        result.body = observe;
    } else {
        result.error = "unknown op '" + *op + "'";
        return result;
    }
    result.ok = true;
    return result;
}

std::string encode_response(const Response &response) {
    auto object = make_object();
    put(object, "v", static_cast<std::int64_t>(kProtocolVersion));
    put(object, "id", static_cast<std::int64_t>(response.id));
    put(object, "ok", response.ok);
    if (response.ok) {
        auto payload = encode_payload(response.payload);
        if (auto *members = payload.as_object(); members != nullptr) {
            for (auto &entry : *members) {
                object.set(entry.first, std::move(entry.second));
            }
        }
    } else {
        auto error = make_object();
        put(error, "code", response.error.code);
        put(error, "message", response.error.message);
        put(object, "error", std::move(error));
    }
    return mira::to_json_string(object);
}

ResponseDecode decode_response(std::string_view payload) {
    ResponseDecode result;
    auto parsed = mira::parse_json(payload);
    if (!parsed) {
        result.error = "payload is not valid JSON: " + parsed.error().safe_message;
        return result;
    }
    if (!parsed.value().is_object()) {
        result.error = "response payload must be a JSON object";
        return result;
    }
    const auto &object = parsed.value();
    if (const auto version = integer_member(object, "v");
        !version || *version != kProtocolVersion) {
        result.error = "unsupported protocol version";
        return result;
    }
    const auto id = integer_member(object, "id");
    if (!id || *id < 0) {
        result.error = "response is missing a non-negative 'id'";
        return result;
    }
    Response &response = result.response;
    response.id = static_cast<std::uint64_t>(*id);
    const auto *ok = member(object, "ok");
    if (ok == nullptr) {
        result.error = "response is missing 'ok'";
        return result;
    }
    if (const auto boolean = ok->as_boolean()) {
        response.ok = *boolean;
    } else {
        result.error = "response 'ok' must be a boolean";
        return result;
    }
    if (!response.ok) {
        const auto *error = member(object, "error");
        if (error == nullptr || !error->is_object()) {
            result.error = "failed response is missing an 'error' object";
            return result;
        }
        auto code = string_member(*error, "code");
        auto message = string_member(*error, "message");
        if (!code || !message) {
            result.error = "response error requires 'code' and 'message'";
            return result;
        }
        response.error = {std::move(*code), std::move(*message)};
        result.ok = true;
        return result;
    }
    if (const auto *service = member(object, "service"); service != nullptr) {
        ServiceIdentity identity;
        auto name = string_member(object, "service");
        auto mirage_version = string_member(object, "mirage_version");
        auto mira_core = string_member(object, "mira_core_version");
        auto host_status = string_member(object, "host_status");
        const auto protocol = integer_member(object, "protocol");
        if (!name || !mirage_version || !mira_core || !host_status || !protocol) {
            result.error = "hello response is missing identity members";
            return result;
        }
        identity.name = std::move(*name);
        identity.mirage_version = std::move(*mirage_version);
        identity.mira_core_version = std::move(*mira_core);
        identity.host_status = std::move(*host_status);
        identity.protocol = static_cast<int>(*protocol);
        // DEC-012 capability member: engaged servers always write it, the
        // decode defaults to disengaged (consumers read value_or(false)).
        if (const auto *events = member(object, "events"); events != nullptr) {
            const auto flag = events->as_boolean();
            if (!flag) {
                result.error = "hello response 'events' must be a boolean";
                return result;
            }
            identity.events = *flag;
        }
        // DEC-020 permission-capability member: same discipline as `events`.
        if (const auto *permissions = member(object, "permissions"); permissions != nullptr) {
            const auto flag = permissions->as_boolean();
            if (!flag) {
                result.error = "hello response 'permissions' must be a boolean";
                return result;
            }
            identity.permissions = *flag;
        }
        // DEC-021 session-face capability member: same discipline as
        // `events`.
        if (const auto *sessions = member(object, "sessions"); sessions != nullptr) {
            const auto flag = sessions->as_boolean();
            if (!flag) {
                result.error = "hello response 'sessions' must be a boolean";
                return result;
            }
            identity.sessions = *flag;
        }
        // DEC-023 workflow-face capability member: same discipline as
        // `events`.
        if (const auto *workflows = member(object, "workflows"); workflows != nullptr) {
            const auto flag = workflows->as_boolean();
            if (!flag) {
                result.error = "hello response 'workflows' must be a boolean";
                return result;
            }
            identity.workflows = *flag;
        }
        // DEC-026 observation-face capability member: same discipline as
        // `events`.
        if (const auto *observation = member(object, "observation"); observation != nullptr) {
            const auto flag = observation->as_boolean();
            if (!flag) {
                result.error = "hello response 'observation' must be a boolean";
                return result;
            }
            identity.observation = *flag;
        }
        // DEC-027 dialog-face capability member: same discipline as `events`.
        if (const auto *chat = member(object, "chat"); chat != nullptr) {
            const auto flag = chat->as_boolean();
            if (!flag) {
                result.error = "hello response 'chat' must be a boolean";
                return result;
            }
            identity.chat = *flag;
        }
        // M5-07 policy-face capability member: same discipline as `events`.
        if (const auto *policy = member(object, "policy"); policy != nullptr) {
            const auto flag = policy->as_boolean();
            if (!flag) {
                result.error = "hello response 'policy' must be a boolean";
                return result;
            }
            identity.policy = *flag;
        }
        // M5-07 policy-face capability member: same discipline as `events`.
        if (const auto *policy = member(object, "policy"); policy != nullptr) {
            const auto flag = policy->as_boolean();
            if (!flag) {
                result.error = "hello response 'policy' must be a boolean";
                return result;
            }
            identity.policy = *flag;
        }
        response.payload = std::move(identity);
    } else if (const auto *task_id = member(object, "task_id"); task_id != nullptr) {
        auto id_text = string_member(object, "task_id");
        if (!id_text || id_text->empty()) {
            result.error = "task.submit response requires a non-empty 'task_id'";
            return result;
        }
        TaskSubmitted submitted;
        submitted.task_id = std::move(*id_text);
        if (const auto session = string_member(object, "session_id")) {
            if (session->empty()) {
                result.error = "task.submit response 'session_id' must be non-empty";
                return result;
            }
            submitted.session_id = std::move(*session);
        }
        response.payload = std::move(submitted);
    } else if (const auto *tasks = member(object, "tasks"); tasks != nullptr) {
        if (!tasks->is_array()) {
            result.error = "task.list 'tasks' must be an array";
            return result;
        }
        TaskList list;
        for (const auto &entry : *tasks->as_array()) {
            if (!entry.is_object()) {
                result.error = "task.list entries must be objects";
                return result;
            }
            TaskSummary summary;
            auto id_text = string_member(entry, "id");
            auto goal = string_member(entry, "goal");
            auto progress = string_member(entry, "progress");
            if (!id_text || !goal || !progress) {
                result.error = "task.list entries require id, goal and progress";
                return result;
            }
            summary.id = std::move(*id_text);
            summary.goal = std::move(*goal);
            summary.progress = std::move(*progress);
            list.tasks.push_back(std::move(summary));
        }
        response.payload = std::move(list);
    } else if (const auto *task = member(object, "task"); task != nullptr) {
        if (!task->is_object()) {
            result.error = "task.inspect 'task' must be an object";
            return result;
        }
        InspectTask inspect;
        auto id_text = string_member(*task, "id");
        auto goal = string_member(*task, "goal");
        auto progress = string_member(*task, "progress");
        if (!id_text || !goal || !progress) {
            result.error = "task.inspect requires id, goal and progress";
            return result;
        }
        inspect.id = std::move(*id_text);
        inspect.goal = std::move(*goal);
        inspect.progress = std::move(*progress);
        if (const auto *success = member(*task, "success"); success != nullptr) {
            if (const auto boolean = success->as_boolean()) {
                inspect.has_success = true;
                inspect.success = *boolean;
            } else {
                result.error = "task.inspect 'success' must be a boolean";
                return result;
            }
        }
        if (const auto *steps = member(*task, "steps"); steps != nullptr) {
            if (!steps->is_array()) {
                result.error = "task.inspect 'steps' must be an array";
                return result;
            }
            for (const auto &entry : *steps->as_array()) {
                std::string step_error;
                auto step = decode_step_view(entry, step_error);
                if (!step) {
                    result.error = step_error;
                    return result;
                }
                inspect.steps.push_back(std::move(*step));
            }
        }
        response.payload = std::move(inspect);
    } else if (const auto *cancelled = member(object, "task_cancelled"); cancelled != nullptr) {
        if (!cancelled->is_object()) {
            result.error = "task.cancel 'task_cancelled' must be an object";
            return result;
        }
        TaskCancelled acknowledgement;
        auto id_text = string_member(*cancelled, "task_id");
        auto progress = string_member(*cancelled, "progress");
        if (!id_text || id_text->empty() || !progress) {
            result.error = "task.cancel requires 'task_id' and 'progress'";
            return result;
        }
        acknowledgement.task_id = std::move(*id_text);
        acknowledgement.progress = std::move(*progress);
        response.payload = std::move(acknowledgement);
    } else if (const auto *paused = member(object, "task_paused"); paused != nullptr) {
        if (!paused->is_object()) {
            result.error = "task.pause 'task_paused' must be an object";
            return result;
        }
        TaskPaused acknowledgement;
        auto id_text = string_member(*paused, "task_id");
        auto progress = string_member(*paused, "progress");
        if (!id_text || id_text->empty() || !progress) {
            result.error = "task.pause requires 'task_id' and 'progress'";
            return result;
        }
        acknowledgement.task_id = std::move(*id_text);
        acknowledgement.progress = std::move(*progress);
        response.payload = std::move(acknowledgement);
    } else if (const auto *resumed = member(object, "task_resumed"); resumed != nullptr) {
        if (!resumed->is_object()) {
            result.error = "task.resume 'task_resumed' must be an object";
            return result;
        }
        TaskResumed acknowledgement;
        auto id_text = string_member(*resumed, "task_id");
        auto progress = string_member(*resumed, "progress");
        if (!id_text || id_text->empty() || !progress) {
            result.error = "task.resume requires 'task_id' and 'progress'";
            return result;
        }
        acknowledgement.task_id = std::move(*id_text);
        acknowledgement.progress = std::move(*progress);
        response.payload = std::move(acknowledgement);
    } else if (const auto *request_id = member(object, "request_id"); request_id != nullptr) {
        auto id_text = string_member(object, "request_id");
        if (!id_text || id_text->empty()) {
            result.error = "permission.respond response requires a non-empty 'request_id'";
            return result;
        }
        response.payload = PermissionResponded{std::move(*id_text)};
    } else if (const auto *pending = member(object, "pending"); pending != nullptr) {
        if (!pending->is_array()) {
            result.error = "permission.list 'pending' must be an array";
            return result;
        }
        PermissionPendingList list;
        for (const auto &entry : *pending->as_array()) {
            if (!entry.is_object()) {
                result.error = "permission.list entries must be objects";
                return result;
            }
            PendingPermission permission;
            auto entry_id = string_member(entry, "request_id");
            auto entry_capability = string_member(entry, "capability");
            auto entry_resource = string_member(entry, "resource");
            auto entry_task_id = string_member(entry, "task_id");
            const auto timeout = integer_member(entry, "timeout_ms");
            if (!entry_id || entry_id->empty() || !entry_capability || !entry_resource ||
                !entry_task_id || entry_task_id->empty() || !timeout) {
                result.error = "permission.list entries require 'request_id', 'capability', "
                               "'resource', 'task_id' and 'timeout_ms'";
                return result;
            }
            if (*timeout <= 0) {
                result.error = "permission.list entry 'timeout_ms' must be positive";
                return result;
            }
            if (!in_stable_set(*entry_capability, kCapabilityNames,
                               sizeof(kCapabilityNames) / sizeof(kCapabilityNames[0]))) {
                result.error = "permission.list entry 'capability' is not a known capability";
                return result;
            }
            permission.request_id = std::move(*entry_id);
            permission.capability = std::move(*entry_capability);
            permission.resource = std::move(*entry_resource);
            permission.task_id = std::move(*entry_task_id);
            permission.timeout_ms = *timeout;
            list.pending.push_back(std::move(permission));
        }
        response.payload = std::move(list);
    } else if (const auto *entries = member(object, "entries"); entries != nullptr) {
        // SessionHistory discriminates on "entries"; it also carries
        // "session_id", so this branch must precede the SessionOpened one.
        if (!entries->is_array()) {
            result.error = "session.history 'entries' must be an array";
            return result;
        }
        auto session_id = string_member(object, "session_id");
        if (!session_id || session_id->empty()) {
            result.error = "session.history requires a non-empty 'session_id'";
            return result;
        }
        const auto *truncated = member(object, "truncated");
        const auto truncated_flag = truncated == nullptr ? std::nullopt : truncated->as_boolean();
        if (!truncated_flag) {
            result.error = "session.history requires a 'truncated' boolean";
            return result;
        }
        SessionHistory history;
        history.session_id = std::move(*session_id);
        history.truncated = *truncated_flag;
        for (const auto &item : *entries->as_array()) {
            if (!item.is_object()) {
                result.error = "session.history entries must be objects";
                return result;
            }
            SessionHistoryEntry entry;
            auto kind = string_member(item, "kind");
            auto text = string_member(item, "text");
            const auto sequence = integer_member(item, "sequence");
            const auto recorded = integer_member(item, "recorded_at_ms");
            if (!kind || !text || kind->empty() || text->empty() || !sequence || *sequence < 1 ||
                !recorded || *recorded < 0) {
                result.error = "session.history entries require 'kind', 'text', a positive "
                               "'sequence' and a non-negative 'recorded_at_ms'";
                return result;
            }
            if (!in_stable_set(*kind, kSessionMessageKinds,
                               sizeof(kSessionMessageKinds) / sizeof(kSessionMessageKinds[0]))) {
                result.error = "session.history entry 'kind' is not a known message kind";
                return result;
            }
            entry.kind = std::move(*kind);
            entry.text = std::move(*text);
            entry.sequence = static_cast<std::uint64_t>(*sequence);
            entry.recorded_at_ms = *recorded;
            history.entries.push_back(std::move(entry));
        }
        response.payload = std::move(history);
    } else if (const auto settings = string_member(object, "model_settings")) {
        if (settings->size() > 65536) {
            result.error = "model settings exceed budget";
            return result;
        }
        response.payload =
            ModelConfiguration{*settings, string_member(object, "warning").value_or("")};
    } else if (const auto *rules = member(object, "rules"); rules != nullptr) {
        // PolicyView discriminates on "rules".
        if (!rules->is_object() || rules->as_object() == nullptr) {
            result.error = "policy.get 'rules' must be an object";
            return result;
        }
        PolicyView view;
        for (const auto &[capability, value] : *rules->as_object()) {
            if (!in_stable_set(capability, kCapabilityNames,
                               sizeof(kCapabilityNames) / sizeof(kCapabilityNames[0]))) {
                result.error = "policy.get 'rules' keys must be DEC-010 capability names";
                return result;
            }
            const auto *text = value.as_string();
            if (text == nullptr ||
                !in_stable_set(*text, kRuleNames, sizeof(kRuleNames) / sizeof(kRuleNames[0]))) {
                result.error = "policy.get rule values must be \"allow\", \"confirm\" or "
                               "\"deny\"";
                return result;
            }
            view.rules.insert_or_assign(capability, *text);
        }
        if (const auto *roots = member(object, "read_roots"); roots != nullptr) {
            if (!roots->is_array() || roots->as_array()->size() > 64) {
                result.error = "policy.get 'read_roots' must be an array of at most 64 strings";
                return result;
            }
            for (const auto &entry : *roots->as_array()) {
                const auto *text = entry.as_string();
                if (text == nullptr || text->size() > 4096) {
                    result.error = "policy.get 'read_roots' entries must be strings of at most "
                                   "4096 bytes";
                    return result;
                }
                view.read_roots.push_back(*text);
            }
        }
        response.payload = std::move(view);
    } else if (const auto *turn_id = member(object, "turn_id"); turn_id != nullptr) {
        auto id_text = string_member(object, "turn_id");
        if (!id_text || id_text->empty()) {
            result.error = "session.chat response requires a non-empty 'turn_id'";
            return result;
        }
        DialogTurnAccepted accepted{std::move(*id_text)};
        if (const auto *value = member(object, "replaces_turn_id")) {
            const auto *replacement = value->as_string();
            if (!replacement || replacement->empty() || replacement->size() > 128) {
                result.error = "invalid replaces_turn_id";
                return result;
            }
            accepted.replaces_turn_id = *replacement;
        }
        response.payload = std::move(accepted);
    } else if (const auto *turns = member(object, "turns"); turns != nullptr) {
        if (!turns->is_array()) {
            result.error = "session.chat.history 'turns' must be an array";
            return result;
        }
        auto session_id = string_member(object, "session_id");
        const auto truncated = boolean_member(object, "truncated");
        if (!session_id || session_id->empty() || !truncated) {
            result.error = "session.chat.history requires 'session_id', 'turns' and 'truncated'";
            return result;
        }
        DialogHistory history;
        history.session_id = std::move(*session_id);
        history.truncated = *truncated;
        for (const auto &entry : *turns->as_array()) {
            std::string turn_error;
            auto turn = decode_dialog_turn(entry, turn_error);
            if (!turn) {
                result.error = std::move(turn_error);
                return result;
            }
            history.turns.push_back(std::move(*turn));
        }
        response.payload = std::move(history);
    } else if (const auto *session_id = member(object, "session_id"); session_id != nullptr) {
        auto id_text = string_member(object, "session_id");
        if (!id_text || id_text->empty()) {
            result.error = "session.open response requires a non-empty 'session_id'";
            return result;
        }
        if (const auto *deleted = member(object, "deleted")) {
            const auto flag = deleted->as_boolean();
            if (!flag || !*flag) {
                result.error = "session.delete reply requires deleted=true";
                return result;
            }
            response.payload = SessionDeleted{*id_text};
        } else if (const auto *state = member(object, "state"); state != nullptr) {
            // The closed reply adds "state" to the same envelope shape
            // (mirrors the workflow.cancel / workflow.run discrimination).
            auto state_text = string_member(object, "state");
            if (!state_text ||
                !in_stable_set(*state_text, kSessionStateNames,
                               sizeof(kSessionStateNames) / sizeof(kSessionStateNames[0]))) {
                result.error = "session.close response requires a 'state' string from the "
                               "session state vocabulary";
                return result;
            }
            response.payload = SessionClosed{std::move(*id_text), std::move(*state_text)};
        } else {
            response.payload = SessionOpened{std::move(*id_text)};
        }
    } else if (const auto *sessions = member(object, "sessions"); sessions != nullptr) {
        if (!sessions->is_array()) {
            result.error = "session.list 'sessions' must be an array";
            return result;
        }
        SessionList list;
        for (const auto &entry : *sessions->as_array()) {
            if (!entry.is_object()) {
                result.error = "session.list entries must be objects";
                return result;
            }
            SessionSummary summary;
            auto id_text = string_member(entry, "id");
            auto state = string_member(entry, "state");
            const auto created = integer_member(entry, "created_at_ms");
            if (!id_text || id_text->empty() || !state || !created || *created < 0) {
                result.error = "session.list entries require 'id', 'state' and 'created_at_ms'";
                return result;
            }
            if (!in_stable_set(*state, kSessionStateNames,
                               sizeof(kSessionStateNames) / sizeof(kSessionStateNames[0]))) {
                result.error = "session.list entry 'state' is not a known session state";
                return result;
            }
            summary.id = std::move(*id_text);
            summary.state = std::move(*state);
            summary.created_at_ms = *created;
            list.sessions.push_back(std::move(summary));
        }
        response.payload = std::move(list);
    } else if (const auto *workflows = member(object, "workflows"); workflows != nullptr) {
        if (!workflows->is_array()) {
            result.error = "workflow.list 'workflows' must be an array";
            return result;
        }
        WorkflowList list;
        for (const auto &entry : *workflows->as_array()) {
            if (!entry.is_object()) {
                result.error = "workflow.list entries must be objects";
                return result;
            }
            WorkflowSummary summary;
            auto entry_id = string_member(entry, "workflow_id");
            auto name = string_member(entry, "name");
            auto digest = string_member(entry, "head_digest");
            auto validation = string_member(entry, "validation");
            const auto *runnable = member(entry, "runnable");
            const auto runnable_flag = runnable == nullptr ? std::nullopt : runnable->as_boolean();
            const auto updated = integer_member(entry, "updated_at_ms");
            if (!entry_id || entry_id->empty() || !name || name->empty() || !digest ||
                digest->empty() || !validation || !runnable_flag || !updated || *updated < 0) {
                result.error = "workflow.list entries require 'workflow_id', 'name', "
                               "'head_digest', 'validation', 'runnable' and 'updated_at_ms'";
                return result;
            }
            if (!in_stable_set(*validation, kWorkflowValidationNames,
                               sizeof(kWorkflowValidationNames) /
                                   sizeof(kWorkflowValidationNames[0]))) {
                result.error = "workflow.list entry 'validation' is not a known validation result";
                return result;
            }
            summary.workflow_id = std::move(*entry_id);
            summary.name = std::move(*name);
            summary.head_digest = std::move(*digest);
            summary.validation = std::move(*validation);
            summary.runnable = *runnable_flag;
            summary.updated_at_ms = *updated;
            list.workflows.push_back(std::move(summary));
        }
        response.payload = std::move(list);
    } else if (const auto *runs = member(object, "runs"); runs != nullptr) {
        if (!runs->is_array()) {
            result.error = "workflow.runs 'runs' must be an array";
            return result;
        }
        WorkflowRunList list;
        for (const auto &entry : *runs->as_array()) {
            if (!entry.is_object()) {
                result.error = "workflow.runs entries must be objects";
                return result;
            }
            WorkflowRunSummary summary;
            auto run_id = string_member(entry, "run_id");
            auto workflow_id = string_member(entry, "workflow_id");
            auto state = string_member(entry, "state");
            const auto epoch = integer_member(entry, "run_epoch");
            const auto created = integer_member(entry, "created_at_ms");
            if (!run_id || run_id->empty() || !workflow_id || workflow_id->empty() || !state ||
                !epoch || *epoch < 0 || !created || *created < 0) {
                result.error = "workflow.runs entries require 'run_id', 'workflow_id', 'state', "
                               "'run_epoch' and 'created_at_ms'";
                return result;
            }
            if (!in_stable_set(*state, kWorkflowRunStateNames,
                               sizeof(kWorkflowRunStateNames) /
                                   sizeof(kWorkflowRunStateNames[0]))) {
                result.error = "workflow.runs entry 'state' is not a known run state";
                return result;
            }
            summary.run_id = std::move(*run_id);
            summary.workflow_id = std::move(*workflow_id);
            summary.state = std::move(*state);
            summary.run_epoch = static_cast<std::uint64_t>(*epoch);
            summary.created_at_ms = *created;
            list.runs.push_back(std::move(summary));
        }
        response.payload = std::move(list);
    } else if (const auto *tools = member(object, "tools"); tools != nullptr) {
        if (!tools->is_array()) {
            result.error = "workflow.atom.catalog 'tools' must be an array";
            return result;
        }
        WorkflowAtomCatalog catalog;
        for (const auto &entry : *tools->as_array()) {
            if (!entry.is_object()) {
                result.error = "workflow.atom.catalog entries must be objects";
                return result;
            }
            ExposedTool tool;
            auto wire_name = string_member(entry, "wire_name");
            auto version = string_member(entry, "version");
            auto description = string_member(entry, "description");
            const auto *side_effects = member(entry, "has_side_effects");
            const auto side_effects_flag =
                side_effects == nullptr ? std::nullopt : side_effects->as_boolean();
            auto schema = object_member_text(entry, "parameters_schema");
            if (!wire_name || wire_name->empty() || !version || version->empty() || !description ||
                description->empty() || !side_effects_flag || !schema ||
                !member(entry, "parameters_schema")->is_object()) {
                result.error = "workflow.atom.catalog entries require 'wire_name', 'version', "
                               "'description', 'has_side_effects' and a 'parameters_schema' object";
                return result;
            }
            tool.wire_name = std::move(*wire_name);
            tool.version = std::move(*version);
            tool.description = std::move(*description);
            tool.has_side_effects = *side_effects_flag;
            tool.parameters_schema_json = std::move(*schema);
            catalog.tools.push_back(std::move(tool));
        }
        response.payload = std::move(catalog);
    } else if (const auto *definition = member(object, "definition"); definition != nullptr) {
        // WorkflowDefinitionView discriminates on "definition"; it also
        // carries "workflow_id" and "digest", so it must precede those
        // branches.
        if (!definition->is_object()) {
            result.error = "workflow.get 'definition' must be an object";
            return result;
        }
        auto workflow_id = string_member(object, "workflow_id");
        auto digest = string_member(object, "digest");
        if (!workflow_id || workflow_id->empty() || !digest || digest->empty()) {
            result.error = "workflow.get response requires 'workflow_id', 'digest' and a "
                           "'definition' object";
            return result;
        }
        WorkflowDefinitionView view;
        view.workflow_id = std::move(*workflow_id);
        view.digest = std::move(*digest);
        view.definition_json = object_member_text(object, "definition").value();
        response.payload = std::move(view);
    } else if (const auto *active_application = member(object, "active_application");
               active_application != nullptr) {
        // ObservationView discriminates on "active_application" — no other
        // payload carries it.
        auto active_application_text = string_member(object, "active_application");
        auto active_window = string_member(object, "active_window");
        auto focused_element = string_member(object, "focused_element");
        auto environment_state = string_member(object, "environment_state");
        const auto pointer_x = integer_member(object, "pointer_x");
        const auto pointer_y = integer_member(object, "pointer_y");
        const auto window_focused = boolean_member(object, "window_focused");
        if (!active_application_text || !active_window || !focused_element || !environment_state ||
            !pointer_x || !pointer_y || !window_focused) {
            result.error = "desktop.observe response requires the frame members 'active_window', "
                           "'window_geometry', 'window_focused', 'focused_element', 'pointer_x', "
                           "'pointer_y' and 'environment_state'";
            return result;
        }
        ObservationView view;
        view.active_application = std::move(*active_application_text);
        view.active_window = std::move(*active_window);
        view.focused_element = std::move(*focused_element);
        view.environment_state = std::move(*environment_state);
        view.pointer_x = static_cast<std::int32_t>(*pointer_x);
        view.pointer_y = static_cast<std::int32_t>(*pointer_y);
        view.window_focused = *window_focused;
        const auto *window_geometry = member(object, "window_geometry");
        if (window_geometry == nullptr) {
            result.error = "desktop.observe response requires the frame members 'active_window', "
                           "'window_geometry', 'window_focused', 'focused_element', 'pointer_x', "
                           "'pointer_y' and 'environment_state'";
            return result;
        }
        std::string geometry_error;
        auto window_rect = decode_geometry(*window_geometry, geometry_error);
        if (!window_rect) {
            result.error = std::move(geometry_error);
            return result;
        }
        view.window_geometry = std::move(*window_rect);
        if (const auto *semantic = member(object, "semantic"); semantic != nullptr) {
            if (!semantic->is_object()) {
                result.error = "desktop.observe 'semantic' must be an object";
                return result;
            }
            ObservationSemantic projection;
            auto application = string_member(*semantic, "application");
            auto window_title = string_member(*semantic, "window_title");
            const auto truncated = boolean_member(*semantic, "truncated");
            const auto *nodes = member(*semantic, "nodes");
            if (!application || !window_title || !truncated || nodes == nullptr ||
                !nodes->is_array()) {
                result.error = "desktop.observe 'semantic' requires 'application', "
                               "'window_title', a 'nodes' array and 'truncated'";
                return result;
            }
            projection.application = std::move(*application);
            projection.window_title = std::move(*window_title);
            projection.truncated = *truncated;
            for (const auto &entry : *nodes->as_array()) {
                std::string node_error;
                auto node = decode_node(entry, node_error);
                if (!node) {
                    result.error = std::move(node_error);
                    return result;
                }
                projection.nodes.push_back(std::move(*node));
            }
            view.semantic = std::move(projection);
        }
        const auto *visual_ref = member(object, "visual_snapshot_ref");
        const auto *visual_regions = member(object, "visual_regions");
        // The visual pair is co-present or co-absent; a half-carried visual
        // component is a contract violation, never a partial projection.
        if ((visual_ref == nullptr) != (visual_regions == nullptr)) {
            result.error = "desktop.observe 'visual_snapshot_ref' and 'visual_regions' are "
                           "co-present";
            return result;
        }
        if (visual_ref != nullptr) {
            auto ref_text = string_member(object, "visual_snapshot_ref");
            if (!ref_text || ref_text->empty() || !visual_regions->is_array()) {
                result.error = "desktop.observe requires a non-empty 'visual_snapshot_ref' and a "
                               "'visual_regions' array";
                return result;
            }
            std::vector<ObservationRegion> regions;
            for (const auto &entry : *visual_regions->as_array()) {
                std::string region_error;
                auto region = decode_region(entry, region_error);
                if (!region) {
                    result.error = std::move(region_error);
                    return result;
                }
                regions.push_back(std::move(*region));
            }
            view.visual_snapshot_ref = std::move(*ref_text);
            view.visual_regions = std::move(regions);
        }
        response.payload = std::move(view);
    } else if (const auto *dry_run_id = member(object, "dry_run_id"); dry_run_id != nullptr) {
        // WorkflowPublished discriminates on "dry_run_id"; it also carries
        // "workflow_id" and "digest", so it must precede those branches.
        auto workflow_id = string_member(object, "workflow_id");
        auto digest = string_member(object, "digest");
        auto dry_run_text = string_member(object, "dry_run_id");
        const auto *idempotent = member(object, "idempotent");
        const auto idempotent_flag =
            idempotent == nullptr ? std::nullopt : idempotent->as_boolean();
        if (!workflow_id || workflow_id->empty() || !digest || digest->empty() || !dry_run_text ||
            dry_run_text->empty() || !idempotent_flag) {
            result.error = "workflow.publish response requires 'workflow_id', 'digest', "
                           "'dry_run_id' and 'idempotent'";
            return result;
        }
        response.payload = WorkflowPublished{std::move(*workflow_id), std::move(*digest),
                                             std::move(*dry_run_text), *idempotent_flag};
    } else if (const auto *digest_member = member(object, "digest"); digest_member != nullptr) {
        // WorkflowSaved discriminates on "digest"; it also carries
        // "workflow_id", so it must precede the WorkflowDeleted branch.
        auto workflow_id = string_member(object, "workflow_id");
        auto digest = string_member(object, "digest");
        if (!workflow_id || workflow_id->empty() || !digest || digest->empty()) {
            result.error = "workflow.save response requires 'workflow_id' and 'digest'";
            return result;
        }
        response.payload = WorkflowSaved{std::move(*workflow_id), std::move(*digest)};
    } else if (const auto *workflow_id = member(object, "workflow_id"); workflow_id != nullptr) {
        auto id_text = string_member(object, "workflow_id");
        if (!id_text || id_text->empty()) {
            result.error = "workflow.delete response requires a non-empty 'workflow_id'";
            return result;
        }
        response.payload = WorkflowDeleted{std::move(*id_text)};
    } else if (const auto *run_id = member(object, "run_id"); run_id != nullptr) {
        auto id_text = string_member(object, "run_id");
        if (!id_text || id_text->empty()) {
            result.error = "workflow.run response requires a non-empty 'run_id'";
            return result;
        }
        if (const auto *state = member(object, "state"); state != nullptr) {
            // The cancelled reply adds "state" to the same envelope shape.
            auto state_text = string_member(object, "state");
            if (!state_text) {
                result.error = "workflow.cancel response requires a 'state' string";
                return result;
            }
            response.payload = WorkflowRunCancelled{std::move(*id_text), std::move(*state_text)};
        } else {
            response.payload = WorkflowRunStarted{std::move(*id_text)};
        }
    } else {
        // An ok response carrying none of the known payload discriminators
        // is the acknowledgement shape (service.shutdown).
        response.payload = ShutdownAccepted{};
    }
    result.ok = true;
    return result;
}

const char *event_name(const EventPayload &payload) {
    if (std::holds_alternative<TaskUpdatedEvent>(payload)) {
        return kEventTaskUpdated;
    }
    if (std::holds_alternative<HostStatusEvent>(payload)) {
        return kEventHostStatus;
    }
    if (std::holds_alternative<PermissionRequestedEvent>(payload)) {
        return kEventPermissionRequest;
    }
    if (std::holds_alternative<SessionUpdatedEvent>(payload)) {
        return kEventSessionUpdated;
    }
    if (std::holds_alternative<SessionMessageEvent>(payload)) {
        return kEventSessionMessage;
    }
    if (std::holds_alternative<SessionTurnEvent>(payload)) {
        return kEventSessionTurn;
    }
    if (std::holds_alternative<SessionOutputEvent>(payload)) {
        return kEventSessionOutput;
    }
    if (std::holds_alternative<WorkflowRunUpdatedEvent>(payload)) {
        return kEventWorkflowRunUpdated;
    }
    if (std::holds_alternative<ChatPreviewEvent>(payload))
        return kEventChatPreview;
    if (std::holds_alternative<ChatTurnUpdatedEvent>(payload)) {
        return kEventChatTurnUpdated;
    }
    return kEventOverflow;
}

std::string encode_event(const Event &event) {
    auto object = make_object();
    put(object, "v", static_cast<std::int64_t>(kProtocolVersion));
    put(object, "seq", static_cast<std::int64_t>(event.seq));
    std::visit(
        [&object](const auto &value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, TaskUpdatedEvent>) {
                put(object, "event", kEventTaskUpdated);
                put(object, "task_id", value.task_id);
                put(object, "goal", value.goal);
                put(object, "progress", value.progress);
                put(object, "has_success", value.has_success);
                put(object, "success", value.success);
            } else if constexpr (std::is_same_v<T, HostStatusEvent>) {
                put(object, "event", kEventHostStatus);
                put(object, "status", value.status);
            } else if constexpr (std::is_same_v<T, EventsOverflowEvent>) {
                put(object, "event", kEventOverflow);
                put(object, "dropped", static_cast<std::int64_t>(value.dropped));
            } else if constexpr (std::is_same_v<T, PermissionRequestedEvent>) {
                put(object, "event", kEventPermissionRequest);
                put(object, "request_id", value.request_id);
                put(object, "capability", value.capability);
                put(object, "resource", value.resource);
                put(object, "task_id", value.task_id);
                put(object, "timeout_ms", value.timeout_ms);
            } else if constexpr (std::is_same_v<T, SessionUpdatedEvent>) {
                put(object, "event", kEventSessionUpdated);
                put(object, "session_id", value.session_id);
                put(object, "state", value.state);
            } else if constexpr (std::is_same_v<T, SessionMessageEvent>) {
                put(object, "event", kEventSessionMessage);
                put(object, "session_id", value.session_id);
                put(object, "task_id", value.task_id);
                put(object, "kind", value.kind);
                put(object, "text", value.text);
                put(object, "sequence", static_cast<std::int64_t>(value.sequence));
            } else if constexpr (std::is_same_v<T, SessionTurnEvent>) {
                put(object, "event", kEventSessionTurn);
                put(object, "session_id", value.session_id);
                put(object, "task_id", value.task_id);
                put(object, "step", static_cast<std::int64_t>(value.step));
                put(object, "kind", value.kind);
                put(object, "status", value.status);
            } else if constexpr (std::is_same_v<T, SessionOutputEvent>) {
                put(object, "event", kEventSessionOutput);
                put(object, "session_id", value.session_id);
                put(object, "task_id", value.task_id);
                put(object, "step", static_cast<std::int64_t>(value.step));
                put(object, "chunk", value.chunk);
                put(object, "truncated", value.truncated);
            } else if constexpr (std::is_same_v<T, WorkflowRunUpdatedEvent>) {
                put(object, "event", kEventWorkflowRunUpdated);
                put(object, "run_id", value.run_id);
                put(object, "workflow_id", value.workflow_id);
                put(object, "state", value.state);
                put(object, "run_epoch", static_cast<std::int64_t>(value.run_epoch));
                if (value.summary) {
                    put(object, "summary", *value.summary);
                }
            } else if constexpr (std::is_same_v<T, ChatPreviewEvent>) {
                put(object, "event", kEventChatPreview);
                put(object, "session_id", value.session_id);
                put(object, "turn_id", value.turn_id);
                put(object, "request_id", value.request_id);
                put(object, "text", value.text);
                put(object, "sequence", static_cast<std::int64_t>(value.sequence));
                put(object, "truncated", value.truncated);
            } else if constexpr (std::is_same_v<T, ChatTurnUpdatedEvent>) {
                put(object, "event", kEventChatTurnUpdated);
                if (!value.replaces_turn_id.empty())
                    put(object, "replaces_turn_id", value.replaces_turn_id);
                if (value.context_usage)
                    put(object, "context_usage", encode_context_usage(*value.context_usage));
                put(object, "session_id", value.session_id);
                put(object, "turn_id", value.turn_id);
                put(object, "status", value.status);
                put(object, "user_text", value.user_text);
                if (value.has_reply) {
                    put(object, "reply_text", value.reply_text);
                }
                if (value.has_error) {
                    put(object, "error", value.error);
                }
                put(object, "sequence", static_cast<std::int64_t>(value.sequence));
            }
        },
        event.payload);
    return mira::to_json_string(object);
}

EventDecode decode_event(std::string_view payload) {
    EventDecode result;
    auto parsed = mira::parse_json(payload);
    if (!parsed) {
        result.error = "payload is not valid JSON: " + parsed.error().safe_message;
        return result;
    }
    if (!parsed.value().is_object()) {
        result.error = "event payload must be a JSON object";
        return result;
    }
    const auto &object = parsed.value();
    if (const auto version = integer_member(object, "v");
        !version || *version != kProtocolVersion) {
        result.error = "unsupported protocol version";
        return result;
    }
    const auto seq = integer_member(object, "seq");
    if (!seq || *seq < 1) {
        result.error = "event is missing a positive 'seq'";
        return result;
    }
    result.event.seq = static_cast<std::uint64_t>(*seq);
    const auto name = string_member(object, "event");
    if (!name) {
        result.error = "event is missing 'event'";
        return result;
    }
    if (*name == kEventTaskUpdated) {
        TaskUpdatedEvent task;
        const auto task_id = string_member(object, "task_id");
        const auto goal = string_member(object, "goal");
        const auto progress = string_member(object, "progress");
        const auto *has_success = member(object, "has_success");
        const auto *success = member(object, "success");
        const auto has_success_flag =
            has_success == nullptr ? std::nullopt : has_success->as_boolean();
        const auto success_flag = success == nullptr ? std::nullopt : success->as_boolean();
        if (!task_id || !goal || !progress || !has_success_flag || !success_flag) {
            result.error = "task.updated requires 'task_id', 'goal', 'progress', 'has_success' and "
                           "'success'";
            return result;
        }
        if (!in_stable_set(*progress, kProgressNames,
                           sizeof(kProgressNames) / sizeof(kProgressNames[0]))) {
            result.error = "task.updated 'progress' is not a known progress name";
            return result;
        }
        task.task_id = std::move(*task_id);
        task.goal = std::move(*goal);
        task.progress = std::move(*progress);
        task.has_success = *has_success_flag;
        task.success = *success_flag;
        result.event.payload = std::move(task);
    } else if (*name == kEventHostStatus) {
        HostStatusEvent status;
        auto status_name = string_member(object, "status");
        if (!status_name) {
            result.error = "host.status requires 'status'";
            return result;
        }
        if (!in_stable_set(*status_name, kHostStatusNames,
                           sizeof(kHostStatusNames) / sizeof(kHostStatusNames[0]))) {
            result.error = "host.status 'status' is not a known host status";
            return result;
        }
        status.status = std::move(*status_name);
        result.event.payload = std::move(status);
    } else if (*name == kEventOverflow) {
        const auto dropped = integer_member(object, "dropped");
        if (!dropped || *dropped < 0) {
            result.error = "events.overflow requires a non-negative 'dropped'";
            return result;
        }
        EventsOverflowEvent overflow;
        overflow.dropped = static_cast<std::uint64_t>(*dropped);
        result.event.payload = overflow;
    } else if (*name == kEventPermissionRequest) {
        PermissionRequestedEvent permission;
        const auto request_id = string_member(object, "request_id");
        const auto capability = string_member(object, "capability");
        const auto resource = string_member(object, "resource");
        const auto task_id = string_member(object, "task_id");
        const auto timeout = integer_member(object, "timeout_ms");
        if (!request_id || request_id->empty() || !capability || !resource || !task_id ||
            task_id->empty() || !timeout) {
            result.error = "permission.request requires 'request_id', 'capability', 'resource', "
                           "'task_id' and 'timeout_ms'";
            return result;
        }
        if (!in_stable_set(*capability, kCapabilityNames,
                           sizeof(kCapabilityNames) / sizeof(kCapabilityNames[0]))) {
            result.error = "permission.request 'capability' is not a known capability";
            return result;
        }
        if (*timeout <= 0) {
            result.error = "permission.request 'timeout_ms' must be positive";
            return result;
        }
        permission.request_id = std::move(*request_id);
        permission.capability = std::move(*capability);
        permission.resource = std::move(*resource);
        permission.task_id = std::move(*task_id);
        permission.timeout_ms = *timeout;
        result.event.payload = std::move(permission);
    } else if (*name == kEventSessionUpdated) {
        SessionUpdatedEvent session;
        const auto session_id = string_member(object, "session_id");
        const auto state = string_member(object, "state");
        if (!session_id || session_id->empty() || !state) {
            result.error = "session.updated requires 'session_id' and 'state'";
            return result;
        }
        if (!in_stable_set(*state, kSessionStateNames,
                           sizeof(kSessionStateNames) / sizeof(kSessionStateNames[0]))) {
            result.error = "session.updated 'state' is not a known session state";
            return result;
        }
        session.session_id = std::move(*session_id);
        session.state = std::move(*state);
        result.event.payload = std::move(session);
    } else if (*name == kEventSessionMessage) {
        SessionMessageEvent message;
        const auto session_id = string_member(object, "session_id");
        const auto task_id = string_member(object, "task_id");
        const auto kind = string_member(object, "kind");
        const auto text = string_member(object, "text");
        const auto sequence = integer_member(object, "sequence");
        if (!session_id || session_id->empty() || !task_id || task_id->empty() || !kind || !text ||
            text->empty() || !sequence || *sequence < 1) {
            result.error = "session.message requires 'session_id', 'task_id', 'kind', non-empty "
                           "'text' and a positive 'sequence'";
            return result;
        }
        if (!in_stable_set(*kind, kSessionMessageKinds,
                           sizeof(kSessionMessageKinds) / sizeof(kSessionMessageKinds[0]))) {
            result.error = "session.message 'kind' is not a known message kind";
            return result;
        }
        message.session_id = std::move(*session_id);
        message.task_id = std::move(*task_id);
        message.kind = std::move(*kind);
        message.text = std::move(*text);
        message.sequence = static_cast<std::uint64_t>(*sequence);
        result.event.payload = std::move(message);
    } else if (*name == kEventSessionTurn) {
        SessionTurnEvent turn;
        const auto session_id = string_member(object, "session_id");
        const auto task_id = string_member(object, "task_id");
        const auto step = integer_member(object, "step");
        const auto kind = string_member(object, "kind");
        const auto status = string_member(object, "status");
        if (!session_id || session_id->empty() || !task_id || task_id->empty() || !step ||
            *step < 1 || !kind || !status) {
            result.error = "session.turn requires 'session_id', 'task_id', a positive 'step', "
                           "'kind' and 'status'";
            return result;
        }
        if (step_kind_from_name(*kind) == std::nullopt) {
            result.error = "session.turn 'kind' is not a known step kind";
            return result;
        }
        if (!in_stable_set(*status, kTurnStatusNames,
                           sizeof(kTurnStatusNames) / sizeof(kTurnStatusNames[0]))) {
            result.error = "session.turn 'status' is not a known settled-step status";
            return result;
        }
        turn.session_id = std::move(*session_id);
        turn.task_id = std::move(*task_id);
        turn.step = static_cast<int>(*step);
        turn.kind = std::move(*kind);
        turn.status = std::move(*status);
        result.event.payload = std::move(turn);
    } else if (*name == kEventSessionOutput) {
        SessionOutputEvent output;
        const auto session_id = string_member(object, "session_id");
        const auto task_id = string_member(object, "task_id");
        const auto step = integer_member(object, "step");
        const auto chunk = string_member(object, "chunk");
        const auto *truncated = member(object, "truncated");
        const auto truncated_flag = truncated == nullptr ? std::nullopt : truncated->as_boolean();
        if (!session_id || session_id->empty() || !task_id || task_id->empty() || !step ||
            *step < 1 || !chunk || !truncated_flag) {
            result.error = "session.output requires 'session_id', 'task_id', a positive 'step', "
                           "'chunk' and 'truncated'";
            return result;
        }
        output.session_id = std::move(*session_id);
        output.task_id = std::move(*task_id);
        output.step = static_cast<int>(*step);
        output.chunk = std::move(*chunk);
        output.truncated = *truncated_flag;
        result.event.payload = std::move(output);
    } else if (*name == kEventWorkflowRunUpdated) {
        WorkflowRunUpdatedEvent run;
        const auto run_id = string_member(object, "run_id");
        const auto workflow_id = string_member(object, "workflow_id");
        const auto state = string_member(object, "state");
        const auto epoch = integer_member(object, "run_epoch");
        if (!run_id || run_id->empty() || !workflow_id || workflow_id->empty() || !state ||
            !epoch || *epoch < 0) {
            result.error = "workflow.run_updated requires 'run_id', 'workflow_id', 'state', a "
                           "non-negative 'run_epoch'";
            return result;
        }
        if (!in_stable_set(*state, kWorkflowRunStateNames,
                           sizeof(kWorkflowRunStateNames) / sizeof(kWorkflowRunStateNames[0]))) {
            result.error = "workflow.run_updated 'state' is not a known run state";
            return result;
        }
        run.run_id = std::move(*run_id);
        run.workflow_id = std::move(*workflow_id);
        run.state = std::move(*state);
        run.run_epoch = static_cast<std::uint64_t>(*epoch);
        if (const auto *summary = member(object, "summary"); summary != nullptr) {
            const auto text = summary->as_string();
            if (!text) {
                result.error = "workflow.run_updated 'summary' must be a string";
                return result;
            }
            run.summary = *text;
        }
        result.event.payload = std::move(run);
    } else if (*name == kEventChatPreview) {
        const auto session = string_member(object, "session_id");
        const auto turn = string_member(object, "turn_id");
        const auto request = string_member(object, "request_id");
        const auto text = string_member(object, "text");
        const auto sequence = integer_member(object, "sequence");
        const auto *flag = member(object, "truncated");
        const auto truncated = flag ? flag->as_boolean() : std::nullopt;
        if (!session || session->empty() || session->size() > 128 || !turn || turn->empty() ||
            turn->size() > 128 || !request || request->empty() || request->size() > 128 || !text ||
            text->size() > 16 * 1024 || !sequence || *sequence < 1 || !truncated) {
            result.error = "invalid bounded session.chat_preview snapshot";
            return result;
        }
        result.event.payload = ChatPreviewEvent{
            *session, *turn, *request, *text, static_cast<std::uint64_t>(*sequence), *truncated};
    } else if (*name == kEventChatTurnUpdated) {
        ChatTurnUpdatedEvent chat;
        const auto session_id = string_member(object, "session_id");
        const auto turn_id = string_member(object, "turn_id");
        const auto status = string_member(object, "status");
        const auto user_text = string_member(object, "user_text");
        const auto sequence = integer_member(object, "sequence");
        if (!session_id || session_id->empty() || !turn_id || turn_id->empty() || !status ||
            !user_text || !sequence || *sequence < 1) {
            result.error = "session.chat_updated requires 'session_id', 'turn_id', 'status', "
                           "'user_text' and a positive 'sequence'";
            return result;
        }
        if (!in_stable_set(*status, kDialogTurnStatuses,
                           sizeof(kDialogTurnStatuses) / sizeof(kDialogTurnStatuses[0]))) {
            result.error = "session.chat_updated 'status' is not a known turn status";
            return result;
        }
        chat.session_id = std::move(*session_id);
        chat.turn_id = std::move(*turn_id);
        chat.status = std::move(*status);
        chat.user_text = std::move(*user_text);
        chat.sequence = static_cast<std::uint64_t>(*sequence);
        if (const auto *value = member(object, "replaces_turn_id")) {
            const auto *id = value->as_string();
            if (!id || id->empty() || id->size() > 128 || *id == chat.turn_id) {
                result.error = "invalid replaces_turn_id";
                return result;
            }
            chat.replaces_turn_id = *id;
        }
        if (const auto *reply = member(object, "reply_text"); reply != nullptr) {
            const auto text = reply->as_string();
            if (!text || chat.status != "ok") {
                result.error = "session.chat_updated 'reply_text' must be a string present "
                               "exactly when status is 'ok'";
                return result;
            }
            chat.reply_text = *text;
            chat.has_reply = true;
        } else if (chat.status == "ok") {
            result.error = "session.chat_updated 'reply_text' must be a string present "
                           "exactly when status is 'ok'";
            return result;
        }
        if (const auto *failure = member(object, "error"); failure != nullptr) {
            const auto text = failure->as_string();
            if (!text || chat.status != "failed") {
                result.error = "session.chat_updated 'error' must be a string present exactly "
                               "when status is 'failed'";
                return result;
            }
            chat.error = *text;
            chat.has_error = true;
        } else if (chat.status == "failed") {
            result.error = "session.chat_updated 'error' must be a string present exactly "
                           "when status is 'failed'";
            return result;
        }
        if (!decode_context_usage(object, chat.status, chat.context_usage, result.error))
            return result;
        result.event.payload = std::move(chat);
    } else {
        result.error = "unknown event '" + *name + "'";
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace mirage::runtime::ipc
