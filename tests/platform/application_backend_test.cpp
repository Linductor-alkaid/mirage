// M2-05 ApplicationBackend tests (DEC-015 topology): a private XDG data
// tree with fixture desktop entries and per-application copies of a helper
// binary under globally unique names, so the backend's /proc name matching
// sees exactly the processes this test creates. No X display, no D-Bus, no
// system application inventory: XDG_DATA_HOME / XDG_DATA_DIRS are
// redirected before the first GIO call (GIO caches the desktop file
// directory snapshot per process, verified: entries created after the first
// call are invisible). The launch contract against the frozen fake
// environment (tests/support/fake_desktop_environment.hpp) is re-verified on
// the real GIO path: rejection order before any side effect, running-state
// truth from the registry and the bounded /proc scan, cooperative
// termination without forced kills, and opportunistic reaping.

#include "../support/test.hpp"

#include <mirage/desktop/cancellation.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

namespace desktop = mirage::desktop;

/// Directory of this test binary: the helper executable sits next to it.
std::filesystem::path executable_directory() {
    char buffer[4096];
    const ssize_t length = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (length <= 0) {
        return std::filesystem::current_path();
    }
    return std::filesystem::path(std::string(buffer, static_cast<std::size_t>(length)))
        .parent_path();
}

void write_desktop_entry(const std::filesystem::path &data_dir, const std::string &id,
                         const std::string &name, const std::string &exec_line,
                         bool no_display = false, bool hidden = false,
                         bool dbus_activatable = false) {
    std::filesystem::create_directories(data_dir / "applications");
    std::ofstream out(data_dir / "applications" / id);
    out << "[Desktop Entry]\nType=Application\nName=" << name << "\n";
    if (!exec_line.empty()) {
        out << "Exec=" << exec_line << "\n";
    }
    if (dbus_activatable) {
        out << "DBusActivatable=true\n";
    }
    if (no_display) {
        out << "NoDisplay=true\n";
    }
    if (hidden) {
        out << "Hidden=true\n";
    }
}

/// True while the process exists (zombies included — this is exactly what
/// kill(pid, 0) reports, so callers that need "fully gone" rely on the
/// backend having reaped a tracked child or the init reaping a foreign one).
bool process_alive(pid_t pid) { return ::kill(pid, 0) == 0; }

/// Tracks every helper process the test started and reaps them all on
/// destruction: SIGKILL to the pid and its process group, then a blocking
/// waitpid for own children (non-children report ECHILD and are ignored —
/// double-forked grandchildren are reaped by init).
class ProcessTracker {
  public:
    ~ProcessTracker() {
        for (const pid_t pid : pids_) {
            ::kill(pid, SIGKILL);
            ::kill(-pid, SIGKILL);
            int status = 0;
            while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
    }

    void track(pid_t pid) {
        if (pid > 0) {
            pids_.push_back(pid);
        }
    }

    /// Kills one process immediately (stuck instances after a failed
    /// terminate) so it cannot leak into later scenarios.
    void kill_now(pid_t pid) {
        ::kill(pid, SIGKILL);
        ::kill(-pid, SIGKILL);
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }

  private:
    std::vector<pid_t> pids_;
};

/// Starts one helper copy as a FOREIGN instance (not through the provider):
/// a double fork orphans the exec'ing grandchild, so it is not this test's
/// child — it becomes a zombie-free instance whose death the backend can
/// observe via kill(pid, 0) == ESRCH, exactly like a user-started process.
pid_t spawn_foreign_instance(const std::string &helper_path, const std::string &mode,
                             ProcessTracker &tracker) {
    int pipefd[2];
    if (::pipe(pipefd) != 0) {
        return -1;
    }
    const pid_t middle = ::fork();
    if (middle < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return -1;
    }
    if (middle == 0) {
        ::close(pipefd[0]);
        ::setpgid(0, 0);
        const pid_t grand = ::fork();
        if (grand == 0) {
            ::close(pipefd[1]);
            ::execl(helper_path.c_str(), helper_path.c_str(), mode.c_str(), "3600",
                    static_cast<char *>(nullptr));
            _exit(127);
        }
        const ssize_t ignored = ::write(pipefd[1], &grand, sizeof(grand));
        (void)ignored;
        _exit(0);
    }
    ::close(pipefd[1]);
    pid_t grand = -1;
    ssize_t got = 0;
    do {
        got = ::read(pipefd[0], &grand, sizeof(grand));
    } while (got < 0 && errno == EINTR);
    ::close(pipefd[0]);
    int status = 0;
    while (::waitpid(middle, &status, 0) < 0 && errno == EINTR) {
    }
    if (got != static_cast<ssize_t>(sizeof(grand)) || grand <= 0) {
        return -1;
    }
    tracker.track(grand);
    return grand;
}

const desktop::ApplicationInfo *find_entry(const desktop::ApplicationListOutcome &listed,
                                           const std::string &id) {
    for (const auto &application : listed.applications) {
        if (application.id == id) {
            return &application;
        }
    }
    return nullptr;
}

bool all_decimal_digits(const std::string &text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
}

/// Waits until the ignore-term helper has installed its SIGTERM disposition
/// (marked by the readiness file it writes from its own main), so the
/// terminate request below cannot land on the exec chain instead.
bool wait_for_ready_file(const std::filesystem::path &path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
    }
    return false;
}

} // namespace

int main() {
    std::random_device random;
    std::uniform_int_distribution<unsigned int> pick;
    const std::string suffix = std::to_string(static_cast<unsigned int>(::getpid())) + "-" +
                               std::to_string(pick(random)) + std::to_string(pick(random));

    const std::filesystem::path fixture_root =
        std::filesystem::temp_directory_path() / ("mirage-m205-app-" + suffix);
    const std::filesystem::path data_home = fixture_root / "data-home";
    const std::filesystem::path data_first = fixture_root / "data-first";
    const std::filesystem::path data_second = fixture_root / "data-second";
    std::filesystem::create_directories(data_home);
    std::filesystem::create_directories(data_first);
    std::filesystem::create_directories(data_second);

    // The helper binary next to this test, copied once per fixture
    // application under a globally unique name: /proc matches must never hit
    // a system process (or another scenario's instance).
    const std::filesystem::path helper_binary = executable_directory() / "m205_process_helper";
    if (!std::filesystem::is_regular_file(helper_binary)) {
        std::fprintf(stderr, "helper binary not found next to the test: %s\n",
                     helper_binary.c_str());
        return 1;
    }
    const auto copy_helper = [&](const std::string &name) {
        const auto target = fixture_root / (name + "-" + suffix);
        std::filesystem::copy_file(helper_binary, target,
                                   std::filesystem::copy_options::overwrite_existing);
        std::filesystem::permissions(target, std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::add);
        return target.string();
    };
    const std::string main_helper = copy_helper("mirage-m205-main");
    const std::string stuck_helper = copy_helper("mirage-m205-stuck");
    const std::string foreign_helper = copy_helper("mirage-m205-foreign");
    const std::string quick_helper = copy_helper("mirage-m205-quick");
    const std::string dbus_helper = copy_helper("mirage-m205-dbus");
    const std::string plain_helper = copy_helper("mirage-m205-plain");
    const std::string pct_helper = copy_helper("mirage-m205-pct");

    // Readiness markers for the stuck variant: the helper writes one after
    // installing SIG_IGN for SIGTERM (deleted before each stuck launch so
    // the wait observes the NEW instance).
    const std::filesystem::path stuck_ready = fixture_root / "stuck-ready";

    // ALL fixture desktop entries are written before the first GIO call —
    // the directory snapshot is cached per process.
    const std::string main_id = "mirage-m205-main-" + suffix + ".desktop";
    const std::string stuck_id = "mirage-m205-stuck-" + suffix + ".desktop";
    const std::string foreign_id = "mirage-m205-foreign-" + suffix + ".desktop";
    const std::string quick_id = "mirage-m205-quick-" + suffix + ".desktop";
    const std::string dup_id = "mirage-m205-dup-" + suffix + ".desktop";
    const std::string broken_id = "mirage-m205-broken-" + suffix + ".desktop";
    const std::string missing_id = "mirage-m205-missing-" + suffix + ".desktop";
    const std::string nodisplay_id = "mirage-m205-nodisplay-" + suffix + ".desktop";
    const std::string hidden_id = "mirage-m205-hidden-" + suffix + ".desktop";
    // DEC-086 fixtures: DBusActivatable entries spawn their Exec argv
    // directly instead of degrading a successful launch into an uncertain
    // side effect. One entry per observable Exec shape, plus two entries
    // whose Exec cannot expand and must fall back to the GIO path.
    const std::string dbus_id = "mirage-m205-dbus-" + suffix + ".desktop";
    const std::string plain_id = "mirage-m205-plain-" + suffix + ".desktop";
    const std::string pct_id = "mirage-m205-pct-" + suffix + ".desktop";
    const std::string dbus_noexec_id = "mirage-m205-dbus-noexec-" + suffix + ".desktop";
    const std::string dbus_overflow_id = "mirage-m205-dbus-overflow-" + suffix + ".desktop";
    const std::string dbus_quote_id = "mirage-m205-dbus-quote-" + suffix + ".desktop";

    write_desktop_entry(data_first, main_id, "Mirage Main Fixture", main_helper + " hold 3600 %u");
    write_desktop_entry(data_first, stuck_id, "Mirage Stuck Fixture",
                        stuck_helper + " ignore-term 3600 " + stuck_ready.string());
    write_desktop_entry(data_first, foreign_id, "Mirage Foreign Fixture",
                        foreign_helper + " hold 3600");
    write_desktop_entry(data_first, quick_id, "Mirage Quick Fixture", quick_helper + " exit");
    // A launchable entry with no Exec key: GIO keeps it enumerable (entries
    // whose Exec target is missing or non-executable are dropped outright),
    // but the spawn itself fails — the backend's io_error path.
    write_desktop_entry(data_first, broken_id, "Mirage Broken Fixture", "");
    write_desktop_entry(data_first, dup_id, "Dup From First Dir", "/bin/true");
    write_desktop_entry(data_second, dup_id, "Dup From Second Dir", "/bin/true");
    write_desktop_entry(data_first, nodisplay_id, "Mirage NoDisplay Fixture", "/bin/true",
                        /*no_display=*/true);
    write_desktop_entry(data_first, hidden_id, "Mirage Hidden Fixture", "/bin/true",
                        /*no_display=*/false, /*hidden=*/true);
    write_desktop_entry(data_first, dbus_id, "Mirage DBus Fixture", dbus_helper + " hold 3600 %U",
                        /*no_display=*/false, /*hidden=*/false, /*dbus_activatable=*/true);
    write_desktop_entry(data_first, pct_id, "Mirage Percent Fixture",
                        pct_helper + " hold 3600 " + (fixture_root / "pct%%.ready").string(),
                        /*no_display=*/false, /*hidden=*/false, /*dbus_activatable=*/true);
    write_desktop_entry(data_first, dbus_noexec_id, "Mirage DBus NoExec Fixture", "",
                        /*no_display=*/false, /*hidden=*/false, /*dbus_activatable=*/true);
    // An Exec line past the expansion caps (33+ tokens) cannot become an
    // argv, so the launch falls back to the GIO path; GIO keeps the entry
    // enumerable and its launch fails in this fixture (no session-bus
    // activation target), which is the honest outcome. The ready-file path
    // sits at argv[3]: a wrongly taken direct spawn would create it.
    {
        std::string overflow_exec =
            dbus_helper + " hold 3600 " + (fixture_root / "overflow-ready").string();
        for (int token = 0; token < 40; ++token) {
            overflow_exec += " pad";
        }
        write_desktop_entry(data_first, dbus_overflow_id, "Mirage DBus Overflow Fixture",
                            overflow_exec,
                            /*no_display=*/false, /*hidden=*/false, /*dbus_activatable=*/true);
    }
    // An Exec line that is not valid shell syntax: GIO refuses to load the
    // entry at all, so it never becomes launchable (fail closed upstream).
    write_desktop_entry(data_first, dbus_quote_id, "Mirage DBus Quote Fixture",
                        dbus_helper + " hold \"3600",
                        /*no_display=*/false, /*hidden=*/false, /*dbus_activatable=*/true);
    // A %%u token survives expansion as the literal text "%u" (the escape,
    // not the field code), so the URI lands appended after it instead of
    // substituting; Path= proves the direct spawn honors the working
    // directory. Written raw because of the extra keys.
    {
        std::ofstream out(data_first / "applications" / plain_id);
        out << "[Desktop Entry]\nType=Application\nName=Mirage Plain Exec Fixture\n"
            << "Exec=" << plain_helper << " hold 3600 %%u\n"
            << "Path=" << fixture_root.string() << "\n"
            << "DBusActivatable=true\n";
    }

    // Redirect the whole application inventory to the fixture (this also
    // keeps the machine's real applications out of the enumeration).
    const auto modules = fixture_root / "gio-modules";
    std::filesystem::create_directories(modules);
    ::setenv("GIO_MODULE_DIR", modules.c_str(), 1);
    ::unsetenv("GIO_EXTRA_MODULES");
    ::setenv("XDG_DATA_HOME", data_home.c_str(), 1);
    const std::string xdg_data_dirs = data_first.string() + ":" + data_second.string();
    ::setenv("XDG_DATA_DIRS", xdg_data_dirs.c_str(), 1);

    ProcessTracker tracker;
    try {
        mirage::platform::linux_backend::LinuxDesktopEnvironment env({}, {}, {}, {.enabled = true},
                                                                     {});
        auto *apps = env.application();
        if (apps == nullptr) {
            std::fprintf(stderr, "application accessor is null despite opt-in\n");
            return 1;
        }

        std::fprintf(stderr, "[application_backend_test] scenario: discovery_and_budgets\n");
        // Discovery: exactly the visible fixture entries, sorted by id, with
        // the NoDisplay / Hidden entries filtered and the same-id entry in
        // both XDG dirs deduplicated (first dir wins — GIO resolves the XDG
        // precedence, the backend deduplicates defensively; redirecting
        // XDG_DATA_DIRS exercises the same two-sources-one-id mechanism the
        // system/fixture coexistence relies on).
        const auto listed = apps->list_applications();
        MIRAGE_CHECK(listed.ok);
        // 6 pre-existing fixtures plus the 5 enumerable DEC-086 entries; the
        // unparseable-Exec DBusActivatable entry is dropped by GIO itself.
        MIRAGE_CHECK(listed.applications.size() == 11);
        bool sorted_by_id =
            std::is_sorted(listed.applications.begin(), listed.applications.end(),
                           [](const desktop::ApplicationInfo &a,
                              const desktop::ApplicationInfo &b) { return a.id < b.id; });
        MIRAGE_CHECK(sorted_by_id);

        const desktop::ApplicationInfo *main_entry = find_entry(listed, main_id);
        MIRAGE_CHECK(main_entry != nullptr);
        if (main_entry != nullptr) {
            MIRAGE_CHECK(main_entry->name == "Mirage Main Fixture");
            MIRAGE_CHECK(!main_entry->running);
        }
        MIRAGE_CHECK(find_entry(listed, nodisplay_id) == nullptr);
        MIRAGE_CHECK(find_entry(listed, hidden_id) == nullptr);
        MIRAGE_CHECK(find_entry(listed, missing_id) == nullptr);
        const desktop::ApplicationInfo *dup_entry = find_entry(listed, dup_id);
        MIRAGE_CHECK(dup_entry != nullptr);
        int dup_count = 0;
        for (const auto &application : listed.applications) {
            if (application.id == dup_id) {
                ++dup_count;
            }
        }
        MIRAGE_CHECK(dup_count == 1);
        if (dup_entry != nullptr) {
            MIRAGE_CHECK(dup_entry->name == "Dup From First Dir");
        }

        // Budgets: refusal instead of truncation, at zero and one below the
        // table size; exactly the table size passes.
        desktop::ApplicationListLimits zero_budget;
        zero_budget.max_applications = 0;
        const auto zero_list = apps->list_applications(zero_budget, {});
        MIRAGE_CHECK(!zero_list.ok);
        MIRAGE_CHECK(zero_list.error.code == "result_too_large");
        MIRAGE_CHECK(zero_list.applications.empty());
        desktop::ApplicationListLimits below_budget;
        below_budget.max_applications = 10;
        MIRAGE_CHECK(apps->list_applications(below_budget, {}).error.code == "result_too_large");
        desktop::ApplicationListLimits exact_budget;
        exact_budget.max_applications = 11;
        const auto exact_list = apps->list_applications(exact_budget, {});
        MIRAGE_CHECK(exact_list.ok);
        MIRAGE_CHECK(exact_list.applications.size() == 11);
        // Cancelled before any work.
        desktop::CancelToken cancelled_list;
        cancelled_list.request_cancel();
        const auto cancelled_listing = apps->list_applications({}, cancelled_list);
        MIRAGE_CHECK(!cancelled_listing.ok);
        MIRAGE_CHECK(cancelled_listing.error.code == "cancelled");

        std::fprintf(stderr, "[application_backend_test] scenario: launch_validation\n");
        // Validation happens before any side effect, in the contract's order.
        MIRAGE_CHECK(apps->running_state("", {}).error.code == "invalid_argument");
        MIRAGE_CHECK(apps->running_state(missing_id, {}).error.code == "not_found");

        desktop::ApplicationLaunchLimits negative_timeout;
        negative_timeout.timeout = std::chrono::milliseconds{-1};
        const auto timeout_rejected = apps->launch(main_id, negative_timeout, {});
        MIRAGE_CHECK(!timeout_rejected.ok);
        MIRAGE_CHECK(timeout_rejected.error.code == "invalid_argument");
        // The timeout check precedes the id lookup: an unknown id with a
        // bad budget reports the budget, not not_found.
        MIRAGE_CHECK(apps->launch(missing_id, negative_timeout, {}).error.code ==
                     "invalid_argument");
        MIRAGE_CHECK(apps->launch("", {}, {}).error.code == "invalid_argument");

        std::string oversized_id(5000, 'x');
        oversized_id += ".desktop";
        MIRAGE_CHECK(apps->launch(oversized_id, {}, {}).error.code == "invalid_argument");

        // Unknown ids never create a process: no instance and no name match
        // for the not-yet-launched main fixture application.
        const auto unknown_launch = apps->launch(missing_id, {}, {});
        MIRAGE_CHECK(!unknown_launch.ok);
        MIRAGE_CHECK(unknown_launch.error.code == "not_found");
        MIRAGE_CHECK(unknown_launch.instance_id.empty());
        const auto still_stopped = apps->running_state(main_id, {});
        MIRAGE_CHECK(still_stopped.ok);
        MIRAGE_CHECK(!still_stopped.running);
        MIRAGE_CHECK(still_stopped.instance_id.empty());

        // Cancellation precedes even the unknown-id rejection and spawns
        // nothing.
        desktop::CancelToken cancelled_launch;
        cancelled_launch.request_cancel();
        const auto cancelled_outcome = apps->launch(main_id, {}, cancelled_launch);
        MIRAGE_CHECK(cancelled_outcome.cancelled);
        MIRAGE_CHECK(!cancelled_outcome.ok);
        MIRAGE_CHECK(cancelled_outcome.error.code == "cancelled");
        MIRAGE_CHECK(!apps->running_state(main_id, {}).running);

        // Broken Exec line: the spawn itself fails and surfaces as io_error
        // without an instance.
        const auto broken_launch = apps->launch(broken_id, {}, {});
        MIRAGE_CHECK(!broken_launch.ok);
        MIRAGE_CHECK(broken_launch.error.code == "io_error");
        MIRAGE_CHECK(apps->running_state(broken_id, {}).ok);
        MIRAGE_CHECK(!apps->running_state(broken_id, {}).running);

        std::fprintf(stderr, "[application_backend_test] scenario: launch_and_running_state\n");
        const auto launched = apps->launch(main_id, {}, {});
        MIRAGE_CHECK(launched.ok);
        MIRAGE_CHECK(all_decimal_digits(launched.instance_id));
        const pid_t main_pid =
            launched.instance_id.empty() ? -1 : std::atoi(launched.instance_id.c_str());
        MIRAGE_CHECK(main_pid > 0);
        tracker.track(main_pid);
        MIRAGE_CHECK(process_alive(main_pid));

        const auto running = apps->running_state(main_id, {});
        MIRAGE_CHECK(running.ok);
        MIRAGE_CHECK(running.running);
        MIRAGE_CHECK(running.instance_id == launched.instance_id);

        const auto listed_while_running = apps->list_applications();
        MIRAGE_CHECK(listed_while_running.ok);
        const desktop::ApplicationInfo *running_entry = find_entry(listed_while_running, main_id);
        MIRAGE_CHECK(running_entry != nullptr);
        if (running_entry != nullptr) {
            MIRAGE_CHECK(running_entry->running);
        }
        // Everything else stays reported as stopped.
        for (const std::string &quiet_id : {stuck_id, foreign_id, quick_id, dup_id}) {
            const desktop::ApplicationInfo *quiet_entry =
                find_entry(listed_while_running, quiet_id);
            MIRAGE_CHECK(quiet_entry != nullptr);
            if (quiet_entry != nullptr) {
                MIRAGE_CHECK(!quiet_entry->running);
            }
        }

        // A second launch on a live single instance is refused without
        // spawning a duplicate.
        const auto duplicate = apps->launch(main_id, {}, {});
        MIRAGE_CHECK(!duplicate.ok);
        MIRAGE_CHECK(duplicate.error.code == "already_running");
        MIRAGE_CHECK(duplicate.instance_id.empty());
        MIRAGE_CHECK(apps->running_state(main_id, {}).instance_id == launched.instance_id);

        std::fprintf(stderr, "[application_backend_test] scenario: terminate_tracked\n");
        desktop::ApplicationLaunchLimits terminate_budget;
        terminate_budget.timeout = std::chrono::milliseconds{10000};
        const auto terminated = apps->terminate(main_id, terminate_budget, {});
        MIRAGE_CHECK(terminated.ok);
        MIRAGE_CHECK(terminated.instance_id == launched.instance_id);
        // The backend reaped its child: the process is fully gone, not a
        // zombie.
        errno = 0;
        MIRAGE_CHECK(::kill(main_pid, 0) != 0 && errno == ESRCH);

        const auto stopped = apps->running_state(main_id, {});
        MIRAGE_CHECK(stopped.ok);
        MIRAGE_CHECK(!stopped.running);
        MIRAGE_CHECK(stopped.instance_id.empty());
        const auto listed_after_stop = apps->list_applications();
        MIRAGE_CHECK(listed_after_stop.ok);
        const desktop::ApplicationInfo *stopped_entry = find_entry(listed_after_stop, main_id);
        MIRAGE_CHECK(stopped_entry != nullptr);
        if (stopped_entry != nullptr) {
            MIRAGE_CHECK(!stopped_entry->running);
        }
        // Terminating an application without a live instance is not_found.
        const auto repeat_terminate = apps->terminate(main_id, terminate_budget, {});
        MIRAGE_CHECK(!repeat_terminate.ok);
        MIRAGE_CHECK(repeat_terminate.error.code == "not_found");
        // Termination argument validation mirrors launch.
        MIRAGE_CHECK(apps->terminate(main_id, negative_timeout, {}).error.code ==
                     "invalid_argument");
        MIRAGE_CHECK(apps->terminate("", {}, {}).error.code == "invalid_argument");
        MIRAGE_CHECK(apps->terminate(missing_id, {}, {}).error.code == "not_found");
        desktop::CancelToken cancelled_terminate;
        cancelled_terminate.request_cancel();
        const auto cancelled_termination =
            apps->terminate(main_id, terminate_budget, cancelled_terminate);
        MIRAGE_CHECK(cancelled_termination.cancelled);
        MIRAGE_CHECK(cancelled_termination.error.code == "cancelled");

        std::fprintf(stderr, "[application_backend_test] scenario: foreign_instance\n");
        const pid_t foreign_pid = spawn_foreign_instance(foreign_helper, "hold", tracker);
        MIRAGE_CHECK(foreign_pid > 0);
        MIRAGE_CHECK(process_alive(foreign_pid));
        // The name scan recognizes the user-started instance.
        const auto foreign_running = apps->running_state(foreign_id, {});
        MIRAGE_CHECK(foreign_running.ok);
        MIRAGE_CHECK(foreign_running.running);
        MIRAGE_CHECK(foreign_running.instance_id == std::to_string(foreign_pid));
        // A launch while a foreign instance lives is already_running and
        // spawns no duplicate.
        const auto foreign_duplicate = apps->launch(foreign_id, {}, {});
        MIRAGE_CHECK(!foreign_duplicate.ok);
        MIRAGE_CHECK(foreign_duplicate.error.code == "already_running");
        // Cooperative termination reaches the foreign pid directly (no
        // process-group signal: the group is not ours).
        const auto foreign_terminated = apps->terminate(foreign_id, terminate_budget, {});
        MIRAGE_CHECK(foreign_terminated.ok);
        MIRAGE_CHECK(foreign_terminated.instance_id == std::to_string(foreign_pid));
        errno = 0;
        MIRAGE_CHECK(::kill(foreign_pid, 0) != 0 && errno == ESRCH);
        MIRAGE_CHECK(!apps->running_state(foreign_id, {}).running);

        std::fprintf(stderr, "[application_backend_test] scenario: stuck_instance_deadline\n");
        std::filesystem::remove(stuck_ready);
        const auto stuck_launch = apps->launch(stuck_id, {}, {});
        MIRAGE_CHECK(stuck_launch.ok);
        const pid_t stuck_pid =
            stuck_launch.instance_id.empty() ? -1 : std::atoi(stuck_launch.instance_id.c_str());
        MIRAGE_CHECK(stuck_pid > 0);
        tracker.track(stuck_pid);
        MIRAGE_CHECK(wait_for_ready_file(stuck_ready));
        MIRAGE_CHECK(process_alive(stuck_pid));
        desktop::ApplicationLaunchLimits tight_budget;
        tight_budget.timeout = std::chrono::milliseconds{300};
        const auto stuck_termination = apps->terminate(stuck_id, tight_budget, {});
        MIRAGE_CHECK(!stuck_termination.ok);
        MIRAGE_CHECK(stuck_termination.error.code == "deadline_exceeded");
        // No forced kill: the stuck instance outlives the failed request.
        MIRAGE_CHECK(process_alive(stuck_pid));
        tracker.kill_now(stuck_pid);
        MIRAGE_CHECK(!process_alive(stuck_pid));
        // Any provider call flushes the stale registry entry afterwards.
        MIRAGE_CHECK(apps->running_state(stuck_id, {}).ok);
        MIRAGE_CHECK(!apps->running_state(stuck_id, {}).running);

        std::fprintf(stderr, "[application_backend_test] scenario: terminate_cancelled\n");
        std::filesystem::remove(stuck_ready);
        const auto second_stuck_launch = apps->launch(stuck_id, {}, {});
        MIRAGE_CHECK(second_stuck_launch.ok);
        const pid_t second_stuck_pid = second_stuck_launch.instance_id.empty()
                                           ? -1
                                           : std::atoi(second_stuck_launch.instance_id.c_str());
        MIRAGE_CHECK(second_stuck_pid > 0);
        tracker.track(second_stuck_pid);
        MIRAGE_CHECK(wait_for_ready_file(stuck_ready));
        // Cancellation must arrive while the cooperative wait is running;
        // the wait blocks this thread, so a helper thread raises the token.
        desktop::CancelToken mid_cancel;
        std::thread canceller([&mid_cancel] {
            std::this_thread::sleep_for(std::chrono::milliseconds{150});
            mid_cancel.request_cancel();
        });
        desktop::ApplicationLaunchLimits patient_budget;
        patient_budget.timeout = std::chrono::milliseconds{10000};
        const auto cancelled_mid_wait = apps->terminate(stuck_id, patient_budget, mid_cancel);
        canceller.join();
        MIRAGE_CHECK(cancelled_mid_wait.cancelled);
        MIRAGE_CHECK(!cancelled_mid_wait.ok);
        MIRAGE_CHECK(cancelled_mid_wait.error.code == "cancelled");
        MIRAGE_CHECK(process_alive(second_stuck_pid));
        tracker.kill_now(second_stuck_pid);
        MIRAGE_CHECK(!process_alive(second_stuck_pid));

        std::fprintf(stderr, "[application_backend_test] scenario: opportunistic_reap\n");
        const auto quick_launch = apps->launch(quick_id, {}, {});
        MIRAGE_CHECK(quick_launch.ok);
        const pid_t quick_pid =
            quick_launch.instance_id.empty() ? -1 : std::atoi(quick_launch.instance_id.c_str());
        MIRAGE_CHECK(quick_pid > 0);
        // Give the helper time to exit on its own: it is now a zombie (the
        // backend owns DO_NOT_REAP_CHILD children).
        std::this_thread::sleep_for(std::chrono::milliseconds{300});
        MIRAGE_CHECK(process_alive(quick_pid)); // zombie: kill(pid, 0) still reports it
        // The next provider call reaps it and answers from a clean state —
        // no background thread involved.
        const auto reaped = apps->running_state(quick_id, {});
        MIRAGE_CHECK(reaped.ok);
        MIRAGE_CHECK(!reaped.running);
        MIRAGE_CHECK(reaped.instance_id.empty());
        errno = 0;
        MIRAGE_CHECK(::kill(quick_pid, 0) != 0 && errno == ESRCH);
        const auto reaped_again = apps->running_state(quick_id, {});
        MIRAGE_CHECK(reaped_again.ok);
        MIRAGE_CHECK(!reaped_again.running);

        // DEC-071: a running application's URI handoff must still execute the
        // fixed desktop entry, preserving a single argument without shell evaluation.
        MIRAGE_CHECK(apps->supports_open_uri());
        const auto uri_log = fixture_root / "uri-argv.log";
        ::setenv("MIRAGE_TEST_URI_LOG", uri_log.c_str(), 1);
        for (const auto &bad : {"file:///tmp/test", "javascript:alert(1)", "https://",
                                "https://user:password@example.invalid",
                                "https://example.invalid/a b", "https://example.invalid/\n"}) {
            const auto refused = apps->open_uri(main_id, bad, {}, {});
            MIRAGE_CHECK(!refused.ok && !refused.side_effect_may_have_occurred);
        }
        MIRAGE_CHECK(
            !apps->open_uri(main_id, "https://example.invalid/" + std::string(2048, 'x'), {}, {})
                 .ok);
        MIRAGE_CHECK(apps->open_uri(foreign_id, "https://example.invalid/", {}, {}).error.code ==
                     "unsupported_content");
        desktop::ApplicationLaunchLimits invalid_uri_limits;
        invalid_uri_limits.timeout = std::chrono::milliseconds(0);
        const auto timed_uri =
            apps->open_uri(main_id, "https://example.invalid/", invalid_uri_limits, {});
        MIRAGE_CHECK(!timed_uri.ok && !timed_uri.side_effect_may_have_occurred);
        desktop::CancelToken cancelled_uri;
        cancelled_uri.request_cancel();
        MIRAGE_CHECK(
            apps->open_uri(main_id, "https://example.invalid/", {}, cancelled_uri).cancelled);
        MIRAGE_CHECK(!std::filesystem::exists(uri_log));
        const auto browser = apps->launch(main_id, {}, {});
        MIRAGE_CHECK(browser.ok);
        tracker.track(std::atoi(browser.instance_id.c_str()));
        const std::string uri =
            "https://example.invalid/watch?q=$(false)&x=`false`&encoded=%20#fragment";
        for (int attempt = 0; attempt < 2; ++attempt) {
            const auto accepted = apps->open_uri(main_id, uri, {}, {});
            MIRAGE_CHECK(accepted.ok && accepted.side_effect_may_have_occurred);
            MIRAGE_CHECK(accepted.instance_id != browser.instance_id);
            tracker.track(std::atoi(accepted.instance_id.c_str()));
        }
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::string delivered;
        do {
            std::ifstream file(uri_log);
            delivered.assign(std::istreambuf_iterator<char>(file), {});
            if (delivered == uri + "\n" + uri + "\n")
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < until);
        MIRAGE_CHECK(delivered == uri + "\n" + uri + "\n");
        for (int instance = 0; instance < 3; ++instance)
            MIRAGE_CHECK(apps->terminate(main_id, {}, {}).ok);
        MIRAGE_CHECK(!apps->running_state(main_id, {}).running);
        ::unsetenv("MIRAGE_TEST_URI_LOG");

        std::fprintf(stderr,
                     "[application_backend_test] scenario: dbus_activatable_direct_spawn\n");
        // DEC-086: a DBusActivatable entry must not degrade a successful
        // launch into an uncertain side effect. The Exec argv is spawned
        // directly, the child reports its pid through the same pipe, and the
        // instance is tracked, visible, and cooperatively terminable.
        const auto launched_dbus = apps->launch(dbus_id, {}, {});
        MIRAGE_CHECK(launched_dbus.ok);
        MIRAGE_CHECK(all_decimal_digits(launched_dbus.instance_id));
        const pid_t dbus_pid =
            launched_dbus.instance_id.empty() ? -1 : std::atoi(launched_dbus.instance_id.c_str());
        MIRAGE_CHECK(dbus_pid > 0);
        tracker.track(dbus_pid);
        MIRAGE_CHECK(process_alive(dbus_pid));
        const auto dbus_running = apps->running_state(dbus_id, {});
        MIRAGE_CHECK(dbus_running.ok);
        MIRAGE_CHECK(dbus_running.running);
        MIRAGE_CHECK(dbus_running.instance_id == launched_dbus.instance_id);
        const auto dbus_listed = apps->list_applications();
        MIRAGE_CHECK(dbus_listed.ok);
        const desktop::ApplicationInfo *dbus_entry = find_entry(dbus_listed, dbus_id);
        MIRAGE_CHECK(dbus_entry != nullptr);
        if (dbus_entry != nullptr) {
            MIRAGE_CHECK(dbus_entry->running);
        }
        // A second launch on the tracked direct-spawned instance is refused.
        const auto dbus_duplicate = apps->launch(dbus_id, {}, {});
        MIRAGE_CHECK(!dbus_duplicate.ok);
        MIRAGE_CHECK(dbus_duplicate.error.code == "already_running");
        const auto dbus_terminated = apps->terminate(dbus_id, terminate_budget, {});
        MIRAGE_CHECK(dbus_terminated.ok);
        MIRAGE_CHECK(dbus_terminated.instance_id == launched_dbus.instance_id);
        errno = 0;
        MIRAGE_CHECK(::kill(dbus_pid, 0) != 0 && errno == ESRCH);
        const auto dbus_stopped = apps->running_state(dbus_id, {});
        MIRAGE_CHECK(dbus_stopped.ok);
        MIRAGE_CHECK(!dbus_stopped.running);
        MIRAGE_CHECK(dbus_stopped.instance_id.empty());

        // Field-code expansion, observed through the helper's URI log and its
        // ready-file proxy: %U substitutes the validated URI, %%u survives as
        // the literal text "%u" with the URI appended after it, and %% in a
        // plain token collapses to a single %.
        const std::filesystem::path dbus_uri_log = fixture_root / "dbus-uri-argv.log";
        ::setenv("MIRAGE_TEST_URI_LOG", dbus_uri_log.c_str(), 1);
        const std::string dbus_uri = "https://example.invalid/dbus-fixture";
        const auto substituted = apps->open_uri(dbus_id, dbus_uri, {}, {});
        MIRAGE_CHECK(substituted.ok);
        tracker.track(std::atoi(substituted.instance_id.c_str()));
        const auto until_dbus_uri = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        std::string delivered_dbus;
        do {
            std::ifstream file(dbus_uri_log);
            delivered_dbus.assign(std::istreambuf_iterator<char>(file), {});
            if (delivered_dbus == dbus_uri + "\n")
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < until_dbus_uri);
        MIRAGE_CHECK(delivered_dbus == dbus_uri + "\n");
        const auto substituted_terminated = apps->terminate(dbus_id, terminate_budget, {});
        MIRAGE_CHECK(substituted_terminated.ok);
        MIRAGE_CHECK(substituted_terminated.instance_id == substituted.instance_id);
        MIRAGE_CHECK(!apps->running_state(dbus_id, {}).running);

        // %%u: no field code inserts, so the URI is appended as the last
        // argument; the literal "%u" token doubles as the helper's ready
        // file, written into the Path= working directory.
        const auto appended = apps->open_uri(plain_id, dbus_uri, {}, {});
        MIRAGE_CHECK(appended.ok);
        tracker.track(std::atoi(appended.instance_id.c_str()));
        MIRAGE_CHECK(wait_for_ready_file(fixture_root / "%u"));
        std::ifstream appended_log(dbus_uri_log);
        std::string delivered_appended;
        delivered_appended.assign(std::istreambuf_iterator<char>(appended_log), {});
        MIRAGE_CHECK(delivered_appended == dbus_uri + "\n" + dbus_uri + "\n");
        const auto appended_terminated = apps->terminate(plain_id, terminate_budget, {});
        MIRAGE_CHECK(appended_terminated.ok);
        MIRAGE_CHECK(appended_terminated.instance_id == appended.instance_id);

        // %% escape without any URI involved (plain launch, no field codes).
        const auto escaped = apps->launch(pct_id, {}, {});
        MIRAGE_CHECK(escaped.ok);
        const pid_t pct_pid =
            escaped.instance_id.empty() ? -1 : std::atoi(escaped.instance_id.c_str());
        MIRAGE_CHECK(pct_pid > 0);
        tracker.track(pct_pid);
        MIRAGE_CHECK(wait_for_ready_file(fixture_root / "pct%.ready"));
        const auto escaped_terminated = apps->terminate(pct_id, terminate_budget, {});
        MIRAGE_CHECK(escaped_terminated.ok);
        MIRAGE_CHECK(escaped_terminated.instance_id == escaped.instance_id);
        ::unsetenv("MIRAGE_TEST_URI_LOG");

        // Fallback: an Exec line past the expansion caps cannot become an
        // argv, so the launch falls back to the GIO path instead of the
        // direct spawn. The fallback never takes the direct-spawn branch:
        // with a session bus the activation fails honestly (io_error),
        // without one GIO spawns the Exec itself and keeps the uncertain
        // side-effect semantics — the flag the direct branch never sets.
        const std::filesystem::path overflow_ready = fixture_root / "overflow-ready";
        const auto overflow_launch = apps->launch(dbus_overflow_id, {}, {});
        if (overflow_launch.ok) {
            MIRAGE_CHECK(overflow_launch.side_effect_may_have_occurred);
            MIRAGE_CHECK(all_decimal_digits(overflow_launch.instance_id));
            tracker.track(std::atoi(overflow_launch.instance_id.c_str()));
            // launch returns once the pid is reported, which is before the
            // child reached exec; wait for its startup marker instead.
            MIRAGE_CHECK(wait_for_ready_file(overflow_ready));
            MIRAGE_CHECK(apps->running_state(dbus_overflow_id, {}).running);
            MIRAGE_CHECK(apps->terminate(dbus_overflow_id, terminate_budget, {}).ok);
            MIRAGE_CHECK(!apps->running_state(dbus_overflow_id, {}).running);
        } else {
            MIRAGE_CHECK(overflow_launch.error.code == "io_error");
            MIRAGE_CHECK(!overflow_launch.side_effect_may_have_occurred);
            std::this_thread::sleep_for(std::chrono::milliseconds{200});
            MIRAGE_CHECK(!std::filesystem::exists(overflow_ready));
            MIRAGE_CHECK(!apps->running_state(dbus_overflow_id, {}).running);
        }
        // A DBusActivatable entry without Exec keeps the GIO activation path
        // (and its uncertainty semantics): still no direct spawn, no false
        // success, no instance.
        const auto noexec_launch = apps->launch(dbus_noexec_id, {}, {});
        MIRAGE_CHECK(!noexec_launch.ok);
        MIRAGE_CHECK(noexec_launch.error.code == "io_error");
        MIRAGE_CHECK(!noexec_launch.side_effect_may_have_occurred);
        const auto noexec_state = apps->running_state(dbus_noexec_id, {});
        MIRAGE_CHECK(noexec_state.ok);
        MIRAGE_CHECK(!noexec_state.running);
        // An Exec line that is not valid shell syntax never becomes an
        // application: GIO drops the entry at load time, so discovery and
        // launch both fail closed upstream of the backend.
        MIRAGE_CHECK(find_entry(listed, dbus_quote_id) == nullptr);
        const auto quote_launch = apps->launch(dbus_quote_id, {}, {});
        MIRAGE_CHECK(!quote_launch.ok);
        MIRAGE_CHECK(!apps->running_state(dbus_quote_id, {}).ok);

        // Non-opt-in environments keep the accessors null (fail closed).
        mirage::platform::linux_backend::LinuxDesktopEnvironment closed_env;
        MIRAGE_CHECK(closed_env.application() == nullptr);
        MIRAGE_CHECK(closed_env.notification() == nullptr);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "application backend setup failed: %s\n", error.what());
        std::filesystem::remove_all(fixture_root);
        return 1;
    }

    std::error_code cleanup_error;
    std::filesystem::remove_all(fixture_root, cleanup_error);
    return mirage::testing::finish("application_backend_test");
}
