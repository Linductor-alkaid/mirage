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
    while (!terminal) {
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        std::vector<ipc::Event> snapshot;
        {
            std::lock_guard<std::mutex> guard(events_mutex);
            snapshot = events;
        }
        for (const ipc::Event &event : snapshot) {
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

// --- fail-closed paths -------------------------------------------------------

void scenario_timeout_closes_session_fail_closed() {
    mirage::testing::TempDir dir;
    const mirage::runtime::ServiceConfig config = make_config(dir);
    ServiceProcess service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)));
    service.run_async();

    Session session;
    MIRAGE_CHECK(session.start(config.socket_path));

    // Connected, but run() is deliberately not consuming: the request sits
    // unserved, the call expires, and the frozen single-outstanding
    // discipline closes the session fail-closed (no issue-after-silence).
    const ipc::Response expired =
        session.client->call(ipc::HelloRequest{}, std::chrono::milliseconds{300}).get();
    MIRAGE_CHECK(!expired.ok);
    MIRAGE_CHECK(expired.error.code == "unavailable");
    MIRAGE_CHECK(expired.error.message.find("timed out") != std::string::npos);

    // The loop drains with the fail-closed close; run() reports the cause.
    session.join_and_stop();
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
    return mirage::testing::finish("session_client_test");
}
