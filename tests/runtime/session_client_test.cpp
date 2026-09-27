// M5-02 session client tests: the long-lived Local IPC session the desktop
// shell drives over an Executor blocking worker, exercised against a real
// RuntimeService on the real transport (the same scenarios the product shell
// performs: hello, subscribe, task round trip, reconnect decisions).
//
// The threading here is the test harness standing in for the owner's
// Executor (the same stand-in the runtime_service_test and the M4-06
// product-process tests use): service.run() on one thread, SessionClient
// run() on another.

#include "../support/ipc_io.hpp"

#include "../support/test.hpp"

#include <mira/core_contracts.hpp>

#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/session_client.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;

// Liveness guards, not latency assertions (the runtime_service_test budget
// rationale: parallel ctest oversubscription can lag the service workers).
constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr auto kConnectBudget = std::chrono::seconds{10};
constexpr auto kTaskBudget = std::chrono::seconds{30};

/// The M5-02 session under test plus its driving loop thread. The loop is
/// the test's stand-in for the shell's Executor blocking worker; stop()
/// models the shell's cooperative shutdown.
struct Session {
    std::unique_ptr<ipc::SessionClient> client;
    std::thread loop;
    ipc::SessionClient::RunExit exit = ipc::SessionClient::RunExit::Stopped;
    std::string diagnostic;

    Session() = default;
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;
    ~Session() {
        if (client) {
            client->stop();
        }
        if (loop.joinable()) {
            loop.join();
        }
    }

    bool start(const std::string &address) {
        client = std::make_unique<ipc::SessionClient>(address);
        std::string connect_error;
        if (!client->connect(kConnectBudget, connect_error)) {
            std::fprintf(stderr, "[session_client_test] connect failed: %s\n",
                         connect_error.c_str());
            return false;
        }
        loop = std::thread([this] {
            std::string reason;
            exit = client->run(reason);
            diagnostic = std::move(reason);
        });
        return true;
    }

    void join_and_stop() {
        client->stop();
        loop.join();
        loop = {};
    }
};

/// The service with its driving runner thread; shutdown goes through the
/// protocol itself (the M4-06 ServiceProcess shape).
struct ServiceProcess {
    RuntimeService service;
    std::thread runner;

    explicit ServiceProcess(mirage::runtime::ServiceConfig config) : service(std::move(config)) {}
    ~ServiceProcess() {
        // A scenario that early-returns before its explicit shutdown must
        // not hang the whole suite on this join: request the stop first.
        service.request_shutdown();
        if (runner.joinable()) {
            runner.join();
        }
    }

    bool start(const std::shared_ptr<integration::MiraEnvironmentBinding> &binding) {
        return service.start(binding).ok;
    }
    void run_async() {
        runner = std::thread([this] { (void)service.run(); });
    }
};

mirage::runtime::ServiceConfig make_config(const mirage::testing::TempDir &dir) {
    mirage::runtime::ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.4.0-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    // Recovery state stays inside the scenario's temp tree (the M1-07
    // discipline of runtime_service_test).
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

std::string submit_task(ipc::SessionClient &session, const std::string &goal_file,
                        const std::string &goal) {
    ipc::SubmitTaskRequest request;
    request.goal = goal;
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file});
    const ipc::Response response = session.call(request, kCallBudget).get();
    MIRAGE_CHECK(response.ok);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    MIRAGE_CHECK(submitted != nullptr);
    return submitted == nullptr ? std::string{} : submitted->task_id;
}

// --- normal session surface --------------------------------------------------

void scenario_hello_list_and_clean_stop() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    Session session;
    MIRAGE_CHECK(session.start(config.socket_path));
    MIRAGE_CHECK(session.client->connected());

    // hello over the session: the identity round trip the shell performs on
    // every (re)connect.
    const ipc::Response hello = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(hello.ok);
    MIRAGE_CHECK(hello.id == 1); // correlation ids start at 1 and echo
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(identity != nullptr);
    if (identity != nullptr) {
        MIRAGE_CHECK(identity->mirage_version == "0.4.0-test");
        MIRAGE_CHECK(identity->protocol == ipc::kProtocolVersion);
        MIRAGE_CHECK(identity->events.has_value() && identity->events.value());
    }

    // A second exchange on the same session (the long-lived surface the
    // one-shot client cannot serve).
    const ipc::Response listed = session.client->call(ipc::ListTasksRequest{}, kCallBudget).get();
    MIRAGE_CHECK(listed.ok);
    MIRAGE_CHECK(std::holds_alternative<ipc::TaskList>(listed.payload));

    // Cooperative stop: run() returns Stopped, pending calls are drained.
    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);
    MIRAGE_CHECK(!session.client->connected());

    // Calls after the loop is gone refuse instead of queueing forever.
    const ipc::Response after_stop = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(!after_stop.ok);
    MIRAGE_CHECK(after_stop.error.code == "unavailable");

    const ipc::Response shutdown =
        ipc::IpcClient(config.socket_path).call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(shutdown.ok);
}

void scenario_overlapped_calls_serialize() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    Session session;
    MIRAGE_CHECK(session.start(config.socket_path));

    // Eight calls issued before any of them is consumed: the single-
    // outstanding discipline queues them, the responses correlate by id,
    // and every future resolves ok with its own exchange's payload.
    constexpr int kCalls = 8;
    std::vector<std::future<ipc::Response>> futures;
    for (int index = 0; index < kCalls; ++index) {
        futures.push_back(session.client->call(ipc::ListTasksRequest{}, kCallBudget));
    }
    std::uint64_t previous_id = 0;
    for (std::future<ipc::Response> &future : futures) {
        const ipc::Response response = future.get();
        MIRAGE_CHECK(response.ok);
        MIRAGE_CHECK(std::holds_alternative<ipc::TaskList>(response.payload));
        // The wire served the requests in issue order (single outstanding).
        MIRAGE_CHECK(response.id > previous_id);
        previous_id = response.id;
    }

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);

    const ipc::Response shutdown =
        ipc::IpcClient(config.socket_path).call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(shutdown.ok);
}

// --- the event stream --------------------------------------------------------

void scenario_events_flow_to_sink_until_terminal() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    Session session;
    MIRAGE_CHECK(session.start(config.socket_path));

    // The sink runs on the loop thread: collect under a mutex, wait from here.
    std::mutex events_mutex;
    std::vector<ipc::Event> events;
    std::atomic<bool> overflow{false};
    session.client->set_event_sink([&](const ipc::Event &event) {
        std::lock_guard<std::mutex> guard(events_mutex);
        if (std::holds_alternative<ipc::EventsOverflowEvent>(event.payload)) {
            overflow.store(true, std::memory_order_release);
        }
        events.push_back(event);
    });

    // Subscribe, then drive a real task to its terminal state; the session
    // must observe the task's creation, progress and settlement.
    const ipc::Response subscribed =
        session.client->call(ipc::SubscribeEventsRequest{}, kCallBudget).get();
    MIRAGE_CHECK(subscribed.ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    write_text_file(goal_file, "session event flow " + token + "\n");
    const std::string task_id =
        submit_task(*session.client, goal_file.string(), "session: watch the read complete");
    MIRAGE_CHECK(!task_id.empty());
    if (task_id.empty()) {
        return;
    }

    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    bool terminal = false;
    std::uint64_t last_seq = 0;
    std::size_t processed = 0; // snapshot prefix already checked
    while (!terminal) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::vector<ipc::Event> snapshot;
        {
            std::lock_guard<std::mutex> guard(events_mutex);
            snapshot = events;
        }
        // Only the newly arrived suffix needs checking this pass.
        for (std::size_t index = processed; index < snapshot.size(); ++index) {
            const ipc::Event &event = snapshot[index];
            // Per-connection seq is strictly monotonic (DEC-012 decision 3).
            MIRAGE_CHECK(event.seq > last_seq);
            last_seq = event.seq;
            if (const auto *updated = std::get_if<ipc::TaskUpdatedEvent>(&event.payload)) {
                if (updated->task_id == task_id && updated->has_success && updated->success) {
                    MIRAGE_CHECK(updated->progress == "Completed");
                    terminal = true;
                }
            }
        }
        processed = snapshot.size();
        if (!terminal) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    }
    MIRAGE_CHECK(terminal); // the terminal task.updated arrived on the session
    MIRAGE_CHECK(!overflow.load());

    // Unsubscribe is idempotent and answered.
    const ipc::Response unsubscribe =
        session.client->call(ipc::UnsubscribeEventsRequest{}, kCallBudget).get();
    MIRAGE_CHECK(unsubscribe.ok);

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);

    const ipc::Response shutdown =
        ipc::IpcClient(config.socket_path).call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(shutdown.ok);
}

// --- the workflow face (M5-05, DEC-023) ---------------------------------------

/// Minimal IR v1 definition: one Verify step whose predicate references an
/// absent parameter (NotEvaluable under DryRun, RULE-10 honesty counter), so
/// the definition runs without a tool registry and settles the publish
/// gate's empty DryRun. `name` varies content: the pinned library resolves
/// versions by content digest and a same-content draft record shadows a
/// later runnable record (MIRA-20260927-001), so the flow under test
/// publishes different content than the draft.
std::string workflow_definition(const std::string &workflow_id, const std::string &step_id,
                                const std::string &name) {
    return R"({"schema_version":{"major":1,"minor":0},"workflow_id":")" + workflow_id +
           R"(","name":")" + name + R"(","parameters":[],"steps":[{"step_id":")" + step_id +
           R"(","kind":"verify","verification":{"signal":"run_parameter:x","op":"eq","value":"y"}}],)"
           R"("default_policy":"strict","allowed_policies":["strict","dry_run"]})";
}

void scenario_workflow_face_round_trip() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    Session session;
    MIRAGE_CHECK(session.start(config.socket_path));

    std::mutex events_mutex;
    std::vector<ipc::Event> events;
    session.client->set_event_sink([&](const ipc::Event &event) {
        std::lock_guard<std::mutex> guard(events_mutex);
        events.push_back(event);
    });
    MIRAGE_CHECK(session.client->call(ipc::SubscribeEventsRequest{}, kCallBudget).get().ok);

    // hello advertises the workflow face (DEC-023 capability discipline).
    const ipc::Response hello = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(hello.ok);
    const auto *identity = std::get_if<ipc::ServiceIdentity>(&hello.payload);
    MIRAGE_CHECK(identity != nullptr);
    MIRAGE_CHECK(identity != nullptr && identity->workflows.value_or(false));

    // Save a draft; the catalog lists it unvalidated and non-runnable.
    const std::string workflow_id = mira::WorkflowId::generate().to_string();
    const std::string step_id = mira::StepId::generate().to_string();
    ipc::WorkflowSaveRequest save;
    save.definition_json = workflow_definition(workflow_id, step_id, "demo draft");
    const ipc::Response saved = session.client->call(save, kCallBudget).get();
    MIRAGE_CHECK(saved.ok);
    const auto *saved_payload = std::get_if<ipc::WorkflowSaved>(&saved.payload);
    MIRAGE_CHECK(saved_payload != nullptr);
    if (saved_payload == nullptr) {
        return;
    }
    MIRAGE_CHECK(saved_payload->workflow_id == workflow_id);
    MIRAGE_CHECK(saved_payload->digest.size() == 64);

    const ipc::Response listed =
        session.client->call(ipc::WorkflowListRequest{}, kCallBudget).get();
    MIRAGE_CHECK(listed.ok);
    const auto *catalog = std::get_if<ipc::WorkflowList>(&listed.payload);
    MIRAGE_CHECK(catalog != nullptr);
    if (catalog != nullptr) {
        bool found = false;
        for (const ipc::WorkflowSummary &entry : catalog->workflows) {
            if (entry.workflow_id == workflow_id) {
                found = true;
                MIRAGE_CHECK(entry.name == "demo draft");
                MIRAGE_CHECK(entry.validation == "not_validated");
                MIRAGE_CHECK(!entry.runnable);
            }
        }
        MIRAGE_CHECK(found);
    }

    // Running the draft digest is the pinned W-04 rejection, verbatim.
    ipc::WorkflowRunRequest draft_run;
    draft_run.workflow_id = workflow_id;
    draft_run.digest = saved_payload->digest;
    const ipc::Response draft_rejected = session.client->call(draft_run, kCallBudget).get();
    MIRAGE_CHECK(!draft_rejected.ok);
    MIRAGE_CHECK(draft_rejected.error.code == "pinned_runtime");

    // An unknown workflow is a stable not_found.
    ipc::WorkflowRunRequest unknown_run;
    unknown_run.workflow_id = mira::WorkflowId::generate().to_string();
    const ipc::Response unknown = session.client->call(unknown_run, kCallBudget).get();
    MIRAGE_CHECK(!unknown.ok);
    MIRAGE_CHECK(unknown.error.code == "not_found");

    // Publish different content (the natural edit-then-publish flow); the
    // gate appends a runnable version and the catalog projects it.
    ipc::WorkflowPublishRequest publish;
    publish.definition_json = workflow_definition(workflow_id, step_id, "demo");
    const ipc::Response published = session.client->call(publish, kCallBudget).get();
    MIRAGE_CHECK(published.ok);
    const auto *published_payload = std::get_if<ipc::WorkflowPublished>(&published.payload);
    MIRAGE_CHECK(published_payload != nullptr);
    if (published_payload == nullptr) {
        return;
    }
    MIRAGE_CHECK(!published_payload->idempotent);
    MIRAGE_CHECK(published_payload->dry_run_id.size() == 32);

    const ipc::Response listed_again =
        session.client->call(ipc::WorkflowListRequest{}, kCallBudget).get();
    if (!listed_again.ok) {
        std::fprintf(stderr, "[session_client_test] list#2 failed: %s: %s\n",
                     listed_again.error.code.c_str(), listed_again.error.message.c_str());
    }
    MIRAGE_CHECK(listed_again.ok);
    const auto *catalog_again = std::get_if<ipc::WorkflowList>(&listed_again.payload);
    MIRAGE_CHECK(catalog_again != nullptr);
    if (catalog_again != nullptr) {
        for (const ipc::WorkflowSummary &entry : catalog_again->workflows) {
            if (entry.workflow_id == workflow_id) {
                MIRAGE_CHECK(entry.head_digest == published_payload->digest);
                MIRAGE_CHECK(entry.validation == "dry_run_passed");
                MIRAGE_CHECK(entry.runnable);
            }
        }
    }

    // Run from the head (digest absent); the drive is asynchronous and the
    // run_updated stream reports started then the terminal state.
    ipc::WorkflowRunRequest run_request;
    run_request.workflow_id = workflow_id;
    const ipc::Response started = session.client->call(run_request, kCallBudget).get();
    if (!started.ok) {
        std::fprintf(stderr, "[session_client_test] run failed: %s: %s\n",
                     started.error.code.c_str(), started.error.message.c_str());
    }
    MIRAGE_CHECK(started.ok);
    const auto *started_payload = std::get_if<ipc::WorkflowRunStarted>(&started.payload);
    MIRAGE_CHECK(started_payload != nullptr);
    if (started_payload == nullptr) {
        return;
    }
    const std::string run_id = started_payload->run_id;
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(run_id));

    bool saw_running = false;
    bool saw_completed = false;
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    while (!saw_completed && std::chrono::steady_clock::now() < deadline) {
        std::vector<ipc::Event> snapshot;
        {
            std::lock_guard<std::mutex> guard(events_mutex);
            snapshot = events;
        }
        for (const ipc::Event &event : snapshot) {
            const auto *run_updated = std::get_if<ipc::WorkflowRunUpdatedEvent>(&event.payload);
            if (run_updated == nullptr || run_updated->run_id != run_id) {
                continue;
            }
            saw_running = saw_running || run_updated->state == "running";
            saw_completed = saw_completed || run_updated->state == "completed";
        }
        if (!saw_completed) {
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
        }
    }
    MIRAGE_CHECK(saw_running);
    MIRAGE_CHECK(saw_completed);

    // workflow.runs projects the terminal state live from the pinned runtime.
    const ipc::Response runs = session.client->call(ipc::WorkflowRunsRequest{}, kCallBudget).get();
    MIRAGE_CHECK(runs.ok);
    const auto *run_list = std::get_if<ipc::WorkflowRunList>(&runs.payload);
    MIRAGE_CHECK(run_list != nullptr);
    if (run_list != nullptr) {
        bool run_found = false;
        for (const ipc::WorkflowRunSummary &summary : run_list->runs) {
            if (summary.run_id == run_id) {
                run_found = true;
                MIRAGE_CHECK(summary.state == "completed");
                MIRAGE_CHECK(summary.workflow_id == workflow_id);
            }
        }
        MIRAGE_CHECK(run_found);
    }

    // Cancelling a terminal run is the idempotent NoOp.
    ipc::WorkflowCancelRunRequest cancel;
    cancel.run_id = run_id;
    const ipc::Response cancelled = session.client->call(cancel, kCallBudget).get();
    MIRAGE_CHECK(cancelled.ok);
    const auto *cancelled_payload = std::get_if<ipc::WorkflowRunCancelled>(&cancelled.payload);
    MIRAGE_CHECK(cancelled_payload != nullptr);
    MIRAGE_CHECK(cancelled_payload != nullptr && cancelled_payload->state == "completed");

    // The atom catalog face is served; it stays empty until the desktop
    // capability tools register (M5-05 second round).
    const ipc::Response atoms =
        session.client->call(ipc::WorkflowAtomCatalogRequest{}, kCallBudget).get();
    MIRAGE_CHECK(atoms.ok);
    const auto *atom_catalog = std::get_if<ipc::WorkflowAtomCatalog>(&atoms.payload);
    MIRAGE_CHECK(atom_catalog != nullptr);
    MIRAGE_CHECK(atom_catalog != nullptr && atom_catalog->tools.empty());

    // Delete removes the catalog entry; the second delete is a not_found.
    ipc::WorkflowDeleteRequest remove;
    remove.workflow_id = workflow_id;
    const ipc::Response deleted = session.client->call(remove, kCallBudget).get();
    MIRAGE_CHECK(deleted.ok);
    const ipc::Response deleted_again = session.client->call(remove, kCallBudget).get();
    MIRAGE_CHECK(!deleted_again.ok);
    MIRAGE_CHECK(deleted_again.error.code == "not_found");

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);

    const ipc::Response shutdown =
        ipc::IpcClient(config.socket_path).call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(shutdown.ok);
}

// --- fail-closed paths -------------------------------------------------------

void scenario_timeout_closes_session_fail_closed() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    // Connected, but no run() loop yet: the request sits unserved, the call
    // expires, and the frozen single-outstanding discipline closes the
    // session fail-closed (no issue-after-silence).
    Session session;
    session.client = std::make_unique<ipc::SessionClient>(config.socket_path);
    std::string connect_error;
    MIRAGE_CHECK(session.client->connect(kConnectBudget, connect_error));

    const ipc::Response expired =
        session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{300}).get();
    MIRAGE_CHECK(!expired.ok);
    MIRAGE_CHECK(expired.error.code == "unavailable");
    MIRAGE_CHECK(expired.error.message.find("timed out") != std::string::npos);

    // The loop, once started, drains with the fail-closed close; run()
    // reports the cause instead of serving the dead session.
    session.loop = std::thread([&session] {
        std::string reason;
        session.exit = session.client->run(reason);
        session.diagnostic = std::move(reason);
    });
    session.loop.join();
    session.loop = {};
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::ConnectionLost);
    MIRAGE_CHECK(session.diagnostic.find("timed out") != std::string::npos);

    const ipc::Response shutdown =
        ipc::IpcClient(config.socket_path).call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(shutdown.ok);
}

void scenario_service_shutdown_loses_the_session() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    Session session;
    MIRAGE_CHECK(session.start(config.socket_path));
    const ipc::Response hello = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(hello.ok);

    // The service goes away (the shell's peer-exit case): the session's run()
    // surfaces ConnectionLost with the transport reason, pending calls fail
    // "unavailable" — reconnect and resync are the owner's decision.
    const ipc::Response shutdown =
        ipc::IpcClient(config.socket_path).call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(shutdown.ok);
    service.runner.join();
    service.runner = {};

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::ConnectionLost);
    MIRAGE_CHECK(!session.client->connected());
}

void scenario_connect_failure_is_bounded() {
    mirage::testing::TempDir dir;
    // Nothing listens at this address: connect fails closed with a
    // diagnostic and no session exists to run.
    Session session;
    session.client = std::make_unique<ipc::SessionClient>((dir.root() / "absent.sock").string());
    std::string diagnostic;
    MIRAGE_CHECK(!session.client->connect(std::chrono::milliseconds{500}, diagnostic));
    MIRAGE_CHECK(!diagnostic.empty());
    MIRAGE_CHECK(!session.client->connected());

    const ipc::Response refused = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "unavailable");
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[session_client_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("hello_list_and_clean_stop", scenario_hello_list_and_clean_stop);
    run_scenario("overlapped_calls_serialize", scenario_overlapped_calls_serialize);
    run_scenario("events_flow_to_sink_until_terminal", scenario_events_flow_to_sink_until_terminal);
    run_scenario("timeout_closes_session_fail_closed", scenario_timeout_closes_session_fail_closed);
    run_scenario("service_shutdown_loses_the_session", scenario_service_shutdown_loses_the_session);
    run_scenario("connect_failure_is_bounded", scenario_connect_failure_is_bounded);
    run_scenario("workflow_face_round_trip", scenario_workflow_face_round_trip);
    return mirage::testing::finish("session_client_test");
}
