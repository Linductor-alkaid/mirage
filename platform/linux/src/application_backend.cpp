#include "application_backend.hpp"

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <gio/gdesktopappinfo.h>
#include <gio/gio.h>

namespace mirage::platform::linux_backend {
namespace {

using mirage::desktop::ApplicationInfo;
using mirage::desktop::ApplicationLaunchLimits;
using mirage::desktop::ApplicationLaunchOutcome;
using mirage::desktop::ApplicationListLimits;
using mirage::desktop::ApplicationListOutcome;
using mirage::desktop::ApplicationQueryOutcome;
using mirage::desktop::CancelToken;
using mirage::desktop::ProviderError;

ProviderError error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Instance registry capacity and /proc scan bound (RULE-07): both are
/// fixed caps, so neither the table nor the scan can grow without bound.
constexpr std::size_t kMaxTrackedInstances = 256;
constexpr std::size_t kMaxScannedPids = 4096;
/// Poll slice for the cooperative termination wait: the deadline and the
/// token are observed at least this often (ProcessProvider precedent).
constexpr std::chrono::milliseconds kPollSlice{25};

std::string basename_of(std::string_view path) {
    const auto slash = path.find_last_of('/');
    return std::string(slash == std::string_view::npos ? path : path.substr(slash + 1));
}

/// Reads a /proc attribute file and returns its content up to the first
/// NUL or newline; "" when unreadable (exit race, permission).
std::string proc_snap(pid_t pid, const char *file) {
    const std::string path = "/proc/" + std::to_string(pid) + "/" + file;
    FILE *stream = fopen(path.c_str(), "re");
    if (stream == nullptr) {
        return "";
    }
    std::string content;
    char buffer[512];
    const std::size_t got = fread(buffer, 1, sizeof(buffer) - 1, stream);
    fclose(stream);
    content.assign(buffer, got);
    // cmdline is NUL-separated, comm ends with a newline: stop at either.
    const auto stop = std::min(content.find('\0'), content.find('\n'));
    return content.substr(0, stop);
}

/// Process state letter from /proc/<pid>/stat; 0 when unreadable.
char proc_state(pid_t pid) {
    const std::string path = "/proc/" + std::to_string(pid) + "/stat";
    FILE *stream = fopen(path.c_str(), "re");
    if (stream == nullptr) {
        return 0;
    }
    std::string stat;
    char buffer[1024];
    const std::size_t got = fread(buffer, 1, sizeof(buffer) - 1, stream);
    fclose(stream);
    stat.assign(buffer, got);
    // The comm field may contain any character including ')': fields after
    // it start behind the LAST closing parenthesis.
    const auto close = stat.rfind(')');
    if (close == std::string::npos || close + 2 >= stat.size()) {
        return 0;
    }
    return stat[close + 2];
}

/// One live process' candidate executable names: the exe symlink target's
/// basename when readable, argv[0]'s basename, and comm. Kernel threads
/// have no cmdline and report their thread name; duplicates are removed.
std::vector<std::string> process_names(pid_t pid) {
    std::vector<std::string> names;
    const std::string cmdline = proc_snap(pid, "cmdline");
    if (!cmdline.empty()) {
        names.push_back(basename_of(cmdline));
    }
    char exe[4096];
    const ssize_t length =
        readlink(("/proc/" + std::to_string(pid) + "/exe").c_str(), exe, sizeof(exe) - 1);
    if (length > 0) {
        names.push_back(basename_of(std::string_view(exe, static_cast<std::size_t>(length))));
    }
    const std::string comm = proc_snap(pid, "comm");
    if (!comm.empty()) {
        names.push_back(comm);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

/// Start time of a process in jiffies since boot (/proc/<pid>/stat field
/// 22); -1 when unreadable. Used to make every "which instance" answer
/// deterministic: the oldest live process wins.
long proc_starttime(pid_t pid) {
    const std::string path = "/proc/" + std::to_string(pid) + "/stat";
    FILE *stream = fopen(path.c_str(), "re");
    if (stream == nullptr) {
        return -1;
    }
    std::string stat;
    char buffer[1024];
    const std::size_t got = fread(buffer, 1, sizeof(buffer) - 1, stream);
    fclose(stream);
    stat.assign(buffer, got);
    const auto close = stat.rfind(')');
    if (close == std::string::npos) {
        return -1;
    }
    std::string_view fields(stat);
    fields = fields.substr(close + 2); // fields[0] is the state (field 3)
    std::size_t start = 0;
    for (int index = 0; index < 19; ++index) {
        const auto space = fields.find(' ', start);
        if (space == std::string_view::npos) {
            return -1;
        }
        start = space + 1;
    }
    const auto end = fields.find(' ', start);
    const std::string_view token =
        fields.substr(start, end == std::string_view::npos ? fields.size() - start : end - start);
    if (token.empty()) {
        return -1;
    }
    return std::strtol(std::string(token).c_str(), nullptr, 10);
}

/// The binary name a desktop entry's Exec line launches: argv[0]'s basename
/// via shell-word parsing, falling back to the first whitespace token when
/// the line does not parse. Exec is optional for D-Bus-activatable entries;
/// an empty binary means foreign instances cannot be recognized by name and
/// only tracked ones can.
std::string exec_binary(GDesktopAppInfo *info) {
    // g_desktop_app_info_get_string transfers the string: every path below
    // must free it.
    gchar *exec = g_desktop_app_info_get_string(info, "Exec");
    if (exec == nullptr) {
        return "";
    }
    gchar **argv = nullptr;
    gint argc = 0;
    GError *parse_error = nullptr;
    std::string binary;
    if (g_shell_parse_argv(exec, &argc, &argv, &parse_error) && argc > 0 && argv[0] != nullptr) {
        binary = basename_of(argv[0]);
    } else {
        const std::string_view raw(exec);
        const auto end = raw.find_first_of(" \t");
        binary = basename_of(raw.substr(0, end == std::string_view::npos ? raw.size() : end));
    }
    if (parse_error != nullptr) {
        g_error_free(parse_error);
    }
    if (argv != nullptr) {
        g_strfreev(argv);
    }
    g_free(exec);
    return binary;
}

/// Live pids (bounded scan) whose candidate names contain `binary`, ordered
/// oldest first (starttime, then pid). The calling process is excluded;
/// zombies count as gone (they hold no live work).
std::vector<pid_t> find_live_instances(const std::string &binary) {
    std::vector<pid_t> matches;
    if (binary.empty()) {
        return matches;
    }
    DIR *proc = opendir("/proc");
    if (proc == nullptr) {
        return matches;
    }
    const pid_t self = getpid();
    std::size_t visited = 0;
    while (visited < kMaxScannedPids) {
        const dirent *entry = readdir(proc);
        if (entry == nullptr) {
            break;
        }
        const std::string_view name(entry->d_name);
        if (name.empty() || name.find_first_not_of("0123456789") != std::string_view::npos) {
            continue;
        }
        ++visited;
        const pid_t pid = std::atoi(entry->d_name);
        if (pid <= 0 || pid == self) {
            continue;
        }
        if (proc_state(pid) == 'Z') {
            continue;
        }
        for (const std::string &candidate : process_names(pid)) {
            if (candidate == binary) {
                matches.push_back(pid);
                break;
            }
        }
    }
    closedir(proc);
    std::sort(matches.begin(), matches.end(), [](pid_t left, pid_t right) {
        const long left_time = proc_starttime(left);
        const long right_time = proc_starttime(right);
        if (left_time != right_time) {
            return left_time < right_time;
        }
        return left < right;
    });
    return matches;
}

/// Index of candidate process names to pids for the whole (bounded) /proc
/// scan, so one enumeration answers running flags for every application at
/// once instead of one scan per application.
std::map<std::string, std::vector<pid_t>> scan_process_index() {
    std::map<std::string, std::vector<pid_t>> index;
    DIR *proc = opendir("/proc");
    if (proc == nullptr) {
        return index;
    }
    const pid_t self = getpid();
    std::size_t visited = 0;
    while (visited < kMaxScannedPids) {
        const dirent *entry = readdir(proc);
        if (entry == nullptr) {
            break;
        }
        const std::string_view name(entry->d_name);
        if (name.empty() || name.find_first_not_of("0123456789") != std::string_view::npos) {
            continue;
        }
        ++visited;
        const pid_t pid = std::atoi(entry->d_name);
        if (pid <= 0 || pid == self || proc_state(pid) == 'Z') {
            continue;
        }
        for (const std::string &candidate : process_names(pid)) {
            index[candidate].push_back(pid);
        }
    }
    closedir(proc);
    return index;
}

/// Launch registry state (Impl derives from it so the free helpers below
/// can take a reference without naming the private nested type).
struct ApplicationState {
    /// Launched instances by application id. One live instance per id is
    /// enforced ("already_running"), so the table only grows with distinct
    /// ids up to its capacity (RULE-07).
    std::map<std::string, pid_t> instances;
};

/// Launched children that exited on their own must not linger as zombies;
/// called at every provider method entry so the table reflects reality
/// without any background thread (RULE-03).
void reap_locked(ApplicationState &state) {
    for (auto it = state.instances.begin(); it != state.instances.end();) {
        int status = 0;
        const pid_t gone = waitpid(it->second, &status, WNOHANG);
        if (gone == it->second || (gone < 0 && errno == ECHILD)) {
            it = state.instances.erase(it);
        } else {
            ++it;
        }
    }
}

/// The backend's view of a live instance for one application: the pid it
/// launched itself, or — when that one already exited (possibly after
/// forking a replacement) — the oldest live process matching the desktop
/// entry's binary name. Returns the pid and whether it is tracked.
struct Instance {
    pid_t pid = -1;
    bool tracked = false;
};

std::optional<Instance> find_instance_locked(ApplicationState &state,
                                             const std::string &application_id,
                                             const std::string &binary) {
    const auto tracked = state.instances.find(application_id);
    if (tracked != state.instances.end()) {
        int status = 0;
        if (waitpid(tracked->second, &status, WNOHANG) == 0) {
            return Instance{tracked->second, true};
        }
        state.instances.erase(tracked);
    }
    const auto matches = find_live_instances(binary);
    if (matches.empty()) {
        return std::nullopt;
    }
    return Instance{matches.front(), false};
}

} // namespace

struct ApplicationBackend::Impl : ApplicationState {};

ApplicationBackend::~ApplicationBackend() = default;

std::unique_ptr<ApplicationBackend> ApplicationBackend::open() {
    auto backend = std::unique_ptr<ApplicationBackend>(new ApplicationBackend());
    // The launch registry lives in Impl; the provider methods dereference
    // impl_ unconditionally on entry (reap), so it must exist for every
    // constructed backend.
    backend->impl_ = std::make_unique<Impl>();
    return backend;
}

ApplicationListOutcome ApplicationBackend::list_applications(const ApplicationListLimits &limits,
                                                             const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    ApplicationListOutcome outcome;
    reap_locked(*impl_);
    if (cancel.cancelled()) {
        outcome.error = error("cancelled", "listing cancelled");
        return outcome;
    }

    struct Entry {
        std::string name;
        std::string binary;
    };
    std::map<std::string, Entry> by_id;
    GList *all = g_app_info_get_all();
    for (GList *node = all; node != nullptr; node = node->next) {
        GObject *object = G_OBJECT(node->data);
        if (!G_IS_DESKTOP_APP_INFO(object)) {
            continue;
        }
        GDesktopAppInfo *desktop = G_DESKTOP_APP_INFO(object);
        // Hidden and NoDisplay entries are not user-visible applications;
        // the agent surface only exposes what a desktop launcher would.
        if (g_desktop_app_info_get_is_hidden(desktop) ||
            g_desktop_app_info_get_nodisplay(desktop)) {
            continue;
        }
        const gchar *id = g_app_info_get_id(G_APP_INFO(desktop));
        if (id == nullptr || *id == '\0') {
            continue;
        }
        const gchar *display = g_app_info_get_display_name(G_APP_INFO(desktop));
        const std::string key(id);
        // GIO already resolves XDG precedence per id; dedup defensively.
        by_id.emplace(key, Entry{display != nullptr ? std::string(display) : std::string(),
                                 exec_binary(desktop)});
    }
    g_list_free_full(all, g_object_unref);

    if (by_id.size() > limits.max_applications) {
        outcome.error =
            error("result_too_large", "application table exceeds the budget of " +
                                          std::to_string(limits.max_applications) + " entries");
        return outcome;
    }
    const std::map<std::string, std::vector<pid_t>> index = scan_process_index();
    outcome.applications.reserve(by_id.size());
    for (const auto &[id, entry] : by_id) {
        const bool running = !entry.binary.empty() && index.count(entry.binary) != 0;
        outcome.applications.push_back({id, entry.name, running});
    }
    outcome.ok = true;
    return outcome;
}

ApplicationQueryOutcome ApplicationBackend::running_state(const std::string &application_id,
                                                          const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    ApplicationQueryOutcome outcome;
    reap_locked(*impl_);
    if (cancel.cancelled()) {
        outcome.error = error("cancelled", "query cancelled");
        return outcome;
    }
    if (application_id.empty()) {
        outcome.error = error("invalid_argument", "application id must not be empty");
        return outcome;
    }
    GDesktopAppInfo *info = g_desktop_app_info_new(application_id.c_str());
    if (info == nullptr) {
        outcome.error = error("not_found", "unknown application id");
        return outcome;
    }
    const std::string binary = exec_binary(info);
    g_object_unref(info);
    const std::optional<Instance> instance = find_instance_locked(*impl_, application_id, binary);
    outcome.ok = true;
    if (instance.has_value()) {
        outcome.running = true;
        outcome.instance_id = std::to_string(instance->pid);
    }
    return outcome;
}

ApplicationLaunchOutcome ApplicationBackend::launch(const std::string &application_id,
                                                    const ApplicationLaunchLimits &limits,
                                                    const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    ApplicationLaunchOutcome outcome;
    reap_locked(*impl_);
    // Argument validation happens before any side effect, in the contract's
    // order: a refused launch must never have reached a fork.
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "launch cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    if (application_id.empty()) {
        outcome.error = error("invalid_argument", "application id must not be empty");
        return outcome;
    }
    if (application_id.size() > limits.max_command_bytes) {
        outcome.error = error("invalid_argument", "application id exceeds the command budget");
        return outcome;
    }
    GDesktopAppInfo *info = g_desktop_app_info_new(application_id.c_str());
    if (info == nullptr) {
        outcome.error = error("not_found", "unknown application id");
        return outcome;
    }
    const std::string binary = exec_binary(info);
    if (find_instance_locked(*impl_, application_id, binary).has_value()) {
        g_object_unref(info);
        outcome.error = error("already_running", "application already has a live instance");
        return outcome;
    }
    if (impl_->instances.size() >= kMaxTrackedInstances) {
        g_object_unref(info);
        outcome.error =
            error("result_too_large", "instance registry reached its capacity of " +
                                          std::to_string(kMaxTrackedInstances) + " entries");
        return outcome;
    }

    // Spawn with the pid captured from the child itself: the child setup
    // runs after fork and before exec, so it writes its own pid through a
    // CLOEXEC pipe and enters its own process group (whole-instance signal
    // delivery on terminate). DO_NOT_REAP_CHILD keeps the pid waitable so
    // this backend owns the reaping (opportunistic, per provider call).
    int pid_pipe[2];
    if (pipe2(pid_pipe, O_CLOEXEC) != 0) {
        g_object_unref(info);
        outcome.error = error("io_error", "pipe creation failed");
        return outcome;
    }
    GAppLaunchContext *context = g_app_launch_context_new();
    GError *gio_error = nullptr;
    const gboolean spawned = g_desktop_app_info_launch_uris_as_manager(
        info, nullptr, context, G_SPAWN_DO_NOT_REAP_CHILD,
        [](gpointer user_data) {
            const int fd = GPOINTER_TO_INT(user_data);
            const pid_t self = getpid();
            ssize_t ignored = ::write(fd, &self, sizeof(self));
            (void)ignored;
            ::setpgid(0, 0);
        },
        GINT_TO_POINTER(pid_pipe[1]), nullptr, nullptr, &gio_error);
    ::close(pid_pipe[1]);
    g_object_unref(context);
    if (spawned == FALSE) {
        ::close(pid_pipe[0]);
        const std::string reason = gio_error != nullptr ? gio_error->message : "launch failed";
        if (gio_error != nullptr) {
            g_error_free(gio_error);
        }
        g_object_unref(info);
        outcome.error = error("io_error", reason);
        return outcome;
    }
    pid_t pid = -1;
    const ssize_t got = ::read(pid_pipe[0], &pid, sizeof(pid));
    ::close(pid_pipe[0]);
    g_object_unref(info);
    if (got != static_cast<ssize_t>(sizeof(pid)) || pid <= 0) {
        outcome.error = error("io_error", "spawned process pid was not reported");
        return outcome;
    }
    impl_->instances.emplace(application_id, pid);
    outcome.ok = true;
    outcome.instance_id = std::to_string(pid);
    return outcome;
}

ApplicationLaunchOutcome ApplicationBackend::terminate(const std::string &application_id,
                                                       const ApplicationLaunchLimits &limits,
                                                       const CancelToken &cancel) {
    std::lock_guard<std::mutex> guard(mutex_);
    ApplicationLaunchOutcome outcome;
    reap_locked(*impl_);
    if (cancel.cancelled()) {
        outcome.cancelled = true;
        outcome.error = error("cancelled", "termination cancelled");
        return outcome;
    }
    if (limits.timeout.count() <= 0) {
        outcome.error = error("invalid_argument", "timeout must be positive");
        return outcome;
    }
    if (application_id.empty()) {
        outcome.error = error("invalid_argument", "application id must not be empty");
        return outcome;
    }
    GDesktopAppInfo *info = g_desktop_app_info_new(application_id.c_str());
    if (info == nullptr) {
        outcome.error = error("not_found", "unknown application id");
        return outcome;
    }
    const std::string binary = exec_binary(info);
    g_object_unref(info);
    const std::optional<Instance> instance = find_instance_locked(*impl_, application_id, binary);
    if (!instance.has_value()) {
        outcome.error = error("not_found", "application has no running instance");
        return outcome;
    }
    const pid_t pid = instance->pid;
    const std::string instance_id = std::to_string(pid);

    // Ask the instance to exit: SIGTERM, no SIGKILL — forced termination is
    // out of contract scope and a surviving instance is a visible failure.
    // Only tracked instances get the whole-group signal: their child setup
    // put them in their own process group. For foreign pids, a group with
    // that id may not even belong to the application.
    if (::kill(pid, SIGTERM) != 0 && errno == ESRCH) {
        if (instance->tracked) {
            impl_->instances.erase(application_id);
        }
        outcome.ok = true;
        outcome.instance_id = instance_id;
        return outcome;
    }
    if (instance->tracked) {
        ::kill(-pid, SIGTERM);
    }

    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    for (;;) {
        bool alive = true;
        if (instance->tracked) {
            int status = 0;
            const pid_t state = waitpid(pid, &status, WNOHANG);
            if (state == pid || (state < 0 && errno == ECHILD)) {
                alive = false;
            }
        } else if (::kill(pid, 0) != 0 && errno == ESRCH) {
            alive = false;
        }
        if (!alive) {
            if (instance->tracked) {
                impl_->instances.erase(application_id);
            }
            outcome.ok = true;
            outcome.instance_id = instance_id;
            return outcome;
        }
        if (cancel.cancelled()) {
            outcome.cancelled = true;
            outcome.error =
                error("cancelled", "termination cancelled while the instance was still running");
            return outcome;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            outcome.error = error("deadline_exceeded",
                                  "instance ignored the termination request within the budget");
            return outcome;
        }
        std::this_thread::sleep_for(std::min(kPollSlice, remaining));
    }
}

} // namespace mirage::platform::linux_backend
