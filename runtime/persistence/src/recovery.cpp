#include <mirage/runtime/persistence/recovery.hpp>

#include <mira/json.hpp>

#include <ctime>

namespace mirage::runtime::persistence {
namespace {

using mira::JsonValue;

constexpr std::string_view kProgressCompleted = "Completed";
constexpr std::string_view kProgressFailed = "Failed";
constexpr std::string_view kProgressCancelled = "Cancelled";

constexpr std::size_t kMaxGoalBytes = 16 * 1024;
constexpr std::size_t kMaxErrorBytes = 8 * 1024;
constexpr std::size_t kMaxIdBytes = 64;
constexpr std::size_t kMaxMetaBytes = 64;

bool known_progress(std::string_view progress) {
    return progress == kProgressCompleted || progress == kProgressFailed ||
           progress == kProgressCancelled;
}

bool known_step_kind(std::string_view kind) {
    return kind == "filesystem.read" || kind == "process.execute";
}

bool known_step_status(std::string_view status) {
    return status == "pending" || status == "running" || status == "ok" || status == "failed" ||
           status == "skipped" || status == "cancelled";
}

bool known_permission(std::string_view permission) {
    return permission.empty() || permission == "allowed" || permission == "confirmed" ||
           permission == "denied" || permission == "confirmation_rejected";
}

JsonValue make_object() { return JsonValue{JsonValue::Object{}}; }

void put(JsonValue &object, std::string key, JsonValue value) {
    object.set(std::move(key), std::move(value));
}

const JsonValue *member(const JsonValue &object, std::string_view key) {
    const auto *entries = object.as_object();
    if (entries == nullptr) {
        return nullptr;
    }
    for (const auto &entry : *entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

bool has_unknown_member(const JsonValue &object, std::initializer_list<std::string_view> known) {
    const auto *entries = object.as_object();
    if (entries == nullptr) {
        return false;
    }
    for (const auto &entry : *entries) {
        bool found = false;
        for (const std::string_view name : known) {
            if (entry.first == name) {
                found = true;
                break;
            }
        }
        if (!found) {
            return true;
        }
    }
    return false;
}

/// Requires a string member within `max_bytes`; sets `error` and returns
/// false when present but malformed. Absent members are the caller's
/// decision (required vs optional fields differ).
std::optional<std::string> bounded_string(const JsonValue &object, std::string_view key,
                                          std::size_t max_bytes, std::string &error) {
    const JsonValue *value = member(object, key);
    if (value == nullptr) {
        return std::nullopt;
    }
    const std::string *text = value->as_string();
    if (text == nullptr || text->size() > max_bytes) {
        error = "member '" + std::string(key) + "' must be a string of at most " +
                std::to_string(max_bytes) + " bytes";
        return std::nullopt;
    }
    return *text;
}

/// Requires an exact string member (frozen vocabulary); absent is a
/// decode failure for fields every record must carry.
std::optional<std::string> vocabulary_member(const JsonValue &object, std::string_view key,
                                             bool (*known)(std::string_view), std::string &error) {
    const JsonValue *value = member(object, key);
    if (value == nullptr) {
        error = "member '" + std::string(key) + "' is required";
        return std::nullopt;
    }
    const std::string *text = value->as_string();
    if (text == nullptr || !known(*text)) {
        error = "member '" + std::string(key) + "' carries a value outside the frozen vocabulary";
        return std::nullopt;
    }
    return *text;
}

JsonValue encode_step(const RecoveryStep &step) {
    JsonValue object = make_object();
    put(object, "kind", JsonValue{step.kind});
    put(object, "argument", JsonValue{step.argument});
    put(object, "status", JsonValue{step.status});
    put(object, "operation_id", JsonValue{step.operation_id});
    put(object, "permission", JsonValue{step.permission});
    put(object, "ok", JsonValue{step.ok});
    put(object, "exit_code", JsonValue{static_cast<std::int64_t>(step.exit_code)});
    put(object, "result", JsonValue{step.result});
    put(object, "result_truncated", JsonValue{step.result_truncated});
    put(object, "error", JsonValue{step.error});
    return object;
}

std::optional<RecoveryStep> decode_step(const JsonValue &value, std::string &error) {
    if (!value.is_object() ||
        has_unknown_member(value, {"kind", "argument", "status", "operation_id", "permission", "ok",
                                   "exit_code", "result", "result_truncated", "error"})) {
        error = "step record carries an unknown member or is not an object";
        return std::nullopt;
    }
    RecoveryStep step;
    if (auto kind = vocabulary_member(value, "kind", known_step_kind, error)) {
        step.kind = std::move(*kind);
    } else {
        return std::nullopt;
    }
    if (auto status = vocabulary_member(value, "status", known_step_status, error)) {
        step.status = std::move(*status);
    } else {
        return std::nullopt;
    }
    if (auto permission = vocabulary_member(value, "permission", known_permission, error)) {
        step.permission = std::move(*permission);
    } else {
        return std::nullopt;
    }
    if (auto argument = bounded_string(value, "argument", kMaxGoalBytes, error)) {
        step.argument = std::move(*argument);
    } else if (member(value, "argument") != nullptr) {
        return std::nullopt;
    }
    if (auto operation_id = bounded_string(value, "operation_id", kMaxIdBytes, error)) {
        step.operation_id = std::move(*operation_id);
    } else if (member(value, "operation_id") != nullptr) {
        return std::nullopt;
    }
    if (auto result = bounded_string(value, "result", kMaxRecoveryResultBytes, error)) {
        step.result = std::move(*result);
    } else if (member(value, "result") != nullptr) {
        return std::nullopt;
    }
    if (auto step_error = bounded_string(value, "error", kMaxErrorBytes, error)) {
        step.error = std::move(*step_error);
    } else if (member(value, "error") != nullptr) {
        return std::nullopt;
    }
    if (const JsonValue *ok = member(value, "ok")) {
        const auto flag = ok->as_boolean();
        if (!flag) {
            error = "member 'ok' must be a boolean";
            return std::nullopt;
        }
        step.ok = *flag;
    } else {
        error = "member 'ok' is required";
        return std::nullopt;
    }
    if (const JsonValue *truncated = member(value, "result_truncated")) {
        const auto flag = truncated->as_boolean();
        if (!flag) {
            error = "member 'result_truncated' must be a boolean";
            return std::nullopt;
        }
        step.result_truncated = *flag;
    }
    if (const JsonValue *exit_code = member(value, "exit_code")) {
        const auto code = exit_code->as_integer();
        if (!code) {
            error = "member 'exit_code' must be an integer";
            return std::nullopt;
        }
        step.exit_code = static_cast<int>(*code);
    }
    return step;
}

} // namespace

std::string utc_timestamp_now() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    if (::gmtime_s(&utc, &now) != 0) {
        return {};
    }
#else
    if (::gmtime_r(&now, &utc) == nullptr) {
        return {};
    }
#endif
    char buffer[32];
    const std::size_t length = std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return std::string(buffer, length);
}

std::string encode_recovery(const RecoveryState &state) {
    JsonValue object = make_object();
    put(object, "schema", JsonValue{static_cast<std::int64_t>(state.schema)});
    put(object, "mirage_version", JsonValue{state.mirage_version});
    put(object, "saved_at", JsonValue{state.saved_at});
    JsonValue::Array tasks;
    tasks.reserve(state.tasks.size());
    for (const RecoveryTask &task : state.tasks) {
        JsonValue entry = make_object();
        put(entry, "id", JsonValue{task.id});
        put(entry, "goal", JsonValue{task.goal});
        put(entry, "progress", JsonValue{task.progress});
        put(entry, "has_success", JsonValue{task.has_success});
        put(entry, "success", JsonValue{task.success});
        JsonValue::Array steps;
        steps.reserve(task.steps.size());
        for (const RecoveryStep &step : task.steps) {
            steps.push_back(encode_step(step));
        }
        put(entry, "steps", JsonValue{std::move(steps)});
        tasks.push_back(std::move(entry));
    }
    put(object, "tasks", JsonValue{std::move(tasks)});
    return mira::to_json_string(object);
}

RecoveryDecode decode_recovery(std::string_view body) {
    RecoveryDecode result;
    auto parsed = mira::parse_json(body);
    if (!parsed) {
        result.error = "invalid JSON: " + parsed.error().safe_message;
        return result;
    }
    const JsonValue &document = parsed.value();
    if (!document.is_object()) {
        result.error = "recovery document must be a JSON object";
        return result;
    }
    if (has_unknown_member(document, {"schema", "mirage_version", "saved_at", "tasks"})) {
        result.error = "recovery document contains an unknown member";
        return result;
    }
    if (const JsonValue *schema = member(document, "schema")) {
        const auto version = schema->as_integer();
        if (!version || *version != kRecoverySchema) {
            result.error = "unsupported recovery schema version";
            return result;
        }
    } else {
        result.error = "recovery document lacks the 'schema' member";
        return result;
    }
    RecoveryState state;
    if (auto version = bounded_string(document, "mirage_version", kMaxMetaBytes, result.error)) {
        state.mirage_version = std::move(*version);
    } else if (member(document, "mirage_version") != nullptr) {
        return result;
    }
    if (auto saved_at = bounded_string(document, "saved_at", kMaxMetaBytes, result.error)) {
        state.saved_at = std::move(*saved_at);
    } else if (member(document, "saved_at") != nullptr) {
        return result;
    }
    const JsonValue *tasks = member(document, "tasks");
    if (tasks == nullptr || !tasks->is_array()) {
        result.error = "member 'tasks' must be an array";
        return result;
    }
    if (tasks->as_array()->size() > kMaxRecoveryTasks) {
        result.error = "member 'tasks' exceeds " + std::to_string(kMaxRecoveryTasks) + " entries";
        return result;
    }
    for (const JsonValue &entry : *tasks->as_array()) {
        if (!entry.is_object() || has_unknown_member(entry, {"id", "goal", "progress",
                                                             "has_success", "success", "steps"})) {
            result.error = "task record carries an unknown member or is not an object";
            return result;
        }
        RecoveryTask task;
        if (auto id = bounded_string(entry, "id", kMaxIdBytes, result.error)) {
            if (id->empty()) {
                result.error = "task record member 'id' must not be empty";
                return result;
            }
            task.id = std::move(*id);
        } else if (member(entry, "id") == nullptr) {
            result.error = "task record member 'id' is required";
            return result;
        } else {
            return result;
        }
        if (auto goal = bounded_string(entry, "goal", kMaxGoalBytes, result.error)) {
            task.goal = std::move(*goal);
        } else if (member(entry, "goal") != nullptr) {
            return result;
        }
        if (auto progress = vocabulary_member(entry, "progress", known_progress, result.error)) {
            task.progress = std::move(*progress);
        } else {
            return result;
        }
        if (const JsonValue *has_success = member(entry, "has_success")) {
            const auto flag = has_success->as_boolean();
            if (!flag) {
                result.error = "member 'has_success' must be a boolean";
                return result;
            }
            task.has_success = *flag;
        }
        if (const JsonValue *success = member(entry, "success")) {
            const auto flag = success->as_boolean();
            if (!flag) {
                result.error = "member 'success' must be a boolean";
                return result;
            }
            task.success = *flag;
        }
        const JsonValue *steps = member(entry, "steps");
        if (steps == nullptr || !steps->is_array()) {
            result.error = "task record member 'steps' must be an array";
            return result;
        }
        if (steps->as_array()->size() > kMaxRecoveryStepsPerTask) {
            result.error = "task record member 'steps' exceeds " +
                           std::to_string(kMaxRecoveryStepsPerTask) + " entries";
            return result;
        }
        task.steps.reserve(steps->as_array()->size());
        for (const JsonValue &raw_step : *steps->as_array()) {
            auto step = decode_step(raw_step, result.error);
            if (!step) {
                return result;
            }
            task.steps.push_back(std::move(*step));
        }
        for (const RecoveryTask &existing : state.tasks) {
            if (existing.id == task.id) {
                result.error = "duplicate task id '" + task.id + "'";
                return result;
            }
        }
        state.tasks.push_back(std::move(task));
    }
    result.ok = true;
    result.state = std::move(state);
    return result;
}

} // namespace mirage::runtime::persistence
