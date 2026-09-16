#include <mirage/platform/linux/linux_desktop_environment.hpp>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace mirage::platform::linux_backend {
namespace {

constexpr std::size_t kReadChunkBytes = 64u * 1024;
/// Cancel observation slice for the capture poll: the poll never sleeps
/// longer than this without re-checking the token, so a cancellation ends a
/// running command within a bounded, sub-second latency even though the
/// token carries no wake-up file descriptor.
constexpr std::chrono::milliseconds kCancelPollSlice{25};

mirage::desktop::ProviderError provider_error(std::string code, std::string message) {
    return {std::move(code), std::move(message)};
}

/// Chunked, budget- and cancellation-aware read of an opened regular file.
/// The budget is enforced during the read as well (a file can grow between
/// the size check and the read), and the token is observed once per chunk,
/// so both paths end the read without unbounded work.
mirage::desktop::FileReadOutcome read_stream(int fd, std::uintmax_t declared_size,
                                             const mirage::desktop::FileReadLimits& limits,
                                             const mirage::desktop::CancelToken& cancel) {
    mirage::desktop::FileReadOutcome outcome;
    std::string content;
    content.reserve(static_cast<std::size_t>(declared_size < limits.max_bytes
                                                 ? declared_size
                                                 : limits.max_bytes));
    std::vector<char> buffer(kReadChunkBytes);
    for (;;) {
        if (cancel.cancelled()) {
            return {false, {},
                    provider_error("cancelled", "read was cancelled before it finished")};
        }
        const ssize_t received = ::read(fd, buffer.data(), buffer.size());
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return {false, {}, provider_error("io_error", std::string("read failed: ") +
                                                             std::strerror(errno))};
        }
        if (received == 0) {
            break;
        }
        if (content.size() + static_cast<std::size_t>(received) > limits.max_bytes) {
            return {false, {},
                    provider_error("file_too_large", "file exceeds the read budget of " +
                                                         std::to_string(limits.max_bytes) +
                                                         " bytes")};
        }
        content.append(buffer.data(), static_cast<std::size_t>(received));
    }
    outcome.ok = true;
    outcome.content = std::move(content);
    return outcome;
}

} // namespace

mirage::desktop::EnvironmentInfo LinuxDesktopEnvironment::info() const {
    return {"mirage-linux-reference", "linux"};
}

mirage::desktop::FileReadOutcome
LinuxDesktopEnvironment::read_text_file(const std::filesystem::path& path,
                                        const mirage::desktop::FileReadLimits& limits,
                                        const mirage::desktop::CancelToken& cancel) {
    if (path.empty()) {
        return {false, {}, provider_error("invalid_argument", "path must not be empty")};
    }
    if (limits.max_bytes == 0) {
        return {false, {},
                provider_error("invalid_argument", "read budget must be positive")};
    }
    // Scope containment before anything else: an out-of-scope path is a
    // permission refusal, not a lookup, and leaks no filesystem detail.
    if (!read_scope_.contains(path)) {
        return {false, {},
                provider_error("permission_denied",
                               "path is outside the allowed read scope: " + path.string())};
    }

    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        return {false, {}, provider_error("not_found", "file does not exist: " + path.string())};
    }
    if (!std::filesystem::is_regular_file(status)) {
        return {false, {},
                provider_error("invalid_argument", "path is not a regular file: " + path.string())};
    }
    const auto size = std::filesystem::file_size(path, ec);
    if (!ec && size > limits.max_bytes) {
        return {false, {},
                provider_error("file_too_large", "file exceeds the read budget of " +
                                                     std::to_string(limits.max_bytes) +
                                                     " bytes")};
    }

    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        const bool denied = errno == EACCES || errno == EPERM;
        return {false, {},
                provider_error(denied ? "permission_denied" : "io_error",
                               "cannot open file: " + path.string())};
    }
    mirage::desktop::FileReadOutcome outcome;
    try {
        outcome = read_stream(fd, ec ? 0 : size, limits, cancel);
    } catch (const std::bad_alloc&) {
        outcome = {false, {}, provider_error("file_too_large", "file exceeds the read budget")};
    }
    ::close(fd);
    return outcome;
}

mirage::desktop::ProcessOutcome
LinuxDesktopEnvironment::execute(const std::string& command,
                                 const mirage::desktop::ProcessLimits& limits,
                                 const mirage::desktop::CancelToken& cancel) {
    using mirage::desktop::ProcessLimits;
    using mirage::desktop::ProcessOutcome;
    // Argument validation happens before any side effect: a refused command
    // must never have reached a shell.
    if (command.empty()) {
        return {false, false, false, false, false, -1, {}, {},
                provider_error("invalid_argument", "command must not be empty")};
    }
    if (limits.timeout <= std::chrono::milliseconds::zero() ||
        limits.max_output_bytes == 0 || limits.max_command_bytes == 0) {
        return {false, false, false, false, false, -1, {}, {},
                provider_error("invalid_argument",
                               "timeout and output and command budgets must be positive")};
    }
    if (command.size() > limits.max_command_bytes) {
        return {false, false, false, false, false, -1, {}, {},
                provider_error("invalid_argument",
                               "command exceeds the execution budget of " +
                                   std::to_string(limits.max_command_bytes) + " bytes")};
    }

    int stdout_pipe[2];
    int stderr_pipe[2];
    if (pipe2(stdout_pipe, O_CLOEXEC) != 0 || pipe2(stderr_pipe, O_CLOEXEC) != 0) {
        return {false, false, false, false, false, -1, {}, {},
                provider_error("io_error", "pipe creation failed: " + std::string(strerror(errno)))};
    }

    const pid_t pid = fork();
    if (pid < 0) {
        const std::string reason = strerror(errno);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        return {false, false, false, false, false, -1, {}, {},
                provider_error("io_error", "fork failed: " + reason)};
    }
    if (pid == 0) {
        // Child: own process group so the budget or cancellation kill reaches
        // the whole command tree; dup2 clears O_CLOEXEC on the duplicated ends.
        setpgid(0, 0);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    // Parent mirrors the child-side setpgid to close the fork race; a failure
    // means the child already exec'd (EACCES, its own setpgid won) or is gone
    // (ESRCH, the direct-pid kill below still lands).
    setpgid(pid, pid);

    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;

    std::string standard_output;
    std::string standard_error;
    bool output_truncated = false;
    bool timed_out = false;
    bool was_cancelled = false;
    int open_streams = 2;
    while (open_streams > 0) {
        if (cancel.cancelled()) {
            was_cancelled = true;
            break;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero()) {
            timed_out = true;
            break;
        }
        // Bound every wait by the cancel slice so the token is observed even
        // while the command is producing no output.
        if (remaining > kCancelPollSlice) {
            remaining = kCancelPollSlice;
        }
        pollfd fds[2] = {{stdout_pipe[0], POLLIN, 0}, {stderr_pipe[0], POLLIN, 0}};
        const int ready = poll(fds, 2, static_cast<int>(remaining.count()));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            timed_out = true;
            break;
        }
        for (int index = 0; index < 2; ++index) {
            if ((fds[index].revents & (POLLIN | POLLHUP)) == 0) {
                continue;
            }
            auto& sink = index == 0 ? standard_output : standard_error;
            char buffer[4096];
            const ssize_t received = read(fds[index].fd, buffer, sizeof(buffer));
            if (received < 0 && errno == EINTR) {
                continue;
            }
            if (received <= 0) {
                close(fds[index].fd);
                --open_streams;
                continue;
            }
            const std::size_t room = limits.max_output_bytes > sink.size()
                                         ? limits.max_output_bytes - sink.size()
                                         : 0;
            if (room == 0) {
                output_truncated = true;
                continue;
            }
            sink.append(buffer, static_cast<std::size_t>(received) < room
                                    ? static_cast<std::size_t>(received)
                                    : room);
            if (static_cast<std::size_t>(received) > room) {
                output_truncated = true;
            }
        }
    }
    if (open_streams > 0) {
        // Timeout, cancellation or poll failure: stop reading and tear the
        // command down.
        if ((open_streams & 1) != 0) {
            close(stdout_pipe[0]);
        }
        if ((open_streams & 2) != 0) {
            close(stderr_pipe[0]);
        }
    }

    // Whole-group teardown: a budget-covered or cancelled command must leave
    // nothing behind, so the process group is SIGKILLed unconditionally before
    // reaping. Descendants can outlive the direct child while still holding
    // the capture pipes, and for members that already exited the kill is a
    // no-op while waitpid still reports the direct child's real exit status.
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);

    // Blocking reap: after SIGKILL the direct child holds no live work, so
    // this completes promptly and guarantees no zombie survives the call.
    int status = 0;
    bool reaped = false;
    while (!reaped) {
        const pid_t wait_result = waitpid(pid, &status, 0);
        if (wait_result == pid) {
            reaped = true;
        } else if (wait_result < 0 && errno != EINTR) {
            break;
        }
    }
    if (was_cancelled) {
        return {false, false, true, output_truncated, false, -1,
                std::move(standard_output), std::move(standard_error),
                provider_error("cancelled", "command was cancelled before its budget expired")};
    }
    if (timed_out) {
        return {false, true, false, output_truncated, false, -1, std::move(standard_output),
                std::move(standard_error),
                provider_error("deadline_exceeded", "command exceeded its time budget")};
    }
    if (!reaped) {
        return {false, false, false, output_truncated, false, -1, std::move(standard_output),
                std::move(standard_error),
                provider_error("io_error", "child could not be reaped")};
    }
    if (WIFSIGNALED(status)) {
        return {false, false, false, output_truncated, false, 128 + WTERMSIG(status),
                std::move(standard_output), std::move(standard_error),
                provider_error("process_signalled",
                               "command was terminated by signal " + std::to_string(WTERMSIG(status)))};
    }
    if (!WIFEXITED(status)) {
        return {false, false, false, output_truncated, false, -1, std::move(standard_output),
                std::move(standard_error),
                provider_error("io_error", "command did not exit normally")};
    }
    ProcessOutcome outcome;
    outcome.ok = true;
    outcome.output_truncated = output_truncated;
    outcome.exited_normally = true;
    outcome.exit_code = WEXITSTATUS(status);
    outcome.standard_output = std::move(standard_output);
    outcome.standard_error = std::move(standard_error);
    return outcome;
}

} // namespace mirage::platform::linux_backend
