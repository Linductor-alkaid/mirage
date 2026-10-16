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
#include <mira/json.hpp>

#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/ipc/session_client.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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
    /// Set after run() returns (the DEC-094 scenarios observe the loop's
    /// liveness from the calling thread without joining it).
    std::atomic<bool> run_finished{false};

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

    /// Establishes the transport only; the run loop starts separately (the
    /// DEC-094 scenarios interleave call()/run() phases).
    bool connect(const std::string &address) {
        client = std::make_unique<ipc::SessionClient>(address);
        std::string connect_error;
        if (!client->connect(kConnectBudget, connect_error)) {
            std::fprintf(stderr, "[session_client_test] connect failed: %s\n",
                         connect_error.c_str());
            return false;
        }
        return true;
    }

    void start_run() {
        loop = std::thread([this] {
            std::string reason;
            exit = client->run(reason);
            diagnostic = std::move(reason);
            run_finished.store(true, std::memory_order_release);
        });
    }

    bool start(const std::string &address) {
        if (!connect(address)) {
            return false;
        }
        start_run();
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
    // DryRun policy: the single Verify step's predicate is NotEvaluable
    // (planned, counted per RULE-10) and the run settles Completed; under a
    // dispatching policy the pinned runtime settles it Failed instead.
    run_request.policy = "dry_run";
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

    // The atom catalog projects the bound environment's real capabilities
    // (DEC-024): the test binding opts into no desktop surface, so exactly
    // the M1 filesystem and process atoms are exposed.
    const ipc::Response atoms =
        session.client->call(ipc::WorkflowAtomCatalogRequest{}, kCallBudget).get();
    MIRAGE_CHECK(atoms.ok);
    const auto *atom_catalog = std::get_if<ipc::WorkflowAtomCatalog>(&atoms.payload);
    MIRAGE_CHECK(atom_catalog != nullptr);
    if (atom_catalog != nullptr) {
        MIRAGE_CHECK(atom_catalog->tools.size() == 2);
        if (atom_catalog->tools.size() == 2) {
            MIRAGE_CHECK(atom_catalog->tools[0].wire_name == "desktop.filesystem.read_text");
            MIRAGE_CHECK(atom_catalog->tools[1].wire_name == "desktop.process.execute");
            MIRAGE_CHECK(atom_catalog->tools[1].has_side_effects);
            const auto schema = mira::parse_json(atom_catalog->tools[1].parameters_schema_json);
            MIRAGE_CHECK(schema.has_value() && schema.value().is_object());
        }
    }

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

// --- DEC-094 request-timeout semantics (REUSE-09 Phase 1) --------------------

void scenario_request_timeout_keeps_session_alive() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    // Connected, but no run() loop yet: the request sits in the outbox and
    // the caller's 300 ms deadline fires. DEC-094 Phase 1: the call resolves
    // "unavailable"/"timed out", the session goes SyncStale (the transport is
    // not closed), and the queued frame is tombstoned — it must never reach
    // the wire as an orphan.
    Session session;
    session.client = std::make_unique<ipc::SessionClient>(config.socket_path);
    std::string connect_error;
    MIRAGE_CHECK(session.client->connect(kConnectBudget, connect_error));

    const ipc::Response expired =
        session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{300}).get();
    MIRAGE_CHECK(!expired.ok);
    MIRAGE_CHECK(expired.error.code == "unavailable");
    MIRAGE_CHECK(expired.error.message.find("timed out") != std::string::npos);
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::SyncStale);

    // The run() loop starts AFTER the takeover: the tombstoned frame is
    // skipped at flush, so the service never sees the abandoned request and
    // no late response can arrive for it. The session survives the timeout
    // and the single-outstanding discipline is not blocked by the orphaned
    // frame: a fresh call within a normal budget round-trips normally (and
    // its success clears SyncStale back to Healthy).
    session.loop = std::thread([&session] {
        std::string reason;
        session.exit = session.client->run(reason);
        session.diagnostic = std::move(reason);
    });

    const ipc::Response hello = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(hello.ok);
    MIRAGE_CHECK(std::holds_alternative<ipc::ServiceIdentity>(hello.payload));
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::Healthy);

    // Ending the session stays the owner's cooperative decision, not a
    // protocol failure: stop() closes it cleanly with RunExit::Stopped.
    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);

    const ipc::Response shutdown =
        ipc::IpcClient(config.socket_path).call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(shutdown.ok);
}

// --- DEC-094 scripted wire peer ----------------------------------------------
//
// The scenario above starts run() only after the timeout, so it never
// exercises the sweep/takeover race on the live pending table. The scenarios
// below run run() concurrently with call() against a peer that withholds and
// replays answers on cue — control the real RuntimeService cannot offer. They
// extend the protocol tests' transport fixtures (IpcListener plus the
// poll-driven ipc_io.hpp helpers) with exactly that policy; no second
// transport stack.

/// Polls `predicate` until it holds or the budget elapses; returns the final
/// predicate value (a liveness wait, not a latency assertion).
template <typename Predicate> bool wait_for(std::chrono::milliseconds budget, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        if (predicate()) {
            return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
}

/// A scriptable in-process service peer. The reader thread is the only user
/// of the accepted connection: it receives and records requests and also
/// performs the writes the test scripted (responses and events, each leaving
/// the wire a planned delay after registration), so the test thread can stay
/// blocked inside a call() while the peer answers behind its back. RAII: the
/// destructor joins the reader before the streams destruct.
struct ScriptedPeer {
    ipc::IpcListener listener;
    ipc::IpcStream peer;
    std::thread reader;

    std::mutex mutex;
    std::condition_variable requests_changed;
    std::vector<std::uint64_t> requests; ///< request ids in arrival order
    /// Wire journal in service order: "req:<id>" when a request frame
    /// arrived, "resp:<id>" when a scripted answer left the peer. The
    /// overlapped-submission regression reads the relative order (did the
    /// peer serve the withheld answer before the queued frame showed up?).
    std::vector<std::string> journal;

    /// One planned write: a hello-shaped response for `id`, or an event with
    /// `seq` when is_event. The reader fires it once `due` has passed (and,
    /// for responses, once the request itself reached the wire).
    struct Scripted {
        std::uint64_t id = 0;
        std::uint64_t seq = 0;
        bool is_event = false;
        std::chrono::steady_clock::time_point due;
    };
    std::vector<Scripted> script; ///< registered by the test under the mutex
    bool stop_requested = false;
    bool close_requested = false;  ///< the reader thread performs the close
    bool close_on_request = false; ///< close while call() is still blocked

    explicit ScriptedPeer(const std::string &path) {
        std::string diagnostic;
        listener = ipc::IpcListener::bind(path, diagnostic);
    }
    ~ScriptedPeer() {
        {
            std::lock_guard<std::mutex> guard(mutex);
            stop_requested = true;
        }
        if (reader.joinable()) {
            reader.join(); // stops polling before the streams destruct
        }
    }
    ScriptedPeer(const ScriptedPeer &) = delete;
    ScriptedPeer &operator=(const ScriptedPeer &) = delete;

    bool valid() const { return listener.valid(); }

    /// Accepts the client's pending connection (the accept_pending shape of
    /// the protocol tests).
    bool accept_connection() {
        const auto deadline = std::chrono::steady_clock::now() + kConnectBudget;
        for (;;) {
            std::string diagnostic;
            peer = listener.accept(diagnostic);
            if (peer.valid()) {
                return true;
            }
            if (std::chrono::steady_clock::now() >= deadline ||
                !mirage::testing::poll_fd(listener.handle(), POLLIN,
                                          std::chrono::milliseconds{50})) {
                return peer.valid();
            }
        }
    }

    void start_reader() {
        reader = std::thread([this] { read_loop(); });
    }

    /// Plans a hello-shaped response for request `id`, written by the reader
    /// `delay` from now (use a long delay for a genuinely late answer).
    void script_response(std::uint64_t id, std::chrono::milliseconds delay) {
        std::lock_guard<std::mutex> guard(mutex);
        script.push_back(Scripted{id, 0, false, std::chrono::steady_clock::now() + delay});
    }

    /// Plans one host.status event, written by the reader `delay` from now.
    void script_event(std::uint64_t seq, std::chrono::milliseconds delay) {
        std::lock_guard<std::mutex> guard(mutex);
        script.push_back(Scripted{0, seq, true, std::chrono::steady_clock::now() + delay});
    }

    /// Waits until the peer observed request `id` on the wire.
    bool wait_for_request(std::uint64_t id, std::chrono::milliseconds budget) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::unique_lock<std::mutex> lock(mutex);
        const auto present = [&] {
            return std::find(requests.begin(), requests.end(), id) != requests.end();
        };
        while (!present()) {
            if (requests_changed.wait_until(lock, deadline) == std::cv_status::timeout) {
                return present();
            }
        }
        return true;
    }

    void close_on_next_request() {
        std::lock_guard<std::mutex> guard(mutex);
        close_on_request = true;
    }

    /// Snapshot of the observed request ids in arrival order (FIFO evidence).
    std::vector<std::uint64_t> observed_requests() {
        std::lock_guard<std::mutex> guard(mutex);
        return requests;
    }

    /// Position of `entry` in the wire journal; nullopt when never seen.
    std::optional<std::size_t> journal_index(const std::string &entry) {
        std::lock_guard<std::mutex> guard(mutex);
        const auto found = std::find(journal.begin(), journal.end(), entry);
        if (found == journal.end()) {
            return std::nullopt;
        }
        return static_cast<std::size_t>(found - journal.begin());
    }

    /// Asks the reader thread to close the transport (peer-side EOF for the
    /// client); the close runs on the reader's own thread.
    void request_close() {
        std::lock_guard<std::mutex> guard(mutex);
        close_requested = true;
    }

  private:
    bool observed(std::uint64_t id) {
        std::lock_guard<std::mutex> guard(mutex);
        return std::find(requests.begin(), requests.end(), id) != requests.end();
    }

    /// Writes the whole buffer through the non-blocking stream (reader
    /// thread only); false when the peer went away or the deadline elapsed.
    bool write_all(const std::string &data) {
        const auto deadline = std::chrono::steady_clock::now() + kCallBudget;
        std::size_t sent = 0;
        while (sent < data.size()) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;
            }
            const auto remaining =
                std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
            if (!mirage::testing::poll_fd(peer.handle(), POLLOUT, remaining)) {
                return false;
            }
            const ipc::IoResult result = peer.write_some(data.data() + sent, data.size() - sent);
            switch (result.status) {
            case ipc::IoStatus::Ok:
                sent += result.bytes;
                break;
            case ipc::IoStatus::WouldBlock:
                break;
            case ipc::IoStatus::Closed:
            case ipc::IoStatus::Error:
                return false;
            }
        }
        return true;
    }

    bool write_hello_response(std::uint64_t id) {
        ipc::Response response;
        response.ok = true;
        response.id = id;
        ipc::ServiceIdentity identity;
        identity.name = "scripted-peer";
        identity.mirage_version = "0.4.0-test";
        identity.protocol = ipc::kProtocolVersion;
        response.payload = std::move(identity);
        const bool sent = write_all(ipc::make_frame(ipc::encode_response(response)));
        if (sent) {
            std::lock_guard<std::mutex> guard(mutex);
            journal.push_back("resp:" + std::to_string(id));
        }
        return sent;
    }

    bool write_host_status_event(std::uint64_t seq) {
        return write_all(
            ipc::make_frame(ipc::encode_event(ipc::Event{seq, ipc::HostStatusEvent{"running"}})));
    }

    /// Fires every scripted write whose moment came (responses also wait for
    /// their request to reach the wire). Called on the reader thread only.
    void fire_due(std::vector<Scripted> &pending) {
        for (std::size_t index = 0; index < pending.size();) {
            if (std::chrono::steady_clock::now() < pending[index].due) {
                ++index;
                continue;
            }
            if (!pending[index].is_event && !observed(pending[index].id)) {
                ++index; // the request has not reached the wire yet
                continue;
            }
            const bool sent = pending[index].is_event ? write_host_status_event(pending[index].seq)
                                                      : write_hello_response(pending[index].id);
            if (!sent) {
                return;
            }
            pending.erase(pending.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }

    void read_loop() {
        std::string buffer;
        std::vector<Scripted> pending; // reader-local due list
        for (;;) {
            {
                std::lock_guard<std::mutex> guard(mutex);
                if (stop_requested) {
                    return;
                }
                if (close_requested) {
                    peer.close();
                    return;
                }
                pending.insert(pending.end(), script.begin(), script.end());
                script.clear();
            }
            fire_due(pending);
            const mirage::testing::FrameRead read =
                mirage::testing::read_frame(peer, buffer, std::chrono::milliseconds{20});
            if (read.status == mirage::testing::FrameRead::Status::Message) {
                const ipc::RequestDecode decoded = ipc::decode_request(read.message);
                if (decoded.ok) {
                    std::lock_guard<std::mutex> guard(mutex);
                    requests.push_back(decoded.id);
                    journal.push_back("req:" + std::to_string(decoded.id));
                    requests_changed.notify_all();
                    if (close_on_request) {
                        peer.close();
                        return;
                    }
                }
            } else if (read.status == mirage::testing::FrameRead::Status::Closed) {
                return; // the client side went away (owner stop / destruction)
            }
        }
    }
};

// The core sweep/takeover race the pre-existing timeout scenario sidesteps:
// with run() already servicing the session, a caller-side timeout must
// resolve that one call and leave the loop up — the pre-fix deadline sweep
// closed the whole session fail-closed from run()'s thread instead.
void scenario_run_loop_survives_a_live_timeout() {
    mirage::testing::TempDir dir;
    const std::string socket = (dir.root() / "stale.sock").string();
    ScriptedPeer peer(socket);
    MIRAGE_CHECK(peer.valid());

    Session session;
    MIRAGE_CHECK(session.connect(socket));
    session.start_run();
    MIRAGE_CHECK(peer.accept_connection());
    peer.start_reader();

    // The request reaches the wire and the peer withholds its answer; the
    // caller's short deadline fires while run() is live.
    std::future<ipc::Response> expired_future =
        session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{300});
    MIRAGE_CHECK(peer.wait_for_request(1, kCallBudget));

    const ipc::Response expired = expired_future.get();
    MIRAGE_CHECK(!expired.ok);
    MIRAGE_CHECK(expired.error.code == "unavailable");
    MIRAGE_CHECK(expired.error.message.find("timed out") != std::string::npos);
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::SyncStale);
    MIRAGE_CHECK(session.client->connected());
    MIRAGE_CHECK(session.client->last_timeout_reason().find("timed out") != std::string::npos);

    // The pre-fix sweep exited run() within one 10 ms slice of the deadline;
    // a quiet window with the loop still up (and the transport still
    // connected) is the regression evidence.
    MIRAGE_CHECK(
        !wait_for(std::chrono::milliseconds{300}, [&] { return session.run_finished.load(); }));
    MIRAGE_CHECK(session.client->connected());
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::SyncStale);

    // Recovery on the living session: the peer answers the abandoned request
    // late (dropped, not an unexpected-id violation), which re-opens the
    // single-outstanding pipeline; the next call round-trips and the success
    // clears SyncStale back to Healthy. Both answers are scripted because the
    // test thread is what issues the calls (call() blocks on its future).
    // Submit the next call while the abandoned wire request is still
    // unanswered. Its queued frame must survive multiple readiness slices.
    peer.script_response(1, std::chrono::milliseconds{150});
    peer.script_response(2, std::chrono::milliseconds{0});
    std::future<ipc::Response> hello_future =
        session.client->call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(peer.wait_for_request(2, kCallBudget));
    const ipc::Response hello = hello_future.get();
    if (!hello.ok) {
        // Diagnostic only after the acquire edge on run_finished (the loop
        // thread writes `diagnostic` before storing the flag).
        const bool finished = session.run_finished.load();
        std::fprintf(stderr,
                     "[session_client_test] recovery call: %s: %s (run_finished=%d, loop='%s')\n",
                     hello.error.code.c_str(), hello.error.message.c_str(), finished,
                     finished ? session.diagnostic.c_str() : "<still running>");
    }
    MIRAGE_CHECK(hello.ok);
    MIRAGE_CHECK(std::holds_alternative<ipc::ServiceIdentity>(hello.payload));
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::Healthy);

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);
}

// A late answer to an already-abandoned request must be dropped instead of
// killing the session as an unexpected response id, and the event stream must
// keep reaching the sink on the surviving loop.
void scenario_late_response_and_events_keep_the_session() {
    mirage::testing::TempDir dir;
    const std::string socket = (dir.root() / "late.sock").string();
    ScriptedPeer peer(socket);
    MIRAGE_CHECK(peer.valid());

    Session session;
    MIRAGE_CHECK(session.connect(socket));
    std::mutex events_mutex;
    std::vector<ipc::Event> events;
    session.client->set_event_sink([&](const ipc::Event &event) {
        std::lock_guard<std::mutex> guard(events_mutex);
        events.push_back(event);
    });
    session.start_run();
    MIRAGE_CHECK(peer.accept_connection());
    peer.start_reader();

    std::future<ipc::Response> expired_future =
        session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{300});
    MIRAGE_CHECK(peer.wait_for_request(1, kCallBudget));
    const ipc::Response expired = expired_future.get();
    MIRAGE_CHECK(!expired.ok);
    MIRAGE_CHECK(expired.error.code == "unavailable");
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::SyncStale);

    // The answer leaves the wire only after the caller gave up (scripted on
    // the reader thread, since this thread just unblocked from call()).
    peer.script_response(1, std::chrono::milliseconds{0});
    peer.script_event(1, std::chrono::milliseconds{50});

    // The session survives the late response (the pre-DEC-094 discipline
    // treated it as an unexpected id and failed closed right here).
    MIRAGE_CHECK(session.client->connected());
    MIRAGE_CHECK(!session.run_finished.load());

    // Events still flow to the sink on the surviving loop.
    MIRAGE_CHECK(wait_for(kTaskBudget, [&] {
        std::lock_guard<std::mutex> guard(events_mutex);
        return !events.empty();
    }));
    {
        std::lock_guard<std::mutex> guard(events_mutex);
        MIRAGE_CHECK(events.size() == 1);
        if (events.size() == 1) {
            MIRAGE_CHECK(events.front().seq == 1);
            MIRAGE_CHECK(std::holds_alternative<ipc::HostStatusEvent>(events.front().payload));
        }
    }
    // A dropped late answer is not a success: SyncStale stands.
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::SyncStale);

    // The session stays usable: a fresh call round-trips and clears SyncStale.
    peer.script_response(2, std::chrono::milliseconds{0});
    std::future<ipc::Response> hello_future =
        session.client->call(ipc::HelloRequest{}, kCallBudget);
    MIRAGE_CHECK(peer.wait_for_request(2, kCallBudget));
    const ipc::Response hello = hello_future.get();
    MIRAGE_CHECK(hello.ok);
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::Healthy);

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);
}

// DEC-094 narrowed only the request-timeout path: a transport loss mid-call
// still fails closed — the pending call resolves "unavailable" and run()
// surfaces ConnectionLost (complements the service-shutdown scenario, which
// has no call in flight when the peer goes away).
void scenario_midcall_transport_loss_fails_closed() {
    mirage::testing::TempDir dir;
    const std::string socket = (dir.root() / "drop.sock").string();
    ScriptedPeer peer(socket);
    MIRAGE_CHECK(peer.valid());

    Session session;
    MIRAGE_CHECK(session.connect(socket));
    session.start_run();
    MIRAGE_CHECK(peer.accept_connection());
    peer.start_reader();

    // call() itself waits for completion; schedule the EOF on the peer so
    // the transport closes during that wait, rather than after its timeout.
    peer.close_on_next_request();
    const auto lost = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(peer.wait_for_request(1, kCallBudget));
    MIRAGE_CHECK(!lost.ok);
    MIRAGE_CHECK(lost.error.code == "unavailable");
    MIRAGE_CHECK(lost.error.message.find("closed") != std::string::npos);
    MIRAGE_CHECK(lost.error.message.find("timed out") == std::string::npos);

    MIRAGE_CHECK(wait_for(kCallBudget, [&] { return session.run_finished.load(); }));
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::ConnectionLost);
    MIRAGE_CHECK(session.client->health() == ipc::SessionClient::Health::Disconnected);
    MIRAGE_CHECK(!session.client->connected());

    session.join_and_stop();
}

// --- overlapped submission across the in-flight window (DEC-012) -------------
//
// DEC-012 serializes the wire, not the callers: "callers may overlap freely"
// (the call() contract). A frame submitted while another request is in
// flight must survive inside the client until the window closes. Regression:
// run() drained the comm channel into a deque scoped to one readiness
// slice, and since flush_writes consumes nothing while in_flight, every
// slice ended with the frame still queued — the slice-local deque destroyed
// it silently, and the overlapped caller hung until its own timeout against
// a channel that showed empty.

void scenario_overlapped_call_frame_survives_in_flight_window() {
    mirage::testing::TempDir dir;
    const std::string socket = (dir.root() / "overlap.sock").string();
    ScriptedPeer peer(socket);
    MIRAGE_CHECK(peer.valid());

    Session session;
    MIRAGE_CHECK(session.connect(socket));
    session.start_run();
    MIRAGE_CHECK(peer.accept_connection());
    peer.start_reader();

    // Caller 1 goes in flight; the peer accepts the request and withholds
    // the answer (nothing is scripted for it yet).
    std::future<ipc::Response> first_future;
    std::thread first_caller(
        [&] { first_future = session.client->call(ipc::HelloRequest{}, kCallBudget); });
    MIRAGE_CHECK(peer.wait_for_request(1, kCallBudget));

    // Answer request 2 the moment its frame reaches the wire (fire_due's
    // observed() gate holds the script until then).
    peer.script_response(2, std::chrono::milliseconds{0});

    // Caller 2 overlaps while request 1 is still unanswered. It arms the
    // withheld answer for +400 ms right before submitting: dozens of run
    // slices pass first, so on the broken client the drained frame died with
    // its slice before the window ever opened. The scripted wire order is
    // then response 1, (the surviving frame must follow), response 2.
    std::future<ipc::Response> second_future;
    std::thread second_caller([&] {
        peer.script_response(1, std::chrono::milliseconds{400});
        second_future = session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{2000});
    });
    first_caller.join();
    second_caller.join();

    const ipc::Response first = first_future.get();
    MIRAGE_CHECK(first.ok);
    MIRAGE_CHECK(first.id == 1);
    const ipc::Response second = second_future.get();
    if (!second.ok) {
        std::fprintf(stderr, "[session_client_test] overlapped call resolved: %s: %s\n",
                     second.error.code.c_str(), second.error.message.c_str());
    }
    MIRAGE_CHECK(second.ok);      // the frame survived: no timeout takeover
    MIRAGE_CHECK(second.id == 2); // responses correlate with their callers

    // The peer served the withheld answer before the queued frame showed
    // up: FIFO across the in-flight window, not a destroyed submission.
    const std::optional<std::size_t> answered = peer.journal_index("resp:1");
    const std::optional<std::size_t> replayed = peer.journal_index("req:2");
    MIRAGE_CHECK(answered.has_value());
    MIRAGE_CHECK(replayed.has_value());
    MIRAGE_CHECK(!answered.has_value() || !replayed.has_value() || *answered < *replayed);

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);
}

// The same overlap eight rounds back to back: every round submits call 2
// while call 1 is in flight, and no frame is lost across the sliding window.
void scenario_repeated_overlapped_rounds_lose_no_frames() {
    mirage::testing::TempDir dir;
    const std::string socket = (dir.root() / "overlap-8.sock").string();
    ScriptedPeer peer(socket);
    MIRAGE_CHECK(peer.valid());

    Session session;
    MIRAGE_CHECK(session.connect(socket));
    session.start_run();
    MIRAGE_CHECK(peer.accept_connection());
    peer.start_reader();

    constexpr int kRounds = 8;
    for (int round = 0; round < kRounds; ++round) {
        const auto first_id = static_cast<std::uint64_t>(2 * round + 1);
        const auto second_id = first_id + 1;

        std::future<ipc::Response> first_future;
        std::thread first_caller([&] {
            first_future =
                session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{5000});
        });
        MIRAGE_CHECK(peer.wait_for_request(first_id, std::chrono::milliseconds{5000}));
        peer.script_response(second_id, std::chrono::milliseconds{0});

        std::future<ipc::Response> second_future;
        std::thread second_caller([&] {
            peer.script_response(first_id, std::chrono::milliseconds{400});
            second_future =
                session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{5000});
        });
        first_caller.join();
        second_caller.join();

        const ipc::Response first = first_future.get();
        MIRAGE_CHECK(first.ok);
        MIRAGE_CHECK(first.id == first_id);
        const ipc::Response second = second_future.get();
        MIRAGE_CHECK(second.ok);
        MIRAGE_CHECK(second.id == second_id);
    }

    // Nothing was dropped along the way: all sixteen requests arrived, in
    // issue order.
    std::vector<std::uint64_t> expected;
    for (int id = 1; id <= 2 * kRounds; ++id) {
        expected.push_back(static_cast<std::uint64_t>(id));
    }
    MIRAGE_CHECK(peer.observed_requests() == expected);

    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);
}

// --- fail-closed paths -------------------------------------------------------

// An unserviced session must reject overload instead of accumulating timed-out
// calls indefinitely. Stopping it still completes the owner lifecycle.
void scenario_outbound_budget_is_bounded() {
    mirage::testing::TempDir dir;
    ScriptedPeer peer((dir.root() / "budget.sock").string());
    MIRAGE_CHECK(peer.valid());
    Session session;
    MIRAGE_CHECK(session.connect((dir.root() / "budget.sock").string()));
    for (int index = 0; index < 16; ++index) {
        const auto expired =
            session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{2}).get();
        MIRAGE_CHECK(!expired.ok);
        MIRAGE_CHECK(expired.error.message.find("timed out") != std::string::npos);
    }
    const auto refused = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "unavailable");
    MIRAGE_CHECK(refused.error.message.find("budget exhausted") != std::string::npos);
    session.client->stop();
    session.start_run();
    session.join_and_stop();
    MIRAGE_CHECK(session.exit == ipc::SessionClient::RunExit::Stopped);
    MIRAGE_CHECK(!session.client->connected());
    const auto closed = session.client->call(ipc::HelloRequest{}, kCallBudget).get();
    MIRAGE_CHECK(!closed.ok);
    MIRAGE_CHECK(closed.error.code == "unavailable");
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
    run_scenario("request_timeout_keeps_session_alive",
                 scenario_request_timeout_keeps_session_alive);
    run_scenario("run_loop_survives_a_live_timeout", scenario_run_loop_survives_a_live_timeout);
    run_scenario("late_response_and_events_keep_the_session",
                 scenario_late_response_and_events_keep_the_session);
    run_scenario("midcall_transport_loss_fails_closed",
                 scenario_midcall_transport_loss_fails_closed);
    run_scenario("outbound_budget_is_bounded", scenario_outbound_budget_is_bounded);
    run_scenario("overlapped_call_frame_survives_in_flight_window",
                 scenario_overlapped_call_frame_survives_in_flight_window);
    run_scenario("repeated_overlapped_rounds_lose_no_frames",
                 scenario_repeated_overlapped_rounds_lose_no_frames);
    run_scenario("service_shutdown_loses_the_session", scenario_service_shutdown_loses_the_session);
    run_scenario("connect_failure_is_bounded", scenario_connect_failure_is_bounded);
    run_scenario("workflow_face_round_trip", scenario_workflow_face_round_trip);
    return mirage::testing::finish("session_client_test");
}
