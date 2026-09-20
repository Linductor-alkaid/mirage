// M1-07 recovery service integration verification (DEC-011, independent
// verification pass). Drives real RuntimeService instances over the Local
// IPC surface and checks the recovery loop end to end: a settled task's
// terminal record reaches the recovery file (progress, success bits, step
// statuses, operation ids and the filesystem.read result), a restart over
// the same socket and state directory hydrates that history back into the
// registry (task.list / task.inspect parity, task.cancel refused with
// invalid_state), hydrated and live tasks coexist, a corrupt or alien-schema
// or oversized recovery file degrades to a loud no-recovery start while the
// service keeps serving, a disabled writer leaves no file behind, and an
// unusable recovery directory never takes the service down.

#include "../support/ipc_io.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/ipc/client.hpp>
#include <mirage/runtime/ipc/protocol.hpp>
#include <mirage/runtime/persistence/recovery.hpp>
#include <mirage/runtime/runtime_service.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace ipc = mirage::runtime::ipc;
namespace persistence = mirage::runtime::persistence;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::RuntimeService;
using mirage::runtime::ServiceConfig;
using mirage::runtime::ServiceRunReport;
using mirage::testing::TempDir;

// Liveness guard against hangs, not a latency assertion: under parallel ctest
// CPU oversubscription the two service workers can lag several seconds behind
// (M2-06 investigation: 5 s produced rare false 'unavailable' under load while
// isolated runs answer in milliseconds), so the wait budget is generous.
constexpr auto kCallBudget = std::chrono::seconds{30};
constexpr auto kTaskBudget = std::chrono::seconds{10};

ServiceConfig make_config(const TempDir &dir) {
    ServiceConfig config;
    config.socket_path = (dir.root() / "svc.sock").string();
    config.mirage_version = "0.7.0-recovery-test";
    config.executor_threads = 2;
    config.step_timeout = std::chrono::milliseconds{10000};
    // The recovery file lives inside the test's own temp tree: no test may
    // touch the user's real XDG state directory (DEC-011 isolation).
    config.recovery_directory = dir.root() / "recovery";
    return config;
}

std::shared_ptr<integration::MiraEnvironmentBinding> make_binding(const TempDir &dir) {
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

std::filesystem::path recovery_file(const ServiceConfig &config) {
    return config.recovery_directory / "task-recovery.json";
}

std::optional<persistence::RecoveryState> load_recovery(const ServiceConfig &config) {
    std::FILE *file = std::fopen(recovery_file(config).c_str(), "rb");
    if (file == nullptr) {
        return std::nullopt;
    }
    std::string body;
    char chunk[4096];
    std::size_t read = 0;
    while ((read = std::fread(chunk, 1, sizeof(chunk), file)) > 0) {
        body.append(chunk, read);
    }
    std::fclose(file);
    const persistence::RecoveryDecode decoded = persistence::decode_recovery(body);
    if (!decoded.ok) {
        return std::nullopt;
    }
    return decoded.state;
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

std::optional<ipc::TaskList> list_once(const std::string &socket_path) {
    ipc::IpcClient client(socket_path);
    const ipc::Response response = client.call(ipc::ListTasksRequest{}, kCallBudget);
    const auto *list = std::get_if<ipc::TaskList>(&response.payload);
    if (!response.ok || list == nullptr) {
        return std::nullopt;
    }
    return *list;
}

/// Polls task.inspect until the task reports a terminal progress state.
std::optional<ipc::InspectTask> wait_terminal(const std::string &socket_path,
                                              const std::string &task_id) {
    ipc::IpcClient client(socket_path);
    const auto deadline = std::chrono::steady_clock::now() + kTaskBudget;
    for (;;) {
        const ipc::Response response = client.call(ipc::InspectTaskRequest{task_id}, kCallBudget);
        const auto *inspect = std::get_if<ipc::InspectTask>(&response.payload);
        if (inspect != nullptr) {
            if (inspect->progress == "Completed" || inspect->progress == "Failed" ||
                inspect->progress == "Cancelled") {
                return *inspect;
            }
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return std::nullopt;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{25});
    }
}

/// Runs one read+execute task to completion and returns its terminal view.
/// The read step must carry the fixture content back and the execute step
/// must echo the marker, so every caller gets a genuinely verified task.
std::optional<ipc::InspectTask>
run_read_exec_task(const ServiceConfig &config, const std::filesystem::path &goal_file,
                   const std::string &content, const std::string &marker, const std::string &goal) {
    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = goal;
    request.steps.push_back({ipc::StepKind::FilesystemRead, goal_file.string()});
    request.steps.push_back({ipc::StepKind::ProcessExecute, "printf '" + marker + "'"});
    const std::optional<std::string> task_id = submit_task(client, request);
    MIRAGE_CHECK(task_id.has_value());
    if (!task_id) {
        return std::nullopt;
    }
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(*task_id));
    const std::optional<ipc::InspectTask> done = wait_terminal(config.socket_path, *task_id);
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        return std::nullopt;
    }
    MIRAGE_CHECK(done->progress == "Completed");
    MIRAGE_CHECK(done->id == *task_id);
    MIRAGE_CHECK(done->steps.size() == 2);
    if (done->steps.size() == 2) {
        MIRAGE_CHECK(done->steps[0].status == "ok");
        MIRAGE_CHECK(done->steps[0].result == content);
        MIRAGE_CHECK(done->steps[1].status == "ok");
        MIRAGE_CHECK(done->steps[1].exit_code == 0);
        MIRAGE_CHECK(done->steps[1].result == marker);
    }
    return done;
}

void check_steps_match(const ipc::InspectTask &expected, const ipc::InspectTask &actual) {
    MIRAGE_CHECK(actual.steps.size() == expected.steps.size());
    if (actual.steps.size() != expected.steps.size()) {
        return;
    }
    for (std::size_t index = 0; index < expected.steps.size(); ++index) {
        const ipc::StepView &before = expected.steps[index];
        const ipc::StepView &after = actual.steps[index];
        MIRAGE_CHECK(after.index == before.index);
        MIRAGE_CHECK(after.kind == before.kind);
        MIRAGE_CHECK(after.status == before.status);
        MIRAGE_CHECK(after.operation_id == before.operation_id);
        MIRAGE_CHECK(after.permission == before.permission);
        MIRAGE_CHECK(after.ok == before.ok);
        MIRAGE_CHECK(after.exit_code == before.exit_code);
        MIRAGE_CHECK(after.result == before.result);
        MIRAGE_CHECK(after.error == before.error);
    }
}

bool list_contains(const ipc::TaskList &list, const std::string &id, const std::string &progress) {
    for (const ipc::TaskSummary &summary : list.tasks) {
        if (summary.id == id) {
            return summary.progress == progress;
        }
    }
    return false;
}

// --- scenarios ---------------------------------------------------------------

void scenario_settlement_persists_terminal_snapshot() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "recovery snapshot " + token + "\n";
    write_text_file(goal_file, content);
    const std::string marker = "persist-marker-" + token;

    const std::optional<ipc::InspectTask> done = run_read_exec_task(
        config, goal_file, content, marker, "read the goal file and echo the marker");
    MIRAGE_CHECK(done.has_value());
    if (!done) {
        service.request_shutdown();
        (void)service.run();
        return;
    }

    // Ordered teardown runs the final persist after the drivers drained.
    service.request_shutdown();
    const ServiceRunReport report = service.run();
    MIRAGE_CHECK(report.clean);
    MIRAGE_CHECK(std::filesystem::exists(recovery_file(config)));

    const std::optional<persistence::RecoveryState> state = load_recovery(config);
    MIRAGE_CHECK(state.has_value());
    if (!state) {
        return;
    }
    MIRAGE_CHECK(state->schema == persistence::kRecoverySchema);
    MIRAGE_CHECK(state->mirage_version == "0.7.0-recovery-test");
    MIRAGE_CHECK(state->saved_at.size() == 20); // ISO-8601 UTC, informational
    MIRAGE_CHECK(state->tasks.size() == 1);
    if (state->tasks.size() != 1) {
        return;
    }
    const persistence::RecoveryTask &task = state->tasks[0];
    MIRAGE_CHECK(task.id == done->id);
    MIRAGE_CHECK(task.goal == done->goal);
    MIRAGE_CHECK(task.progress == "Completed");
    MIRAGE_CHECK(task.has_success);
    MIRAGE_CHECK(task.success);
    MIRAGE_CHECK(task.steps.size() == 2);
    if (task.steps.size() != 2) {
        return;
    }
    // The filesystem.read step carried its full result content into the
    // snapshot, exactly as task.inspect reported it before the restart.
    MIRAGE_CHECK(task.steps[0].kind == "filesystem.read");
    MIRAGE_CHECK(task.steps[0].argument == goal_file.string());
    MIRAGE_CHECK(task.steps[0].status == "ok");
    MIRAGE_CHECK(task.steps[0].ok);
    MIRAGE_CHECK(mirage::testing::is_32_lowercase_hex(task.steps[0].operation_id));
    MIRAGE_CHECK(task.steps[0].operation_id == done->steps[0].operation_id);
    MIRAGE_CHECK(task.steps[0].permission == "allowed");
    MIRAGE_CHECK(task.steps[0].result == content);
    MIRAGE_CHECK(task.steps[1].kind == "process.execute");
    MIRAGE_CHECK(task.steps[1].status == "ok");
    MIRAGE_CHECK(task.steps[1].exit_code == 0);
    MIRAGE_CHECK(task.steps[1].result == marker);
}

void scenario_restart_hydrates_history_and_refuses_cancel() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "history hydration " + token + "\n";
    write_text_file(goal_file, content);
    const std::string marker = "hydrate-marker-" + token;
    const std::string goal = "remember me across the restart";

    // First era: settle one task and stop, keeping its terminal view.
    std::string task_id;
    std::optional<ipc::InspectTask> before;
    {
        RuntimeService service(config);
        MIRAGE_CHECK(service.start(make_binding(dir)).ok);
        before = run_read_exec_task(config, goal_file, content, marker, goal);
        MIRAGE_CHECK(before.has_value());
        if (!before) {
            return;
        }
        task_id = before->id;
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
    }

    // Second era over the same socket and state directory: the settled task
    // is part of task.list again and inspects exactly like it did before.
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::optional<ipc::TaskList> list = list_once(config.socket_path);
    MIRAGE_CHECK(list.has_value());
    if (list) {
        MIRAGE_CHECK(list->tasks.size() == 1);
        MIRAGE_CHECK(list_contains(*list, task_id, "Completed"));
    }

    const std::optional<ipc::InspectTask> historical = inspect_once(config.socket_path, task_id);
    MIRAGE_CHECK(historical.has_value());
    if (!historical) {
        service.request_shutdown();
        (void)service.run();
        return;
    }
    MIRAGE_CHECK(historical->goal == goal);
    MIRAGE_CHECK(historical->progress == "Completed");
    MIRAGE_CHECK(historical->has_success);
    MIRAGE_CHECK(historical->success);
    // Step-for-step parity with the pre-restart view: kinds, statuses,
    // operation ids, permissions, results.
    check_steps_match(*before, *historical);

    // The history belongs to a previous service era: cancelling it is an
    // invalid-state rejection, not a pinned round trip.
    ipc::IpcClient client(config.socket_path);
    const ipc::Response cancelled = client.call(ipc::CancelTaskRequest{task_id}, kCallBudget);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.error.code == "invalid_state");
    MIRAGE_CHECK(cancelled.error.message.find("previous service run") != std::string::npos);

    // The refusal changed nothing: the record stays settled and inspectable.
    const std::optional<ipc::InspectTask> after_cancel = inspect_once(config.socket_path, task_id);
    MIRAGE_CHECK(after_cancel.has_value() && after_cancel->progress == "Completed");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_hydrated_and_live_tasks_coexist() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "coexistence " + token + "\n";
    write_text_file(goal_file, content);

    // First era: one completed task.
    std::string historical_id;
    {
        RuntimeService service(config);
        MIRAGE_CHECK(service.start(make_binding(dir)).ok);
        const std::optional<ipc::InspectTask> done =
            run_read_exec_task(config, goal_file, content, "coexist-" + token, "first era task");
        MIRAGE_CHECK(done.has_value());
        if (!done) {
            return;
        }
        historical_id = done->id;
        service.request_shutdown();
        MIRAGE_CHECK(service.run().clean);
    }

    // Second era: the historical task is visible and a fresh task settles
    // alongside it.
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::optional<ipc::TaskList> before = list_once(config.socket_path);
    MIRAGE_CHECK(before.has_value() && before->tasks.size() == 1);
    if (before) {
        MIRAGE_CHECK(list_contains(*before, historical_id, "Completed"));
    }

    const std::optional<ipc::InspectTask> fresh =
        run_read_exec_task(config, goal_file, content, "fresh-" + token, "second era task");
    MIRAGE_CHECK(fresh.has_value() && fresh->progress == "Completed");
    if (!fresh) {
        service.request_shutdown();
        (void)service.run();
        return;
    }

    const std::optional<ipc::TaskList> after = list_once(config.socket_path);
    MIRAGE_CHECK(after.has_value());
    if (after) {
        MIRAGE_CHECK(after->tasks.size() == 2);
        MIRAGE_CHECK(list_contains(*after, historical_id, "Completed"));
        MIRAGE_CHECK(list_contains(*after, fresh->id, "Completed"));
    }

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);

    // The final snapshot of the second era carries both terminal records.
    const std::optional<persistence::RecoveryState> state = load_recovery(config);
    MIRAGE_CHECK(state.has_value());
    if (state) {
        MIRAGE_CHECK(state->tasks.size() == 2);
    }
}

void scenario_corrupt_recovery_file_degrades_then_recovers() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    std::filesystem::create_directories(config.recovery_directory);
    write_text_file(recovery_file(config), "not json");

    // The corrupt file must not take the service down: it starts without
    // recovery (a warning lands on stderr), serves normally, and the next
    // settlement snapshot replaces the unusable content.
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::optional<ipc::TaskList> list = list_once(config.socket_path);
    MIRAGE_CHECK(list.has_value() && list->tasks.empty());

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "after corruption " + token + "\n";
    write_text_file(goal_file, content);
    const std::optional<ipc::InspectTask> done = run_read_exec_task(
        config, goal_file, content, "corrupt-" + token, "task after a corrupt recovery file");
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);

    // The file is a valid snapshot again.
    const std::optional<persistence::RecoveryState> state = load_recovery(config);
    MIRAGE_CHECK(state.has_value());
    if (state) {
        MIRAGE_CHECK(state->tasks.size() == 1);
        if (state->tasks.size() == 1) {
            MIRAGE_CHECK(state->tasks[0].id == done->id);
            MIRAGE_CHECK(state->tasks[0].progress == "Completed");
        }
    }
}

void scenario_disabled_persistence_leaves_no_file() {
    TempDir dir;
    ServiceConfig config = make_config(dir);
    config.persist_recovery_state = false;
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "memory only " + token + "\n";
    write_text_file(goal_file, content);
    const std::optional<ipc::InspectTask> done =
        run_read_exec_task(config, goal_file, content, "off-" + token, "memory-only task");
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
    // A memory-only registry persists nothing: no file, no state directory.
    MIRAGE_CHECK(!std::filesystem::exists(recovery_file(config)));
}

void scenario_alien_schema_file_starts_without_recovery() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    std::filesystem::create_directories(config.recovery_directory);
    write_text_file(recovery_file(config), R"({"schema":99,"tasks":[]})");

    // An unknown schema is a loud no-recovery start, not a failure and not a
    // silent adoption of unknown records.
    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::optional<ipc::TaskList> list = list_once(config.socket_path);
    MIRAGE_CHECK(list.has_value() && list->tasks.empty());

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "schema drift " + token + "\n";
    write_text_file(goal_file, content);
    const std::optional<ipc::InspectTask> done = run_read_exec_task(
        config, goal_file, content, "schema-" + token, "task after an alien recovery schema");
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);

    // The next settlement wrote a snapshot in the schema this build knows.
    const std::optional<persistence::RecoveryState> state = load_recovery(config);
    MIRAGE_CHECK(state.has_value());
    if (state) {
        MIRAGE_CHECK(state->schema == persistence::kRecoverySchema);
        MIRAGE_CHECK(state->tasks.size() == 1);
    }
}

void scenario_oversized_recovery_file_starts_without_recovery() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    std::filesystem::create_directories(config.recovery_directory);
    // One byte beyond the 4 MiB document budget (RULE-07: never truncated).
    write_text_file(recovery_file(config),
                    std::string(persistence::kMaxRecoveryFileBytes + 1, 'p'));

    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::optional<ipc::TaskList> list = list_once(config.socket_path);
    MIRAGE_CHECK(list.has_value() && list->tasks.empty());

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void scenario_unusable_recovery_directory_still_serves() {
    TempDir dir;
    ServiceConfig config = make_config(dir);
    // A regular file where the recovery directory should be: every persist
    // and hydrate fails, and none of that may touch task traffic.
    const std::filesystem::path blocker = dir.root() / "recovery";
    write_text_file(blocker, "this is a file, not a directory");

    RuntimeService service(config);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::optional<ipc::TaskList> list = list_once(config.socket_path);
    MIRAGE_CHECK(list.has_value() && list->tasks.empty());

    const std::string token = mirage::testing::unique_token();
    const std::filesystem::path goal_file = dir.root() / ("goal-" + token + ".txt");
    const std::string content = "broken state dir " + token + "\n";
    write_text_file(goal_file, content);
    const std::optional<ipc::InspectTask> done = run_read_exec_task(
        config, goal_file, content, "blocker-" + token, "task beside a broken recovery directory");
    MIRAGE_CHECK(done.has_value() && done->progress == "Completed");

    service.request_shutdown();
    // The host shutdown is what defines clean; the recovery writer records
    // its failures without failing the run.
    MIRAGE_CHECK(service.run().clean);
}

void scenario_hydrate_beyond_registry_capacity_drops_remainder() {
    TempDir dir;
    const ServiceConfig config = make_config(dir);
    // A recovery file with three settled tasks, but the registry only has
    // room for two: hydration is a bounded drop-with-warning, never an
    // eviction or an unbounded insert.
    std::filesystem::create_directories(config.recovery_directory);
    persistence::RecoveryState state;
    state.mirage_version = "0.7.0-recovery-test";
    for (const char *id : {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
                           "cccccccccccccccccccccccccccccccc"}) {
        persistence::RecoveryTask task;
        task.id = id;
        task.goal = std::string("goal ") + id;
        task.progress = "Completed";
        task.has_success = true;
        task.success = true;
        state.tasks.push_back(std::move(task));
    }
    write_text_file(recovery_file(config), persistence::encode_recovery(state));

    ServiceConfig cramped = make_config(dir);
    cramped.max_task_records = 2;
    RuntimeService service(cramped);
    MIRAGE_CHECK(service.start(make_binding(dir)).ok);

    const std::optional<ipc::TaskList> list = list_once(config.socket_path);
    MIRAGE_CHECK(list.has_value());
    if (list) {
        // The first two records (map order) hydrate, the third is dropped.
        MIRAGE_CHECK(list->tasks.size() == 2);
        MIRAGE_CHECK(list_contains(*list, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "Completed"));
        MIRAGE_CHECK(list_contains(*list, "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "Completed"));
    }

    // The dropped room is the registry's own bound: a fresh task still fits
    // nowhere beyond two records, so submissions keep the bounded rejection.
    ipc::IpcClient client(config.socket_path);
    ipc::SubmitTaskRequest request;
    request.goal = "over the cramped registry";
    const ipc::Response refused = client.call(request, kCallBudget);
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "invalid_state");

    service.request_shutdown();
    MIRAGE_CHECK(service.run().clean);
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[recovery_service_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("settlement_persists_terminal_snapshot",
                 scenario_settlement_persists_terminal_snapshot);
    run_scenario("restart_hydrates_history_and_refuses_cancel",
                 scenario_restart_hydrates_history_and_refuses_cancel);
    run_scenario("hydrated_and_live_tasks_coexist", scenario_hydrated_and_live_tasks_coexist);
    run_scenario("corrupt_recovery_file_degrades_then_recovers",
                 scenario_corrupt_recovery_file_degrades_then_recovers);
    run_scenario("disabled_persistence_leaves_no_file",
                 scenario_disabled_persistence_leaves_no_file);
    run_scenario("alien_schema_file_starts_without_recovery",
                 scenario_alien_schema_file_starts_without_recovery);
    run_scenario("oversized_recovery_file_starts_without_recovery",
                 scenario_oversized_recovery_file_starts_without_recovery);
    run_scenario("unusable_recovery_directory_still_serves",
                 scenario_unusable_recovery_directory_still_serves);
    run_scenario("hydrate_beyond_registry_capacity_drops_remainder",
                 scenario_hydrate_beyond_registry_capacity_drops_remainder);
    return mirage::testing::finish("recovery_service_test");
}
