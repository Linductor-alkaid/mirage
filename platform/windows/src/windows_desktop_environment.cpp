#include <mirage/platform/windows/windows_desktop_environment.hpp>

#include "application_backend.hpp"
#include "notification_backend.hpp"
#include "uia_backend.hpp"
#include "win32_backend.hpp"
#include "win32_util.hpp"

// The Win32 surface is owned by win32_util.hpp (macro-neutral include,
// RULE-01, DEC-017 decision 5); every Win32 type stays inside this
// translation unit.
#include <tlhelp32.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
/// Returns true when the stream is settled for this round: EOF (closed) or
/// simply empty. Settled-empty is deliberate — the completion predicate in
/// execute drains to empty instead of waiting for a write-end close that
/// may never come (see the conhost note above).
bool drain_pipe(HANDLE read_end, UniqueHandle &open_flag_handle, std::string &sink,
                std::size_t max_output_bytes, bool &output_truncated, bool &stream_closed) {
    for (;;) {
        DWORD available = 0;
        if (::PeekNamedPipe(read_end, nullptr, 0, nullptr, &available, nullptr) == 0) {
            // The write end closed (or the pipe broke): this stream is done.
            open_flag_handle.reset();
            stream_closed = true;
            return true;
        }
        if (available == 0) {
            return true;
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
            return true;
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

/// Job-less teardown fallback (used only when the session's parent job
/// chain refuses the nested assignment): walks the process snapshot for
/// descendants of the command tree and terminates each. Two passes cover
/// grandchildren spawned before their parent died; terminating first and
/// walking again keeps the snapshot race window small, and a reused PID
/// inside it only risks one extra TerminateProcess on an unrelated process
/// — the same bounded-reachability tradeoff TerminateJobObject makes
/// unnecessary but the environment forces here.
void terminate_tree_fallback(DWORD root_pid) {
    std::vector<DWORD> known{root_pid};
    for (int pass = 0; pass < 2; ++pass) {
        UniqueHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
        // The API's failure value is INVALID_HANDLE_VALUE (not nullptr);
        // a failed snapshot must be loud — silently skipping a teardown
        // pass would leave descendants of the command tree alive.
        if (snapshot.get() == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "[win32-exec] tree fallback: process snapshot unavailable%s\n",
                         win32_util::last_error_suffix().c_str());
            std::fflush(stderr);
            return;
        }
        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (::Process32FirstW(snapshot.get(), &entry) == 0) {
            return;
        }
        do {
            for (const DWORD parent : known) {
                if (entry.th32ParentProcessID != parent) {
                    continue;
                }
                UniqueHandle victim(::OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID));
                if (victim != nullptr) {
                    ::TerminateProcess(victim.get(), 1);
                }
                known.push_back(entry.th32ProcessID);
                break;
            }
        } while (::Process32NextW(snapshot.get(), &entry));
    }
}

} // namespace

WindowsDesktopEnvironment::WindowsDesktopEnvironment(Win32Options win32_options,
                                                     UiaOptions uia_options,
                                                     ApplicationOptions application_options,
                                                     NotificationsOptions notifications_options) {
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
    if (application_options.enabled) {
        // Unconditional open (M4-04): discovery, CreateProcess, the process
        // snapshot and WM_CLOSE posting work in any session, including
        // service contexts — the ProcessProvider availability model.
        application_ = ApplicationBackend::open();
    }
    if (notifications_options.enabled) {
        notification_ = NotificationBackend::open();
        // A failed probe (no notification area in this session — a service
        // context, a shell-less desktop) intentionally leaves notification_
        // null: the Linux backend's missing-session-bus discipline (fail
        // closed, DEC-018 decision 2).
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

mirage::desktop::ApplicationProvider *WindowsDesktopEnvironment::application() {
    return application_ != nullptr ? application_->application() : nullptr;
}

mirage::desktop::NotificationProvider *WindowsDesktopEnvironment::notification() {
    return notification_ != nullptr ? notification_->notification() : nullptr;
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

    UniqueHandle null_stdin(::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          &inherit, OPEN_EXISTING, 0, nullptr));
    if (null_stdin == nullptr) {
        return refused("io_error",
                       "stdin device for the command failed" + win32_util::last_error_suffix());
    }
    // Blanket bInheritHandles=TRUE duplicates every inheritable handle of
    // this process into the child — including, on the hidden console's
    // behalf, the stdio set into its conhost surrogate. When that surrogate
    // outlives cmd, the capture pipes never reach EOF and every quick
    // command burns its whole budget (the M4-03 CI evidence). Inheritance
    // is therefore pinned to exactly the three stdio handles (the
    // libuv/Python subprocess pattern).
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = stdout_write.get();
    startup.StartupInfo.hStdError = stderr_write.get();
    // A real (NUL) stdin: STARTF_USESTDHANDLES with a NULL stdin handle
    // stalls some console programs at startup, and the command never reads
    // it anyway.
    startup.StartupInfo.hStdInput = null_stdin.get();
    SIZE_T attribute_size = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
    const std::unique_ptr<unsigned char[]> attribute_storage(new unsigned char[attribute_size]);
    startup.lpAttributeList =
        reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_storage.get());
    if (::InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size) == 0) {
        return refused("io_error", "attribute list init failed" + win32_util::last_error_suffix());
    }
    HANDLE std_handles[3] = {null_stdin.get(), stdout_write.get(), stderr_write.get()};
    if (::UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                    std_handles, sizeof(std_handles), nullptr, nullptr) == 0) {
        return refused("io_error", "handle list update failed" + win32_util::last_error_suffix());
    }
    // Commands run through the platform shell (contract): cmd.exe /c on
    // Windows, the UTF-8 command converted at the contract boundary. The
    // command is appended verbatim after "/c " — cmd's own quoting rules
    // apply, exactly as if the command had been typed in that shell.
    const std::wstring command_line = L"cmd.exe /c " + *command_wide;
    std::wstring mutable_command_line(command_line);
    // Created suspended so the command never runs unless the budget
    // machinery (job or the fallback below) is in place — a refused
    // assignment cannot leave a running, unbudgeted command behind.
    PROCESS_INFORMATION process_info{};
    const BOOL created =
        ::CreateProcessW(nullptr, mutable_command_line.data(), nullptr, nullptr, TRUE,
                         CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
                         nullptr, nullptr, &startup.StartupInfo, &process_info);
    if (created == 0) {
        ::DeleteProcThreadAttributeList(startup.lpAttributeList);
        return refused("io_error", "process creation failed" + win32_util::last_error_suffix());
    }
    ::DeleteProcThreadAttributeList(startup.lpAttributeList);
    UniqueHandle process(process_info.hProcess);
    UniqueHandle thread(process_info.hThread);
    const bool job_assigned = ::AssignProcessToJobObject(job.get(), process.get()) != 0;
    if (!job_assigned) {
        // Sessions whose parent job chain refuses the nested assignment
        // (CI runner services do): the tree teardown falls back to snapshot
        // enumeration, which covers the same descendants less atomically —
        // recorded as a platform fact, not silently degraded.
        ::TerminateProcess(process.get(), 1);
        ::WaitForSingleObject(process.get(), static_cast<DWORD>(kReapWait.count()));
        return refused("io_error", "command could not be placed under the execution budget" +
                                       win32_util::last_error_suffix());
    }
    ::ResumeThread(process_info.hThread);
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
    int wait_slice = 0;
    DWORD wait_result = WAIT_TIMEOUT;
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
        wait_result = ::WaitForSingleObject(process.get(), static_cast<DWORD>(remaining.count()));
        if (wait_result == WAIT_OBJECT_0) {
            process_exited = true; // keep draining the pipes until EOF
        }
        ++wait_slice;
        const bool stdout_settled =
            drain_pipe(stdout_read.get(), stdout_read, standard_output, limits.max_output_bytes,
                       output_truncated, stdout_open);
        const bool stderr_settled =
            drain_pipe(stderr_read.get(), stderr_read, standard_error, limits.max_output_bytes,
                       output_truncated, stderr_open);
        // Completion predicate: the command exited and both streams drained
        // to empty (EOF may never arrive — see drain_pipe). Descendants that
        // keep running after the direct child exits are torn down by the
        // whole-tree teardown below, never left behind.
        if (process_exited && stdout_settled && stderr_settled) {
            break;
        }
    }

    // Verdict evidence for a burned budget: the exit code BEFORE the kill
    // (259 = STILL_ACTIVE means the command really lived; a real code means
    // it finished and only the pipe EOF was withheld), plus whether an exit
    // time was ever recorded.
    if (timed_out) {
        DWORD pre_kill_exit = 0;
        ::GetExitCodeProcess(process.get(), &pre_kill_exit);
        FILETIME created_ft{}, exit_ft{}, kernel_ft{}, user_ft{};
        ::GetProcessTimes(process.get(), &created_ft, &exit_ft, &kernel_ft, &user_ft);
        const bool has_exit_time = exit_ft.dwLowDateTime != 0 || exit_ft.dwHighDateTime != 0;
        std::fprintf(stderr,
                     "[win32-exec] verdict pid=%lu pre_kill_exit=%lu has_exit_time=%d "
                     "out_open=%d err_open=%d process_exited=%d\n",
                     process_info.dwProcessId, pre_kill_exit, static_cast<int>(has_exit_time),
                     static_cast<int>(stdout_open), static_cast<int>(stderr_open),
                     static_cast<int>(process_exited));
        std::fflush(stderr);
    }
    // Whole-tree teardown, unconditional on every early exit (the Linux
    // backend's kill(-pid, SIGKILL) analog); KILL_ON_JOB_CLOSE covers any
    // descendant that survives this call's lifetime.
    if (job_assigned) {
        ::TerminateJobObject(job.get(), 1);
    } else {
        terminate_tree_fallback(process_info.dwProcessId);
    }
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
