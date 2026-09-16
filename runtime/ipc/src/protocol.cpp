#include <mirage/runtime/ipc/protocol.hpp>

#include <mira/json.hpp>

#include <string>
#include <type_traits>

namespace mirage::runtime::ipc {
namespace {

constexpr const char* kOpHello = "hello";
constexpr const char* kOpSubmit = "task.submit";
constexpr const char* kOpList = "task.list";
constexpr const char* kOpInspect = "task.inspect";
constexpr const char* kOpShutdown = "service.shutdown";

constexpr const char* kStepRead = "filesystem.read";
constexpr const char* kStepExecute = "process.execute";

mira::JsonValue make_object() { return mira::JsonValue{mira::JsonValue::Object{}}; }

void put(mira::JsonValue& object, std::string key, mira::JsonValue value) {
    object.set(std::move(key), std::move(value));
}

const mira::JsonValue* member(const mira::JsonValue& object, std::string_view key) {
    return object.find(key);
}

std::optional<std::int64_t> integer_member(const mira::JsonValue& object,
                                           std::string_view key) {
    const auto* value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    return value->as_integer();
}

std::optional<std::string> string_member(const mira::JsonValue& object,
                                         std::string_view key) {
    const auto* value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    if (const auto* text = value->as_string(); text != nullptr) {
        return *text;
    }
    return std::nullopt;
}

std::optional<TaskStep> decode_step(const mira::JsonValue& value, std::string& error) {
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

mira::JsonValue encode_step_view(const StepView& step) {
    auto object = make_object();
    put(object, "index", static_cast<std::int64_t>(step.index));
    put(object, "kind", step.kind);
    put(object, "status", step.status);
    put(object, "operation_id", step.operation_id);
    put(object, "ok", step.ok);
    put(object, "exit_code", static_cast<std::int64_t>(step.exit_code));
    put(object, "result", step.result);
    put(object, "result_truncated", step.result_truncated);
    put(object, "error", step.error);
    return object;
}

std::optional<StepView> decode_step_view(const mira::JsonValue& value,
                                         std::string& error) {
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
    if (const auto* flag = member(value, "ok"); flag != nullptr) {
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
    if (const auto* flag = member(value, "result_truncated"); flag != nullptr) {
        if (const auto boolean = flag->as_boolean()) {
            step.result_truncated = *boolean;
        }
    }
    if (auto text = string_member(value, "error")) {
        step.error = std::move(*text);
    }
    return step;
}

mira::JsonValue encode_inspect(const InspectTask& task) {
    auto object = make_object();
    put(object, "id", task.id);
    put(object, "goal", task.goal);
    put(object, "progress", task.progress);
    if (task.has_success) {
        put(object, "success", task.success);
    }
    mira::JsonValue::Array entries;
    for (const auto& step : task.steps) {
        entries.emplace_back(encode_step_view(step));
    }
    put(object, "steps", mira::JsonValue{std::move(entries)});
    return object;
}

mira::JsonValue encode_payload(const ResponsePayload& payload) {
    auto object = make_object();
    std::visit(
        [&object](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, ServiceIdentity>) {
                put(object, "service", value.name);
                put(object, "mirage_version", value.mirage_version);
                put(object, "mira_core_version", value.mira_core_version);
                put(object, "host_status", value.host_status);
                put(object, "protocol", static_cast<std::int64_t>(value.protocol));
            } else if constexpr (std::is_same_v<T, TaskSubmitted>) {
                put(object, "task_id", value.task_id);
            } else if constexpr (std::is_same_v<T, TaskList>) {
                mira::JsonValue::Array entries;
                for (const auto& task : value.tasks) {
                    auto entry = make_object();
                    put(entry, "id", task.id);
                    put(entry, "goal", task.goal);
                    put(entry, "progress", task.progress);
                    entries.emplace_back(std::move(entry));
                }
                put(object, "tasks", mira::JsonValue{std::move(entries)});
            } else if constexpr (std::is_same_v<T, InspectTask>) {
                put(object, "task", encode_inspect(value));
            } else if constexpr (std::is_same_v<T, ShutdownAccepted>) {
                // No payload members beyond the ok envelope.
            }
        },
        payload);
    return object;
}

} // namespace

const char* step_kind_name(StepKind kind) {
    return kind == StepKind::FilesystemRead ? kStepRead : kStepExecute;
}

std::string encode_request(std::uint64_t id, const Request& body) {
    auto object = make_object();
    put(object, "v", static_cast<std::int64_t>(kProtocolVersion));
    put(object, "id", static_cast<std::int64_t>(id));
    std::visit(
        [&object](const auto& value) {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, HelloRequest>) {
                put(object, "op", kOpHello);
            } else if constexpr (std::is_same_v<T, SubmitTaskRequest>) {
                put(object, "op", kOpSubmit);
                put(object, "goal", value.goal);
                mira::JsonValue::Array entries;
                for (const auto& step : value.steps) {
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
            } else if constexpr (std::is_same_v<T, ShutdownRequest>) {
                put(object, "op", kOpShutdown);
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
    const auto& object = parsed.value();
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
        if (const auto* steps = member(object, "steps"); steps != nullptr) {
            if (!steps->is_array()) {
                result.error = "task.submit 'steps' must be an array";
                return result;
            }
            for (const auto& entry : *steps->as_array()) {
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
    } else {
        result.error = "unknown op '" + *op + "'";
        return result;
    }
    result.ok = true;
    return result;
}

std::string encode_response(const Response& response) {
    auto object = make_object();
    put(object, "v", static_cast<std::int64_t>(kProtocolVersion));
    put(object, "id", static_cast<std::int64_t>(response.id));
    put(object, "ok", response.ok);
    if (response.ok) {
        auto payload = encode_payload(response.payload);
        if (auto* members = payload.as_object(); members != nullptr) {
            for (auto& entry : *members) {
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
    const auto& object = parsed.value();
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
    Response& response = result.response;
    response.id = static_cast<std::uint64_t>(*id);
    const auto* ok = member(object, "ok");
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
        const auto* error = member(object, "error");
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
    if (const auto* service = member(object, "service"); service != nullptr) {
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
        response.payload = std::move(identity);
    } else if (const auto* task_id = member(object, "task_id"); task_id != nullptr) {
        auto id_text = string_member(object, "task_id");
        if (!id_text || id_text->empty()) {
            result.error = "task.submit response requires a non-empty 'task_id'";
            return result;
        }
        response.payload = TaskSubmitted{std::move(*id_text)};
    } else if (const auto* tasks = member(object, "tasks"); tasks != nullptr) {
        if (!tasks->is_array()) {
            result.error = "task.list 'tasks' must be an array";
            return result;
        }
        TaskList list;
        for (const auto& entry : *tasks->as_array()) {
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
    } else if (const auto* task = member(object, "task"); task != nullptr) {
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
        if (const auto* success = member(*task, "success"); success != nullptr) {
            if (const auto boolean = success->as_boolean()) {
                inspect.has_success = true;
                inspect.success = *boolean;
            } else {
                result.error = "task.inspect 'success' must be a boolean";
                return result;
            }
        }
        if (const auto* steps = member(*task, "steps"); steps != nullptr) {
            if (!steps->is_array()) {
                result.error = "task.inspect 'steps' must be an array";
                return result;
            }
            for (const auto& entry : *steps->as_array()) {
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
    } else {
        // An ok response carrying none of the known payload discriminators
        // is the acknowledgement shape (service.shutdown).
        response.payload = ShutdownAccepted{};
    }
    result.ok = true;
    return result;
}

} // namespace mirage::runtime::ipc
