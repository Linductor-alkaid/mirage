#include <mirage/runtime/ipc/protocol.hpp>

#include <mira/json.hpp>

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
constexpr const char *kOpShutdown = "service.shutdown";
constexpr const char *kOpSubscribe = "events.subscribe";
constexpr const char *kOpUnsubscribe = "events.unsubscribe";
constexpr const char *kOpPermissionRespond = "permission.respond";
constexpr const char *kOpPermissionList = "permission.list";

constexpr const char *kStepRead = "filesystem.read";
constexpr const char *kStepExecute = "process.execute";

constexpr const char *kEventTaskUpdated = "task.updated";
constexpr const char *kEventHostStatus = "host.status";
constexpr const char *kEventOverflow = "events.overflow";
constexpr const char *kEventPermissionRequest = "permission.request";

/// Closed Capability vocabulary (DEC-010 / DEC-020) carried by
/// permission.request events and permission.list entries. Kept local so the
/// ipc layer stays independent of the permission module; the golden vectors
/// pin the set on both ends.
constexpr const char *kCapabilityNames[] = {
    "filesystem.read",    "filesystem.write",      "process.execute",   "window.activate",
    "screen.capture",     "input.inject",          "clipboard.read",    "clipboard.write",
    "application.launch", "application.terminate", "notification.post",
};

/// Closed product progress projection carried by task.updated events (schema
/// doc 6.2). Kept local so the ipc layer stays independent of mira_host;
/// the golden vectors pin the set on both ends.
constexpr const char *kProgressNames[] = {"Idle",      "Active", "Paused",    "Cancelling",
                                          "Completed", "Failed", "Cancelled", "Unknown"};

/// Closed Mira Host five-state set (DEC-004) carried by host.status events.
constexpr const char *kHostStatusNames[] = {"stopped", "starting", "running", "stopping", "failed"};

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
            } else if constexpr (std::is_same_v<T, TaskSubmitted>) {
                put(object, "task_id", value.task_id);
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
            } else if constexpr (std::is_same_v<T, ListTasksRequest>) {
                put(object, "op", kOpList);
            } else if constexpr (std::is_same_v<T, InspectTaskRequest>) {
                put(object, "op", kOpInspect);
                put(object, "task_id", value.task_id);
            } else if constexpr (std::is_same_v<T, CancelTaskRequest>) {
                put(object, "op", kOpCancel);
                put(object, "task_id", value.task_id);
            } else if constexpr (std::is_same_v<T, ShutdownRequest>) {
                put(object, "op", kOpShutdown);
            } else if constexpr (std::is_same_v<T, SubscribeEventsRequest>) {
                put(object, "op", kOpSubscribe);
            } else if constexpr (std::is_same_v<T, UnsubscribeEventsRequest>) {
                put(object, "op", kOpUnsubscribe);
            } else if constexpr (std::is_same_v<T, RespondPermissionRequest>) {
                put(object, "op", kOpPermissionRespond);
                put(object, "request_id", value.request_id);
                put(object, "approved", value.approved);
            } else if constexpr (std::is_same_v<T, ListPermissionsRequest>) {
                put(object, "op", kOpPermissionList);
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
        result.body = SubscribeEventsRequest{};
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
        response.payload = std::move(identity);
    } else if (const auto *task_id = member(object, "task_id"); task_id != nullptr) {
        auto id_text = string_member(object, "task_id");
        if (!id_text || id_text->empty()) {
            result.error = "task.submit response requires a non-empty 'task_id'";
            return result;
        }
        response.payload = TaskSubmitted{std::move(*id_text)};
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
    } else {
        result.error = "unknown event '" + *name + "'";
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace mirage::runtime::ipc
