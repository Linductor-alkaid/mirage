// M1-05 cancellation path verification (independent verification pass).
// Covers the service-level task.cancel surface end to end over the local IPC:
// cancelling a task mid desktop-action (step cancelled, remainder skipped,
// pinned runtime settles Cancelled, whole process group torn down), unknown
// ids (not_found), already terminal tasks (invalid_state, terminal state is
// never revived) and the interaction of cancellation with a bounded clean
// shutdown. Protocol-level codec coverage for task.cancel / TaskCancelled
// lives in ipc_protocol_test.

#include "../support/ipc_io.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <signal.h>
#include <unistd.h>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::runtime::ServiceRunReport;
using mirage::testing::TempDir;
using mirage::testing::unique_token;

// Liveness guard against hangs, not a latency assertion: under parallel ctest
// CPU oversubscription the two service workers can lag several seconds behind
// (M2-06 investigation: 5 s produced rare false 'unavailable' under load while
// isolated runs answer in milliseconds), so the wait budget is generous.
constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr auto kTaskBudget = std::chrono::seconds{10};
/// Cancellation is observed by the provider within its 25 ms poll slice; the
/// settlement and inspect round trip must stay far inside this bound.
constexpr auto kCancelConvergence = std::chrono::seconds{2};

ServiceConfig make_config(const TempDir &dir, std::chrono::milliseconds step_timeout) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "cancel-svc.sock").string();
    config.mirage_version = "0.5.0-cancel-test";
    config.executor_threads = 2;
    config.step_timeout = step_timeout;
    // Recovery state stays inside the scenario's temp tree (M1-07): the
    // default XDG state directory must stay untouched and foreign history
    // must not hydrate into these scenarios.
    config.recovery_directory = dir.root() / "recovery";
    return config;
}

/// Service binding whose read scope is the test's temp directory (M1-05).
std::shared_ptr<integration::MiraEnvironmentBinding> make_binding(const TempDir &dir) {
    return std::make_shared<integration::MiraEnvironmentBinding>(
        std::make_shared<linux_backend::LinuxDesktopEnvironment>(
            std::vector<std::filesystem::path>{dir.root()}));
}

std::optional<std::string> submit_task(ipc::IpcClient &client, ipc::SubmitTaskRequest request) {
    const ipc::Response response = client.call(request, kCallBudget);
    const auto *submitted = std::get_if<ipc::TaskSubmitted>(&response.payload);
    if (!response.ok || submitted == nullptr) {
        return std::nullopt;
    }
    return submitted->task_id;
}

std::optional<ipc::InspectTask> inspect_once(const std::string &socket_path,
                                             const std::string &task_id) {
    ipc::IpcClient client(socket_path);
    const ipc::Response response = client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
    const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
    if (!response.ok || inspect == nullptr) {
        return std::nullopt;
    }
    return *inspect;
}

/// Polls task.inspect until `predicate` holds; std::nullopt on timeout.
template <typename Predicate>
std::optional<ipc::InspectTask> wait_for(const std::string &socket_path, const std::string &task_id,
                                         std::chrono::milliseconds budget, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    for (;;) {
        const auto view = inspect_once(socket_path, task_id);
        if (view && predicate(*view)) {
            return view;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
    }
}

std::optional<ipc::InspectTask> wait_terminal(const std::string &socket_path,
                                              const std::string &task_id,
                                              std::chrono::milliseconds budget = kTaskBudget) {
    return wait_for(socket_path, task_id, budget, [](const ipc::InspectTask &view) {
        return view.progress == "Completed" || view.progress == "Failed" ||
               view.progress == "Cancelled";
    });
}

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::FILE *file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) {
        return;
    }
    std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
}

/// True when the process does not exist anymore (reaped or never born).
bool process_is_gone(pid_t pid, std::chrono::milliseconds grace) {
    const auto deadline = std::chrono::steady_clock::now() + grace;
    while (std::chrono::steady_clock::now() < deadline) {
        if (::kill(pid, 0) != 0 && errno == ESRCH) {
            return true;
        }
        ::usleep(20000);
    }
    return ::kill(pid, 0) != 0 && errno == ESRCH;
}

std::vector<pid_t> read_pid_file(const std::filesystem::path &path) {
    std::vector<pid_t> pids;
    std::ifstream stream(path);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty()) {
            pids.push_back(std::stoi(line));
        }
    }
    return pids;
}

// --- cancelling a running task ----------------------------------------------

void scenario_cancel_running_task_mid_action() {
    TempDir dir;
    // A 60 s step budget: only the cancel path can end the sleep in time; a
    // timeout can never mask a broken cancellation inside this test's budget.
    const ServiceConfig config = make_config(dir, std::chrono::milliseconds{60000});
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = unique_token();
    const auto pid_file = dir.root() / ("pids-" + token);
    const auto canary = dir.root() / ("canary-" + token);
    const auto readable = dir.root() / ("readable-" + token + ".txt");
    write_text_file(readable, "in-scope content\n");

    // Step 0 forks a live process group (shell + background sleep) and waits;
    // step 1 would create a canary file; step 2 would read a file. The
    // cancellation must interrupt step 0, skip the rest and create nothing.
    ipc::SubmitTaskRequest request;
    request.goal = "long task cancelled mid action";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "echo $$ > '" + pid_file.string() +
                                                                "'; sleep 30 & echo $! >> '" +
                                                                pid_file.string() + "'; wait"});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "touch '" + canary.string() + "'"});
    request.steps.push_back({ipc::StepKind::FilesystemRead, readable.string()});

    ipc::IpcClient client(config.socket_path);
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }

    // Wait until the driver entered step 0 and the shell is really alive.
    const auto running =
        wait_for(config.socket_path, *task_id, kTaskBudget, [](const ipc::InspectTask &view) {
            return !view.steps.empty() && view.steps[0].status == "running";
        });
    MIRAGE_CHECK(running.has_value());
    if (!running) {
        service.request_shutdown();
        service.run();
        return;
    }
    const auto live =
        wait_for(config.socket_path, *task_id, kCallBudget, [&pid_file](const ipc::InspectTask &) {
            return std::filesystem::exists(pid_file);
        });
    MIRAGE_CHECK(live.has_value());

    // Cancel over IPC: acknowledged with the task id and a sensible progress.
    const ipc::Response cancel_response =
        client.call(ipc::CancelTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(cancel_response.ok);
    const auto *acknowledged = std::get_if<ipc::TaskCancelled>(&cancel_response.payload);
    MIRAGE_CHECK(acknowledged != nullptr);
    if (acknowledged != nullptr) {
        MIRAGE_CHECK(acknowledged->task_id == *task_id);
        MIRAGE_CHECK(acknowledged->progress == "Cancelling" ||
                     acknowledged->progress == "Cancelled");
    }

    // The task settles Cancelled promptly AND the driver's step bookkeeping
    // converges to the same picture: the interrupted step is cancelled, the
    // rest skipped. The pinned progress can flip a poll slice before the
    // driver finishes marking steps, so wait for the full converged shape.
    const auto started = std::chrono::steady_clock::now();
    const auto done =
        wait_for(config.socket_path, *task_id, kCallBudget, [](const ipc::InspectTask &view) {
            return view.progress == "Cancelled" && view.steps.size() == 3 &&
                   view.steps[0].status == "cancelled" && view.steps[1].status == "skipped" &&
                   view.steps[2].status == "skipped";
        });
    const auto convergence = std::chrono::steady_clock::now() - started;
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        service.request_shutdown();
        service.run();
        return;
    }
    MIRAGE_CHECK(done->progress == "Cancelled");
    MIRAGE_CHECK(convergence < kCancelConvergence);

    // Step states: the interrupted action is cancelled, the rest skipped.
    MIRAGE_CHECK(done->steps.size() == 3);
    if (done->steps.size() == 3) {
        MIRAGE_CHECK(done->steps[0].status == "cancelled");
        MIRAGE_CHECK(!done->steps[0].ok);
        MIRAGE_CHECK(done->steps[1].status == "skipped");
        MIRAGE_CHECK(done->steps[2].status == "skipped");
    }

    // No side effects from the skipped steps, no survivors from the group.
    MIRAGE_CHECK(!std::filesystem::exists(canary));
    const auto pids = read_pid_file(pid_file);
    MIRAGE_CHECK(pids.size() == 2);
    for (const pid_t pid : pids) {
        if (!process_is_gone(pid, std::chrono::milliseconds{2000})) {
            std::fprintf(stderr, "[task_cancel_test] sleep survivor after task.cancel: pid=%d\n",
                         pid);
        }
        MIRAGE_CHECK(process_is_gone(pid, std::chrono::milliseconds{2000}));
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- refusal paths ----------------------------------------------------------

void scenario_cancel_unknown_task_id_is_not_found() {
    TempDir dir;
    const ServiceConfig config = make_config(dir, std::chrono::milliseconds{10000});
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    const ipc::Response response =
        client.call(ipc::CancelTaskRequest{"00000000000000000000000000000000"}, kCallBudget);
    MIRAGE_CHECK(!response.ok);
    MIRAGE_CHECK(response.error.code == "not_found");
    MIRAGE_CHECK(!response.error.message.empty());

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_cancel_completed_task_is_invalid_state_and_stays_terminal() {
    TempDir dir;
    const ServiceConfig config = make_config(dir, std::chrono::milliseconds{10000});
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "task finished before anyone cancels it";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "printf done"});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    const auto completed = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(completed.has_value() && completed->progress == "Completed");
    if (!completed || completed->progress != "Completed") {
        service.request_shutdown();
        service.run();
        return;
    }

    // The pinned runtime owns the terminal state: the cancel is refused and
    // the host surfaces the pinned rejection verbatim (code "pinned_runtime",
    // the pinned invalid_state diagnosis inside the message). Nothing is
    // revived.
    const ipc::Response cancel_response =
        client.call(ipc::CancelTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(!cancel_response.ok);
    MIRAGE_CHECK(cancel_response.error.code == "pinned_runtime");
    MIRAGE_CHECK(cancel_response.error.message.find("invalid_state") != std::string::npos);

    const auto after = inspect_once(config.socket_path, *task_id);
    MIRAGE_CHECK(after.has_value());
    if (after) {
        MIRAGE_CHECK(after->progress == "Completed");
        MIRAGE_CHECK(after->has_success);
        MIRAGE_CHECK(after->success);
        MIRAGE_CHECK(after->steps.size() == 1 && after->steps[0].status == "ok");
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

// --- cancellation and shutdown -----------------------------------------------

void scenario_cancel_inflight_then_shutdown_is_bounded_and_clean() {
    TempDir dir;
    const ServiceConfig config = make_config(dir, std::chrono::milliseconds{10000});
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "in-flight task cancelled, then the service shuts down";
    request.steps.push_back({ipc::StepKind::ProcessExecute, "sleep 30"});

    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return;
    }
    // Let the driver enter the sleep step, then cancel it.
    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    const ipc::Response cancel_response =
        client.call(ipc::CancelTaskRequest{*task_id}, kCallBudget);
    MIRAGE_CHECK(cancel_response.ok);

    const auto done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value() && done->progress == "Cancelled");
    if (!done) {
        service.request_shutdown();
        service.run();
        return;
    }

    // With no task in flight the ordered shutdown converges promptly.
    const auto started = std::chrono::steady_clock::now();
    const ipc::Response ack = client.call(ipc::ShutdownRequest{}, kCallBudget);
    MIRAGE_CHECK(ack.ok);
    MIRAGE_CHECK(std::holds_alternative<ipc::ShutdownAccepted>(ack.payload));

    const ServiceRunReport report = service.run();
    const auto elapsed = std::chrono::steady_clock::now() - started;
    MIRAGE_CHECK(elapsed < std::chrono::seconds{10});
    MIRAGE_CHECK(report.clean);
    MIRAGE_CHECK(report.host_shutdown.clean);
    MIRAGE_CHECK(!std::filesystem::exists(config.socket_path));
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[task_cancel_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("cancel_running_task_mid_action", scenario_cancel_running_task_mid_action);
    run_scenario("cancel_unknown_task_id_is_not_found",
                 scenario_cancel_unknown_task_id_is_not_found);
    run_scenario("cancel_completed_task_is_invalid_state_and_stays_terminal",
                 scenario_cancel_completed_task_is_invalid_state_and_stays_terminal);
    run_scenario("cancel_inflight_then_shutdown_is_bounded_and_clean",
                 scenario_cancel_inflight_then_shutdown_is_bounded_and_clean);
    return mirage::testing::finish("task_cancel_test");
}
