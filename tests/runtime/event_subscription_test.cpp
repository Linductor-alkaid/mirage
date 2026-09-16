// M1.5-02 service-level event subscription tests (DEC-012 decisions 2-5).
// Drives the real RuntimeService over its local IPC socket: a subscribed
// connection receives the seed host.status event, the full task.updated
// sequence of submitted tasks and the events.overflow marker on a tiny
// per-connection queue, while unsubscribed connections never see an event
// frame. Wire-level golden bytes are locked by ipc_protocol_golden_test;
// this file locks the service behavior on the live transport.
//
// Harness discipline (AGENTS.md): the test never spawns threads. The
// subscriber connections are raw ipc::IpcStream + poll-driven frame helpers
// (tests/support/ipc_io.hpp) because ipc::IpcClient decodes every inbound
// frame as a response and would misread event frames; frame classification
// is by discriminating member (an "event" member decodes as an event, an
// "ok" member as a response — the two decode paths are mutually exclusive).

#include "../support/ipc_io.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/framing.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/stream.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::testing::FrameRead;

constexpr auto kCallBudget = std::chrono::seconds{5};
constexpr auto kTaskBudget = std::chrono::seconds{10};
/// Total wait for a specific event to appear on a subscriber.
constexpr auto kEventBudget = std::chrono::seconds{10};
/// Quiet window proving no event frame arrives (server poll pass is 200 ms).
constexpr auto kQuietWindow = std::chrono::milliseconds{700};

ServiceConfig make_config(const mirage::testing::TempDir &dir) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.4.0-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    config.recovery_directory = dir.root() / "recovery";
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

std::optional<std::string> submit_task(ipc::IpcClient &client, ipc::SubmitTaskRequest request,
                                       std::chrono::milliseconds budget = kCallBudget) {
    const ipc::Response response = client.call(request, budget);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    if (!response.ok || submitted == nullptr) {
        std::fprintf(stderr, "[event_subscription_test] task.submit failed: ok=%d code='%s' "
                             "message='%s'\n",
                     response.ok ? 1 : 0, response.error.code.c_str(),
                     response.error.message.c_str());
        return std::nullopt;
    }
    return submitted->task_id;
}

/// Polls task.inspect on a dedicated (unsubscribed) connection until the
/// task reports a terminal progress state.
std::optional<ipc::InspectTask> wait_terminal(const std::string &socket_path,
                                              const std::string &task_id) {
    ipc::IpcClient client(socket_path);
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    for (;;) {
        const ipc::Response response = client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
        const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
        if (inspect != nullptr && (inspect->progress == "Completed" ||
                                   inspect->progress == "Failed" ||
                                   inspect->progress == "Cancelled")) {
            return *inspect;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

// --- raw subscriber connection ----------------------------------------------

/// One raw connection to the service with per-connection frame decoding:
/// events go through decode_event, responses through decode_response (an
/// event frame never carries "id"/"ok" and a response frame never carries
/// "seq", so the classification is exact).
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

    void close() { stream.close(); }
};

/// Every event frame read from one subscriber, in arrival order.
using EventLog = std::vector<ipc::Event>;

/// Reads frames until a response with `correlation_id` arrives; every event
/// frame seen on the way is appended to `log`. Nullopt on timeout/close.
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
            MIRAGE_CHECK(false); // responses cannot interleave: one outstanding request
            return std::nullopt;
        default:
            return std::nullopt;
        }
    }
}

/// Reads frames until `predicate` accepts an event; everything read on the
/// way lands in `log`. Nullopt on timeout/close/protocol error.
template <typename Predicate>
std::optional<ipc::Event> wait_for_event(Subscriber &subscriber, EventLog &log,
                                         Predicate predicate, std::chrono::milliseconds budget) {
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

/// True when the connection stays silent for the whole window (no frame of
/// any kind, so the socket would stay readable if the server pushed one).
bool is_silent(Subscriber &subscriber, std::chrono::milliseconds window) {
    return subscriber.next_frame(window).kind == Subscriber::Incoming::Kind::Timeout;
}

/// Subscribes the raw connection and consumes the acknowledgement. Returns
/// false on any transport or protocol deviation. The seed event (if any) is
/// left unread on the wire for the caller to inspect.
bool subscribe_and_consume_ack(Subscriber &subscriber, EventLog &log) {
    const std::uint64_t id = subscriber.next_id;
    if (!subscriber.send(ipc::SubscribeEventsRequest{})) {
        return false;
    }
    const auto response = read_response(subscriber, log, id, kCallBudget);
    if (!response || !response->ok ||
        !std::holds_alternative<ipc::ShutdownAccepted>(response->payload)) {
        return false;
    }
    return true;
}

/// Seq values are assigned by the service at write-out and advance by
/// exactly one per frame on a connection.
void check_seq_relative(const char *scenario, const EventLog &log) {
    for (std::size_t index = 1; index < log.size(); ++index) {
        if (log[index].seq != log[index - 1].seq + 1) {
            std::fprintf(stderr, "[%s] event %zu has seq %llu, expected %llu\n", scenario, index,
                         static_cast<unsigned long long>(log[index].seq),
                         static_cast<unsigned long long>(log[index - 1].seq + 1));
        }
        MIRAGE_CHECK(log[index].seq == log[index - 1].seq + 1);
    }
}

/// Relative contiguity plus the expected first seq: 2 when the seq-1 seed
/// was consumed before the log started, higher when drops swallowed the
/// head of the stream.
void check_seq_from(const char *scenario, const EventLog &log, std::uint64_t first_seq) {
    if (!log.empty()) {
        if (log.front().seq != first_seq) {
            std::fprintf(stderr, "[%s] first event has seq %llu, expected %llu\n", scenario,
                         static_cast<unsigned long long>(log.front().seq),
                         static_cast<unsigned long long>(first_seq));
        }
        MIRAGE_CHECK(log.front().seq == first_seq);
    }
    check_seq_relative(scenario, log);
}

/// All task.updated events of one task must share its identity and carry a
/// progress projection consistent with task.inspect; the terminal snapshot
/// must be the last one and mirror the pinned settlement.
void check_task_stream(const char *scenario, const EventLog &log, const std::string &task_id,
                       const std::string &goal, const char *expected_terminal) {
    std::size_t updates = 0;
    for (const ipc::Event &event : log) {
        const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
        if (task == nullptr || task->task_id != task_id) {
            continue;
        }
        ++updates;
        MIRAGE_CHECK(task->goal == goal);
        MIRAGE_CHECK(!task->progress.empty());
        if (task->progress == expected_terminal) {
            MIRAGE_CHECK(task->has_success);
            MIRAGE_CHECK(task->success == (std::string(expected_terminal) == "Completed"));
        }
    }
    MIRAGE_CHECK(updates >= 2); // creation + terminal, at minimum
    if (updates < 2) {
        std::fprintf(stderr, "[%s] only %zu task.updated snapshots for the task\n", scenario,
                     updates);
        return;
    }
    const auto *last = std::get_if<ipc::TaskUpdatedEvent>(&log.back().payload);
    MIRAGE_CHECK(last != nullptr);
    if (last != nullptr) {
        MIRAGE_CHECK(last->task_id == task_id);
        MIRAGE_CHECK(last->progress == expected_terminal);
    }
}

// --- scenarios ---------------------------------------------------------------

/// (a) hello advertises the DEC-012 event capability.
void scenario_hello_advertises_events_capability() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    // Client-shaped path: the decoded identity carries events == true.
    ipc::IpcClient client(config.socket_path);
    const ipc::Response response = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(response.ok);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&response.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->events.has_value());
        MIRAGE_CHECK(identity->events.value_or(false));
    }

    // Raw path: the hello-capability wire form carries the member.
    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    const std::uint64_t id = subscriber.next_id;
    MIRAGE_CHECK(subscriber.send(ipc::HelloRequest{}));
    const auto hello = read_response(subscriber, log, id, kCallBudget);
    MIRAGE_CHECK(hello.has_value() && hello->ok);
    if (hello && hello->ok) {
        const auto *raw_identity = std::get_if<ipc::ServiceIdentity>(&hello->payload);
        MIRAGE_CHECK(raw_identity != nullptr);
        if (raw_identity != nullptr) {
            MIRAGE_CHECK(raw_identity->events.has_value());
            MIRAGE_CHECK(raw_identity->events.value_or(false));
        }
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (b) events.subscribe / events.unsubscribe round-trip; unsubscribe is
/// idempotent.
void scenario_subscribe_unsubscribe_round_trip() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;

    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    // The subscription seed follows the ack; consume it so the second
    // unsubscribe is not confounded by it.
    const auto seed = subscriber.next_frame(kCallBudget);
    MIRAGE_CHECK(seed.kind == Subscriber::Incoming::Kind::Event);
    if (seed.kind != Subscriber::Incoming::Kind::Event) {
        return;
    }
    MIRAGE_CHECK(std::holds_alternative<ipc::HostStatusEvent>(seed.event.payload));

    // Unsubscribe twice: both attempts acknowledge with the empty ok shape.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const std::uint64_t id = subscriber.next_id;
        MIRAGE_CHECK(subscriber.send(ipc::UnsubscribeEventsRequest{}));
        const auto ack = read_response(subscriber, log, id, kCallBudget);
        MIRAGE_CHECK(ack.has_value());
        if (ack) {
            MIRAGE_CHECK(ack->ok);
            MIRAGE_CHECK(std::holds_alternative<ipc::ShutdownAccepted>(ack->payload));
        }
    }
    // Nothing further is streamed to the detached connection.
    MIRAGE_CHECK(is_silent(subscriber, kQuietWindow));

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (c) the subscribe seed is the current host status and becomes seq 1.
void scenario_subscribe_seed_is_current_host_status() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));

    const auto seed = subscriber.next_frame(kCallBudget);
    MIRAGE_CHECK(seed.kind == Subscriber::Incoming::Kind::Event);
    if (seed.kind != Subscriber::Incoming::Kind::Event) {
        return;
    }
    MIRAGE_CHECK(seed.event.seq == 1);
    const auto *status = std::get_if<ipc::HostStatusEvent>(&seed.event.payload);
    MIRAGE_CHECK(status != nullptr);
    if (status != nullptr) {
        // Connections can only exist once the listener is up, so the live
        // state every subscriber can observe is "running".
        MIRAGE_CHECK(status->status == "running");
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (d) the full task.updated sequence of a multi-step task, with strictly
/// contiguous per-connection seq numbers starting at the seed (seq 1).
void scenario_task_updated_full_sequence() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    const auto seed = subscriber.next_frame(kCallBudget);
    MIRAGE_CHECK(seed.kind == Subscriber::Incoming::Kind::Event);
    if (seed.kind != Subscriber::Incoming::Kind::Event) {
        return;
    }

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path file = dir.root() / ("seq-" + token + ".txt");
    const std::string content = "event sequence fixture " + token;
    write_text_file(file, content);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "watch this task advance through three reads";
    for (int index = 0; index < 3; ++index) {
        request.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});
    }
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const auto terminal = wait_for_event(
        subscriber, log,
        [&](const ipc::Event &event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            return task != nullptr && task->task_id == *task_id &&
                   (task->progress == "Completed" || task->progress == "Failed" ||
                    task->progress == "Cancelled");
        },
        kEventBudget);
    MIRAGE_CHECK(terminal.has_value());
    if (!terminal) {
        return;
    }
    const auto *done = std::get_if<ipc::TaskUpdatedEvent>(&terminal->payload);
    MIRAGE_CHECK(done != nullptr);
    if (done != nullptr) {
        MIRAGE_CHECK(done->progress == "Completed");
        MIRAGE_CHECK(done->has_success);
        MIRAGE_CHECK(done->success);
        MIRAGE_CHECK(done->goal == request.goal);
    }

    // Creation snapshot: the first task.updated for this id, before any
    // step advance, still pre-terminal.
    std::optional<ipc::TaskUpdatedEvent> creation;
    for (const ipc::Event &event : log) {
        const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
        if (task != nullptr && task->task_id == *task_id) {
            creation = *task;
            break;
        }
    }
    MIRAGE_CHECK(creation.has_value());
    if (creation) {
        MIRAGE_CHECK(creation->goal == request.goal);
        MIRAGE_CHECK(creation->progress == "Idle" || creation->progress == "Active");
        MIRAGE_CHECK(!creation->has_success);
    }

    // Every snapshot between creation and terminal is a non-terminal advance.
    for (const ipc::Event &event : log) {
        const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
        if (task == nullptr || task->task_id != *task_id) {
            continue;
        }
        const bool terminal_progress = task->progress == "Completed" ||
                                       task->progress == "Failed" ||
                                       task->progress == "Cancelled";
        if (!terminal_progress) {
            MIRAGE_CHECK(task->progress == "Idle" || task->progress == "Active");
            MIRAGE_CHECK(!task->has_success);
        }
    }

    check_seq_from("task_updated_full_sequence", log, 2);
    check_task_stream("task_updated_full_sequence", log, *task_id, request.goal, "Completed");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// Failure path: a failed task publishes a terminal task.updated snapshot
/// with success == false.
void scenario_task_failed_terminal_event() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    if (subscriber.next_frame(kCallBudget).kind != Subscriber::Incoming::Kind::Event) {
        MIRAGE_CHECK(false); // expected the seed event
        return;
    }

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "fail on a missing file";
    request.steps.push_back(
        {ipc::StepKind::FilesystemRead, (dir.root() / "definitely-missing.txt").string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const auto terminal = wait_for_event(
        subscriber, log,
        [&](const ipc::Event &event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            return task != nullptr && task->task_id == *task_id &&
                   (task->progress == "Completed" || task->progress == "Failed" ||
                    task->progress == "Cancelled");
        },
        kEventBudget);
    MIRAGE_CHECK(terminal.has_value());
    if (terminal) {
        const auto *done = std::get_if<ipc::TaskUpdatedEvent>(&terminal->payload);
        MIRAGE_CHECK(done != nullptr && done->progress == "Failed");
        if (done != nullptr) {
            MIRAGE_CHECK(done->progress == "Failed");
            MIRAGE_CHECK(done->has_success);
            MIRAGE_CHECK(!done->success);
        }
    }
    check_seq_from("task_failed_terminal_event", log, 2);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// Cancellation path: task.cancel publishes a post-admission snapshot (the
/// pinned cancel may settle synchronously, so the admission snapshot can
/// already carry the terminal name) and the settled "Cancelled" snapshot
/// closes the task without a revival.
void scenario_cancel_publishes_cancellation_snapshots() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    if (subscriber.next_frame(kCallBudget).kind != Subscriber::Incoming::Kind::Event) {
        MIRAGE_CHECK(false); // expected the seed event
        return;
    }

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path file = dir.root() / ("cancel-" + token + ".txt");
    write_text_file(file, "cancel fixture");
    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "sleep long enough to be cancelled";
    request.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 4"});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    // Wait until the driver advanced past creation (the first step's
    // completion snapshot), so the cancel lands while the sleep step runs.
    // No progress name is assumed here: the mid-flight snapshot naming is
    // the pinned runtime's own projection.
    std::size_t updates = 0;
    const auto advanced = wait_for_event(
        subscriber, log,
        [&](const ipc::Event &event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            if (task != nullptr && task->task_id == *task_id) {
                ++updates;
            }
            return updates >= 2;
        },
        kEventBudget);
    if (!advanced.has_value()) {
        MIRAGE_CHECK(false); // the task never published a second snapshot
        return;
    }

    const ipc::Response cancelled = client.call(ipc::CancelTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(cancelled.ok);
    if (cancelled.ok) {
        const auto *ack = std::get_if<ipc::TaskCancelled>(&cancelled.payload);
        MIRAGE_CHECK(ack != nullptr);
        if (ack != nullptr) {
            // "Cancelling", or the terminal state the pinned cancel already
            // settled under before the view was taken.
            MIRAGE_CHECK(ack->progress == "Cancelling" || ack->progress == "Cancelled");
        }
    }

    // The post-cancel publish mirrors task.inspect (DEC-012 decision 3): the
    // pinned cancel may settle synchronously, so the admission snapshot can
    // already carry the terminal name; either way the settled "Cancelled"
    // snapshot follows. The task must never revive after it.
    const auto settled = wait_for_event(
        subscriber, log,
        [&](const ipc::Event &event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            return task != nullptr && task->task_id == *task_id && task->progress == "Cancelled";
        },
        kEventBudget);
    MIRAGE_CHECK(settled.has_value());
    if (settled) {
        const auto *done = std::get_if<ipc::TaskUpdatedEvent>(&settled->payload);
        MIRAGE_CHECK(done != nullptr);
        if (done != nullptr) {
            MIRAGE_CHECK(done->has_success);
            MIRAGE_CHECK(!done->success);
        }
        // Drain a quiet window: nothing non-terminal may follow the
        // cancellation for this task (a cancelled task is never revived).
        Subscriber::Incoming after = subscriber.next_frame(kQuietWindow);
        while (after.kind == Subscriber::Incoming::Kind::Event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&after.event.payload);
            if (task != nullptr && task->task_id == *task_id) {
                MIRAGE_CHECK(task->progress == "Cancelled");
            }
            after = subscriber.next_frame(kQuietWindow);
        }
    }
    check_seq_from("cancel_emits_cancelling_then_cancelled", log, 2);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (e) a connection that never subscribes receives zero event frames, even
/// while tasks it submitted advance.
void scenario_unsubscribed_connection_never_sees_event_frames() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber plain;
    std::string diagnostic;
    plain.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(plain.valid());
    if (!plain.valid()) {
        return;
    }
    EventLog log; // must stay empty forever on this connection

    ipc::IpcClient client(config.socket_path);
    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path file = dir.root() / ("quiet-" + token + ".txt");
    write_text_file(file, "unsubscribed observer fixture");
    ipc::SubmitTaskRequest request;
    request.goal = "advance while an unsubscribed connection watches";
    for (int index = 0; index < 4; ++index) {
        request.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});
    }
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    // While the task runs: every frame the plain connection solicits is the
    // response it asked for, never an event.
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    bool terminal = false;
    while (!terminal) {
        const std::uint64_t id = plain.next_id;
        MIRAGE_CHECK(plain.send(ipc::ListTasksRequest{}));
        const auto response = read_response(plain, log, id, kCallBudget);
        MIRAGE_CHECK(response.has_value() && response->ok);
        if (!response || !response->ok) {
            return;
        }
        const auto *list = std::get_if<ipc::TaskList>(&response->payload);
        MIRAGE_CHECK(list != nullptr);
        if (list != nullptr) {
            for (const ipc::TaskSummary &summary : list->tasks) {
                if (summary.id == *task_id &&
                    (summary.progress == "Completed" || summary.progress == "Failed" ||
                     summary.progress == "Cancelled")) {
                    terminal = true;
                }
            }
        }
        MIRAGE_CHECK(std::chrono::steady_clock::now() < deadline);
        if (std::chrono::steady_clock::now() >= deadline) {
            return;
        }
    }

    // After settlement: still nothing unsolicited.
    MIRAGE_CHECK(is_silent(plain, kQuietWindow));
    MIRAGE_CHECK(log.empty());

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (f) two subscribers receive the same event stream, each with its own seq
/// numbering starting at 1.
void scenario_two_subscribers_receive_identical_streams() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    std::string diagnostic;
    Subscriber first;
    Subscriber second;
    first.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    second.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(first.valid() && second.valid());
    if (!first.valid() || !second.valid()) {
        return;
    }
    EventLog first_log;
    EventLog second_log;
    MIRAGE_CHECK(subscribe_and_consume_ack(first, first_log));
    MIRAGE_CHECK(subscribe_and_consume_ack(second, second_log));
    // Both seeds before the task starts, so both subscriptions predate it.
    MIRAGE_CHECK(first.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);
    MIRAGE_CHECK(second.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "fan out to both subscribers";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "true"});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "true"});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    const auto is_terminal = [&](const ipc::Event &event) {
        const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
        return task != nullptr && task->task_id == *task_id &&
               (task->progress == "Completed" || task->progress == "Failed" ||
                task->progress == "Cancelled");
    };
    const auto first_done =
        wait_for_event(first, first_log, is_terminal, kEventBudget);
    const auto second_done =
        wait_for_event(second, second_log, is_terminal, kEventBudget);
    MIRAGE_CHECK(first_done.has_value() && second_done.has_value());
    if (!first_done || !second_done) {
        return;
    }

    // Per-connection seq numbering, then identical content (payloads minus
    // the per-connection seq).
    check_seq_from("two_subscribers first", first_log, 2);
    check_seq_from("two_subscribers second", second_log, 2);
    MIRAGE_CHECK(first_log.size() == second_log.size());
    const std::size_t count = std::min(first_log.size(), second_log.size());
    for (std::size_t index = 0; index < count; ++index) {
        const std::string left = ipc::encode_event(
            ipc::Event{0, first_log[index].payload});
        const std::string right = ipc::encode_event(
            ipc::Event{0, second_log[index].payload});
        if (left != right) {
            std::fprintf(stderr,
                         "[two_subscribers] stream divergence at %zu:\n  first:  %s\n  second: %s\n",
                         index, left.c_str(), right.c_str());
        }
        MIRAGE_CHECK(left == right);
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (g) a subscriber that disconnects mid-task is cleaned up; the remaining
/// subscriber keeps streaming and the service stays healthy.
void scenario_disconnect_mid_task_cleans_up() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    std::string diagnostic;
    Subscriber leaving;
    Subscriber staying;
    leaving.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    staying.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(leaving.valid() && staying.valid());
    if (!leaving.valid() || !staying.valid()) {
        return;
    }
    EventLog leaving_log;
    EventLog staying_log;
    MIRAGE_CHECK(subscribe_and_consume_ack(leaving, leaving_log));
    MIRAGE_CHECK(subscribe_and_consume_ack(staying, staying_log));
    MIRAGE_CHECK(leaving.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);
    MIRAGE_CHECK(staying.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path file = dir.root() / ("bye-" + token + ".txt");
    write_text_file(file, "disconnect fixture");
    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "outlive the first subscriber";
    request.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 0.4"});
    request.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    // Leave once the stream is demonstrably flowing.
    const auto creation = wait_for_event(
        leaving, leaving_log,
        [&](const ipc::Event &event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            return task != nullptr && task->task_id == *task_id;
        },
        kEventBudget);
    MIRAGE_CHECK(creation.has_value());
    leaving.close();

    // The staying subscriber still receives the whole sequence to the end.
    const auto terminal = wait_for_event(
        staying, staying_log,
        [&](const ipc::Event &event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            return task != nullptr && task->task_id == *task_id &&
                   (task->progress == "Completed" || task->progress == "Failed" ||
                    task->progress == "Cancelled");
        },
        kEventBudget);
    MIRAGE_CHECK(terminal.has_value());
    check_seq_from("disconnect_mid_task staying", staying_log, 2);

    // The service answers new work without a hiccup after the cleanup.
    const ipc::Response hello = client.call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(hello.ok);
    ipc::SubmitTaskRequest follow_up;
    follow_up.goal = "post-disconnect sanity task";
    follow_up.steps.push_back({ipc::StepKind::ProcessExecute, "true"});
    const std::optional<std::string> follow_up_id = submit_task(client, follow_up);
    MIRAGE_CHECK(follow_up_id.has_value());
    if (follow_up_id) {
        const auto done = wait_terminal(config.socket_path, *follow_up_id);
        MIRAGE_CHECK(done.has_value() && done->progress == "Completed");
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (h) after events.unsubscribe the connection receives no further event
/// frames, even for tasks submitted afterwards.
void scenario_unsubscribe_stops_event_delivery() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    MIRAGE_CHECK(subscriber.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);

    // Detach is queued before the ack on the loop's FIFO, so once the ack is
    // read the subscription is provably gone.
    const std::uint64_t id = subscriber.next_id;
    MIRAGE_CHECK(subscriber.send(ipc::UnsubscribeEventsRequest{}));
    const auto ack = read_response(subscriber, log, id, kCallBudget);
    MIRAGE_CHECK(ack.has_value() && ack->ok);
    if (!ack || !ack->ok) {
        return;
    }

    // A whole task lifecycle happens after the detach; none of it may reach
    // the former subscriber.
    ipc::IpcClient client(config.socket_path);
    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path file = dir.root() / ("post-" + token + ".txt");
    write_text_file(file, "post-detach fixture");
    ipc::SubmitTaskRequest request;
    request.goal = "must not be streamed to the detached connection";
    request.steps.push_back({ipc::StepKind::FilesystemRead, file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 0.2"});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    const auto done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");
    if (!done) {
        return;
    }

    MIRAGE_CHECK(is_silent(subscriber, kQuietWindow));
    MIRAGE_CHECK(log.empty());

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// Counts the distinct tasks of the burst that have a Completed snapshot in
/// the log so far.
std::size_t count_settled_tasks(const EventLog &log,
                                const std::vector<std::string> &task_ids) {
    std::vector<std::string> settled;
    for (const ipc::Event &event : log) {
        const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
        if (task == nullptr || task->progress != "Completed") {
            continue;
        }
        if (std::find(task_ids.begin(), task_ids.end(), task->task_id) == task_ids.end()) {
            continue;
        }
        if (std::find(settled.begin(), settled.end(), task->task_id) == settled.end()) {
            settled.push_back(task->task_id);
        }
    }
    return settled.size();
}

/// (i) a tiny per-connection queue surfaces its drops as events.overflow
/// while seq stays contiguous; the newest snapshots survive drop-oldest.
///
/// Deterministic injection, no wall-clock race: the loop handles every
/// readable request frame of one poll tick before its single tail
/// drain_outbound()+drain_events(), and handle_frame waits for the serial
/// handler to finish (runtime_service handle_frame: submit_on + get). Each
/// handle_submit publishes its creation event synchronously on serial, so N
/// task.submit frames that the loop reads in one tick push N events into the
/// capacity-2 subscriber queue with no drain in between. The burst clients
/// write all their frames up front (microseconds, non-blocking) while one
/// serial submit round-trip takes orders of magnitude longer, so at least
/// one tick always handles >= 3 of the 8 frames: the overflow is structural.
/// A slower serial (TSan) only widens the handled batch.
void scenario_small_queue_overflow_emits_marker() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    // Capacity 2 is the DEC-012 "small queue" injection shape.
    config.event_queue_capacity = 2;
    config.executor_threads = 8;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    MIRAGE_CHECK(subscriber.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);

    // Eight plain (unsubscribed) connections, one zero-step task.submit each.
    // No submit is answered before every frame has been written, so the loop
    // handles the batch inside one or two ticks and the creation events pile
    // into the subscriber's capacity-2 queue with no drain in between.
    constexpr int kBurstConnections = 8;
    std::vector<Subscriber> clients;
    clients.reserve(static_cast<std::size_t>(kBurstConnections));
    std::vector<std::uint64_t> correlation_ids;
    for (int index = 0; index < kBurstConnections; ++index) {
        clients.emplace_back();
        Subscriber &client = clients.back();
        client.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
        MIRAGE_CHECK(client.valid());
        if (!client.valid()) {
            return;
        }
        ipc::SubmitTaskRequest request;
        request.goal = "deterministic overflow burst " + std::to_string(index);
        const std::uint64_t correlation_id = client.next_id;
        MIRAGE_CHECK(client.send(request));
        correlation_ids.push_back(correlation_id);
    }

    // Every submit must be admitted; the responses carry the identities the
    // event assertions below are keyed on. The submit burst overlaps the
    // drivers' own bounded serial waits, so a response can surface only
    // after several command_wait windows: the reads share one generous
    // deadline instead of trusting per-request latency. The overflow
    // assertion above this line's drain does not depend on these responses.
    std::vector<std::string> task_ids;
    {
        const auto response_deadline = std::chrono::steady_clock::now() + std::chrono::seconds{60};
        for (std::size_t index = 0; index < clients.size(); ++index) {
            EventLog scratch; // unsubscribed connections never carry events
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                response_deadline - std::chrono::steady_clock::now());
            const std::optional<ipc::Response> response =
                remaining <= std::chrono::milliseconds::zero()
                    ? std::nullopt
                    : read_response(clients[index], scratch, correlation_ids[index],
                                    std::min(remaining, std::chrono::milliseconds{20000}));
            if (!response || !response->ok) {
                std::fprintf(stderr,
                             "[small_queue_overflow] client %zu submit response missing/bad\n",
                             index);
            }
            MIRAGE_CHECK(response.has_value() && response->ok);
            if (response && response->ok) {
                const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response->payload);
                MIRAGE_CHECK(submitted != nullptr);
                if (submitted != nullptr) {
                    task_ids.push_back(submitted->task_id);
                }
            }
        }
    }
    MIRAGE_CHECK(task_ids.size() == clients.size());

    // Registry truth first: all zero-step tasks complete (event drops below
    // must not blur that). Then collect the stream until at least one
    // terminal snapshot survived drop-oldest. Every frame read is appended
    // to the log — discarding reads here would hide the overflow markers
    // this scenario asserts on.
    for (const std::string &task_id : task_ids) {
        const auto done = wait_terminal(config.socket_path, task_id);
        MIRAGE_CHECK(done.has_value() && done->progress == "Completed");
    }
    const auto settle_deadline = std::chrono::steady_clock::now() + kEventBudget;
    while (count_settled_tasks(log, task_ids) < 1) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            settle_deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            break;
        }
        (void)wait_for_event(subscriber, log, [](const ipc::Event &) { return false; },
                             std::min(remaining, kQuietWindow));
    }

    std::uint64_t dropped_total = 0;
    std::size_t overflow_markers = 0;
    for (const ipc::Event &event : log) {
        if (const auto *overflow = std::get_if<ipc::EventsOverflowEvent>(&event.payload)) {
            ++overflow_markers;
            dropped_total += overflow->dropped;
        }
    }
    if (overflow_markers == 0) {
        std::fprintf(stderr, "[small_queue_overflow] diagnostics: %zu events, names:",
                     log.size());
        for (const ipc::Event &event : log) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            if (task != nullptr) {
                std::fprintf(stderr, " %s(seq %llu %s)", ipc::event_name(event.payload),
                             static_cast<unsigned long long>(event.seq),
                             task->progress.c_str());
            } else {
                std::fprintf(stderr, " %s(seq %llu)", ipc::event_name(event.payload),
                             static_cast<unsigned long long>(event.seq));
            }
        }
        std::fprintf(stderr, "\n");
    }
    MIRAGE_CHECK(overflow_markers >= 1);
    MIRAGE_CHECK(dropped_total >= 1);

    // seq was assigned at write-out: contiguous across the drops, with the
    // overflow marker(s) inside the same numbering.
    check_seq_relative("small_queue_overflow", log);

    // Drop-oldest keeps only the newest queue entries, so the tasks that
    // settle EARLY can lose their terminal snapshot to later events — the
    // snapshots stay the source of truth (DEC-012 decision 4) and at least
    // the last-settling task's terminal publish must survive.
    const std::size_t settled = count_settled_tasks(log, task_ids);
    MIRAGE_CHECK(settled >= 1);

    // The newest task snapshot in the stream is a terminal settlement (the
    // stream may end on the overflow marker itself, so scan back).
    const auto *last_task = static_cast<const ipc::TaskUpdatedEvent *>(nullptr);
    for (auto it = log.rbegin(); it != log.rend(); ++it) {
        if (const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&it->payload)) {
            last_task = task;
            break;
        }
    }
    MIRAGE_CHECK(last_task != nullptr);
    if (last_task != nullptr) {
        MIRAGE_CHECK(std::find(task_ids.begin(), task_ids.end(), last_task->task_id) !=
                     task_ids.end());
        MIRAGE_CHECK(last_task->progress == "Completed");
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (j) responses are written ahead of queued events: task.list on a busy
/// subscription connection is answered while the event stream is active.
void scenario_response_not_starved_by_event_stream() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    MIRAGE_CHECK(subscriber.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "keep the event stream busy";
    for (int index = 0; index < 5; ++index) {
        request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 0.2"});
    }
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    // Prove the stream is live before making the demand.
    MIRAGE_CHECK(wait_for_event(
                     subscriber, log,
                     [&](const ipc::Event &event) {
                         const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
                         return task != nullptr && task->task_id == *task_id;
                     },
                     kEventBudget)
                     .has_value());

    const std::uint64_t id = subscriber.next_id;
    const auto sent = std::chrono::steady_clock::now();
    MIRAGE_CHECK(subscriber.send(ipc::ListTasksRequest{}));
    const auto response = read_response(subscriber, log, id, std::chrono::seconds{5});
    const auto elapsed = std::chrono::steady_clock::now() - sent;
    MIRAGE_CHECK(response.has_value());
    if (response) {
        MIRAGE_CHECK(response->ok);
        MIRAGE_CHECK(std::holds_alternative<ipc::TaskList>(response->payload));
    }
    // Not just delivered, delivered while events kept flowing.
    MIRAGE_CHECK(elapsed < std::chrono::seconds{4});

    // Let the sleeping task finish so shutdown is clean.
    const auto settled = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(settled.has_value());

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// (k) service-level unknown-op contract: stable protocol_error message and
/// connection close (wire twin of the unknown-op-registry-edit vector).
void scenario_unknown_op_error_then_close() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    const std::string frame = ipc::make_frame("{\"v\":1,\"id\":77,\"op\":\"registry.edit\"}");
    MIRAGE_CHECK(mirage::testing::write_all(subscriber.stream, frame, kCallBudget));

    Subscriber::Incoming incoming = subscriber.next_frame(kCallBudget);
    MIRAGE_CHECK(incoming.kind == Subscriber::Incoming::Kind::Response);
    if (incoming.kind == Subscriber::Incoming::Kind::Response) {
        MIRAGE_CHECK(!incoming.response.ok);
        // Protocol violations carry correlation id 0 (no trusted id to echo
        // on the error path — the same shape as every other protocol_error).
        MIRAGE_CHECK(incoming.response.id == 0);
        MIRAGE_CHECK(incoming.response.error.code == "protocol_error");
        MIRAGE_CHECK(incoming.response.error.message == "unknown op 'registry.edit'");
    }
    // The violation closes the connection after the error frame.
    const auto closing = subscriber.next_frame(kCallBudget);
    MIRAGE_CHECK(closing.kind == Subscriber::Incoming::Kind::Closed);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

/// Config validation: a zero event queue capacity is rejected at start().
void scenario_zero_event_queue_capacity_rejected() {
    mirage::testing::TempDir dir;
    ServiceConfig config = make_config(dir);
    config.event_queue_capacity = 0;
    RuntimeService service(config);
    const auto refused = service.start(make_binding(dir));
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "invalid_argument");
}

/// Resync-shaped double subscribe on one connection: both attempts
/// acknowledge and the stream keeps working with contiguous seq.
void scenario_double_subscribe_keeps_stream_alive() {
    mirage::testing::TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    Subscriber subscriber;
    std::string diagnostic;
    subscriber.stream = ipc::connect_stream(config.socket_path, kCallBudget, diagnostic);
    MIRAGE_CHECK(subscriber.valid());
    if (!subscriber.valid()) {
        return;
    }
    EventLog log;
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));
    MIRAGE_CHECK(subscriber.next_frame(kCallBudget).kind == Subscriber::Incoming::Kind::Event);
    MIRAGE_CHECK(subscribe_and_consume_ack(subscriber, log));

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "stream survives a second subscribe";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "true"});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    const auto terminal = wait_for_event(
        subscriber, log,
        [&](const ipc::Event &event) {
            const auto *task = std::get_if<ipc::TaskUpdatedEvent>(&event.payload);
            return task != nullptr && task->task_id == *task_id && task->progress == "Completed";
        },
        kEventBudget);
    MIRAGE_CHECK(terminal.has_value());
    // Per-connection seq numbering stays contiguous across the re-subscribe:
    // the second subscribe's own seed consumed seq 2, so the frames collected
    // from there start at 2 and never jump.
    check_seq_from("double_subscribe", log, 2);

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[event_subscription_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    // SIGPIPE regression guard (DEC-012 decision 5, M1.5-02): the library
    // write path (IpcStream::write_some) uses send(MSG_NOSIGNAL), so a
    // subscriber that hangs up mid-stream must surface as IoStatus::Closed
    // inside the IPC loop instead of killing the host. This suite therefore
    // deliberately does NOT ignore SIGPIPE: if any service write path
    // regressed to a plain write() against a dead peer, the scenarios below
    // (notably disconnect_mid_task_cleans_up, which drops a subscriber while
    // its event stream is still being written) would die with SIGPIPE here
    // rather than fail a check.

    run_scenario("hello_advertises_events_capability",
                 scenario_hello_advertises_events_capability);
    run_scenario("subscribe_unsubscribe_round_trip", scenario_subscribe_unsubscribe_round_trip);
    run_scenario("subscribe_seed_is_current_host_status",
                 scenario_subscribe_seed_is_current_host_status);
    run_scenario("task_updated_full_sequence", scenario_task_updated_full_sequence);
    run_scenario("task_failed_terminal_event", scenario_task_failed_terminal_event);
    run_scenario("cancel_publishes_cancellation_snapshots",
                 scenario_cancel_publishes_cancellation_snapshots);
    run_scenario("unsubscribed_connection_never_sees_event_frames",
                 scenario_unsubscribed_connection_never_sees_event_frames);
    run_scenario("two_subscribers_receive_identical_streams",
                 scenario_two_subscribers_receive_identical_streams);
    run_scenario("disconnect_mid_task_cleans_up", scenario_disconnect_mid_task_cleans_up);
    run_scenario("unsubscribe_stops_event_delivery", scenario_unsubscribe_stops_event_delivery);
    run_scenario("response_not_starved_by_event_stream",
                 scenario_response_not_starved_by_event_stream);
    run_scenario("unknown_op_error_then_close", scenario_unknown_op_error_then_close);
    run_scenario("zero_event_queue_capacity_rejected", scenario_zero_event_queue_capacity_rejected);
    run_scenario("double_subscribe_keeps_stream_alive",
                 scenario_double_subscribe_keeps_stream_alive);
    // Last on purpose: this is the heaviest scenario (eight concurrent
    // drivers against a capacity-2 queue), so a failure here must never
    // obscure the cheaper scenarios above it.
    run_scenario("small_queue_overflow_emits_marker", scenario_small_queue_overflow_emits_marker);
    return mirage::testing::finish("event_subscription_test");
}
