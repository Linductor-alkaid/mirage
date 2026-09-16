/// M1.5-01 golden-vector consistency gate (docs/design/mirage-ipc-protocol-v1.md
/// section 8): consumes the shared vectors file
/// tests/runtime/data/ipc_protocol_golden.json — the very same file the
/// TypeScript mirror (ui/contracts/test/golden-vectors.test.ts) reads — and
/// asserts the `runtime/ipc` codec reproduces every canonical wire form
/// byte-for-byte plus the stable decode error strings.
///
/// Section coverage split (M1.5-01): `events` / `event_failures` are consumed
/// by the TypeScript side only; the C++ event codec lands in M1.5-02, which
/// also lifts the `encode_pending` restriction on the hello-capability
/// response vector (the C++ encoder has no `events` identity member yet).
/// There are deliberately no skip placeholders for those sections here.

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
    if (op == "task.inspect") {
        return ipc::InspectTaskRequest{vector_string(body, "task_id")};
    }
    if (op == "task.cancel") {
        return ipc::CancelTaskRequest{vector_string(body, "task_id")};
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
                          std::is_same_v<T, ipc::ShutdownRequest>) {
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
        // hello-capability additionally carries "events": true; the C++
        // struct has no such member yet, so it is ignored here — the decode
        // assertion below proves unknown members are tolerated (M1.5-02
        // brings the encoder side).
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
    // Every section this gate consumes must exist; the events sections are
    // consumed by the TypeScript mirror in M1.5-01 and are intentionally not
    // touched here (no placeholder assertions).
    for (const char *name : {"requests", "request_failures", "responses", "response_failures",
                             "framing", "framing_failures"}) {
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

        // Vectors marked encode_pending (hello-capability: `events` identity
        // member) are decode-only until M1.5-02 gives the C++ encoder that
        // member; the decode assertion proves unknown members are tolerated.
        if (vector.find("encode_pending") == nullptr) {
            const std::string encoded = ipc::encode_response(expected);
            check_string_equal(name, "encoded response", encoded, canonical);
        }

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
    run_scenario("golden_framing", scenario_golden_framing);
    run_scenario("golden_framing_failures", scenario_golden_framing_failures);
    return mirage::testing::finish("ipc_protocol_golden_test");
}
