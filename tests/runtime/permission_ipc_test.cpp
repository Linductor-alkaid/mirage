// M5-03 permission async confirmation surface verification (independent
// verification pass, DEC-020). Drives a real RuntimeService over its Local
// IPC surface: a subscribed connection receives the permission.request event
// for a Confirm-rule step, any connection can resolve it via
// permission.respond (first-response-wins), permission.list is the pending
// snapshot, the wait budget converges fail closed on silence, cancellation
// during the wait settles the task Cancelled with an unjudged permission
// field, and a service without the async surface answers `unavailable`.
//
// Harness discipline (AGENTS.md): the test never spawns threads. Subscriber
// connections are raw ipc::IpcStream + poll-driven frame helpers
// (tests/support/ipc_io.hpp) exactly like event_subscription_test; the
// service under test provides the concurrency.

#include "../support/ipc_io.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/permission/confirmation_hub.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace permission = mirage::runtime::permission;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::testing::FrameRead;

// Liveness guards against hangs, not latency assertions (see
// event_subscription_test for the load rationale behind the generous
// budgets); the quiet window proving "no further frame" stays tight.
constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr auto kTaskBudget = std::chrono::seconds{30};
constexpr auto kEventBudget = std::chrono::seconds{30};
constexpr auto kQuietWindow = std::chrono::milliseconds{700};
/// The hub's wait budget for "respond in time" scenarios: comfortably above
/// any scheduler lag a CI runner may add, far below the task budget.
constexpr auto kConfirmBudget = std::chrono::seconds{20};
/// The hub's wait budget for timeout scenarios: short, deterministic fail
/// closed, still above one scheduler slice.
constexpr auto kTimeoutBudget = std::chrono::milliseconds{400};

ServiceConfig make_config(const mirage::testing::TempDir &dir,
                          std::chrono::milliseconds confirm_budget = kConfirmBudget) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.5.0-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    config.recovery_directory = dir.root() / "recovery";
    // The scenario capability: filesystem.read gates behind confirmation;
    // process.execute stays allowed so reject scenarios can prove fail-fast
    // with a canary.
    config.permission_policy
        .rules[static_cast<std::size_t>(permission::Capability::FilesystemRead)] =
        permission::Rule::Confirm;
    if (confirm_budget > std::chrono::milliseconds::zero()) {
        config.confirmation_hub =
            std::make_shared<permission::AsyncConfirmationHub>(confirm_budget);
    }
    return config;
}

std::shared_ptr<integration::MiraEnvironmentBinding>
make_binding(const mirage::testing::TempDir &dir) {
    return std::make_shared<integration::MiraEnvironmentBinding>(
        std::make_shared<linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{dir.root()}));
}

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
}

std::optional<std::string> submit_task(ipc::IpcClient &client, ipc::SubmitTaskRequest request) {
    const ipc::Response response = client.call(request, kCallBudget);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    if (!response.ok || submitted == nullptr) {
        std::fprintf(stderr,
                     "[permission_ipc_test] task.submit failed: ok=%d code='%s' "
                     "message='%s'\n",
                     response.ok ? 1 : 0, response.error.code.c_str(),
                     response.error.message.c_str());
        return std::nullopt;
    }
    return submitted->task_id;
}

std::optional<ipc::InspectTask> wait_terminal(const std::string &socket_path,
                                              const std::string &task_id) {
    ipc::IpcClient client(socket_path);
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    for (;;) {
        const ipc::Response response = client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
        const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
        if (inspect != nullptr &&
            (inspect->progress == "Completed" || inspect->progress == "Failed" ||
             inspect->progress == "Cancelled")) {
            return *inspect;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

/// One permission.respond call over a dedicated one-shot connection (the
/// IpcClient shape). Returns the raw response for code assertions.
ipc::Response respond(const std::string &socket_path, const std::string &request_id,
                      bool approved) {
    ipc::IpcClient client(socket_path);
    return client.call(ipc::RespondPermissionRequest{request_id, approved}, kCallBudget);
}

ipc::Response list_pending(const std::string &socket_path) {
    ipc::IpcClient client(socket_path);
    return client.call(ipc::ListPermissionsRequest{}, kCallBudget);
}

// --- raw subscriber connection (shared shape with event_subscription_test) ---

struct Subscriber {
    ipc::IpcStream stream;
    std::string buffer;
    std::uint64_t next_id = 100;

    bool valid() const { return stream.valid(); }

    bool send(const ipc::Request &body) {
        return mirage::testing::write_all(
            stream, ipc::make_frame(ipc::encode_request(next_id++, body)), kCallBudget);
    }

    struct Incoming {
        enum class Kind { Event, Response, Closed, Timeout, ProtocolError };
        Kind kind = Kind::Closed;
        ipc::Event event;       ///< meaningful for Kind::Event
        ipc::Response response; ///< meaningful for Kind::Response
        std::string reason;     ///< meaningful for Kind::ProtocolError
    };

    Incoming next_frame(std::chrono::milliseconds budget) {
        Incoming incoming;
        const FrameRead read = mirage::testing::read_frame(stream, buffer, budget);
        switch (read.status) {
        case FrameRead::Status::Message:
            break;
        case FrameRead::Status::Timeout:
            incoming.kind = Incoming::Kind::Timeout;
            return incoming;
        case FrameRead::Status::Closed:
            incoming.kind = Incoming::Kind::Closed;
            return incoming;
        case FrameRead::Status::ProtocolError:
            incoming.kind = Incoming::Kind::ProtocolError;
            incoming.reason = read.reason;
            return incoming;
        }
        if (const ipc::EventDecode event = ipc::decode_event(read.message); event.ok) {
            incoming.kind = Incoming::Kind::Event;
            incoming.event = event.event;
            return incoming;
        }
        const ipc::ResponseDecode response = ipc::decode_response(read.message);
        if (response.ok) {
            incoming.kind = Incoming::Kind::Response;
            incoming.response = response.response;
            return incoming;
        }
        incoming.kind = Incoming::Kind::ProtocolError;
        incoming.reason = "frame is neither a decodable event nor a decodable response";
        return incoming;
    }
};

using EventLog = std::vector<ipc::Event>;

std::optional<ipc::Response> read_response(Subscriber &subscriber, EventLog &log,
                                           std::uint64_t correlation_id,
                                           std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            return std::nullopt;
        }
        Subscriber::Incoming incoming = subscriber.next_frame(remaining);
        switch (incoming.kind) {
        case Subscriber::Incoming::Kind::Event:
            log.push_back(incoming.event);
            break;
        case Subscriber::Incoming::Kind::Response:
            if (incoming.response.id == correlation_id) {
                return incoming.response;
            }
            MIRAGE_CHECK(false); // one outstanding request per connection
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }
}

template <typename Predicate>
std::optional<ipc::Event> wait_for_event(Subscriber &subscriber, EventLog &log, Predicate predicate,
                                         std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            return std::nullopt;
        }
        Subscriber::Incoming incoming = subscriber.next_frame(remaining);
        switch (incoming.kind) {
        case Subscriber::Incoming::Kind::Event:
            log.push_back(incoming.event);
            if (predicate(incoming.event)) {
                return incoming.event;
            }
            break;
        default:
            return std::nullopt;
        }
    }
}

bool subscribe_and_consume_ack(Subscriber &subscriber, EventLog &log) {
    const std::uint64_t id = subscriber.next_id;
    if (!subscriber.send(ipc::SubscribeEventsRequest{})) {
        return false;
    }
    const auto response = read_response(subscriber, log, id, kCallBudget);
    return response.has_value() && response->ok &&
           std::holds_alternative<ipc::ShutdownAccepted>(response->payload);
}

/// Waits for the permission.request event of `task_id` and returns it.
std::optional<ipc::PermissionRequestedEvent>
wait_permission_request(Subscriber &subscriber, EventLog &log, const std::string &task_id) {
    const auto event = wait_for_event(
        subscriber, log,
        [&](const ipc::Event &candidate) {
            const auto *request = std::get_if<ipc::PermissionRequestedEvent>(&candidate.payload);
            return request != nullptr && request->task_id == task_id;
        },
        kEventBudget);
    if (!event) {
        return std::nullopt;
    }
    return std::get<ipc::PermissionRequestedEvent>(event->payload);
}

// --- scenarios ---------------------------------------------------------------

void scenario_hello_advertises_permissions_capability() {
    // Hub-equipped service: the member is engaged and true.
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);
    ipc::IpcClient client(config.socket_path);
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(hello.ok && identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->events.value_or(false));
        MIRAGE_CHECK(identity->permissions.has_value() && *identity->permissions);
    }
    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_request_event_and_approval_completes_step() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "approved read " + token + "\n";
    write_text_file(goal_file, content);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read a file the policy gates behind confirmation";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    // The raised request carries the full approval context (DEC-020 wire
    // shape): stable id, capability, the action resource, the task and a
    // live wait budget.
    const auto raised = wait_permission_request(subscriber, log, *task_id);
    MIRAGE_CHECK(raised.has_value());
    if (!raised) {
        return;
    }
    MIRAGE_CHECK(raised->request_id.rfind("perm-", 0) == 0);
    MIRAGE_CHECK(raised->capability == "filesystem.read");
    MIRAGE_CHECK(raised->resource == goal_file.string());
    MIRAGE_CHECK(raised->timeout_ms >= 1 && raised->timeout_ms <= kConfirmBudget.count());

    // The pending snapshot already reports the request before the answer.
    const ipc::Response listed_before = list_pending(config.socket_path);
    const auto *pending_before = std::get_if<ipc::PermissionPendingList>(&listed_before.payload);
    MIRAGE_CHECK(listed_before.ok && pending_before != nullptr);
    if (pending_before != nullptr) {
        MIRAGE_CHECK(pending_before->pending.size() == 1);
        if (pending_before->pending.size() == 1) {
            MIRAGE_CHECK(pending_before->pending[0].request_id == raised->request_id);
            MIRAGE_CHECK(pending_before->pending[0].capability == "filesystem.read");
            MIRAGE_CHECK(pending_before->pending[0].task_id == *task_id);
            MIRAGE_CHECK(pending_before->pending[0].timeout_ms >= 1);
        }
    }

    // Approval from a separate connection (first-response-wins winner).
    const ipc::Response approval = respond(config.socket_path, raised->request_id, true);
    MIRAGE_CHECK(approval.ok);
    const auto *responded = std::get_if<ipc::PermissionResponded>(&approval.payload);
    MIRAGE_CHECK(responded != nullptr && responded->request_id == raised->request_id);

    // The step ran with the confirmed decision and a real operation id.
    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done) {
        MIRAGE_CHECK(done->progress == "Completed");
        MIRAGE_CHECK(done->has_success && done->success);
        MIRAGE_CHECK(done->steps.size() == 1);
        if (done->steps.size() == 1) {
            MIRAGE_CHECK(done->steps[0].permission == "confirmed");
            MIRAGE_CHECK(done->steps[0].status == "ok");
            MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(done->steps[0].operation_id));
            MIRAGE_CHECK(done->steps[0].result == content);
        }
    }

    // The decided request left the pending set.
    const ipc::Response listed_after = list_pending(config.socket_path);
    const auto *pending_after = std::get_if<ipc::PermissionPendingList>(&listed_after.payload);
    MIRAGE_CHECK(listed_after.ok && pending_after != nullptr && pending_after->pending.empty());

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_rejection_fails_closed_before_side_effect() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    write_text_file(goal_file, "unreachable content\n");
    const std::filesystem::path canary = dir.root() / ("canary-" + token + ".txt");

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read a rejected file then touch a canary";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch " + canary.string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const auto raised = wait_permission_request(subscriber, log, *task_id);
    MIRAGE_CHECK(raised.has_value());
    if (!raised) {
        return;
    }
    const ipc::Response rejection = respond(config.socket_path, raised->request_id, false);
    MIRAGE_CHECK(rejection.ok);

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done) {
        MIRAGE_CHECK(done->progress == "Failed");
        MIRAGE_CHECK(done->has_success && !done->success);
        MIRAGE_CHECK(done->steps.size() == 2);
        if (done->steps.size() == 2) {
            const ipc::StepView &step = done->steps[0];
            MIRAGE_CHECK(step.status == "failed");
            MIRAGE_CHECK(!step.ok);
            MIRAGE_CHECK(step.permission == "confirmation_rejected");
            MIRAGE_CHECK(step.operation_id.empty());
            MIRAGE_CHECK(step.error.rfind("permission_denied: ", 0) == 0);
            MIRAGE_CHECK(step.error.find("confirmation rejected for filesystem.read") !=
                         std::string::npos);
            // Fail-fast: the follower never ran, and no desktop side effect
            // exists anywhere.
            MIRAGE_CHECK(done->steps[1].status == "skipped");
        }
    }
    MIRAGE_CHECK(!std::filesystem::exists(canary));

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_timeout_converges_fail_closed() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir, kTimeoutBudget);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    write_text_file(goal_file, "nobody approves this\n");

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "read a file nobody approves";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    // No responder exists; the budget converges the confirmation fail
    // closed (DEC-020: silence never approves).
    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done) {
        MIRAGE_CHECK(done->progress == "Failed");
        MIRAGE_CHECK(done->steps.size() == 1);
        if (done->steps.size() == 1) {
            MIRAGE_CHECK(done->steps[0].permission == "confirmation_rejected");
            MIRAGE_CHECK(done->steps[0].operation_id.empty());
            MIRAGE_CHECK(done->steps[0].error.rfind("permission_denied: ", 0) == 0);
            MIRAGE_CHECK(done->steps[0].error.find("confirmation timed out for filesystem.read") !=
                         std::string::npos);
        }
    }

    // The expired request left the pending set (and late replies lose).
    const ipc::Response listed = list_pending(config.socket_path);
    const auto *pending = std::get_if<ipc::PermissionPendingList>(&listed.payload);
    MIRAGE_CHECK(listed.ok && pending != nullptr && pending->pending.empty());
    const ipc::Response late = respond(config.socket_path, "perm-1", true);
    MIRAGE_CHECK(!late.ok);
    MIRAGE_CHECK(late.error.code == "not_found");
    MIRAGE_CHECK(late.error.message == "unknown or already decided permission request id");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_pending_snapshot_is_the_resync_face() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    write_text_file(goal_file, "snapshot check\n");

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "list the pending confirmation while it waits";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    const auto raised = wait_permission_request(subscriber, log, *task_id);
    MIRAGE_CHECK(raised.has_value());
    if (!raised) {
        return;
    }

    // The list works on a connection that never subscribed (any client may
    // resync) and reports the request with a positive remaining budget.
    const ipc::Response listed = list_pending(config.socket_path);
    const auto *pending = std::get_if<ipc::PermissionPendingList>(&listed.payload);
    MIRAGE_CHECK(listed.ok && pending != nullptr);
    if (pending) {
        MIRAGE_CHECK(pending->pending.size() == 1);
        if (pending->pending.size() == 1) {
            MIRAGE_CHECK(pending->pending[0].request_id == raised->request_id);
            MIRAGE_CHECK(pending->pending[0].resource == goal_file.string());
            MIRAGE_CHECK(pending->pending[0].timeout_ms >= 1 &&
                         pending->pending[0].timeout_ms <= kConfirmBudget.count());
        }
    }

    // Approving through the list-derived id completes the round trip.
    const ipc::Response approval =
        respond(config.socket_path, pending->pending[0].request_id, true);
    MIRAGE_CHECK(approval.ok);
    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_first_response_wins_second_loses() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "double approval race " + token + "\n";
    write_text_file(goal_file, content);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "two connections answer the same request";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    const auto raised = wait_permission_request(subscriber, log, *task_id);
    MIRAGE_CHECK(raised.has_value());
    if (!raised) {
        return;
    }

    // Both connections answer the same request; the first is the winner,
    // the second is a stable not_found (DEC-020 decision 4).
    const ipc::Response winner = respond(config.socket_path, raised->request_id, true);
    const ipc::Response loser = respond(config.socket_path, raised->request_id, false);
    MIRAGE_CHECK(winner.ok);
    MIRAGE_CHECK(!loser.ok);
    MIRAGE_CHECK(loser.error.code == "not_found");
    MIRAGE_CHECK(loser.error.message == "unknown or already decided permission request id");

    // The winner's answer stands: the step completes.
    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done) {
        MIRAGE_CHECK(done->progress == "Completed");
        if (done->steps.size() == 1) {
            MIRAGE_CHECK(done->steps[0].permission == "confirmed");
            MIRAGE_CHECK(done->steps[0].result == content);
        }
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_cancel_during_confirmation_wait() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    write_text_file(goal_file, "cancelled before anyone answers\n");
    const std::filesystem::path canary = dir.root() / ("canary-" + token + ".txt");

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "cancel the task while its confirmation waits";
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch " + canary.string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    const auto raised = wait_permission_request(subscriber, log, *task_id);
    MIRAGE_CHECK(raised.has_value());
    if (!raised) {
        return;
    }

    // Cancelling mid-wait must interrupt the confirmation, not execute it:
    // the interrupted step is cancelled with an unjudged permission field
    // (the gate never concluded, DEC-020 decision 5), the follower is
    // skipped and no side effect exists.
    ipc::IpcClient canceller(config.socket_path);
    const ipc::Response cancelled = canceller.call(ipc::CancelTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(cancelled.ok);

    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (done) {
        MIRAGE_CHECK(done->progress == "Cancelled");
        MIRAGE_CHECK(done->steps.size() == 2);
        if (done->steps.size() == 2) {
            MIRAGE_CHECK(done->steps[0].status == "cancelled");
            MIRAGE_CHECK(done->steps[0].permission.empty());
            MIRAGE_CHECK(done->steps[0].operation_id.empty());
            MIRAGE_CHECK(done->steps[1].status == "skipped");
        }
    }
    MIRAGE_CHECK(!std::filesystem::exists(canary));

    // The retracted request left the pending set; answering it now loses.
    const ipc::Response listed = list_pending(config.socket_path);
    const auto *pending = std::get_if<ipc::PermissionPendingList>(&listed.payload);
    MIRAGE_CHECK(listed.ok && pending != nullptr && pending->pending.empty());
    const ipc::Response late = respond(config.socket_path, raised->request_id, true);
    MIRAGE_CHECK(!late.ok && late.error.code == "not_found");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_surface_without_hub_is_unavailable() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir, std::chrono::milliseconds::zero());
    // No hub, no legacy hook: Confirm rules fall back to the fail-closed
    // DenyAll hook (DEC-010) and the permission.* face is `unavailable`.
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(hello.ok && identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->permissions.has_value() && !*identity->permissions);
    }

    const ipc::Response listed = list_pending(config.socket_path);
    MIRAGE_CHECK(!listed.ok);
    MIRAGE_CHECK(listed.error.code == "unavailable");
    MIRAGE_CHECK(listed.error.message == "permission confirmation surface is not active");

    const ipc::Response answered = respond(config.socket_path, "perm-1", true);
    MIRAGE_CHECK(!answered.ok);
    MIRAGE_CHECK(answered.error.code == "unavailable");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[permission_ipc_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("hello_advertises_permissions_capability",
                 scenario_hello_advertises_permissions_capability);
    run_scenario("request_event_and_approval_completes_step",
                 scenario_request_event_and_approval_completes_step);
    run_scenario("rejection_fails_closed_before_side_effect",
                 scenario_rejection_fails_closed_before_side_effect);
    run_scenario("timeout_converges_fail_closed", scenario_timeout_converges_fail_closed);
    run_scenario("pending_snapshot_is_the_resync_face",
                 scenario_pending_snapshot_is_the_resync_face);
    run_scenario("first_response_wins_second_loses", scenario_first_response_wins_second_loses);
    run_scenario("cancel_during_confirmation_wait", scenario_cancel_during_confirmation_wait);
    run_scenario("surface_without_hub_is_unavailable", scenario_surface_without_hub_is_unavailable);
    return mirage::testing::finish("permission_ipc_test");
}
