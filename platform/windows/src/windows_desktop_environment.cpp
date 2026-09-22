#include <mirage/platform/windows/windows_desktop_environment.hpp>

#include "uia_backend.hpp"
#include "win32_backend.hpp"
#include "win32_util.hpp"

// The Win32 surface is owned by win32_util.hpp (macro-neutral include,
// RULE-01, DEC-017 decision 5); every Win32 type stays inside this
// translation unit.
#include <chrono>
#include <string>
#include <utility>

namespace mirage::platform::windows_backend {

using mirage::desktop::CancelToken;
using mirage::desktop::ProcessLimits;
using mirage::desktop::ProcessOutcome;
using win32_util::error;
using win32_util::UniqueHandle;
using win32_util::utf8_to_utf16;

namespace {

/// Cancellation is observed cooperatively; the slice bounds how long a
/// cancelled call can keep waiting (same value as the Linux backend's
/// kCancelPollSlice, M1-05 discipline).
constexpr auto kCancelPollSlice = std::chrono::milliseconds{25};

/// A process terminated by TerminateJobObject holds no live work, so this
/// is the reap wait upper bound; exceeding it is an honest io_error.
constexpr auto kReapWait = std::chrono::milliseconds{10000};

ProcessOutcome refused(std::string code, std::string message) {
    ProcessOutcome outcome;
    outcome.error = error(std::move(code), std::move(message));
    return outcome;
}

/// Drains one capture pipe without blocking: reads while PeekNamedPipe
/// reports data, applies the per-stream budget (truncation flag, never a
/// failure), and closes the read end on EOF so the stream counts down.
/// Pipe bytes are the child's own output encoding, transported verbatim —
/// the contract's UTF-8 assumption holds for programs that emit UTF-8 and
/// is not silently transcoded (recorded in the M4-03 verification record).
void drain_pipe(HANDLE read_end, UniqueHandle &open_flag_handle, std::string &sink,
                std::size_t max_output_bytes, bool &output_truncated, bool &stream_closed) {
    for (;;) {
        DWORD available = 0;
        if (::PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr) == 0) {
            // The write end closed (or the pipe broke): this stream is done.
            open_flag_handle.reset();
            stream_closed = true;
            return;
        }
        if (available == 0) {
            return;
        }
        char buffer[4096];
        DWORD to_read = static_cast<DWORD>(sizeof(buffer));
        if (available < to_read) {
            to_read = available;
        }
        DWORD received = 0;
        if (::ReadFile(read_end, buffer, to_read, &received, nullptr) == 0 || received == 0) {
            open_flag_handle.reset();
            stream_closed = true;
            return;
        }
        const std::size_t room =
            max_output_bytes > sink.size() ? max_output_bytes - sink.size() : 0;
        if (room == 0) {
            output_truncated = true;
            continue;
        }
        const std::size_t take =
            static_cast<std::size_t>(received) < room ? static_cast<std::size_t>(received) : room;
        sink.append(buffer, take);
        if (static_cast<std::size_t>(received) > room) {
            output_truncated = true;
        }
    }
}

} // namespace

WindowsDesktopEnvironment::WindowsDesktopEnvironment(Win32Options win32_options,
                                                     UiaOptions uia_options) {
    if (win32_options.enabled) {
        win32_ = Win32Backend::open();
        // A failed probe (no interactive display in this session)
        // intentionally leaves win32_ null: the window / screen / input
        // accessors report an absent capability instead of returning broken
        // providers (DEC-017 capability honesty).
    }
    if (uia_options.enabled) {
        uia_ = UiaBackend::open();
        // A failed probe (UIA client core unavailable, or the constructing
        // thread is bound to a foreign COM apartment) intentionally leaves
        // uia_ null — fail closed, same honesty discipline.
    }
}

WindowsDesktopEnvironment::~WindowsDesktopEnvironment() = default;

mirage::desktop::EnvironmentInfo WindowsDesktopEnvironment::info() const {
    return {"mirage-windows", "windows"};
}

mirage::desktop::WindowProvider *WindowsDesktopEnvironment::window() {
    return win32_ != nullptr ? win32_->window() : nullptr;
}

mirage::desktop::ScreenProvider *WindowsDesktopEnvironment::screen() {
    return win32_ != nullptr ? win32_->screen() : nullptr;
}

mirage::desktop::InputProvider *WindowsDesktopEnvironment::input() {
    return win32_ != nullptr ? win32_->input() : nullptr;
}

mirage::desktop::AccessibilityProvider *WindowsDesktopEnvironment::accessibility() {
    return uia_ != nullptr ? uia_->accessibility() : nullptr;
}

mirage::desktop::ClipboardProvider *WindowsDesktopEnvironment::clipboard() {
    return win32_ != nullptr ? win32_->clipboard() : nullptr;
}

ProcessOutcome WindowsDesktopEnvironment::execute(const std::string &command,
                                                  const ProcessLimits &limits,
                                                  const CancelToken &cancel) {
    // Argument validation happens before any side effect: a refused command
    // must never have reached a shell (M1-05 refusal order).
    if (command.empty()) {
        return refused("invalid_argument", "command must not be empty");
    }
    if (limits.timeout <= std::chrono::milliseconds::zero() || limits.max_output_bytes == 0 ||
        limits.max_command_bytes == 0) {
        return refused("invalid_argument",
                       "timeout and output and command budgets must be positive");
    }
    if (command.size() > limits.max_command_bytes) {
        return refused("invalid_argument", "command exceeds the execution budget of " +
                                               std::to_string(limits.max_command_bytes) + " bytes");
    }
    const auto command_wide = utf8_to_utf16(command);
    if (!command_wide.has_value()) {
        return refused("invalid_argument", "command must be UTF-8");
    }

    // Capture pipes with inheritable write ends; the read ends must NOT be
    // inherited, or EOF never arrives while the child lives.
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE stdout_read_raw = nullptr;
    HANDLE stdout_write_raw = nullptr;
    HANDLE stderr_read_raw = nullptr;
    HANDLE stderr_write_raw = nullptr;
    if (::CreatePipe(&stdout_read_raw, &stdout_write_raw, &inherit, 0) == 0 ||
        ::CreatePipe(&stderr_read_raw, &stderr_write_raw, &inherit, 0) == 0) {
        return refused("io_error", "pipe creation failed" + win32_util::last_error_suffix());
    }
    UniqueHandle stdout_read(stdout_read_raw);
    UniqueHandle stdout_write(stdout_write_raw);
    UniqueHandle stderr_read(stderr_read_raw);
    UniqueHandle stderr_write(stderr_write_raw);
    ::SetHandleInformation(stdout_read.get(), HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(stderr_read.get(), HANDLE_FLAG_INHERIT, 0);

    // The whole command tree lands in a job object: the budget/cancellation
    // teardown kills the tree in one TerminateJobObject (the process-group
    // analog of the Linux backend's kill(-pid, SIGKILL)), and
    // KILL_ON_JOB_CLOSE guarantees no descendant outlives the call even on
    // an unexpected path.
    UniqueHandle job(::CreateJobObjectW(nullptr, nullptr));
    if (job == nullptr) {
        return refused("io_error", "job object creation failed" + win32_util::last_error_suffix());
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_limits{};
    job_limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (::SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &job_limits,
                                  sizeof(job_limits)) == 0) {
        return refused("io_error",
                       "job object configuration failed" + win32_util::last_error_suffix());
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdout_write.get();
    startup.hStdError = stderr_write.get();
    startup.hStdInput = nullptr;
    // Commands run through the platform shell (contract): cmd.exe /c on
    // Windows, the UTF-8 command converted at the contract boundary. The
    // command is appended verbatim after "/c " — cmd's own quoting rules
    // apply, exactly as if the command had been typed in that shell.
    const std::wstring command_line = L"cmd.exe /c " + *command_wide;
    std::wstring mutable_command_line(command_line);
    PROCESS_INFORMATION process_info{};
    const BOOL created =
        ::CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_info);
    if (created == 0) {
        return refused("io_error", "process creation failed" + win32_util::last_error_suffix());
    }
    UniqueHandle process(process_info.hProcess);
    UniqueHandle thread(process_info.hThread);
    if (::AssignProcessToJobObject(job.get(), process.get()) == 0) {
        // The command already runs; tear it down honestly instead of
        // letting it outlive the call.
        ::TerminateProcess(process.get(), 1);
        ::WaitForSingleObject(process.get(), static_cast<DWORD>(kReapWait.count()));
        return refused("io_error", "command could not be placed under the execution budget" +
                                       win32_util::last_error_suffix());
    }
    // The child holds its own write ends; our copies must close or EOF on
    // the read ends never arrives.
    stdout_write.reset();
    stderr_write.reset();

    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    std::string standard_output;
    std::string standard_error;
    bool output_truncated = false;
    bool timed_out = false;
    bool was_cancelled = false;
    bool stdout_open = true;
    bool stderr_open = true;
    bool process_exited = false;
    while ((stdout_open || stderr_open || !process_exited)) {
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
        // while the command produces no output.
        if (remaining > kCancelPollSlice) {
            remaining = kCancelPollSlice;
        }
        if (::WaitForSingleObject(process.get(), static_cast<DWORD>(remaining.count())) ==
            WAIT_OBJECT_0) {
            process_exited = true; // keep draining the pipes until EOF
        }
        drain_pipe(stdout_read.get(), stdout_read, standard_output, limits.max_output_bytes,
                   output_truncated, stdout_open);
        drain_pipe(stderr_read.get(), stderr_read, standard_error, limits.max_output_bytes,
                   output_truncated, stderr_open);
    }

    // Whole-tree teardown, unconditional on every early exit (the Linux
    // backend's kill(-pid, SIGKILL) analog); KILL_ON_JOB_CLOSE covers any
    // descendant that survives this call's lifetime.
    ::TerminateJobObject(job.get(), 1);
    stdout_read.reset();
    stderr_read.reset();
    if (::WaitForSingleObject(process.get(), static_cast<DWORD>(kReapWait.count())) !=
        WAIT_OBJECT_0) {
        return refused("io_error", "command could not be reaped");
    }
    DWORD exit_code = 0;
    ::GetExitCodeProcess(process.get(), &exit_code);

    if (was_cancelled) {
        ProcessOutcome outcome =
            refused("cancelled", "command was cancelled before its budget expired");
        outcome.cancelled = true;
        outcome.output_truncated = output_truncated;
        outcome.standard_output = std::move(standard_output);
        outcome.standard_error = std::move(standard_error);
        return outcome;
    }
    if (timed_out) {
        ProcessOutcome outcome = refused("deadline_exceeded", "command exceeded its time budget");
        outcome.timed_out = true;
        outcome.output_truncated = output_truncated;
        outcome.standard_output = std::move(standard_output);
        outcome.standard_error = std::move(standard_error);
        return outcome;
    }
    ProcessOutcome outcome;
    outcome.ok = true;
    outcome.output_truncated = output_truncated;
    outcome.exited_normally = true;
    outcome.exit_code = static_cast<int>(exit_code);
    outcome.standard_output = std::move(standard_output);
    outcome.standard_error = std::move(standard_error);
    return outcome;
}

} // namespace mirage::platform::windows_backend
