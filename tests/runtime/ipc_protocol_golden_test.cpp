/// M1.5-01 golden-vector consistency gate (docs/design/mirage-ipc-protocol-v1.md
/// section 8): consumes the shared vectors file
/// tests/runtime/data/ipc_protocol_golden.json — the very same file the
/// TypeScript mirror (ui/contracts/test/golden-vectors.test.ts) reads — and
/// asserts the `runtime/ipc` codec reproduces every canonical wire form
/// byte-for-byte plus the stable decode error strings.
///
/// Section coverage (since M1.5-02): every section is consumed here on the
/// C++ side. M1.5-01 restricted `events` / `event_failures` to the
/// TypeScript mirror and marked the hello-capability response vector
/// `encode_pending`; M1.5-02 lands the C++ event codec, consumes both
/// sections and lifts that restriction — the identity encoder now writes
/// the `events` capability member, so all response vectors assert both
/// directions. There are deliberately no skip placeholders.

#include "../support/test.hpp"

#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>

#include <mira/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#ifndef MIRAGE_GOLDEN_VECTORS_JSON
#error "MIRAGE_GOLDEN_VECTORS_JSON must point at tests/runtime/data/ipc_protocol_golden.json"
#endif

namespace {

namespace ipc = mirage::runtime::ipc;

constexpr std::string_view kVectorsPath = MIRAGE_GOLDEN_VECTORS_JSON;

// --- vectors-file accessors --------------------------------------------------
//
// The vectors file is a build-time artifact of the contract, so structural
// problems inside it are recorded as check failures (and, where a value is
// required to continue, a safe default is returned instead of crashing).

const mira::JsonValue &vectors() {
    static const mira::JsonValue root = []() -> mira::JsonValue {
        std::ifstream stream{std::string(kVectorsPath)};
        std::stringstream buffer;
        buffer << stream.rdbuf();
        auto parsed = mira::parse_json(buffer.str());
        if (!parsed) {
            std::fprintf(stderr,
                         "[ipc_protocol_golden_test] cannot read golden vectors at %s: %s\n",
                         std::string(kVectorsPath).c_str(), parsed.error().safe_message.c_str());
            std::exit(1);
        }
        return std::move(parsed.value());
    }();
    return root;
}

const mira::JsonValue::Array &section(const char *name) {
    static const mira::JsonValue::Array empty;
    const auto *value = vectors().find(name);
    if (value == nullptr || value->as_array() == nullptr) {
        std::fprintf(stderr, "golden vectors file is missing the '%s' array\n", name);
        MIRAGE_CHECK(false);
        return empty;
    }
    return *value->as_array();
}

const mira::JsonValue &vector_member(const mira::JsonValue &object, std::string_view key) {
    static const mira::JsonValue fallback = mira::JsonValue{mira::JsonValue::Object{}};
    const auto *value = object.find(key);
    if (value != nullptr) {
        return *value;
    }
    MIRAGE_CHECK(false); // the shared vectors file is malformed
    return fallback;
}

std::string vector_string(const mira::JsonValue &object, std::string_view key) {
    const auto *value = object.find(key);
    if (value != nullptr) {
        if (const auto *text = value->as_string(); text != nullptr) {
            return *text;
        }
    }
    MIRAGE_CHECK(false); // the shared vectors file is malformed
    return {};
}

std::int64_t vector_integer(const mira::JsonValue &object, std::string_view key) {
    const auto *value = object.find(key);
    if (value != nullptr) {
        if (const auto integer = value->as_integer()) {
            return *integer;
        }
    }
    MIRAGE_CHECK(false); // the shared vectors file is malformed
    return 0;
}

bool vector_boolean(const mira::JsonValue &object, std::string_view key) {
    const auto *value = object.find(key);
    if (value != nullptr) {
        if (const auto flag = value->as_boolean()) {
            return *flag;
        }
    }
    MIRAGE_CHECK(false); // the shared vectors file is malformed
    return false;
}

std::string vector_name(const mira::JsonValue &vector) { return vector_string(vector, "name"); }

// --- assertion helpers -------------------------------------------------------

/// Byte-exact comparison that prints both sides on mismatch so a canonical
/// string drift is immediately actionable in the test log.
void check_string_equal(const std::string &vector_name, std::string_view what,
                        std::string_view actual, std::string_view expected) {
    if (actual != expected) {
        std::fprintf(stderr,
                     "golden vector '%s' (%.*s) mismatch\n  actual:   %.*s\n  expected: %.*s\n",
                     vector_name.c_str(), static_cast<int>(what.size()), what.data(),
                     static_cast<int>(actual.size()), actual.data(),
                     static_cast<int>(expected.size()), expected.data());
    }
    MIRAGE_CHECK(actual == expected);
}

/// `error` vectors match exactly; `error_prefix` vectors match by prefix
/// because the C++ parser appends its own diagnostic after
/// "payload is not valid JSON" (meta.notes in the vectors file).
void check_decode_failure(const std::string &name, const std::string &actual_error,
                          const mira::JsonValue &vector) {
    if (vector.find("error") != nullptr) {
        check_string_equal(name, "decode error", actual_error, vector_string(vector, "error"));
        return;
    }
    if (vector.find("error_prefix") != nullptr) {
        const auto expected = vector_string(vector, "error_prefix");
        if (actual_error.rfind(expected, 0) != 0) {
            std::fprintf(stderr, "golden vector '%s' decode error '%s' does not start with '%s'\n",
                         name.c_str(), actual_error.c_str(), expected.c_str());
        }
        MIRAGE_CHECK(actual_error.rfind(expected, 0) == 0);
        return;
    }
    MIRAGE_CHECK(false); // vector carries neither 'error' nor 'error_prefix'
}

std::string to_hex(const std::string &bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const char byte : bytes) {
        const auto value = static_cast<unsigned char>(byte);
        hex.push_back(kDigits[value >> 4]);
        hex.push_back(kDigits[value & 0xF]);
    }
    return hex;
}

/// Structured expectation for one `events` vector: the vectors file carries
/// the decoded event object (`{v, seq, event, ...payload}`); dispatch on the
/// stable event name and map it onto the codec's Event value.
ipc::Event event_from_vector(const std::string &name, const mira::JsonValue &body) {
    ipc::Event event;
    event.seq = static_cast<std::uint64_t>(vector_integer(body, "seq"));
    const auto kind = vector_string(body, "event");
    if (kind == "task.updated") {
        ipc::TaskUpdatedEvent payload;
        payload.task_id = vector_string(body, "task_id");
        payload.goal = vector_string(body, "goal");
        payload.progress = vector_string(body, "progress");
        payload.has_success = vector_boolean(body, "has_success");
        payload.success = vector_boolean(body, "success");
        event.payload = std::move(payload);
    } else if (kind == "host.status") {
        event.payload = ipc::HostStatusEvent{vector_string(body, "status")};
    } else if (kind == "events.overflow") {
        ipc::EventsOverflowEvent payload;
        payload.dropped = static_cast<std::uint64_t>(vector_integer(body, "dropped"));
        event.payload = std::move(payload);
    } else if (kind == "permission.request") {
        ipc::PermissionRequestedEvent payload;
        payload.request_id = vector_string(body, "request_id");
        payload.capability = vector_string(body, "capability");
        payload.resource = vector_string(body, "resource");
        payload.task_id = vector_string(body, "task_id");
        payload.timeout_ms = vector_integer(body, "timeout_ms");
        event.payload = std::move(payload);
    } else {
        std::fprintf(stderr, "golden event vector '%s' carries unknown event name '%s'\n",
                     name.c_str(), kind.c_str());
        MIRAGE_CHECK(false);
    }
    return event;
}

/// Full-structure event equality (seq plus every payload member), so a
/// decode that drops or mangles any member fails here instead of silently
/// round-tripping.
void check_event_equal(const std::string &name, const ipc::Event &expected,
                       const ipc::Event &actual) {
    MIRAGE_CHECK(actual.seq == expected.seq);
    if (actual.seq != expected.seq) {
        return;
    }
    MIRAGE_CHECK(actual.payload.index() == expected.payload.index());
    if (actual.payload.index() != expected.payload.index()) {
        return;
    }
    if (const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::TaskUpdatedEvent>(actual.payload);
        check_string_equal(name, "task_id", decoded.task_id, task->task_id);
        check_string_equal(name, "goal", decoded.goal, task->goal);
        check_string_equal(name, "progress", decoded.progress, task->progress);
        MIRAGE_CHECK(decoded.has_success == task->has_success);
        MIRAGE_CHECK(decoded.success == task->success);
    } else if (const auto *status = std::get_if<ipc::HostStatusEvent>(&expected.payload)) {
        check_string_equal(name, "status", std::get<ipc::HostStatusEvent>(actual.payload).status,
                           status->status);
    } else if (const auto *overflow = std::get_if<ipc::EventsOverflowEvent>(&expected.payload)) {
        MIRAGE_CHECK(std::get<ipc::EventsOverflowEvent>(actual.payload).dropped ==
                     overflow->dropped);
    } else if (const auto *permission =
                   std::get_if<ipc::PermissionRequestedEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::PermissionRequestedEvent>(actual.payload);
        check_string_equal(name, "request_id", decoded.request_id, permission->request_id);
        check_string_equal(name, "capability", decoded.capability, permission->capability);
        check_string_equal(name, "resource", decoded.resource, permission->resource);
        check_string_equal(name, "task_id", decoded.task_id, permission->task_id);
        MIRAGE_CHECK(decoded.timeout_ms == permission->timeout_ms);
    }
}

// --- structured expectations from the vectors file ---------------------------

ipc::Request request_from_body(const mira::JsonValue &body) {
    const auto op = vector_string(body, "op");
    if (op == "hello") {
        return ipc::HelloRequest{};
    }
    if (op == "task.list") {
        return ipc::ListTasksRequest{};
    }
    if (op == "service.shutdown") {
        return ipc::ShutdownRequest{};
    }
    if (op == "events.subscribe") {
        return ipc::SubscribeEventsRequest{};
    }
    if (op == "events.unsubscribe") {
        return ipc::UnsubscribeEventsRequest{};
    }
    if (op == "task.inspect") {
        return ipc::InspectTaskRequest{vector_string(body, "task_id")};
    }
    if (op == "task.cancel") {
        return ipc::CancelTaskRequest{vector_string(body, "task_id")};
    }
    if (op == "permission.respond") {
        ipc::RespondPermissionRequest respond;
        respond.request_id = vector_string(body, "request_id");
        respond.approved = vector_boolean(body, "approved");
        return respond;
    }
    if (op == "permission.list") {
        return ipc::ListPermissionsRequest{};
    }
    if (op == "task.submit") {
        ipc::SubmitTaskRequest submit;
        submit.goal = vector_string(body, "goal");
        if (const auto *steps = body.find("steps"); steps != nullptr) {
            const auto *entries = steps->as_array();
            MIRAGE_CHECK(entries != nullptr);
            if (entries != nullptr) {
                for (const auto &entry : *entries) {
                    ipc::TaskStep step;
                    const auto kind = ipc::step_kind_from_name(vector_string(entry, "op"));
                    MIRAGE_CHECK(kind.has_value());
                    step.kind = kind.value_or(ipc::StepKind::FilesystemRead);
                    step.argument = vector_string(entry, "arg");
                    submit.steps.push_back(std::move(step));
                }
            }
        }
        if (const auto *timeout = body.find("step_timeout_ms"); timeout != nullptr) {
            const auto milliseconds_value = timeout->as_integer();
            MIRAGE_CHECK(milliseconds_value.has_value());
            if (milliseconds_value) {
                submit.step_timeout = std::chrono::milliseconds(*milliseconds_value);
            }
        }
        return submit;
    }
    MIRAGE_CHECK(false); // unknown op in the vectors file
    return ipc::HelloRequest{};
}

void check_request_equal(const std::string &name, const ipc::Request &expected,
                         const ipc::Request &actual) {
    if (expected.index() != actual.index()) {
        std::fprintf(stderr,
                     "golden vector '%s': decoded request holds body variant index %zu, "
                     "expected %zu\n",
                     name.c_str(), actual.index(), expected.index());
        MIRAGE_CHECK(false);
        return;
    }
    std::visit(
        [&](const auto &expected_value) {
            using T = std::decay_t<decltype(expected_value)>;
            if constexpr (std::is_same_v<T, ipc::HelloRequest> ||
                          std::is_same_v<T, ipc::ListTasksRequest> ||
                          std::is_same_v<T, ipc::ShutdownRequest> ||
                          std::is_same_v<T, ipc::SubscribeEventsRequest> ||
                          std::is_same_v<T, ipc::UnsubscribeEventsRequest>) {
                // Stateless bodies: the variant index comparison above suffices.
            } else if constexpr (std::is_same_v<T, ipc::SubmitTaskRequest>) {
                const auto &submit = std::get<ipc::SubmitTaskRequest>(actual);
                check_string_equal(name, "submit goal", submit.goal, expected_value.goal);
                MIRAGE_CHECK(submit.steps.size() == expected_value.steps.size());
                const std::size_t count =
                    std::min(submit.steps.size(), expected_value.steps.size());
                for (std::size_t index = 0; index < count; ++index) {
                    MIRAGE_CHECK(submit.steps[index].kind == expected_value.steps[index].kind);
                    check_string_equal(name, "step argument", submit.steps[index].argument,
                                       expected_value.steps[index].argument);
                }
                MIRAGE_CHECK(submit.step_timeout.has_value() ==
                             expected_value.step_timeout.has_value());
                if (submit.step_timeout.has_value() && expected_value.step_timeout.has_value()) {
                    MIRAGE_CHECK(submit.step_timeout->count() ==
                                 expected_value.step_timeout->count());
                }
            } else if constexpr (std::is_same_v<T, ipc::InspectTaskRequest> ||
                                 std::is_same_v<T, ipc::CancelTaskRequest>) {
                const auto &request = std::get<T>(actual);
                check_string_equal(name, "task_id", request.task_id, expected_value.task_id);
            } else if constexpr (std::is_same_v<T, ipc::RespondPermissionRequest>) {
                const auto &respond = std::get<ipc::RespondPermissionRequest>(actual);
                check_string_equal(name, "request_id", respond.request_id,
                                   expected_value.request_id);
                MIRAGE_CHECK(respond.approved == expected_value.approved);
            }
        },
        expected);
}

ipc::Response response_from_vector(const mira::JsonValue &vector) {
    ipc::Response response;
    response.id = static_cast<std::uint64_t>(vector_integer(vector, "id"));
    response.ok = vector_boolean(vector, "ok");
    if (!response.ok) {
        const auto &error = vector_member(vector, "error");
        response.error.code = vector_string(error, "code");
        response.error.message = vector_string(error, "message");
        return response;
    }
    const auto &payload = vector_member(vector, "payload");
    const auto kind = vector_string(payload, "kind");
    if (kind == "shutdown-accepted") {
        response.payload = ipc::ShutdownAccepted{};
        return response;
    }
    const auto &value = vector_member(payload, "value");
    if (kind == "identity") {
        // The wire member is "service"; the C++ struct member is `name`.
        ipc::ServiceIdentity identity;
        identity.name = vector_string(value, "service");
        identity.mirage_version = vector_string(value, "mirage_version");
        identity.mira_core_version = vector_string(value, "mira_core_version");
        identity.host_status = vector_string(value, "host_status");
        identity.protocol = static_cast<int>(vector_integer(value, "protocol"));
        // DEC-012 capability member: hello-capability carries "events":true,
        // hello-identity omits it. Presence maps onto the optional — missing
        // decodes to nullopt, so consumers read `value_or(false)`.
        if (const auto *events = value.find("events"); events != nullptr) {
            const auto flag = events->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            identity.events = flag;
        }
        // DEC-020 permission-capability member: same optional discipline.
        if (const auto *permissions = value.find("permissions"); permissions != nullptr) {
            const auto flag = permissions->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            identity.permissions = flag;
        }
        response.payload = std::move(identity);
    } else if (kind == "submitted") {
        response.payload = ipc::TaskSubmitted{vector_string(value, "task_id")};
    } else if (kind == "list") {
        ipc::TaskList list;
        const auto *entries = vector_member(value, "tasks").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                list.tasks.push_back(ipc::TaskSummary{vector_string(entry, "id"),
                                                      vector_string(entry, "goal"),
                                                      vector_string(entry, "progress")});
            }
        }
        response.payload = std::move(list);
    } else if (kind == "inspect") {
        ipc::InspectTask inspect;
        inspect.id = vector_string(value, "id");
        inspect.goal = vector_string(value, "goal");
        inspect.progress = vector_string(value, "progress");
        // Member presence *is* the has_success semantics (schema doc 6.2).
        if (const auto *success = value.find("success"); success != nullptr) {
            const auto flag = success->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            inspect.has_success = true;
            inspect.success = flag.value_or(false);
        }
        const auto *entries = vector_member(value, "steps").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                ipc::StepView step;
                step.index = static_cast<int>(vector_integer(entry, "index"));
                step.kind = vector_string(entry, "kind");
                step.status = vector_string(entry, "status");
                step.operation_id = vector_string(entry, "operation_id");
                step.permission = vector_string(entry, "permission");
                step.ok = vector_boolean(entry, "ok");
                step.exit_code = static_cast<int>(vector_integer(entry, "exit_code"));
                step.result = vector_string(entry, "result");
                step.result_truncated = vector_boolean(entry, "result_truncated");
                step.error = vector_string(entry, "error");
                inspect.steps.push_back(std::move(step));
            }
        }
        response.payload = std::move(inspect);
    } else if (kind == "cancelled") {
        response.payload =
            ipc::TaskCancelled{vector_string(value, "task_id"), vector_string(value, "progress")};
    } else if (kind == "permission-responded") {
        response.payload = ipc::PermissionResponded{vector_string(value, "request_id")};
    } else if (kind == "permission-list") {
        ipc::PermissionPendingList list;
        const auto *entries = vector_member(value, "pending").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                ipc::PendingPermission permission;
                permission.request_id = vector_string(entry, "request_id");
                permission.capability = vector_string(entry, "capability");
                permission.resource = vector_string(entry, "resource");
                permission.task_id = vector_string(entry, "task_id");
                permission.timeout_ms = vector_integer(entry, "timeout_ms");
                list.pending.push_back(std::move(permission));
            }
        }
        response.payload = std::move(list);
    } else {
        MIRAGE_CHECK(false); // unknown payload kind in the vectors file
    }
    return response;
}

void check_response_equal(const std::string &name, const ipc::Response &expected,
                          const ipc::Response &actual) {
    MIRAGE_CHECK(actual.ok == expected.ok);
    MIRAGE_CHECK(actual.id == expected.id);
    if (!expected.ok) {
        check_string_equal(name, "error code", actual.error.code, expected.error.code);
        check_string_equal(name, "error message", actual.error.message, expected.error.message);
        return;
    }
    if (expected.payload.index() != actual.payload.index()) {
        std::fprintf(stderr,
                     "golden vector '%s': decoded response holds payload variant index %zu, "
                     "expected %zu\n",
                     name.c_str(), actual.payload.index(), expected.payload.index());
        MIRAGE_CHECK(false);
        return;
    }
    std::visit(
        [&](const auto &expected_value) {
            using T = std::decay_t<decltype(expected_value)>;
            const auto &actual_value = std::get<T>(actual.payload);
            if constexpr (std::is_same_v<T, ipc::ShutdownAccepted>) {
                // Nothing beyond the ok envelope.
            } else if constexpr (std::is_same_v<T, ipc::ServiceIdentity>) {
                check_string_equal(name, "service name", actual_value.name, expected_value.name);
                MIRAGE_CHECK(actual_value.mirage_version == expected_value.mirage_version);
                MIRAGE_CHECK(actual_value.mira_core_version == expected_value.mira_core_version);
                MIRAGE_CHECK(actual_value.host_status == expected_value.host_status);
                MIRAGE_CHECK(actual_value.protocol == expected_value.protocol);
                // Wire presence is part of the contract: an engaged encoder
                // must write the member, a disengaged one must omit it.
                MIRAGE_CHECK(actual_value.events.has_value() == expected_value.events.has_value());
                if (actual_value.events.has_value() && expected_value.events.has_value()) {
                    MIRAGE_CHECK(*actual_value.events == *expected_value.events);
                }
                MIRAGE_CHECK(actual_value.permissions.has_value() ==
                             expected_value.permissions.has_value());
                if (actual_value.permissions.has_value() &&
                    expected_value.permissions.has_value()) {
                    MIRAGE_CHECK(*actual_value.permissions == *expected_value.permissions);
                }
            } else if constexpr (std::is_same_v<T, ipc::TaskSubmitted>) {
                check_string_equal(name, "task_id", actual_value.task_id, expected_value.task_id);
            } else if constexpr (std::is_same_v<T, ipc::TaskList>) {
                MIRAGE_CHECK(actual_value.tasks.size() == expected_value.tasks.size());
                const std::size_t count =
                    std::min(actual_value.tasks.size(), expected_value.tasks.size());
                for (std::size_t index = 0; index < count; ++index) {
                    check_string_equal(name, "task list id", actual_value.tasks[index].id,
                                       expected_value.tasks[index].id);
                    MIRAGE_CHECK(actual_value.tasks[index].goal ==
                                 expected_value.tasks[index].goal);
                    MIRAGE_CHECK(actual_value.tasks[index].progress ==
                                 expected_value.tasks[index].progress);
                }
            } else if constexpr (std::is_same_v<T, ipc::InspectTask>) {
                MIRAGE_CHECK(actual_value.id == expected_value.id);
                check_string_equal(name, "inspect goal", actual_value.goal, expected_value.goal);
                MIRAGE_CHECK(actual_value.progress == expected_value.progress);
                MIRAGE_CHECK(actual_value.has_success == expected_value.has_success);
                MIRAGE_CHECK(actual_value.success == expected_value.success);
                MIRAGE_CHECK(actual_value.steps.size() == expected_value.steps.size());
                const std::size_t count =
                    std::min(actual_value.steps.size(), expected_value.steps.size());
                for (std::size_t index = 0; index < count; ++index) {
                    const auto &decoded = actual_value.steps[index];
                    const auto &wanted = expected_value.steps[index];
                    MIRAGE_CHECK(decoded.index == wanted.index);
                    MIRAGE_CHECK(decoded.kind == wanted.kind);
                    MIRAGE_CHECK(decoded.status == wanted.status);
                    MIRAGE_CHECK(decoded.operation_id == wanted.operation_id);
                    MIRAGE_CHECK(decoded.permission == wanted.permission);
                    MIRAGE_CHECK(decoded.ok == wanted.ok);
                    MIRAGE_CHECK(decoded.exit_code == wanted.exit_code);
                    MIRAGE_CHECK(decoded.result == wanted.result);
                    MIRAGE_CHECK(decoded.result_truncated == wanted.result_truncated);
                    MIRAGE_CHECK(decoded.error == wanted.error);
                }
            } else if constexpr (std::is_same_v<T, ipc::TaskCancelled>) {
                MIRAGE_CHECK(actual_value.task_id == expected_value.task_id);
                MIRAGE_CHECK(actual_value.progress == expected_value.progress);
            } else if constexpr (std::is_same_v<T, ipc::PermissionResponded>) {
                check_string_equal(name, "request_id", actual_value.request_id,
                                   expected_value.request_id);
            } else if constexpr (std::is_same_v<T, ipc::PermissionPendingList>) {
                MIRAGE_CHECK(actual_value.pending.size() == expected_value.pending.size());
                const std::size_t count =
                    std::min(actual_value.pending.size(), expected_value.pending.size());
                for (std::size_t index = 0; index < count; ++index) {
                    const auto &actual_entry = actual_value.pending[index];
                    const auto &wanted = expected_value.pending[index];
                    check_string_equal(name, "pending request_id", actual_entry.request_id,
                                       wanted.request_id);
                    check_string_equal(name, "pending capability", actual_entry.capability,
                                       wanted.capability);
                    check_string_equal(name, "pending resource", actual_entry.resource,
                                       wanted.resource);
                    check_string_equal(name, "pending task_id", actual_entry.task_id,
                                       wanted.task_id);
                    MIRAGE_CHECK(actual_entry.timeout_ms == wanted.timeout_ms);
                }
            }
        },
        expected.payload);
}

// --- scenarios ---------------------------------------------------------------

void scenario_golden_meta() {
    const auto &meta = vector_member(vectors(), "meta");
    check_string_equal("meta", "schema", vector_string(meta, "schema"),
                       "mirage-ipc-protocol-golden-vectors");
    MIRAGE_CHECK(vector_integer(meta, "version") >= 1);
    MIRAGE_CHECK(vector_integer(meta, "protocol_version") == 1);
    // Every section this gate consumes must exist; since M1.5-02 that is all
    // of them, on the C++ side as well (events / event_failures included).
    for (const char *name : {"requests", "request_failures", "responses", "response_failures",
                             "events", "event_failures", "framing", "framing_failures"}) {
        MIRAGE_CHECK(vectors().find(name) != nullptr);
    }
}

void scenario_golden_requests() {
    for (const auto &vector : section("requests")) {
        const auto name = vector_name(vector);
        const auto id = static_cast<std::uint64_t>(vector_integer(vector, "id"));
        const ipc::Request request = request_from_body(vector_member(vector, "body"));
        const auto canonical = vector_string(vector, "canonical");

        const std::string encoded = ipc::encode_request(id, request);
        check_string_equal(name, "encoded request", encoded, canonical);

        const ipc::RequestDecode decoded = ipc::decode_request(canonical);
        if (!decoded.ok) {
            std::fprintf(stderr, "golden vector '%s': decode_request failed: %s\n", name.c_str(),
                         decoded.error.c_str());
        }
        MIRAGE_CHECK(decoded.ok);
        MIRAGE_CHECK(decoded.id == id);
        if (decoded.ok) {
            check_request_equal(name, request, decoded.body);
        }
    }
}

void scenario_golden_request_failures() {
    for (const auto &vector : section("request_failures")) {
        const auto name = vector_name(vector);
        const ipc::RequestDecode decoded = ipc::decode_request(vector_string(vector, "payload"));
        MIRAGE_CHECK(!decoded.ok);
        if (!decoded.ok) {
            check_decode_failure(name, decoded.error, vector);
        }
    }
}

void scenario_golden_responses() {
    for (const auto &vector : section("responses")) {
        const auto name = vector_name(vector);
        const ipc::Response expected = response_from_vector(vector_member(vector, "response"));
        const auto canonical = vector_string(vector, "canonical");

        // Every response vector asserts both directions since M1.5-02: the
        // identity encoder writes the `events` capability member, so no
        // vector carries the old encode_pending restriction anymore.
        const std::string encoded = ipc::encode_response(expected);
        check_string_equal(name, "encoded response", encoded, canonical);

        const ipc::ResponseDecode decoded = ipc::decode_response(canonical);
        if (!decoded.ok) {
            std::fprintf(stderr, "golden vector '%s': decode_response failed: %s\n", name.c_str(),
                         decoded.error.c_str());
        }
        MIRAGE_CHECK(decoded.ok);
        if (decoded.ok) {
            check_response_equal(name, expected, decoded.response);
        }
    }
}

void scenario_golden_response_failures() {
    for (const auto &vector : section("response_failures")) {
        const auto name = vector_name(vector);
        const ipc::ResponseDecode decoded = ipc::decode_response(vector_string(vector, "payload"));
        MIRAGE_CHECK(!decoded.ok);
        if (!decoded.ok) {
            check_decode_failure(name, decoded.error, vector);
        }
    }
}

void scenario_golden_framing() {
    for (const auto &vector : section("framing")) {
        const auto name = vector_name(vector);
        const auto payload = vector_string(vector, "payload");

        const std::string frame = ipc::make_frame(payload);
        check_string_equal(name, "frame hex", to_hex(frame), vector_string(vector, "frame_hex"));

        std::string buffer = frame;
        const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
        MIRAGE_CHECK(extraction.status == ipc::FrameExtract::Message);
        if (extraction.status == ipc::FrameExtract::Message) {
            check_string_equal(name, "extracted payload", extraction.message, payload);
            MIRAGE_CHECK(buffer.empty());
        }
    }
}

void scenario_golden_framing_failures() {
    for (const auto &vector : section("framing_failures")) {
        const auto name = vector_name(vector);
        const auto declared = vector_integer(vector, "declared_length");

        // Bare 4-byte little-endian header with the declared length, no body.
        std::string buffer;
        buffer.push_back(static_cast<char>(declared & 0xFF));
        buffer.push_back(static_cast<char>((declared >> 8) & 0xFF));
        buffer.push_back(static_cast<char>((declared >> 16) & 0xFF));
        buffer.push_back(static_cast<char>((declared >> 24) & 0xFF));

        const ipc::FrameExtraction extraction = ipc::try_extract_frame(buffer);
        MIRAGE_CHECK(extraction.status == ipc::FrameExtract::ProtocolError);
        check_string_equal(name, "protocol error reason", extraction.reason,
                           vector_string(vector, "reason"));
    }
}

void scenario_golden_events() {
    for (const auto &vector : section("events")) {
        const auto name = vector_name(vector);
        const ipc::Event expected = event_from_vector(name, vector_member(vector, "event"));
        const auto canonical = vector_string(vector, "canonical");

        const std::string encoded = ipc::encode_event(expected);
        check_string_equal(name, "encoded event", encoded, canonical);

        const ipc::EventDecode decoded = ipc::decode_event(canonical);
        if (!decoded.ok) {
            std::fprintf(stderr, "golden vector '%s': decode_event failed: %s\n", name.c_str(),
                         decoded.error.c_str());
        }
        MIRAGE_CHECK(decoded.ok);
        if (decoded.ok) {
            check_event_equal(name, expected, decoded.event);
        }
    }
}

void scenario_golden_event_failures() {
    for (const auto &vector : section("event_failures")) {
        const auto name = vector_name(vector);
        const ipc::EventDecode decoded = ipc::decode_event(vector_string(vector, "payload"));
        MIRAGE_CHECK(!decoded.ok);
        if (!decoded.ok) {
            check_decode_failure(name, decoded.error, vector);
        }
    }
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[ipc_protocol_golden_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("golden_meta", scenario_golden_meta);
    run_scenario("golden_requests", scenario_golden_requests);
    run_scenario("golden_request_failures", scenario_golden_request_failures);
    run_scenario("golden_responses", scenario_golden_responses);
    run_scenario("golden_response_failures", scenario_golden_response_failures);
    run_scenario("golden_events", scenario_golden_events);
    run_scenario("golden_event_failures", scenario_golden_event_failures);
    run_scenario("golden_framing", scenario_golden_framing);
    run_scenario("golden_framing_failures", scenario_golden_framing_failures);
    return mirage::testing::finish("ipc_protocol_golden_test");
}
