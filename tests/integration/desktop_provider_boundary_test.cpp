// Supplementary M1-03 verification scenarios (independent verification pass).
// Covers gaps the 91-check mira_binding_test does not reach: process-group
// cleanup on the timeout path (including grandchildren surviving the direct
// child), duplicate / late operation completion idempotency, host-state gates
// on the operation surface, capture-budget truncation boundaries, and
// filesystem edge cases (empty file, permission denied, zero output budget).

#include "../support/test.hpp"

#include <mira/environment.hpp>

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <mirage/runtime/mira_host.hpp>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <signal.h>
#include <unistd.h>

namespace {

namespace desktop = mirage::desktop;
namespace integration = mirage::integration;
namespace linux_backend = mirage::platform::linux_backend;
using mirage::runtime::HostStatus;
using mirage::runtime::MiraHost;
using mirage::runtime::OperationTicket;
using mirage::runtime::TaskIdentity;
using mirage::runtime::TaskProgress;

class TempWorkspace {
  public:
    TempWorkspace() {
        std::error_code ec;
        root_ = std::filesystem::temp_directory_path(ec) /
                ("mirage-m1-03-verify-" + std::to_string(::getpid()) + "-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root_, ec);
    }
    ~TempWorkspace() {
        std::error_code ec;
        std::filesystem::remove_all(root_, ec);
    }
    TempWorkspace(const TempWorkspace &) = delete;
    TempWorkspace &operator=(const TempWorkspace &) = delete;

    [[nodiscard]] const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

/// True when the process does not exist anymore (reaped or never born).
/// Zombies still answer kill(pid, 0) with success, so the probe waits for the
/// reaper to finish within a short grace window.
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

/// The command records its own pid and the pids of two long-running
/// background children, then either waits on them (keep_parent) or exits
/// early, leaving the children behind in the process group.
std::string spawn_sleep_pair_command(const std::filesystem::path &pid_file, bool keep_parent) {
    const auto file = pid_file.string();
    std::string command = "echo $$ > '" + file + "'";
    command += "; sleep 30 & echo $! >> '" + file + "'";
    command += "; sleep 30 & echo $! >> '" + file + "'";
    command += keep_parent ? "; wait" : "; exit 0";
    return command;
}

/// Diagnoses a surviving pid: 'Z' marks a zombie (dead but unreaped), 'A'
/// marks a live process, 'G' marks gone (ESRCH).
char process_state(pid_t pid) {
    if (::kill(pid, 0) != 0 && errno == ESRCH) {
        return 'G';
    }
    char stat_path[64];
    std::snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    if (FILE *f = std::fopen(stat_path, "r")) {
        char state = '?';
        char line[512];
        if (std::fgets(line, sizeof(line), f) != nullptr) {
            // The comm field may contain spaces; the state follows the last ')'.
            const char *close = std::strrchr(line, ')');
            if (close != nullptr && close[1] == ' ') {
                state = close[2];
            }
        }
        std::fclose(f);
        return state;
    }
    return 'A'; // answers kill(pid, 0) but /proc entry unreadable: alive
}

void scenario_timeout_kills_whole_process_group() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    const auto pid_file = workspace.root() / "pids";
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{200};

    const auto started = std::chrono::steady_clock::now();
    const auto outcome = environment.execute(spawn_sleep_pair_command(pid_file, true), limits);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.timed_out);
    MIRAGE_CHECK(outcome.error.code == "deadline_exceeded");
    MIRAGE_CHECK(elapsed < std::chrono::seconds{5});

    const auto pids = read_pid_file(pid_file);
    MIRAGE_CHECK(pids.size() == 3);
    for (const pid_t pid : pids) {
        if (!process_is_gone(pid, std::chrono::milliseconds{2000})) {
            std::fprintf(stderr,
                         "[desktop_provider_boundary_test] survivor after timeout: pid=%d "
                         "state=%c\n",
                         pid, process_state(pid));
        }
        MIRAGE_CHECK(process_is_gone(pid, std::chrono::milliseconds{2000}));
    }
}

void scenario_timeout_kills_surviving_grandchildren_after_parent_exit() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    const auto pid_file = workspace.root() / "pids";
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{200};

    // The direct child (sh) exits immediately; the two `sleep 30` descendants
    // keep the capture pipes and the process group alive. The timeout must
    // still tear the whole group down instead of leaving orphans behind.
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = environment.execute(spawn_sleep_pair_command(pid_file, false), limits);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.timed_out);
    MIRAGE_CHECK(elapsed < std::chrono::seconds{5});

    const auto pids = read_pid_file(pid_file);
    MIRAGE_CHECK(pids.size() == 3);
    // pids[0] is the direct child (already reaped by waitpid). The two sleep
    // descendants must be gone as well: a survivor would keep running for the
    // full 30 seconds as an orphan in the killed group.
    for (std::size_t index = 1; index < pids.size(); ++index) {
        if (!process_is_gone(pids[index], std::chrono::milliseconds{2000})) {
            std::fprintf(stderr,
                         "[desktop_provider_boundary_test] orphaned grandchild after timeout: "
                         "pid=%d state=%c\n",
                         pids[index], process_state(pids[index]));
        }
        MIRAGE_CHECK(process_is_gone(pids[index], std::chrono::milliseconds{2000}));
    }
}

void scenario_duplicate_and_late_operation_completions_never_revive_task() {
    TempWorkspace workspace;
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    MiraHost host;
    MIRAGE_CHECK(host.start(std::make_shared<integration::MiraEnvironmentBinding>(environment)).ok);

    const auto submission = host.submit_task("duplicate completion idempotency");
    MIRAGE_CHECK(submission.ok);
    const TaskIdentity task = submission.task;

    const auto operation = host.begin_operation(task);
    MIRAGE_CHECK(operation.ok);
    MIRAGE_CHECK(host.admit_operation_completion(operation.ticket).ok);

    // Re-stating the same settled ticket is an idempotent NoOp: ok, and the
    // task stays in its non-terminal state.
    MIRAGE_CHECK(host.admit_operation_completion(operation.ticket).ok);
    const auto after_duplicate = host.task_view(task);
    MIRAGE_CHECK(after_duplicate.ok);
    MIRAGE_CHECK(after_duplicate.view.progress == TaskProgress::Idle);

    // A ticket admitted after the task reached its terminal state settles as
    // a stale NoOp: ok, and the terminal state is not revived or changed.
    const auto second_operation = host.begin_operation(task);
    MIRAGE_CHECK(second_operation.ok);
    MIRAGE_CHECK(host.complete_task(task, true).ok);
    const auto completed = host.task_view(task);
    MIRAGE_CHECK(completed.ok);
    MIRAGE_CHECK(completed.view.progress == TaskProgress::Completed);

    MIRAGE_CHECK(host.admit_operation_completion(second_operation.ticket).ok);
    const auto after_late = host.task_view(task);
    MIRAGE_CHECK(after_late.ok);
    MIRAGE_CHECK(after_late.view.progress == TaskProgress::Completed);
    MIRAGE_CHECK(after_late.view.success);

    MIRAGE_CHECK(host.shutdown().ok);
}

void scenario_operation_surface_requires_running_host() {
    // A host that never started refuses the whole operation surface.
    MiraHost host;
    const TaskIdentity fake{"00000000000000000000000000000000"};

    const auto begin = host.begin_operation(fake);
    MIRAGE_CHECK(!begin.ok);
    MIRAGE_CHECK(begin.error.code == "invalid_state");

    const OperationTicket ticket{fake.id, 0, fake.id, fake.id};
    const auto admit = host.admit_operation_completion(ticket);
    MIRAGE_CHECK(!admit.ok);
    MIRAGE_CHECK(admit.error.code == "invalid_state");

    // And after an orderly shutdown the surface stays closed.
    auto environment = std::make_shared<linux_backend::LinuxDesktopEnvironment>();
    MIRAGE_CHECK(host.start(std::make_shared<integration::MiraEnvironmentBinding>(environment)).ok);
    MIRAGE_CHECK(host.shutdown().ok);
    MIRAGE_CHECK(host.status() == HostStatus::Stopped);

    const auto begin_after = host.begin_operation(fake);
    MIRAGE_CHECK(!begin_after.ok);
    MIRAGE_CHECK(begin_after.error.code == "invalid_state");

    const auto admit_after = host.admit_operation_completion(ticket);
    MIRAGE_CHECK(!admit_after.ok);
    MIRAGE_CHECK(admit_after.error.code == "invalid_state");
}

void scenario_process_output_truncation_and_exact_budget_boundary() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    const std::string payload(64, 'a');

    desktop::ProcessLimits over;
    over.timeout = std::chrono::milliseconds{5000};
    over.max_output_bytes = 16;
    const auto truncated =
        environment.execute("printf '" + payload + "'; printf '" + payload + "' 1>&2", over);
    MIRAGE_CHECK(truncated.ok);
    MIRAGE_CHECK(truncated.output_truncated);
    MIRAGE_CHECK(truncated.standard_output.size() == 16);
    MIRAGE_CHECK(truncated.standard_error.size() == 16);

    desktop::ProcessLimits exact;
    exact.timeout = std::chrono::milliseconds{5000};
    exact.max_output_bytes = 64;
    const auto boundary = environment.execute("printf '" + payload + "'", exact);
    MIRAGE_CHECK(boundary.ok);
    MIRAGE_CHECK(!boundary.output_truncated);
    MIRAGE_CHECK(boundary.standard_output == payload);

    desktop::ProcessLimits zero;
    zero.timeout = std::chrono::milliseconds{5000};
    zero.max_output_bytes = 0;
    const auto invalid = environment.execute("true", zero);
    MIRAGE_CHECK(!invalid.ok);
    MIRAGE_CHECK(invalid.error.code == "invalid_argument");
}

void scenario_filesystem_edge_cases() {
    TempWorkspace workspace;
    // M1-05: the filesystem boundary scenarios run against an environment
    // whose read scope is the workspace root.
    linux_backend::LinuxDesktopEnvironment environment({workspace.root()});

    // An empty regular file reads back successfully as empty content.
    const auto empty_path = workspace.root() / "empty.txt";
    { std::ofstream stream(empty_path, std::ios::binary | std::ios::trunc); }
    const auto empty_read = environment.read_text_file(empty_path);
    MIRAGE_CHECK(empty_read.ok);
    MIRAGE_CHECK(empty_read.content.empty());

    // An unreadable regular file fails closed with permission_denied. The
    // check is meaningless for root, which bypasses file modes.
    if (::geteuid() != 0) {
        const auto locked = workspace.root() / "locked.txt";
        {
            std::ofstream stream(locked, std::ios::binary | std::ios::trunc);
            stream << "secret";
        }
        std::error_code ec;
        std::filesystem::permissions(locked, std::filesystem::perms::none, ec);
        MIRAGE_CHECK(!ec);
        const auto denied = environment.read_text_file(locked);
        MIRAGE_CHECK(!denied.ok);
        MIRAGE_CHECK(denied.error.code == "permission_denied");
        std::filesystem::permissions(locked, std::filesystem::perms::owner_all, ec);
    } else {
        std::fprintf(stderr,
                     "[desktop_provider_boundary_test] permission_denied case skipped (root)\n");
    }

    // M1-05 containment still holds when the file mode would allow the read:
    // the scope check runs before the file is even looked up.
    const auto outside = workspace.root() / ".." / "readable-outside.txt";
    {
        std::ofstream stream(outside, std::ios::binary | std::ios::trunc);
        stream << "beyond the scope";
    }
    const auto escaped = environment.read_text_file(outside);
    MIRAGE_CHECK(!escaped.ok);
    MIRAGE_CHECK(escaped.error.code == "permission_denied");
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[desktop_provider_boundary_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("timeout_kills_whole_process_group", scenario_timeout_kills_whole_process_group);
    run_scenario("timeout_kills_surviving_grandchildren_after_parent_exit",
                 scenario_timeout_kills_surviving_grandchildren_after_parent_exit);
    run_scenario("duplicate_and_late_operation_completions_never_revive_task",
                 scenario_duplicate_and_late_operation_completions_never_revive_task);
    run_scenario("operation_surface_requires_running_host",
                 scenario_operation_surface_requires_running_host);
    run_scenario("process_output_truncation_and_exact_budget_boundary",
                 scenario_process_output_truncation_and_exact_budget_boundary);
    run_scenario("filesystem_edge_cases", scenario_filesystem_edge_cases);
    return mirage::testing::finish("desktop_provider_boundary_test");
}
