// M1-05 provider hardening verification (independent verification pass).
// Covers the new desktop primitives (CancelToken, PathScope) and the hardened
// LinuxDesktopEnvironment contracts: fail-closed empty read scope, path
// containment (sibling roots, ".." escapes, symlink escapes, file-as-root),
// read budgets (exact boundary, oversize refusal without silent truncation),
// read cancellation, process command budgets (refused before any side effect)
// and cooperative execution cancellation including whole-process-group
// teardown and preservation of already-captured output.

#include "../support/test.hpp"

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/filesystem_provider.hpp>
#include <mirage/desktop/path_scope.hpp>
#include <mirage/desktop/process_provider.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

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
namespace linux_backend = mirage::platform::linux_backend;

class TempWorkspace {
  public:
    TempWorkspace() {
        std::error_code ec;
        root_ = std::filesystem::temp_directory_path(ec) /
                ("mirage-m1-05-" + std::to_string(::getpid()) + "-" +
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

void write_text_file(const std::filesystem::path &path, const std::string &content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content;
}

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

// --- primitives --------------------------------------------------------------

void scenario_cancel_token_semantics() {
    // A default-constructed token can never be cancelled.
    desktop::CancelToken fresh;
    MIRAGE_CHECK(!fresh.cancelled());

    // Copies share one state: a request through one is observed by all.
    desktop::CancelToken original;
    desktop::CancelToken copy = original;
    const desktop::CancelToken second_copy = original;
    MIRAGE_CHECK(!original.cancelled());
    MIRAGE_CHECK(!copy.cancelled());
    MIRAGE_CHECK(!second_copy.cancelled());

    original.request_cancel();
    MIRAGE_CHECK(original.cancelled());
    MIRAGE_CHECK(copy.cancelled());
    MIRAGE_CHECK(second_copy.cancelled());

    // Requests are idempotent and cannot be un-requested.
    original.request_cancel();
    copy.request_cancel();
    MIRAGE_CHECK(original.cancelled());
    MIRAGE_CHECK(copy.cancelled());

    // An independent token stays untouched.
    desktop::CancelToken other;
    MIRAGE_CHECK(!other.cancelled());
}

void scenario_path_scope_unit_semantics() {
    // Empty scope denies everything, including existing absolute paths.
    const desktop::PathScope empty;
    MIRAGE_CHECK(empty.roots().empty());
    MIRAGE_CHECK(!empty.contains("/etc/hostname"));
    MIRAGE_CHECK(!empty.contains("/tmp"));
    MIRAGE_CHECK(!empty.contains("relative/path"));

    // An explicitly empty root list behaves the same; blank roots are dropped.
    const desktop::PathScope blank({{}, std::filesystem::path{""}});
    MIRAGE_CHECK(blank.roots().empty());
    MIRAGE_CHECK(!blank.contains("/etc/hostname"));

    // A root of "/" contains every absolute path.
    const desktop::PathScope filesystem_root({"/"});
    MIRAGE_CHECK(filesystem_root.contains("/"));
    MIRAGE_CHECK(filesystem_root.contains("/etc/hostname"));
    MIRAGE_CHECK(filesystem_root.contains("/tmp"));

    // A non-absolute root never matches an absolute request path.
    const desktop::PathScope relative({"some/relative/root"});
    MIRAGE_CHECK(!relative.contains("/some/relative/root"));
    MIRAGE_CHECK(!relative.contains("/etc/hostname"));

    // Component-wise prefix: "/a/b" is not a string prefix of "/a/bc".
    const desktop::PathScope ab({"/a/b"});
    MIRAGE_CHECK(!ab.contains("/a/bc"));
    MIRAGE_CHECK(!ab.contains("/a/bc/deeper"));
    MIRAGE_CHECK(ab.contains("/a/b"));
    MIRAGE_CHECK(ab.contains("/a/b/c"));

    // An empty request path is denied even with roots declared.
    MIRAGE_CHECK(!ab.contains(std::filesystem::path{}));

    // Non-existent leaves beneath an existing root are contained
    // (weakly_canonical resolves the existing prefix only).
    TempWorkspace workspace;
    const desktop::PathScope scoped({workspace.root()});
    MIRAGE_CHECK(scoped.contains(workspace.root()));
    MIRAGE_CHECK(scoped.contains(workspace.root() / "does-not-exist-yet.txt"));
    MIRAGE_CHECK(scoped.contains(workspace.root() / "missing" / "leaf.bin"));
    MIRAGE_CHECK(!scoped.contains(workspace.root() / ".." / "elsewhere.txt"));
}

// --- filesystem read scope ---------------------------------------------------

void scenario_default_scope_denies_every_read() {
    TempWorkspace workspace;
    const auto existing = workspace.root() / "readable.txt";
    write_text_file(existing, "plain text\n");

    // Default-constructed: no declared roots, every read refused.
    linux_backend::LinuxDesktopEnvironment unscoped;
    const auto denied = unscoped.read_text_file(existing);
    MIRAGE_CHECK(!denied.ok);
    MIRAGE_CHECK(denied.error.code == "permission_denied");
    MIRAGE_CHECK(denied.content.empty());
    // The refusal names the requested path, not any filesystem detail beyond.
    MIRAGE_CHECK(denied.error.message.find(existing.string()) != std::string::npos);

    // An explicitly empty root list is the same fail-closed scope.
    linux_backend::LinuxDesktopEnvironment explicit_empty(
        std::vector<std::filesystem::path>{});
    const auto denied_again = explicit_empty.read_text_file(existing);
    MIRAGE_CHECK(!denied_again.ok);
    MIRAGE_CHECK(denied_again.error.code == "permission_denied");

    // Even a root of "/" (permissive scope) refuses an empty path lookup but
    // the empty-scope environment refuses it with the argument error first.
    const auto empty_path = unscoped.read_text_file({});
    MIRAGE_CHECK(!empty_path.ok);
    MIRAGE_CHECK(empty_path.error.code == "invalid_argument");
}

void scenario_scope_containment_blocks_escapes() {
    TempWorkspace base;
    const auto root = base.root() / "root";
    const auto sibling = base.root() / "roottwo";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::filesystem::create_directories(sibling, ec);
    const auto inside = root / "inside.txt";
    const auto outside = base.root() / "outside.txt";
    write_text_file(inside, "inside\n");
    write_text_file(sibling / "sibling.txt", "sibling\n");
    write_text_file(outside, "outside\n");

    linux_backend::LinuxDesktopEnvironment environment({root});

    // In-scope regular file reads succeed.
    const auto read = environment.read_text_file(inside);
    MIRAGE_CHECK(read.ok);
    MIRAGE_CHECK(read.content == "inside\n");

    // A sibling directory that shares a string prefix with the root
    // ("root" vs "roottwo") is outside the scope.
    const auto neighbour = environment.read_text_file(sibling / "sibling.txt");
    MIRAGE_CHECK(!neighbour.ok);
    MIRAGE_CHECK(neighbour.error.code == "permission_denied");

    // A real file reachable only through a ".." escape is refused even though
    // it exists and would be readable.
    const auto escape = environment.read_text_file(root / ".." / "outside.txt");
    MIRAGE_CHECK(!escape.ok);
    MIRAGE_CHECK(escape.error.code == "permission_denied");
    MIRAGE_CHECK(std::filesystem::exists(outside)); // refused, not missing

    // A symlink inside the root pointing outside is resolved before the
    // containment decision: denied, without leaking the resolved target.
    const auto link = root / "escape-link.txt";
    std::filesystem::create_symlink(outside, link, ec);
    MIRAGE_CHECK(!ec);
    const auto through_link = environment.read_text_file(link);
    MIRAGE_CHECK(!through_link.ok);
    MIRAGE_CHECK(through_link.error.code == "permission_denied");
    MIRAGE_CHECK(through_link.error.message.find(link.string()) != std::string::npos);
    MIRAGE_CHECK(through_link.error.message.find(outside.string()) == std::string::npos);
    MIRAGE_CHECK(through_link.content.empty());
}

void scenario_scope_root_can_be_a_file() {
    TempWorkspace base;
    const auto root_file = base.root() / "rootfile.txt";
    const auto other = base.root() / "other.txt";
    write_text_file(root_file, "the root itself\n");
    write_text_file(other, "not in scope\n");

    // The scope root is a regular file: the root path itself is readable,
    // anything else (even a neighbour in the same directory) is not.
    linux_backend::LinuxDesktopEnvironment environment({root_file});
    const auto read = environment.read_text_file(root_file);
    MIRAGE_CHECK(read.ok);
    MIRAGE_CHECK(read.content == "the root itself\n");

    const auto denied = environment.read_text_file(other);
    MIRAGE_CHECK(!denied.ok);
    MIRAGE_CHECK(denied.error.code == "permission_denied");
}

// --- filesystem read budget --------------------------------------------------

void scenario_read_budget_boundaries() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment({workspace.root()});

    const std::string payload(4096, 'b');
    const auto exact_file = workspace.root() / "exact.bin";
    write_text_file(exact_file, payload);
    const auto over_file = workspace.root() / "over.bin";
    write_text_file(over_file, payload + "x");

    desktop::FileReadLimits exact;
    exact.max_bytes = payload.size();
    const auto exact_read = environment.read_text_file(exact_file, exact, desktop::CancelToken{});
    MIRAGE_CHECK(exact_read.ok);
    MIRAGE_CHECK(exact_read.content == payload);

    // One byte beyond the budget is a refusal, never a silent truncation.
    desktop::FileReadLimits tight;
    tight.max_bytes = payload.size();
    const auto refused = environment.read_text_file(over_file, tight, desktop::CancelToken{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "file_too_large");
    MIRAGE_CHECK(refused.content.empty());

    // A zero budget is an argument error before any file access.
    desktop::FileReadLimits zero;
    zero.max_bytes = 0;
    const auto invalid = environment.read_text_file(exact_file, zero, desktop::CancelToken{});
    MIRAGE_CHECK(!invalid.ok);
    MIRAGE_CHECK(invalid.error.code == "invalid_argument");
}

void scenario_read_cancelled_before_work() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment({workspace.root()});

    const std::string payload(256 * 1024, 'c');
    const auto big = workspace.root() / "big.txt";
    write_text_file(big, payload);

    desktop::CancelToken token;
    token.request_cancel();
    const auto outcome =
        environment.read_text_file(big, desktop::FileReadLimits{}, token);
    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    MIRAGE_CHECK(outcome.content.empty());
}

// --- process command budget --------------------------------------------------

void scenario_command_budget_refuses_before_side_effects() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    constexpr std::size_t kBudget = 128;
    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{5000};
    limits.max_command_bytes = kBudget;

    // Exactly the budget is executable: pad a trivial command with a comment.
    std::string exact = "true";
    exact += " #";
    exact += std::string(kBudget - exact.size(), 'a');
    MIRAGE_CHECK(exact.size() == kBudget);
    const auto ok = environment.execute(exact, limits, desktop::CancelToken{});
    MIRAGE_CHECK(ok.ok);
    MIRAGE_CHECK(ok.exited_normally);
    MIRAGE_CHECK(ok.exit_code == 0);

    // One byte beyond the budget is refused before any process is created:
    // the command would touch a canary file, which must not come into
    // existence.
    const auto canary = workspace.root() / "canary-created.txt";
    std::string over = "touch '" + canary.string() + "'; exit 0 #";
    over += std::string(kBudget, 'b'); // far beyond the budget
    MIRAGE_CHECK(over.size() > kBudget);
    const auto refused = environment.execute(over, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "invalid_argument");
    MIRAGE_CHECK(refused.error.message.find(std::to_string(kBudget)) != std::string::npos);
    MIRAGE_CHECK(!refused.exited_normally);
    MIRAGE_CHECK(refused.standard_output.empty());
    MIRAGE_CHECK(refused.standard_error.empty());
    MIRAGE_CHECK(!std::filesystem::exists(canary));

    // A zero command budget is an argument error.
    desktop::ProcessLimits zero;
    zero.timeout = std::chrono::milliseconds{5000};
    zero.max_command_bytes = 0;
    const auto invalid = environment.execute("true", zero, desktop::CancelToken{});
    MIRAGE_CHECK(!invalid.ok);
    MIRAGE_CHECK(invalid.error.code == "invalid_argument");
    MIRAGE_CHECK(!std::filesystem::exists(canary));
}

// --- execution cancellation --------------------------------------------------

/// The single-threaded mid-flight cancellation probe: SIGALRM fires once
/// while execute() is parked in its poll slice; the handler only performs the
/// async-signal-safe atomic store of request_cancel().
desktop::CancelToken *g_alarm_cancel_token = nullptr;

extern "C" void on_alarm_request_cancel(int) {
    if (g_alarm_cancel_token != nullptr) {
        g_alarm_cancel_token->request_cancel();
    }
}

void scenario_cancel_midflight_tears_down_group_and_keeps_output() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    const auto pid_file = workspace.root() / "pids";
    desktop::CancelToken token;
    g_alarm_cancel_token = &token;

    struct sigaction action {};
    action.sa_handler = on_alarm_request_cancel;
    ::sigemptyset(&action.sa_mask);
    MIRAGE_CHECK(::sigaction(SIGALRM, &action, nullptr) == 0);
    ::alarm(1); // cancel about one second into the five-second budget

    // The command records its own pid and the pid of a long-running
    // background child, emits capturable output, then waits so the
    // cancellation has to tear down a whole live process group.
    std::string command = "echo $$ > '" + pid_file.string() + "'";
    command += "; sleep 30 & echo $! >> '" + pid_file.string() + "'";
    command += "; echo out-line; echo err-line 1>&2; wait";

    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{5000};

    const auto started = std::chrono::steady_clock::now();
    const auto outcome = environment.execute(command, limits, token);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    ::alarm(0);
    g_alarm_cancel_token = nullptr;

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(!outcome.timed_out);
    MIRAGE_CHECK(!outcome.ok && outcome.error.code == "cancelled");
    // The cancellation ended the command far before its own budget.
    MIRAGE_CHECK(elapsed < std::chrono::seconds{3});

    // Output captured before the cancellation is preserved, not dropped.
    MIRAGE_CHECK(outcome.standard_output.find("out-line") != std::string::npos);
    MIRAGE_CHECK(outcome.standard_error.find("err-line") != std::string::npos);

    // Whole-group teardown: the direct child and the background sleep are
    // both gone (no orphans, no zombies).
    const auto pids = read_pid_file(pid_file);
    MIRAGE_CHECK(pids.size() == 2);
    for (const pid_t pid : pids) {
        if (!process_is_gone(pid, std::chrono::milliseconds{2000})) {
            std::fprintf(stderr,
                         "[provider_hardening_test] survivor after cancel: pid=%d still alive\n",
                         pid);
        }
        MIRAGE_CHECK(process_is_gone(pid, std::chrono::milliseconds{2000}));
    }

    struct sigaction reset {};
    reset.sa_handler = SIG_DFL;
    ::sigaction(SIGALRM, &reset, nullptr);
}

void scenario_cancel_token_set_before_execute_returns_promptly() {
    TempWorkspace workspace;
    linux_backend::LinuxDesktopEnvironment environment;

    const auto pid_file = workspace.root() / "pids";
    desktop::CancelToken token;
    token.request_cancel();

    // The command would run for half a minute and record a whole process
    // group; the pre-cancelled token must end the call almost immediately.
    std::string command = "echo $$ > '" + pid_file.string() + "'";
    command += "; sleep 30 & echo $! >> '" + pid_file.string() + "'";
    command += "; wait";

    desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{10000};

    const auto started = std::chrono::steady_clock::now();
    const auto outcome = environment.execute(command, limits, token);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(!outcome.timed_out);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    MIRAGE_CHECK(elapsed < std::chrono::seconds{2});

    // Whatever the command managed to write before the group kill, no member
    // of the group may survive the call. The pid file may be missing (the
    // shell can be killed before its first write); every pid that did land
    // there must be gone.
    for (const pid_t pid : read_pid_file(pid_file)) {
        MIRAGE_CHECK(process_is_gone(pid, std::chrono::milliseconds{2000}));
    }
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[provider_hardening_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("cancel_token_semantics", scenario_cancel_token_semantics);
    run_scenario("path_scope_unit_semantics", scenario_path_scope_unit_semantics);
    run_scenario("default_scope_denies_every_read", scenario_default_scope_denies_every_read);
    run_scenario("scope_containment_blocks_escapes", scenario_scope_containment_blocks_escapes);
    run_scenario("scope_root_can_be_a_file", scenario_scope_root_can_be_a_file);
    run_scenario("read_budget_boundaries", scenario_read_budget_boundaries);
    run_scenario("read_cancelled_before_work", scenario_read_cancelled_before_work);
    run_scenario("command_budget_refuses_before_side_effects",
                 scenario_command_budget_refuses_before_side_effects);
    run_scenario("cancel_midflight_tears_down_group_and_keeps_output",
                 scenario_cancel_midflight_tears_down_group_and_keeps_output);
    run_scenario("cancel_token_set_before_execute_returns_promptly",
                 scenario_cancel_token_set_before_execute_returns_promptly);
    return mirage::testing::finish("provider_hardening_test");
}
