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
    } else if (kind == "session.updated") {
        ipc::SessionUpdatedEvent payload;
        payload.session_id = vector_string(body, "session_id");
        payload.state = vector_string(body, "state");
        event.payload = std::move(payload);
    } else if (kind == "session.message") {
        ipc::SessionMessageEvent payload;
        payload.session_id = vector_string(body, "session_id");
        payload.task_id = vector_string(body, "task_id");
        payload.kind = vector_string(body, "kind");
        payload.text = vector_string(body, "text");
        payload.sequence = static_cast<std::uint64_t>(vector_integer(body, "sequence"));
        event.payload = std::move(payload);
    } else if (kind == "session.turn") {
        ipc::SessionTurnEvent payload;
        payload.session_id = vector_string(body, "session_id");
        payload.task_id = vector_string(body, "task_id");
        payload.step = static_cast<int>(vector_integer(body, "step"));
        payload.kind = vector_string(body, "kind");
        payload.status = vector_string(body, "status");
        event.payload = std::move(payload);
    } else if (kind == "session.output") {
        ipc::SessionOutputEvent payload;
        payload.session_id = vector_string(body, "session_id");
        payload.task_id = vector_string(body, "task_id");
        payload.step = static_cast<int>(vector_integer(body, "step"));
        payload.chunk = vector_string(body, "chunk");
        payload.truncated = vector_boolean(body, "truncated");
        event.payload = std::move(payload);
    } else if (kind == "workflow.run_updated") {
        ipc::WorkflowRunUpdatedEvent payload;
        payload.run_id = vector_string(body, "run_id");
        payload.workflow_id = vector_string(body, "workflow_id");
        payload.state = vector_string(body, "state");
        payload.run_epoch = static_cast<std::uint64_t>(vector_integer(body, "run_epoch"));
        if (const auto *summary = body.find("summary"); summary != nullptr) {
            const auto summary_value = summary->as_string();
            MIRAGE_CHECK(summary_value != nullptr);
            if (summary_value != nullptr) {
                payload.summary = *summary_value;
            }
        }
        event.payload = std::move(payload);
    } else if (kind == "session.chat_updated") {
        ipc::ChatTurnUpdatedEvent payload;
        payload.session_id = vector_string(body, "session_id");
        payload.turn_id = vector_string(body, "turn_id");
        payload.status = vector_string(body, "status");
        payload.user_text = vector_string(body, "user_text");
        payload.sequence = static_cast<std::uint64_t>(vector_integer(body, "sequence"));
        if (const auto *reply = body.find("reply_text"); reply != nullptr) {
            const auto text = reply->as_string();
            MIRAGE_CHECK(text != nullptr);
            if (text != nullptr) {
                payload.reply_text = *text;
                payload.has_reply = true;
            }
        }
        if (const auto *failure = body.find("error"); failure != nullptr) {
            const auto text = failure->as_string();
            MIRAGE_CHECK(text != nullptr);
            if (text != nullptr) {
                payload.error = *text;
                payload.has_error = true;
            }
        }
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
    } else if (const auto *session = std::get_if<ipc::SessionUpdatedEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::SessionUpdatedEvent>(actual.payload);
        check_string_equal(name, "session_id", decoded.session_id, session->session_id);
        check_string_equal(name, "state", decoded.state, session->state);
    } else if (const auto *message = std::get_if<ipc::SessionMessageEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::SessionMessageEvent>(actual.payload);
        check_string_equal(name, "session_id", decoded.session_id, message->session_id);
        check_string_equal(name, "task_id", decoded.task_id, message->task_id);
        check_string_equal(name, "kind", decoded.kind, message->kind);
        check_string_equal(name, "text", decoded.text, message->text);
        MIRAGE_CHECK(decoded.sequence == message->sequence);
    } else if (const auto *turn = std::get_if<ipc::SessionTurnEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::SessionTurnEvent>(actual.payload);
        check_string_equal(name, "session_id", decoded.session_id, turn->session_id);
        check_string_equal(name, "task_id", decoded.task_id, turn->task_id);
        MIRAGE_CHECK(decoded.step == turn->step);
        check_string_equal(name, "kind", decoded.kind, turn->kind);
        check_string_equal(name, "status", decoded.status, turn->status);
    } else if (const auto *output = std::get_if<ipc::SessionOutputEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::SessionOutputEvent>(actual.payload);
        check_string_equal(name, "session_id", decoded.session_id, output->session_id);
        check_string_equal(name, "task_id", decoded.task_id, output->task_id);
        MIRAGE_CHECK(decoded.step == output->step);
        check_string_equal(name, "chunk", decoded.chunk, output->chunk);
        MIRAGE_CHECK(decoded.truncated == output->truncated);
    } else if (const auto *run = std::get_if<ipc::WorkflowRunUpdatedEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::WorkflowRunUpdatedEvent>(actual.payload);
        check_string_equal(name, "run_id", decoded.run_id, run->run_id);
        check_string_equal(name, "workflow_id", decoded.workflow_id, run->workflow_id);
        check_string_equal(name, "state", decoded.state, run->state);
        MIRAGE_CHECK(decoded.run_epoch == run->run_epoch);
        MIRAGE_CHECK(decoded.summary.has_value() == run->summary.has_value());
        if (decoded.summary.has_value() && run->summary.has_value()) {
            check_string_equal(name, "summary", *decoded.summary, *run->summary);
        }
    } else if (const auto *chat = std::get_if<ipc::ChatTurnUpdatedEvent>(&expected.payload)) {
        const auto &decoded = std::get<ipc::ChatTurnUpdatedEvent>(actual.payload);
        check_string_equal(name, "chat session_id", decoded.session_id, chat->session_id);
        check_string_equal(name, "chat turn_id", decoded.turn_id, chat->turn_id);
        check_string_equal(name, "chat status", decoded.status, chat->status);
        check_string_equal(name, "chat user_text", decoded.user_text, chat->user_text);
        MIRAGE_CHECK(decoded.has_reply == chat->has_reply);
        if (decoded.has_reply && chat->has_reply) {
            check_string_equal(name, "chat reply_text", decoded.reply_text, chat->reply_text);
        }
        MIRAGE_CHECK(decoded.has_error == chat->has_error);
        if (decoded.has_error && chat->has_error) {
            check_string_equal(name, "chat error", decoded.error, chat->error);
        }
        MIRAGE_CHECK(decoded.sequence == chat->sequence);
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
    if (op == "session.list") {
        return ipc::ListSessionsRequest{};
    }
    if (op == "session.open") {
        return ipc::OpenSessionRequest{};
    }
    if (op == "session.close") {
        return ipc::CloseSessionRequest{vector_string(body, "session_id")};
    }
    if (op == "policy.get") {
        return ipc::GetPolicyRequest{};
    }
    if (op == "policy.set") {
        ipc::SetPolicyRequest set_policy;
        const auto *rules = vector_member(body, "rules").as_object();
        MIRAGE_CHECK(rules != nullptr);
        if (rules != nullptr) {
            for (const auto &[capability, value] : *rules) {
                const auto *text = value.as_string();
                MIRAGE_CHECK(text != nullptr);
                if (text != nullptr) {
                    set_policy.rules.insert_or_assign(capability, *text);
                }
            }
        }
        if (const auto *roots = body.find("read_roots"); roots != nullptr) {
            const auto *entries = roots->as_array();
            MIRAGE_CHECK(entries != nullptr);
            if (entries != nullptr) {
                for (const auto &entry : *entries) {
                    const auto *text = entry.as_string();
                    MIRAGE_CHECK(text != nullptr);
                    if (text != nullptr) {
                        set_policy.read_roots.push_back(*text);
                    }
                }
            }
            set_policy.has_read_roots = true;
        }
        return set_policy;
    }
    if (op == "session.chat") {
        ipc::SessionChatRequest chat;
        chat.session_id = vector_string(body, "session_id");
        chat.text = vector_string(body, "text");
        return chat;
    }
    if (op == "session.chat.history") {
        ipc::ChatHistoryRequest history;
        history.session_id = vector_string(body, "session_id");
        if (const auto *limit = body.find("limit"); limit != nullptr) {
            const auto limit_value = limit->as_integer();
            MIRAGE_CHECK(limit_value.has_value());
            if (limit_value) {
                history.limit = static_cast<int>(*limit_value);
            }
        }
        return history;
    }
    if (op == "workflow.list") {
        return ipc::WorkflowListRequest{};
    }
    if (op == "workflow.atom.catalog") {
        return ipc::WorkflowAtomCatalogRequest{};
    }
    if (op == "workflow.runs") {
        return ipc::WorkflowRunsRequest{};
    }
    if (op == "workflow.save" || op == "workflow.publish") {
        // The definition object is carried canonically serialized in the
        // pinned-free request struct; re-serializing the subtree is lossless
        // (mira JSON objects preserve insertion order).
        const auto *definition = body.find("definition");
        MIRAGE_CHECK(definition != nullptr);
        if (definition == nullptr) {
            return op == "workflow.save" ? ipc::Request{ipc::WorkflowSaveRequest{}}
                                         : ipc::Request{ipc::WorkflowPublishRequest{}};
        }
        const std::string canonical = mira::to_json_string(*definition);
        if (op == "workflow.save") {
            return ipc::WorkflowSaveRequest{canonical};
        }
        return ipc::WorkflowPublishRequest{canonical};
    }
    if (op == "workflow.delete") {
        return ipc::WorkflowDeleteRequest{vector_string(body, "workflow_id")};
    }
    if (op == "workflow.run") {
        ipc::WorkflowRunRequest run;
        run.workflow_id = vector_string(body, "workflow_id");
        if (const auto *digest = body.find("digest"); digest != nullptr) {
            const auto digest_value = digest->as_string();
            MIRAGE_CHECK(digest_value != nullptr);
            if (digest_value != nullptr) {
                run.digest = *digest_value;
            }
        }
        if (const auto *parameters = body.find("parameters"); parameters != nullptr) {
            run.parameters_json = mira::to_json_string(*parameters);
        }
        if (const auto *policy = body.find("policy"); policy != nullptr) {
            const auto policy_value = policy->as_string();
            MIRAGE_CHECK(policy_value != nullptr);
            if (policy_value != nullptr) {
                run.policy = *policy_value;
            }
        }
        return run;
    }
    if (op == "workflow.cancel") {
        return ipc::WorkflowCancelRunRequest{vector_string(body, "run_id")};
    }
    if (op == "workflow.get") {
        return ipc::WorkflowGetRequest{vector_string(body, "workflow_id")};
    }
    if (op == "desktop.observe") {
        ipc::DesktopObserveRequest observe;
        // The encoder always writes both flags; absent members on a vector
        // would mean the defaults, but the pinned canonical form carries
        // them explicitly.
        if (const auto *semantic = body.find("semantic"); semantic != nullptr) {
            const auto flag = semantic->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            observe.semantic = flag.value_or(true);
        }
        if (const auto *visual = body.find("visual"); visual != nullptr) {
            const auto flag = visual->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            observe.visual = flag.value_or(false);
        }
        return observe;
    }
    if (op == "session.history") {
        ipc::SessionHistoryRequest history;
        history.session_id = vector_string(body, "session_id");
        if (const auto *limit = body.find("limit"); limit != nullptr) {
            const auto limit_value = limit->as_integer();
            MIRAGE_CHECK(limit_value.has_value());
            if (limit_value) {
                history.limit = static_cast<int>(*limit_value);
            }
        }
        return history;
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
        if (const auto *session = body.find("session_id"); session != nullptr) {
            const auto session_value = session->as_string();
            MIRAGE_CHECK(session_value != nullptr);
            if (session_value != nullptr) {
                submit.session_id = *session_value;
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
                          std::is_same_v<T, ipc::UnsubscribeEventsRequest> ||
                          std::is_same_v<T, ipc::ListPermissionsRequest> ||
                          std::is_same_v<T, ipc::ListSessionsRequest> ||
                          std::is_same_v<T, ipc::OpenSessionRequest> ||
                          std::is_same_v<T, ipc::WorkflowListRequest> ||
                          std::is_same_v<T, ipc::WorkflowAtomCatalogRequest> ||
                          std::is_same_v<T, ipc::WorkflowRunsRequest>) {
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
                MIRAGE_CHECK(submit.session_id.has_value() ==
                             expected_value.session_id.has_value());
                if (submit.session_id.has_value() && expected_value.session_id.has_value()) {
                    check_string_equal(name, "submit session_id", *submit.session_id,
                                       *expected_value.session_id);
                }
            } else if constexpr (std::is_same_v<T, ipc::InspectTaskRequest> ||
                                 std::is_same_v<T, ipc::CancelTaskRequest>) {
                const auto &request = std::get<T>(actual);
                check_string_equal(name, "task_id", request.task_id, expected_value.task_id);
            } else if constexpr (std::is_same_v<T, ipc::CloseSessionRequest>) {
                const auto &close = std::get<ipc::CloseSessionRequest>(actual);
                check_string_equal(name, "session_id", close.session_id, expected_value.session_id);
            } else if constexpr (std::is_same_v<T, ipc::GetPolicyRequest>) {
                // Stateless body: the variant index comparison above suffices.
            } else if constexpr (std::is_same_v<T, ipc::SetPolicyRequest>) {
                const auto &set_policy = std::get<ipc::SetPolicyRequest>(actual);
                MIRAGE_CHECK(set_policy.rules.size() == expected_value.rules.size());
                for (const auto &[capability, rule] : expected_value.rules) {
                    const auto found = set_policy.rules.find(capability);
                    MIRAGE_CHECK(found != set_policy.rules.end());
                    if (found != set_policy.rules.end()) {
                        check_string_equal(name, "policy rule " + capability, found->second, rule);
                    }
                }
                MIRAGE_CHECK(set_policy.has_read_roots == expected_value.has_read_roots);
                MIRAGE_CHECK(set_policy.read_roots == expected_value.read_roots);
            } else if constexpr (std::is_same_v<T, ipc::SessionChatRequest>) {
                const auto &chat = std::get<ipc::SessionChatRequest>(actual);
                check_string_equal(name, "chat session_id", chat.session_id,
                                   expected_value.session_id);
                check_string_equal(name, "chat text", chat.text, expected_value.text);
            } else if constexpr (std::is_same_v<T, ipc::ChatHistoryRequest>) {
                const auto &history = std::get<ipc::ChatHistoryRequest>(actual);
                check_string_equal(name, "chat history session_id", history.session_id,
                                   expected_value.session_id);
                MIRAGE_CHECK(history.limit.has_value() == expected_value.limit.has_value());
                if (history.limit.has_value() && expected_value.limit.has_value()) {
                    MIRAGE_CHECK(*history.limit == *expected_value.limit);
                }
            } else if constexpr (std::is_same_v<T, ipc::RespondPermissionRequest>) {
                const auto &respond = std::get<ipc::RespondPermissionRequest>(actual);
                check_string_equal(name, "request_id", respond.request_id,
                                   expected_value.request_id);
                MIRAGE_CHECK(respond.approved == expected_value.approved);
            } else if constexpr (std::is_same_v<T, ipc::SessionHistoryRequest>) {
                const auto &history = std::get<ipc::SessionHistoryRequest>(actual);
                check_string_equal(name, "session_id", history.session_id,
                                   expected_value.session_id);
                MIRAGE_CHECK(history.limit.has_value() == expected_value.limit.has_value());
                if (history.limit.has_value() && expected_value.limit.has_value()) {
                    MIRAGE_CHECK(*history.limit == *expected_value.limit);
                }
            } else if constexpr (std::is_same_v<T, ipc::WorkflowSaveRequest> ||
                                 std::is_same_v<T, ipc::WorkflowPublishRequest>) {
                // The decoder canonicalizes the definition subtree; compare
                // the serialized forms byte-exactly.
                const auto &request = std::get<T>(actual);
                check_string_equal(name, "definition_json", request.definition_json,
                                   expected_value.definition_json);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowDeleteRequest>) {
                const auto &request = std::get<ipc::WorkflowDeleteRequest>(actual);
                check_string_equal(name, "workflow_id", request.workflow_id,
                                   expected_value.workflow_id);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowRunRequest>) {
                const auto &run = std::get<ipc::WorkflowRunRequest>(actual);
                check_string_equal(name, "workflow_id", run.workflow_id,
                                   expected_value.workflow_id);
                check_string_equal(name, "digest", run.digest, expected_value.digest);
                check_string_equal(name, "parameters_json", run.parameters_json,
                                   expected_value.parameters_json);
                check_string_equal(name, "policy", run.policy, expected_value.policy);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowCancelRunRequest>) {
                const auto &cancel = std::get<ipc::WorkflowCancelRunRequest>(actual);
                check_string_equal(name, "run_id", cancel.run_id, expected_value.run_id);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowGetRequest>) {
                const auto &get = std::get<ipc::WorkflowGetRequest>(actual);
                check_string_equal(name, "workflow_id", get.workflow_id,
                                   expected_value.workflow_id);
            } else if constexpr (std::is_same_v<T, ipc::DesktopObserveRequest>) {
                const auto &observe = std::get<ipc::DesktopObserveRequest>(actual);
                MIRAGE_CHECK(observe.semantic == expected_value.semantic);
                MIRAGE_CHECK(observe.visual == expected_value.visual);
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
        // DEC-021 session-face capability member: same optional discipline.
        if (const auto *sessions = value.find("sessions"); sessions != nullptr) {
            const auto flag = sessions->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            identity.sessions = flag;
        }
        // DEC-023 workflow-face capability member: same optional discipline.
        if (const auto *workflows = value.find("workflows"); workflows != nullptr) {
            const auto flag = workflows->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            identity.workflows = flag;
        }
        // DEC-026 observation-face capability member: same optional discipline.
        if (const auto *observation = value.find("observation"); observation != nullptr) {
            const auto flag = observation->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            identity.observation = flag;
        }
        // DEC-027 dialog-face capability member: same optional discipline.
        if (const auto *chat = value.find("chat"); chat != nullptr) {
            const auto flag = chat->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            identity.chat = flag;
        }
        // M5-07 policy-face capability member: same optional discipline.
        if (const auto *policy = value.find("policy"); policy != nullptr) {
            const auto flag = policy->as_boolean();
            MIRAGE_CHECK(flag.has_value());
            identity.policy = flag;
        }
        response.payload = std::move(identity);
    } else if (kind == "submitted") {
        ipc::TaskSubmitted submitted;
        submitted.task_id = vector_string(value, "task_id");
        if (const auto *session = value.find("session_id"); session != nullptr) {
            const auto session_value = session->as_string();
            MIRAGE_CHECK(session_value != nullptr);
            if (session_value != nullptr) {
                submitted.session_id = *session_value;
            }
        }
        response.payload = std::move(submitted);
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
    } else if (kind == "session-list") {
        ipc::SessionList list;
        const auto *entries = vector_member(value, "sessions").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                ipc::SessionSummary summary;
                summary.id = vector_string(entry, "id");
                summary.state = vector_string(entry, "state");
                summary.created_at_ms = vector_integer(entry, "created_at_ms");
                list.sessions.push_back(std::move(summary));
            }
        }
        response.payload = std::move(list);
    } else if (kind == "session-opened") {
        response.payload = ipc::SessionOpened{vector_string(value, "session_id")};
    } else if (kind == "session-closed") {
        response.payload =
            ipc::SessionClosed{vector_string(value, "session_id"), vector_string(value, "state")};
    } else if (kind == "policy-view") {
        ipc::PolicyView view;
        const auto *policy_rules = vector_member(value, "rules").as_object();
        MIRAGE_CHECK(policy_rules != nullptr);
        if (policy_rules != nullptr) {
            for (const auto &[capability, rule_value] : *policy_rules) {
                const auto *text = rule_value.as_string();
                MIRAGE_CHECK(text != nullptr);
                if (text != nullptr) {
                    view.rules.insert_or_assign(capability, *text);
                }
            }
        }
        const auto *roots = vector_member(value, "read_roots").as_array();
        MIRAGE_CHECK(roots != nullptr);
        if (roots != nullptr) {
            for (const auto &entry : *roots) {
                const auto *text = entry.as_string();
                MIRAGE_CHECK(text != nullptr);
                if (text != nullptr) {
                    view.read_roots.push_back(*text);
                }
            }
        }
        response.payload = std::move(view);
    } else if (kind == "session-chat-accepted") {
        response.payload = ipc::DialogTurnAccepted{vector_string(value, "turn_id")};
    } else if (kind == "session-chat-history") {
        ipc::DialogHistory history;
        history.session_id = vector_string(value, "session_id");
        history.truncated = vector_boolean(value, "truncated");
        const auto *turns = vector_member(value, "turns").as_array();
        MIRAGE_CHECK(turns != nullptr);
        if (turns != nullptr) {
            for (const auto &entry : *turns) {
                ipc::DialogTurnEntry turn;
                turn.turn_id = vector_string(entry, "turn_id");
                turn.status = vector_string(entry, "status");
                turn.user_text = vector_string(entry, "user_text");
                if (const auto *reply = entry.find("reply_text"); reply != nullptr) {
                    turn.reply_text = vector_string(entry, "reply_text");
                    turn.has_reply = true;
                }
                if (const auto *failure = entry.find("error"); failure != nullptr) {
                    turn.error = vector_string(entry, "error");
                    turn.has_error = true;
                }
                turn.sequence = static_cast<std::uint64_t>(vector_integer(entry, "sequence"));
                turn.recorded_at_ms = vector_integer(entry, "recorded_at_ms");
                history.turns.push_back(std::move(turn));
            }
        }
        response.payload = std::move(history);
    } else if (kind == "workflow-list") {
        ipc::WorkflowList list;
        const auto *entries = vector_member(value, "workflows").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                ipc::WorkflowSummary summary;
                summary.workflow_id = vector_string(entry, "workflow_id");
                summary.name = vector_string(entry, "name");
                summary.head_digest = vector_string(entry, "head_digest");
                summary.validation = vector_string(entry, "validation");
                summary.runnable = vector_boolean(entry, "runnable");
                summary.updated_at_ms = vector_integer(entry, "updated_at_ms");
                list.workflows.push_back(std::move(summary));
            }
        }
        response.payload = std::move(list);
    } else if (kind == "workflow-saved") {
        response.payload =
            ipc::WorkflowSaved{vector_string(value, "workflow_id"), vector_string(value, "digest")};
    } else if (kind == "workflow-published") {
        ipc::WorkflowPublished published;
        published.workflow_id = vector_string(value, "workflow_id");
        published.digest = vector_string(value, "digest");
        published.dry_run_id = vector_string(value, "dry_run_id");
        published.idempotent = vector_boolean(value, "idempotent");
        response.payload = std::move(published);
    } else if (kind == "workflow-deleted") {
        response.payload = ipc::WorkflowDeleted{vector_string(value, "workflow_id")};
    } else if (kind == "workflow-atom-catalog") {
        ipc::WorkflowAtomCatalog catalog;
        const auto *entries = vector_member(value, "tools").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                ipc::ExposedTool tool;
                tool.wire_name = vector_string(entry, "wire_name");
                tool.version = vector_string(entry, "version");
                tool.description = vector_string(entry, "description");
                tool.has_side_effects = vector_boolean(entry, "has_side_effects");
                tool.parameters_schema_json =
                    mira::to_json_string(vector_member(entry, "parameters_schema"));
                catalog.tools.push_back(std::move(tool));
            }
        }
        response.payload = std::move(catalog);
    } else if (kind == "workflow-run-list") {
        ipc::WorkflowRunList list;
        const auto *entries = vector_member(value, "runs").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                ipc::WorkflowRunSummary summary;
                summary.run_id = vector_string(entry, "run_id");
                summary.workflow_id = vector_string(entry, "workflow_id");
                summary.state = vector_string(entry, "state");
                summary.run_epoch = static_cast<std::uint64_t>(vector_integer(entry, "run_epoch"));
                summary.created_at_ms = vector_integer(entry, "created_at_ms");
                list.runs.push_back(std::move(summary));
            }
        }
        response.payload = std::move(list);
    } else if (kind == "workflow-run-started") {
        response.payload = ipc::WorkflowRunStarted{vector_string(value, "run_id")};
    } else if (kind == "workflow-run-cancelled") {
        response.payload = ipc::WorkflowRunCancelled{vector_string(value, "run_id"),
                                                     vector_string(value, "state")};
    } else if (kind == "workflow-get") {
        ipc::WorkflowDefinitionView view;
        view.workflow_id = vector_string(value, "workflow_id");
        view.digest = vector_string(value, "digest");
        view.definition_json = mira::to_json_string(vector_member(value, "definition"));
        response.payload = std::move(view);
    } else if (kind == "observation-view") {
        ipc::ObservationView view;
        view.active_application = vector_string(value, "active_application");
        view.active_window = vector_string(value, "active_window");
        const auto &geometry = vector_member(value, "window_geometry");
        view.window_geometry = {static_cast<std::int32_t>(vector_integer(geometry, "x")),
                                static_cast<std::int32_t>(vector_integer(geometry, "y")),
                                static_cast<std::int32_t>(vector_integer(geometry, "width")),
                                static_cast<std::int32_t>(vector_integer(geometry, "height"))};
        view.window_focused = vector_boolean(value, "window_focused");
        view.focused_element = vector_string(value, "focused_element");
        view.pointer_x = static_cast<std::int32_t>(vector_integer(value, "pointer_x"));
        view.pointer_y = static_cast<std::int32_t>(vector_integer(value, "pointer_y"));
        view.environment_state = vector_string(value, "environment_state");
        if (const auto *semantic = value.find("semantic"); semantic != nullptr) {
            ipc::ObservationSemantic projection;
            projection.application = vector_string(*semantic, "application");
            projection.window_title = vector_string(*semantic, "window_title");
            projection.truncated = vector_boolean(*semantic, "truncated");
            const auto *nodes = vector_member(*semantic, "nodes").as_array();
            MIRAGE_CHECK(nodes != nullptr);
            if (nodes != nullptr) {
                for (const auto &entry : *nodes) {
                    ipc::ObservationNode node;
                    node.ref = vector_string(entry, "ref");
                    node.role = vector_string(entry, "role");
                    node.name = vector_string(entry, "name");
                    node.description = vector_string(entry, "description");
                    node.parent = vector_integer(entry, "parent");
                    const auto &node_geometry = vector_member(entry, "geometry");
                    node.geometry = {
                        static_cast<std::int32_t>(vector_integer(node_geometry, "x")),
                        static_cast<std::int32_t>(vector_integer(node_geometry, "y")),
                        static_cast<std::int32_t>(vector_integer(node_geometry, "width")),
                        static_cast<std::int32_t>(vector_integer(node_geometry, "height"))};
                    node.focused = vector_boolean(entry, "focused");
                    node.enabled = vector_boolean(entry, "enabled");
                    projection.nodes.push_back(std::move(node));
                }
            }
            view.semantic = std::move(projection);
        }
        if (const auto *visual_ref = value.find("visual_snapshot_ref"); visual_ref != nullptr) {
            view.visual_snapshot_ref = vector_string(value, "visual_snapshot_ref");
            std::vector<ipc::ObservationRegion> regions;
            const auto *entries = vector_member(value, "visual_regions").as_array();
            MIRAGE_CHECK(entries != nullptr);
            if (entries != nullptr) {
                for (const auto &entry : *entries) {
                    ipc::ObservationRegion region;
                    region.ref = vector_string(entry, "ref");
                    region.source = vector_string(entry, "source");
                    const auto &region_geometry = vector_member(entry, "geometry");
                    region.geometry = {
                        static_cast<std::int32_t>(vector_integer(region_geometry, "x")),
                        static_cast<std::int32_t>(vector_integer(region_geometry, "y")),
                        static_cast<std::int32_t>(vector_integer(region_geometry, "width")),
                        static_cast<std::int32_t>(vector_integer(region_geometry, "height"))};
                    region.text = vector_string(entry, "text");
                    region.template_id = vector_string(entry, "template_id");
                    regions.push_back(std::move(region));
                }
            }
            view.visual_regions = std::move(regions);
        }
        response.payload = std::move(view);
    } else if (kind == "session-history") {
        ipc::SessionHistory history;
        history.session_id = vector_string(value, "session_id");
        history.truncated = vector_boolean(value, "truncated");
        const auto *entries = vector_member(value, "entries").as_array();
        MIRAGE_CHECK(entries != nullptr);
        if (entries != nullptr) {
            for (const auto &entry : *entries) {
                ipc::SessionHistoryEntry item;
                item.kind = vector_string(entry, "kind");
                item.text = vector_string(entry, "text");
                item.sequence = static_cast<std::uint64_t>(vector_integer(entry, "sequence"));
                item.recorded_at_ms = vector_integer(entry, "recorded_at_ms");
                history.entries.push_back(std::move(item));
            }
        }
        response.payload = std::move(history);
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
                MIRAGE_CHECK(actual_value.sessions.has_value() ==
                             expected_value.sessions.has_value());
                if (actual_value.sessions.has_value() && expected_value.sessions.has_value()) {
                    MIRAGE_CHECK(*actual_value.sessions == *expected_value.sessions);
                }
                MIRAGE_CHECK(actual_value.workflows.has_value() ==
                             expected_value.workflows.has_value());
                if (actual_value.workflows.has_value() && expected_value.workflows.has_value()) {
                    MIRAGE_CHECK(*actual_value.workflows == *expected_value.workflows);
                }
                MIRAGE_CHECK(actual_value.observation.has_value() ==
                             expected_value.observation.has_value());
                if (actual_value.observation.has_value() &&
                    expected_value.observation.has_value()) {
                    MIRAGE_CHECK(*actual_value.observation == *expected_value.observation);
                }
                MIRAGE_CHECK(actual_value.chat.has_value() == expected_value.chat.has_value());
                if (actual_value.chat.has_value() && expected_value.chat.has_value()) {
                    MIRAGE_CHECK(*actual_value.chat == *expected_value.chat);
                }
                MIRAGE_CHECK(actual_value.policy.has_value() == expected_value.policy.has_value());
                if (actual_value.policy.has_value() && expected_value.policy.has_value()) {
                    MIRAGE_CHECK(*actual_value.policy == *expected_value.policy);
                }
            } else if constexpr (std::is_same_v<T, ipc::TaskSubmitted>) {
                check_string_equal(name, "task_id", actual_value.task_id, expected_value.task_id);
                MIRAGE_CHECK(actual_value.session_id.has_value() ==
                             expected_value.session_id.has_value());
                if (actual_value.session_id.has_value() && expected_value.session_id.has_value()) {
                    check_string_equal(name, "submit session_id", *actual_value.session_id,
                                       *expected_value.session_id);
                }
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
            } else if constexpr (std::is_same_v<T, ipc::SessionList>) {
                MIRAGE_CHECK(actual_value.sessions.size() == expected_value.sessions.size());
                const std::size_t count =
                    std::min(actual_value.sessions.size(), expected_value.sessions.size());
                for (std::size_t index = 0; index < count; ++index) {
                    const auto &actual_entry = actual_value.sessions[index];
                    const auto &wanted = expected_value.sessions[index];
                    check_string_equal(name, "session id", actual_entry.id, wanted.id);
                    check_string_equal(name, "session state", actual_entry.state, wanted.state);
                    MIRAGE_CHECK(actual_entry.created_at_ms == wanted.created_at_ms);
                }
            } else if constexpr (std::is_same_v<T, ipc::SessionOpened>) {
                check_string_equal(name, "session_id", actual_value.session_id,
                                   expected_value.session_id);
            } else if constexpr (std::is_same_v<T, ipc::SessionClosed>) {
                check_string_equal(name, "session_id", actual_value.session_id,
                                   expected_value.session_id);
                check_string_equal(name, "session state", actual_value.state, expected_value.state);
            } else if constexpr (std::is_same_v<T, ipc::PolicyView>) {
                MIRAGE_CHECK(actual_value.rules.size() == expected_value.rules.size());
                for (const auto &[capability, rule] : expected_value.rules) {
                    const auto found = actual_value.rules.find(capability);
                    MIRAGE_CHECK(found != actual_value.rules.end());
                    if (found != actual_value.rules.end()) {
                        check_string_equal(name, "policy rule " + capability, found->second, rule);
                    }
                }
                MIRAGE_CHECK(actual_value.read_roots == expected_value.read_roots);
            } else if constexpr (std::is_same_v<T, ipc::DialogTurnAccepted>) {
                check_string_equal(name, "turn_id", actual_value.turn_id, expected_value.turn_id);
            } else if constexpr (std::is_same_v<T, ipc::DialogHistory>) {
                check_string_equal(name, "dialog session_id", actual_value.session_id,
                                   expected_value.session_id);
                MIRAGE_CHECK(actual_value.truncated == expected_value.truncated);
                MIRAGE_CHECK(actual_value.turns.size() == expected_value.turns.size());
                const std::size_t turn_count =
                    std::min(actual_value.turns.size(), expected_value.turns.size());
                for (std::size_t index = 0; index < turn_count; ++index) {
                    const auto &actual_turn = actual_value.turns[index];
                    const auto &wanted = expected_value.turns[index];
                    check_string_equal(name, "turn_id", actual_turn.turn_id, wanted.turn_id);
                    check_string_equal(name, "turn status", actual_turn.status, wanted.status);
                    check_string_equal(name, "turn user_text", actual_turn.user_text,
                                       wanted.user_text);
                    MIRAGE_CHECK(actual_turn.has_reply == wanted.has_reply);
                    if (actual_turn.has_reply && wanted.has_reply) {
                        check_string_equal(name, "turn reply_text", actual_turn.reply_text,
                                           wanted.reply_text);
                    }
                    MIRAGE_CHECK(actual_turn.has_error == wanted.has_error);
                    if (actual_turn.has_error && wanted.has_error) {
                        check_string_equal(name, "turn error", actual_turn.error, wanted.error);
                    }
                    MIRAGE_CHECK(actual_turn.sequence == wanted.sequence);
                    MIRAGE_CHECK(actual_turn.recorded_at_ms == wanted.recorded_at_ms);
                }
            } else if constexpr (std::is_same_v<T, ipc::SessionHistory>) {
                check_string_equal(name, "history session_id", actual_value.session_id,
                                   expected_value.session_id);
                MIRAGE_CHECK(actual_value.truncated == expected_value.truncated);
                MIRAGE_CHECK(actual_value.entries.size() == expected_value.entries.size());
                const std::size_t count =
                    std::min(actual_value.entries.size(), expected_value.entries.size());
                for (std::size_t index = 0; index < count; ++index) {
                    const auto &actual_entry = actual_value.entries[index];
                    const auto &wanted = expected_value.entries[index];
                    check_string_equal(name, "history kind", actual_entry.kind, wanted.kind);
                    check_string_equal(name, "history text", actual_entry.text, wanted.text);
                    MIRAGE_CHECK(actual_entry.sequence == wanted.sequence);
                    MIRAGE_CHECK(actual_entry.recorded_at_ms == wanted.recorded_at_ms);
                }
            } else if constexpr (std::is_same_v<T, ipc::WorkflowList>) {
                MIRAGE_CHECK(actual_value.workflows.size() == expected_value.workflows.size());
                const std::size_t count =
                    std::min(actual_value.workflows.size(), expected_value.workflows.size());
                for (std::size_t index = 0; index < count; ++index) {
                    const auto &actual_entry = actual_value.workflows[index];
                    const auto &wanted = expected_value.workflows[index];
                    check_string_equal(name, "workflow id", actual_entry.workflow_id,
                                       wanted.workflow_id);
                    check_string_equal(name, "workflow name", actual_entry.name, wanted.name);
                    check_string_equal(name, "workflow head_digest", actual_entry.head_digest,
                                       wanted.head_digest);
                    check_string_equal(name, "workflow validation", actual_entry.validation,
                                       wanted.validation);
                    MIRAGE_CHECK(actual_entry.runnable == wanted.runnable);
                    MIRAGE_CHECK(actual_entry.updated_at_ms == wanted.updated_at_ms);
                }
            } else if constexpr (std::is_same_v<T, ipc::WorkflowSaved>) {
                check_string_equal(name, "workflow_id", actual_value.workflow_id,
                                   expected_value.workflow_id);
                check_string_equal(name, "digest", actual_value.digest, expected_value.digest);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowPublished>) {
                check_string_equal(name, "workflow_id", actual_value.workflow_id,
                                   expected_value.workflow_id);
                check_string_equal(name, "digest", actual_value.digest, expected_value.digest);
                check_string_equal(name, "dry_run_id", actual_value.dry_run_id,
                                   expected_value.dry_run_id);
                MIRAGE_CHECK(actual_value.idempotent == expected_value.idempotent);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowDeleted>) {
                check_string_equal(name, "workflow_id", actual_value.workflow_id,
                                   expected_value.workflow_id);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowAtomCatalog>) {
                MIRAGE_CHECK(actual_value.tools.size() == expected_value.tools.size());
                const std::size_t count =
                    std::min(actual_value.tools.size(), expected_value.tools.size());
                for (std::size_t index = 0; index < count; ++index) {
                    const auto &actual_entry = actual_value.tools[index];
                    const auto &wanted = expected_value.tools[index];
                    check_string_equal(name, "tool wire_name", actual_entry.wire_name,
                                       wanted.wire_name);
                    check_string_equal(name, "tool version", actual_entry.version, wanted.version);
                    check_string_equal(name, "tool description", actual_entry.description,
                                       wanted.description);
                    MIRAGE_CHECK(actual_entry.has_side_effects == wanted.has_side_effects);
                    check_string_equal(name, "tool schema", actual_entry.parameters_schema_json,
                                       wanted.parameters_schema_json);
                }
            } else if constexpr (std::is_same_v<T, ipc::WorkflowRunList>) {
                MIRAGE_CHECK(actual_value.runs.size() == expected_value.runs.size());
                const std::size_t count =
                    std::min(actual_value.runs.size(), expected_value.runs.size());
                for (std::size_t index = 0; index < count; ++index) {
                    const auto &actual_entry = actual_value.runs[index];
                    const auto &wanted = expected_value.runs[index];
                    check_string_equal(name, "run id", actual_entry.run_id, wanted.run_id);
                    check_string_equal(name, "run workflow_id", actual_entry.workflow_id,
                                       wanted.workflow_id);
                    check_string_equal(name, "run state", actual_entry.state, wanted.state);
                    MIRAGE_CHECK(actual_entry.run_epoch == wanted.run_epoch);
                    MIRAGE_CHECK(actual_entry.created_at_ms == wanted.created_at_ms);
                }
            } else if constexpr (std::is_same_v<T, ipc::WorkflowRunStarted>) {
                check_string_equal(name, "run_id", actual_value.run_id, expected_value.run_id);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowRunCancelled>) {
                check_string_equal(name, "run_id", actual_value.run_id, expected_value.run_id);
                check_string_equal(name, "state", actual_value.state, expected_value.state);
            } else if constexpr (std::is_same_v<T, ipc::WorkflowDefinitionView>) {
                check_string_equal(name, "workflow_id", actual_value.workflow_id,
                                   expected_value.workflow_id);
                check_string_equal(name, "digest", actual_value.digest, expected_value.digest);
                check_string_equal(name, "definition_json", actual_value.definition_json,
                                   expected_value.definition_json);
            } else if constexpr (std::is_same_v<T, ipc::ObservationView>) {
                check_string_equal(name, "active_application", actual_value.active_application,
                                   expected_value.active_application);
                check_string_equal(name, "active_window", actual_value.active_window,
                                   expected_value.active_window);
                MIRAGE_CHECK(actual_value.window_geometry.x == expected_value.window_geometry.x);
                MIRAGE_CHECK(actual_value.window_geometry.y == expected_value.window_geometry.y);
                MIRAGE_CHECK(actual_value.window_geometry.width ==
                             expected_value.window_geometry.width);
                MIRAGE_CHECK(actual_value.window_geometry.height ==
                             expected_value.window_geometry.height);
                MIRAGE_CHECK(actual_value.window_focused == expected_value.window_focused);
                check_string_equal(name, "focused_element", actual_value.focused_element,
                                   expected_value.focused_element);
                MIRAGE_CHECK(actual_value.pointer_x == expected_value.pointer_x);
                MIRAGE_CHECK(actual_value.pointer_y == expected_value.pointer_y);
                check_string_equal(name, "environment_state", actual_value.environment_state,
                                   expected_value.environment_state);
                MIRAGE_CHECK(actual_value.semantic.has_value() ==
                             expected_value.semantic.has_value());
                if (actual_value.semantic.has_value() && expected_value.semantic.has_value()) {
                    const auto &actual_semantic = *actual_value.semantic;
                    const auto &wanted_semantic = *expected_value.semantic;
                    check_string_equal(name, "semantic application", actual_semantic.application,
                                       wanted_semantic.application);
                    check_string_equal(name, "semantic window_title", actual_semantic.window_title,
                                       wanted_semantic.window_title);
                    MIRAGE_CHECK(actual_semantic.truncated == wanted_semantic.truncated);
                    MIRAGE_CHECK(actual_semantic.nodes.size() == wanted_semantic.nodes.size());
                    const std::size_t node_count =
                        std::min(actual_semantic.nodes.size(), wanted_semantic.nodes.size());
                    for (std::size_t index = 0; index < node_count; ++index) {
                        const auto &actual_node = actual_semantic.nodes[index];
                        const auto &wanted_node = wanted_semantic.nodes[index];
                        check_string_equal(name, "node ref", actual_node.ref, wanted_node.ref);
                        check_string_equal(name, "node role", actual_node.role, wanted_node.role);
                        check_string_equal(name, "node name", actual_node.name, wanted_node.name);
                        check_string_equal(name, "node description", actual_node.description,
                                           wanted_node.description);
                        MIRAGE_CHECK(actual_node.parent == wanted_node.parent);
                        MIRAGE_CHECK(actual_node.geometry.x == wanted_node.geometry.x);
                        MIRAGE_CHECK(actual_node.geometry.y == wanted_node.geometry.y);
                        MIRAGE_CHECK(actual_node.geometry.width == wanted_node.geometry.width);
                        MIRAGE_CHECK(actual_node.geometry.height == wanted_node.geometry.height);
                        MIRAGE_CHECK(actual_node.focused == wanted_node.focused);
                        MIRAGE_CHECK(actual_node.enabled == wanted_node.enabled);
                    }
                }
                MIRAGE_CHECK(actual_value.visual_snapshot_ref.has_value() ==
                             expected_value.visual_snapshot_ref.has_value());
                MIRAGE_CHECK(actual_value.visual_regions.has_value() ==
                             expected_value.visual_regions.has_value());
                if (actual_value.visual_snapshot_ref.has_value() &&
                    expected_value.visual_snapshot_ref.has_value()) {
                    check_string_equal(name, "visual_snapshot_ref",
                                       *actual_value.visual_snapshot_ref,
                                       *expected_value.visual_snapshot_ref);
                }
                if (actual_value.visual_regions.has_value() &&
                    expected_value.visual_regions.has_value()) {
                    const auto &actual_regions = *actual_value.visual_regions;
                    const auto &wanted_regions = *expected_value.visual_regions;
                    MIRAGE_CHECK(actual_regions.size() == wanted_regions.size());
                    const std::size_t region_count =
                        std::min(actual_regions.size(), wanted_regions.size());
                    for (std::size_t index = 0; index < region_count; ++index) {
                        const auto &actual_region = actual_regions[index];
                        const auto &wanted_region = wanted_regions[index];
                        check_string_equal(name, "region ref", actual_region.ref,
                                           wanted_region.ref);
                        check_string_equal(name, "region source", actual_region.source,
                                           wanted_region.source);
                        MIRAGE_CHECK(actual_region.geometry.x == wanted_region.geometry.x);
                        MIRAGE_CHECK(actual_region.geometry.y == wanted_region.geometry.y);
                        MIRAGE_CHECK(actual_region.geometry.width == wanted_region.geometry.width);
                        MIRAGE_CHECK(actual_region.geometry.height ==
                                     wanted_region.geometry.height);
                        check_string_equal(name, "region text", actual_region.text,
                                           wanted_region.text);
                        check_string_equal(name, "region template_id", actual_region.template_id,
                                           wanted_region.template_id);
                    }
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
