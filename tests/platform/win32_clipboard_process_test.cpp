// M4-03 Win32 clipboard + Windows process execution tests against the
// session's real clipboard and a real cmd.exe child tree (DEC-017 decision
// 9): the clipboard scenarios manufacture their states through the raw
// Win32 API (empty it, inject a private format, hold it open as a peer),
// the process scenarios run real commands under the provider's job object
// — no simulator. Run evidence can only come from a machine with an
// interactive desktop for the clipboard half (the CI windows job or a
// maintainer Windows machine); the MinGW cross builds on Linux provide
// compile and link evidence only. The process half is unconditional
// (CreateProcess works in any session) and runs wherever the binary runs.
//
// Assertion philosophy (same as win32_backend_test): invariant-style, never
// environment-fragile. Every reported outcome must agree with an
// independently observed channel (GetClipboardData read back through raw
// Win32, exit codes, elapsed wall clock), while runner-limited situations
// (a clipboard manager stealing the shared clipboard mid-scenario, an OEM
// codepage in cmd output) are handled by ASCII-only substring assertions
// and loud stderr notes instead of full-content matches. Contract
// rejections (empty commands, budgets, bad UTF-8, cancellation, held
// clipboard) are strict and happen before any side effect.
//
// Mid-flight cancellation needs an async trigger while the single-threaded
// main() is parked inside execute(): the scenario arms a one-shot OS
// threadpool timer whose callback only performs request_cancel() — the
// direct analog of the Linux hardening test's SIGALRM probe (kernel timer
// invokes an async-signal-safe store; CancelToken::request_cancel is
// documented safe from any thread). The test itself spawns no threads
// (RULE-03).
//
// Known unobservable: the job object's KILL_ON_JOB_CLOSE backstop (no
// descendant outlives the call) has no independent probe in this harness —
// tasklist/name scans on a shared runner are unreliable, so the descendant
// scenario asserts the budget bound (timed_out) and prints a note instead
// of asserting survivorship (never a silent pass, never a flaky assert).

#include "../support/test.hpp"

#include <mirage/platform/windows/windows_desktop_environment.hpp>

// Keep every entry point on the explicit W surface regardless of the
// toolchain's default (MinGW defaults to ANSI).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::ClipboardReadLimits;
using mirage::desktop::ClipboardWriteLimits;
using mirage::desktop::ProcessLimits;
using mirage::platform::windows_backend::Win32Options;
using mirage::platform::windows_backend::WindowsDesktopEnvironment;

double elapsed_seconds_since(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

/// Normal executions carry a tight wall-clock budget: a runner whose
/// command never terminates burns seconds, not the default 30 (and never
/// the ctest timeout).
mirage::desktop::ProcessLimits quick_limit() {
    mirage::desktop::ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{5000};
    return limits;
}

/// Prints the full outcome verdict of one execute call (code, message,
/// timed_out/cancelled flags) so a CI refusal is attributable without a
/// second run.
void note_outcome(const char *label, const mirage::desktop::ProcessOutcome &outcome) {
    std::fprintf(stderr,
                 "[win32-cbp-test] process outcome %s: ok=%d timed_out=%d cancelled=%d "
                 "code=%s (%s)\n",
                 label, static_cast<int>(outcome.ok), static_cast<int>(outcome.timed_out),
                 static_cast<int>(outcome.cancelled), outcome.error.code.c_str(),
                 outcome.error.message.c_str());
    std::fflush(stderr);
}

/// Diagnostic probe (environment fact, not a provider claim): a bare
/// CreateProcess of "cmd.exe /c exit 0" with no pipes, no job and no
/// STARTF — if even this does not terminate within the budget, the session
/// itself starts commands slowly and every timed_out below is honest; if
/// it terminates fast, the stall lives in the redirected-handle
/// combination the provider uses.
void process_bare_start_probe() {
#ifndef UNICODE
#error test requires the W surface
#endif
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    std::wstring command_line = L"cmd.exe /c exit 0";
    const BOOL created = ::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE,
                                          CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    if (created == 0) {
        std::fprintf(stderr, "[win32-cbp-test] bare probe: CreateProcess failed %lu\n",
                     ::GetLastError());
        std::fflush(stderr);
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    const DWORD wait = ::WaitForSingleObject(info.hProcess, 5000);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    std::fprintf(stderr, "[win32-cbp-test] bare probe: wait=%lu elapsed=%lldms\n", wait,
                 static_cast<long long>(elapsed));
    std::fflush(stderr);
    ::CloseHandle(info.hThread);
    ::CloseHandle(info.hProcess);
    // The probe records the environment fact; the assertion is deliberately
    // soft (a slow session is a note, not a failure).
}

/// Factor matrix for the stall (environment diagnosis, not provider
/// claims): the bare start terminates in ~13ms on this session, so one of
/// the provider's start factors keeps cmd alive. Each variant waits 3s and
/// prints its verdict; together they separate pipes, STARTF_USESTDHANDLES
/// and the job object.
/// Local owning HANDLE for the matrix probe (the support header does not
/// carry one and the production util header stays out of tests).
struct ProbeHandle {
    HANDLE handle = nullptr;
    explicit ProbeHandle(HANDLE h) : handle(h) {}
    ~ProbeHandle() {
        if (handle != nullptr) {
            ::CloseHandle(handle);
        }
    }
    ProbeHandle(const ProbeHandle &) = delete;
    ProbeHandle &operator=(const ProbeHandle &) = delete;
    HANDLE get() const { return handle; }
    void reset() {
        if (handle != nullptr) {
            ::CloseHandle(handle);
        }
        handle = nullptr;
    }
};

void process_start_matrix_probe() {
    auto wait_and_report = [](const char *name, PROCESS_INFORMATION info) {
        const DWORD wait = ::WaitForSingleObject(info.hProcess, 3000);
        std::fprintf(stderr, "[win32-cbp-test] matrix %s: wait=%lu\n", name, wait);
        std::fflush(stderr);
        ::CloseHandle(info.hThread);
        ::CloseHandle(info.hProcess);
    };
    SECURITY_ATTRIBUTES inherit{};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    auto open_nul = [&inherit]() {
        return ::CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                             OPEN_EXISTING, 0, nullptr);
    };
    auto close_pipe = [](HANDLE &r, HANDLE &w) {
        if (r != nullptr) {
            ::CloseHandle(r);
        }
        if (w != nullptr) {
            ::CloseHandle(w);
        }
        r = nullptr;
        w = nullptr;
    };

    // v1: pipes + STARTF (no job).
    {
        HANDLE r = nullptr, w = nullptr;
        if (::CreatePipe(&r, &w, &inherit, 0) != 0) {
            ProbeHandle null_stdin(::CreateFileW(L"NUL", GENERIC_READ,
                                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                                 OPEN_EXISTING, 0, nullptr));
            ProbeHandle read(r), write(w);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = null_stdin.get();
            startup.hStdOutput = write.get();
            startup.hStdError = write.get();
            PROCESS_INFORMATION info{};
            std::wstring line = L"cmd.exe /c exit 0";
            if (::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                 nullptr, nullptr, &startup, &info) != 0) {
                write.reset();
                wait_and_report("v1 pipes+startf", info);
            } else {
                std::fprintf(stderr, "[win32-cbp-test] matrix v1: CreateProcess failed %lu\n",
                             ::GetLastError());
                std::fflush(stderr);
            }
        }
    }
    // v2: pipes + STARTF + job (assign before resume via suspend).
    {
        HANDLE r = nullptr, w = nullptr;
        if (::CreatePipe(&r, &w, &inherit, 0) != 0) {
            ProbeHandle null_stdin(::CreateFileW(L"NUL", GENERIC_READ,
                                                 FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                                 OPEN_EXISTING, 0, nullptr));
            ProbeHandle read(r), write(w);
            ProbeHandle job(::CreateJobObjectW(nullptr, nullptr));
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = null_stdin.get();
            startup.hStdOutput = write.get();
            startup.hStdError = write.get();
            PROCESS_INFORMATION info{};
            std::wstring line = L"cmd.exe /c exit 0";
            if (::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup,
                                 &info) != 0) {
                const bool assigned = ::AssignProcessToJobObject(job.get(), info.hProcess) != 0;
                ::ResumeThread(info.hThread);
                write.reset();
                std::fprintf(stderr, "[win32-cbp-test] matrix v2: assigned=%d\n",
                             static_cast<int>(assigned));
                std::fflush(stderr);
                wait_and_report("v2 pipes+startf+job", info);
            } else {
                std::fprintf(stderr, "[win32-cbp-test] matrix v2: CreateProcess failed %lu\n",
                             ::GetLastError());
                std::fflush(stderr);
            }
        }
    }
    // v3: pipes only (default stdio, no STARTF) + job.
    {
        HANDLE r = nullptr, w = nullptr;
        if (::CreatePipe(&r, &w, &inherit, 0) != 0) {
            ProbeHandle read(r), write(w);
            ProbeHandle job(::CreateJobObjectW(nullptr, nullptr));
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION info{};
            std::wstring line = L"cmd.exe /c exit 0";
            if (::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup,
                                 &info) != 0) {
                const bool assigned = ::AssignProcessToJobObject(job.get(), info.hProcess) != 0;
                ::ResumeThread(info.hThread);
                write.reset();
                std::fprintf(stderr, "[win32-cbp-test] matrix v3: assigned=%d\n",
                             static_cast<int>(assigned));
                std::fflush(stderr);
                wait_and_report("v3 pipes+job", info);
            } else {
                std::fprintf(stderr, "[win32-cbp-test] matrix v3: CreateProcess failed %lu\n",
                             ::GetLastError());
                std::fflush(stderr);
            }
        }
    }
    // v4: STARTF with NUL handles only (no pipes), no job.
    {
        ProbeHandle null_stdin(::CreateFileW(L"NUL", GENERIC_READ,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit,
                                             OPEN_EXISTING, 0, nullptr));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = null_stdin.get();
        startup.hStdOutput = null_stdin.get();
        startup.hStdError = null_stdin.get();
        PROCESS_INFORMATION info{};
        std::wstring line = L"cmd.exe /c exit 0";
        if (::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &info) != 0) {
            wait_and_report("v4 startf-null", info);
        } else {
            std::fprintf(stderr, "[win32-cbp-test] matrix v4: CreateProcess failed %lu\n",
                         ::GetLastError());
            std::fflush(stderr);
        }
    }
    // v5: provider start verbatim — job with KILL_ON_JOB_CLOSE, SUSPENDED
    // start, pipes + STARTF + NUL stdin — then the provider's wait shape:
    // a 25ms-sliced polling loop with a drain pass per slice.
    {
        HANDLE r = nullptr, w = nullptr;
        if (::CreatePipe(&r, &w, &inherit, 0) != 0) {
            HANDLE nul = open_nul();
            HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits,
                                      sizeof(limits));
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = nul;
            startup.hStdOutput = w;
            startup.hStdError = w;
            PROCESS_INFORMATION info{};
            std::wstring line = L"cmd.exe /c exit 0";
            if (::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup,
                                 &info) != 0) {
                const bool assigned = ::AssignProcessToJobObject(job, info.hProcess) != 0;
                ::ResumeThread(info.hThread);
                ::CloseHandle(w);
                w = nullptr;
                bool exited = false;
                int slices = 0;
                while (!exited && slices < 200) {
                    if (::WaitForSingleObject(info.hProcess, 25) == WAIT_OBJECT_0) {
                        exited = true;
                    }
                    DWORD avail = 0;
                    ::PeekNamedPipe(r, nullptr, 0, nullptr, &avail, nullptr);
                    ++slices;
                }
                std::fprintf(stderr,
                             "[win32-cbp-test] matrix v5 kill-on-close+loop: assigned=%d "
                             "exited=%d slices=%d\n",
                             static_cast<int>(assigned), static_cast<int>(exited), slices);
                std::fflush(stderr);
            }
            if (job != nullptr)
                ::CloseHandle(job);
            if (nul != nullptr)
                ::CloseHandle(nul);
            close_pipe(r, w);
        }
    }
    // v6: same KILL_ON_JOB_CLOSE job but a single 3s wait — separates the
    // limit flag from the sliced loop.
    {
        HANDLE job = ::CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION info{};
        std::wstring line = L"cmd.exe /c exit 0";
        if (::CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &startup, &info) != 0) {
            ::AssignProcessToJobObject(job, info.hProcess);
            wait_and_report("v6 kill-on-close+single-wait", info);
        } else {
            std::fprintf(stderr, "[win32-cbp-test] matrix v6: CreateProcess failed %lu\n",
                         ::GetLastError());
            std::fflush(stderr);
        }
        if (job != nullptr)
            ::CloseHandle(job);
    }
}

void run_scenario(const char *name, void (*scenario)()) {
    std::fprintf(stderr, "[win32-cbp-test] scenario: %s\n", name);
    std::fflush(stderr);
    scenario();
}

// --- raw Win32 clipboard fixture (independent observation channel) ----------

/// RAII raw clipboard open with the same bounded retry discipline as the
/// provider (5 s deadline, 25 ms slices) so a transiently contended runner
/// clipboard cannot flake fixture setup.
class RawClipboardOpen {
  public:
    RawClipboardOpen() {
        const ULONGLONG deadline = ::GetTickCount64() + 5000;
        for (;;) {
            owned_ = ::OpenClipboard(nullptr) != 0;
            if (owned_) {
                return;
            }
            if (::GetTickCount64() >= deadline) {
                return;
            }
            ::Sleep(25);
        }
    }
    ~RawClipboardOpen() {
        if (owned_) {
            ::CloseClipboard();
        }
    }
    RawClipboardOpen(const RawClipboardOpen &) = delete;
    RawClipboardOpen &operator=(const RawClipboardOpen &) = delete;

    bool ok() const { return owned_; }

  private:
    bool owned_ = false;
};

/// Empties the shared clipboard through raw Win32 (the honest way to
/// manufacture the empty state for the not_found contract).
bool raw_empty_clipboard() {
    RawClipboardOpen open;
    if (!open.ok()) {
        return false;
    }
    return ::EmptyClipboard() != 0;
}

/// Number of formats on the clipboard, or -1 when the clipboard could not
/// be opened at all.
int raw_clipboard_format_count() {
    RawClipboardOpen open;
    if (!open.ok()) {
        return -1;
    }
    return ::CountClipboardFormats();
}

/// Injects a registered (non-text) private format, simulating clipboard
/// content the text contract cannot serve (images, private formats).
bool raw_inject_private_format() {
    RawClipboardOpen open;
    if (!open.ok()) {
        return false;
    }
    if (::EmptyClipboard() == 0) {
        return false;
    }
    const UINT format = ::RegisterClipboardFormatW(L"Mirage.M4-03.Test.PrivateFormat");
    if (format == 0) {
        return false;
    }
    HGLOBAL global = ::GlobalAlloc(GMEM_MOVEABLE, 16);
    if (global == nullptr) {
        return false;
    }
    void *mem = ::GlobalLock(global);
    if (mem == nullptr) {
        ::GlobalFree(global);
        return false;
    }
    std::memcpy(mem, L"non-text", 16);
    ::GlobalUnlock(global);
    if (::SetClipboardData(format, global) == nullptr) {
        ::GlobalFree(global);
        return false;
    }
    return true; // ownership of `global` moved to the clipboard
}

/// Reads the clipboard text through an independent raw Win32 path
/// (GetClipboardData + WideCharToMultiByte), nullopt when there is no text.
std::optional<std::string> raw_read_utf8_text() {
    RawClipboardOpen open;
    if (!open.ok()) {
        return std::nullopt;
    }
    if (::IsClipboardFormatAvailable(CF_UNICODETEXT) == 0) {
        return std::nullopt;
    }
    HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
    if (data == nullptr) {
        return std::nullopt;
    }
    const wchar_t *wide = static_cast<const wchar_t *>(::GlobalLock(data));
    if (wide == nullptr) {
        return std::nullopt;
    }
    const std::wstring content(wide);
    ::GlobalUnlock(data);
    if (content.empty()) {
        return std::string{};
    }
    const int size = ::WideCharToMultiByte(
        CP_UTF8, 0, content.data(), static_cast<int>(content.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return std::nullopt;
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    const int written =
        ::WideCharToMultiByte(CP_UTF8, 0, content.data(), static_cast<int>(content.size()),
                              out.data(), size, nullptr, nullptr);
    if (written <= 0) {
        return std::nullopt;
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// True when a raw open really makes the clipboard exclusive (a second open
/// fails) — the precondition the held-peer scenarios rely on.
bool clipboard_open_is_exclusive() {
    RawClipboardOpen holder;
    if (!holder.ok()) {
        return false;
    }
    if (::OpenClipboard(nullptr) != 0) {
        ::CloseClipboard();
        return false;
    }
    return true;
}

// --- accessors and identity --------------------------------------------------

/// Disabled (default) construction exposes nothing but the always-on M4-03
/// process surface; enabled construction probes the interactive desktop and
/// additionally exposes the M4-03 clipboard. Both identities report the
/// product strings.
void environment_probe_and_identity() {
    WindowsDesktopEnvironment disabled;
    MIRAGE_CHECK(disabled.window() == nullptr);
    MIRAGE_CHECK(disabled.screen() == nullptr);
    MIRAGE_CHECK(disabled.input() == nullptr);
    MIRAGE_CHECK(disabled.filesystem() == nullptr); // still fail-closed (M4-03 remainder)
    MIRAGE_CHECK(disabled.application() == nullptr);
    MIRAGE_CHECK(disabled.notification() == nullptr);
    MIRAGE_CHECK(disabled.accessibility() == nullptr);
    MIRAGE_CHECK(disabled.clipboard() == nullptr); // rides the win32 probe (off here)
    MIRAGE_CHECK(disabled.process() != nullptr);   // unconditional, any session
    MIRAGE_CHECK(disabled.info().name == "mirage-windows");
    MIRAGE_CHECK(disabled.info().platform == "windows");

    WindowsDesktopEnvironment env(Win32Options{true});
    MIRAGE_CHECK(env.clipboard() != nullptr); // M4-03: probe success exposes the clipboard
    MIRAGE_CHECK(env.process() != nullptr);
    MIRAGE_CHECK(env.info().name == "mirage-windows");
    MIRAGE_CHECK(env.info().platform == "windows");
}

// --- clipboard scenarios -----------------------------------------------------

/// States manufactured through raw Win32 must map onto the read contract's
/// honest failures: an empty clipboard is not_found, non-text content is
/// unsupported_content (never a substitute read).
void clipboard_manufactured_states() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();

    MIRAGE_CHECK(raw_empty_clipboard());
    MIRAGE_CHECK(raw_clipboard_format_count() == 0);
    const auto empty = clipboard.read_text();
    MIRAGE_CHECK(!empty.ok);
    MIRAGE_CHECK(empty.error.code == "not_found");
    MIRAGE_CHECK(empty.content.empty());

    MIRAGE_CHECK(raw_inject_private_format());
    const auto non_text = clipboard.read_text();
    MIRAGE_CHECK(!non_text.ok);
    MIRAGE_CHECK(non_text.error.code == "unsupported_content");
    MIRAGE_CHECK(non_text.content.empty());
}

/// Write/read roundtrip of a multi-byte UTF-8 payload, verified against the
/// provider's own read AND an independent raw Win32 observation; overwrite
/// replaces (not appends); an empty payload is legal text.
void clipboard_roundtrip_and_independent_observation() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();

    // héllo ✓ mirage as explicit UTF-8 bytes (source-encoding independent).
    const std::string payload = "h\xc3\xa9"
                                "llo \xe2\x9c\x93 mirage";
    const auto written = clipboard.write_text(payload);
    MIRAGE_CHECK(written.ok);
    MIRAGE_CHECK(!written.cancelled);

    const auto read_back = clipboard.read_text();
    MIRAGE_CHECK(read_back.ok);
    MIRAGE_CHECK(read_back.content == payload);

    const auto independent = raw_read_utf8_text();
    MIRAGE_CHECK(independent.has_value());
    if (independent.has_value()) {
        MIRAGE_CHECK(*independent == payload);
    }

    MIRAGE_CHECK(clipboard.write_text("second-write").ok);
    const auto second = clipboard.read_text();
    MIRAGE_CHECK(second.ok);
    MIRAGE_CHECK(second.content == "second-write");

    MIRAGE_CHECK(clipboard.write_text("").ok);
    const auto empty_text = clipboard.read_text();
    MIRAGE_CHECK(empty_text.ok);
    MIRAGE_CHECK(empty_text.content.empty());
}

/// Every refusal happens before the clipboard is touched: after each
/// rejected call the sentinel content is still exactly there.
void clipboard_rejections_keep_state() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();

    const std::string sentinel = "0123456789"; // 10 bytes
    MIRAGE_CHECK(clipboard.write_text(sentinel).ok);

    // Cancelled write: refused at the entry, nothing replaced.
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled = clipboard.write_text("nope", ClipboardWriteLimits{}, cancel);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.cancelled);
    MIRAGE_CHECK(cancelled.error.code == "cancelled");
    MIRAGE_CHECK(clipboard.read_text().content == sentinel);

    // Zero write budget is an argument error.
    ClipboardWriteLimits zero;
    zero.max_bytes = 0;
    MIRAGE_CHECK(clipboard.write_text(sentinel, zero, CancelToken{}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(clipboard.read_text().content == sentinel);

    // Exact-budget payload is accepted; one byte beyond is refused.
    ClipboardWriteLimits exact;
    exact.max_bytes = 10;
    MIRAGE_CHECK(clipboard.write_text("budget-ok!", exact, CancelToken{}).ok);
    MIRAGE_CHECK(clipboard.read_text().content == "budget-ok!");
    const auto over = clipboard.write_text("budget-ok!X", exact, CancelToken{});
    MIRAGE_CHECK(!over.ok);
    MIRAGE_CHECK(over.error.code == "invalid_argument");
    MIRAGE_CHECK(clipboard.read_text().content == "budget-ok!");

    // Non-UTF-8 payloads (invalid lead byte, truncated sequence) are
    // refused before any side effect.
    MIRAGE_CHECK(
        clipboard.write_text("\xff\xfe", ClipboardWriteLimits{}, CancelToken{}).error.code ==
        "invalid_argument");
    MIRAGE_CHECK(clipboard.read_text().content == "budget-ok!");
    MIRAGE_CHECK(clipboard.write_text("ok\xc3", ClipboardWriteLimits{}, CancelToken{}).error.code ==
                 "invalid_argument");
    MIRAGE_CHECK(clipboard.read_text().content == "budget-ok!");

    // Read side: zero budget is an argument error; the exact content size is
    // served; one byte less is clipboard_too_large with the content cleared
    // (never a silent truncation) and the clipboard state intact.
    ClipboardReadLimits zero_read;
    zero_read.max_bytes = 0;
    MIRAGE_CHECK(clipboard.read_text(zero_read, CancelToken{}).error.code == "invalid_argument");
    ClipboardReadLimits exact_read;
    exact_read.max_bytes = 10;
    const auto exact_served = clipboard.read_text(exact_read, CancelToken{});
    MIRAGE_CHECK(exact_served.ok);
    MIRAGE_CHECK(exact_served.content == "budget-ok!");
    ClipboardReadLimits tight_read;
    tight_read.max_bytes = 9;
    const auto too_large = clipboard.read_text(tight_read, CancelToken{});
    MIRAGE_CHECK(!too_large.ok);
    MIRAGE_CHECK(too_large.error.code == "clipboard_too_large");
    MIRAGE_CHECK(too_large.content.empty());
    MIRAGE_CHECK(clipboard.read_text().content == "budget-ok!");

    // Cancelled read.
    CancelToken read_cancel;
    read_cancel.request_cancel();
    const auto cancelled_read = clipboard.read_text(ClipboardReadLimits{}, read_cancel);
    MIRAGE_CHECK(!cancelled_read.ok);
    MIRAGE_CHECK(cancelled_read.error.code == "cancelled");
    MIRAGE_CHECK(clipboard.read_text().content == "budget-ok!");
}

/// A peer holding the clipboard open (this same thread's raw open) must be
/// reported honestly: cancellation is observed inside the retry window, and
/// exhausting the window is an io_error — never a hang, never a fabricated
/// read. Skipped loudly when this environment does not make a raw open
/// exclusive (the precondition the scenario needs).
void clipboard_held_by_peer_is_bounded() {
    if (!clipboard_open_is_exclusive()) {
        std::fprintf(stderr,
                     "note: a raw OpenClipboard does not make the clipboard exclusive here; "
                     "held-peer scenarios are environment-limited and skipped\n");
        std::fflush(stderr);
        return;
    }
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ClipboardProvider &clipboard = *env.clipboard();
    MIRAGE_CHECK(clipboard.write_text("budget-ok!").ok);

    {
        // Cancelled token: observed within the retry window, not after it.
        RawClipboardOpen holder;
        if (!holder.ok()) {
            std::fprintf(stderr, "note: raw open failed (clipboard contended); held-cancel "
                                 "scenario skipped\n");
            std::fflush(stderr);
            return;
        }
        CancelToken cancel;
        cancel.request_cancel();
        const auto started = std::chrono::steady_clock::now();
        const auto outcome = clipboard.read_text(ClipboardReadLimits{}, cancel);
        MIRAGE_CHECK(!outcome.ok);
        MIRAGE_CHECK(outcome.error.code == "cancelled");
        MIRAGE_CHECK(elapsed_seconds_since(started) < 3.0);
    }
    {
        // No cancellation: the read reports the exhausted window as io_error.
        RawClipboardOpen holder;
        if (!holder.ok()) {
            std::fprintf(stderr, "note: raw open failed (clipboard contended); held-read "
                                 "scenario skipped\n");
            std::fflush(stderr);
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        const auto outcome = clipboard.read_text();
        MIRAGE_CHECK(!outcome.ok);
        MIRAGE_CHECK(outcome.error.code == "io_error");
        const double elapsed = elapsed_seconds_since(started);
        MIRAGE_CHECK(elapsed >= 4.0);
        MIRAGE_CHECK(elapsed < 15.0);
    }
    {
        // Same for the write path; the refused write leaves the content.
        RawClipboardOpen holder;
        if (!holder.ok()) {
            std::fprintf(stderr, "note: raw open failed (clipboard contended); held-write "
                                 "scenario skipped\n");
            std::fflush(stderr);
            return;
        }
        const auto started = std::chrono::steady_clock::now();
        const auto outcome = clipboard.write_text("held-payload");
        MIRAGE_CHECK(!outcome.ok);
        MIRAGE_CHECK(!outcome.cancelled);
        MIRAGE_CHECK(outcome.error.code == "io_error");
        const double elapsed = elapsed_seconds_since(started);
        MIRAGE_CHECK(elapsed >= 4.0);
        MIRAGE_CHECK(elapsed < 15.0);
    }
    MIRAGE_CHECK(clipboard.read_text().content == "budget-ok!");
}

// --- process scenarios -------------------------------------------------------

/// Argument refusals happen before any process is created: a refused command
/// must never have reached the shell (M1-05 refusal order).
void process_refusals_before_effects() {
    WindowsDesktopEnvironment env;
    mirage::desktop::ProcessProvider &process = *env.process();

    const auto empty = process.execute("", ProcessLimits{}, CancelToken{});
    MIRAGE_CHECK(!empty.ok);
    MIRAGE_CHECK(empty.error.code == "invalid_argument");
    MIRAGE_CHECK(!empty.exited_normally);
    MIRAGE_CHECK(empty.standard_output.empty());
    MIRAGE_CHECK(empty.standard_error.empty());

    ProcessLimits zero_timeout;
    zero_timeout.timeout = std::chrono::milliseconds{0};
    MIRAGE_CHECK(process.execute("exit 0", zero_timeout, CancelToken{}).error.code ==
                 "invalid_argument");

    ProcessLimits zero_output;
    zero_output.max_output_bytes = 0;
    MIRAGE_CHECK(process.execute("exit 0", zero_output, CancelToken{}).error.code ==
                 "invalid_argument");

    ProcessLimits zero_command;
    zero_command.max_command_bytes = 0;
    MIRAGE_CHECK(process.execute("exit 0", zero_command, CancelToken{}).error.code ==
                 "invalid_argument");

    // Command-length budget: exactly the budget runs; one byte less refuses
    // the same command. An over-budget command that would create a canary
    // file must not reach the shell — the file never comes into existence.
    constexpr std::size_t kBudget = 6; // "exit 0"
    ProcessLimits exact;
    exact.max_command_bytes = kBudget;
    const auto boundary = process.execute("exit 0", exact, CancelToken{});
    note_outcome("budget-boundary exit 0", boundary);
    MIRAGE_CHECK(boundary.ok);
    MIRAGE_CHECK(boundary.exited_normally);

    const auto canary = std::filesystem::temp_directory_path() / "mirage-m4-03-process-canary.txt";
    std::error_code ec;
    std::filesystem::remove(canary, ec);
    ProcessLimits tight;
    tight.max_command_bytes = kBudget;
    std::string over = "echo overflow> \"" + canary.string() + "\"";
    over += std::string(kBudget, 'x');
    MIRAGE_CHECK(over.size() > kBudget);
    const auto refused = process.execute(over, tight, CancelToken{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "invalid_argument");
    MIRAGE_CHECK(refused.error.message.find(std::to_string(kBudget)) != std::string::npos);
    MIRAGE_CHECK(!refused.exited_normally);
    MIRAGE_CHECK(refused.standard_output.empty());
    MIRAGE_CHECK(refused.standard_error.empty());
    MIRAGE_CHECK(!std::filesystem::exists(canary));

    // Non-UTF-8 commands are refused at the contract boundary.
    MIRAGE_CHECK(
        process.execute(std::string("echo \xff\xfe"), ProcessLimits{}, CancelToken{}).error.code ==
        "invalid_argument");
    MIRAGE_CHECK(!std::filesystem::exists(canary));
}

/// Normal executions: echo output is captured (ASCII substring — cmd emits
/// the session's OEM codepage), stderr reaches its own stream, exit codes
/// are structured results, and the disabled (non-probing) environment still
/// executes because the process surface is unconditional.
void process_normal_execution_and_exit_codes() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ProcessProvider &process = *env.process();

    const auto echoed = process.execute("echo mirage-ok", quick_limit());
    note_outcome("echo mirage-ok", echoed);
    MIRAGE_CHECK(echoed.ok);
    MIRAGE_CHECK(echoed.exited_normally);
    MIRAGE_CHECK(echoed.exit_code == 0);
    MIRAGE_CHECK(echoed.standard_output.find("mirage-ok") != std::string::npos);

    const auto errored = process.execute("echo err-text 1>&2", quick_limit(), CancelToken{});
    note_outcome("echo err-text", errored);
    MIRAGE_CHECK(errored.ok);
    MIRAGE_CHECK(errored.standard_output.empty());
    MIRAGE_CHECK(errored.standard_error.find("err-text") != std::string::npos);

    const auto quoted = process.execute("echo \"quoted arg\"", quick_limit(), CancelToken{});
    note_outcome("echo quoted", quoted);
    MIRAGE_CHECK(quoted.ok);
    MIRAGE_CHECK(quoted.standard_output.find("quoted arg") != std::string::npos);

    // Exit codes are structured results, not provider failures.
    const auto coded = process.execute("exit 7", ProcessLimits{}, CancelToken{});
    MIRAGE_CHECK(coded.ok);
    MIRAGE_CHECK(coded.exited_normally);
    MIRAGE_CHECK(coded.exit_code == 7);
    MIRAGE_CHECK(coded.standard_output.empty());
    MIRAGE_CHECK(coded.standard_error.empty());

    // A disabled (non-probing) environment carries the process surface all
    // the same (CreateProcess works in any session, including services).
    WindowsDesktopEnvironment disabled;
    const auto from_disabled = disabled.process()->execute("exit 0", ProcessLimits{});
    MIRAGE_CHECK(from_disabled.ok);
    MIRAGE_CHECK(from_disabled.exit_code == 0);
}

/// A command that exceeds its wall-clock budget is killed and reported with
/// timed_out + deadline_exceeded; the call itself stays bounded.
void process_timeout_bounds_the_call() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ProcessProvider &process = *env.process();

    ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{1000};
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = process.execute("ping -n 30 127.0.0.1 > nul", limits, CancelToken{});
    const double elapsed = elapsed_seconds_since(started);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.timed_out);
    MIRAGE_CHECK(!outcome.cancelled);
    MIRAGE_CHECK(outcome.error.code == "deadline_exceeded");
    MIRAGE_CHECK(elapsed >= 0.9); // the budget bounded the call, no early abort
    MIRAGE_CHECK(elapsed < 15.0); // beyond the worst-case teardown (kReapWait)
}

/// Pre-cancelled token: the call returns promptly with cancelled semantics.
void process_pre_cancelled_returns_promptly() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ProcessProvider &process = *env.process();

    CancelToken token;
    token.request_cancel();
    ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{10000};
    const auto started = std::chrono::steady_clock::now();
    const auto outcome = process.execute("ping -n 30 127.0.0.1 > nul", limits, token);
    const double elapsed = elapsed_seconds_since(started);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(!outcome.timed_out);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    MIRAGE_CHECK(elapsed < 5.0);
}

/// One-shot threadpool timer: fires ~2 s into the call and only performs
/// the cooperative request_cancel store (SIGALRM analog, see file header).
VOID CALLBACK cancel_timer_callback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER) {
    static_cast<CancelToken *>(context)->request_cancel();
}

/// Mid-flight cancellation: the token is observed at the loop's bounded
/// slice, the command is torn down well before its own budget, and output
/// captured before the cancellation is preserved.
void process_cancel_midflight_tears_down_and_keeps_output() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ProcessProvider &process = *env.process();

    CancelToken token;
    PTP_TIMER timer = ::CreateThreadpoolTimer(&cancel_timer_callback, &token, nullptr);
    MIRAGE_CHECK(timer != nullptr);
    if (timer == nullptr) {
        return;
    }
    LARGE_INTEGER due{};
    due.QuadPart = -20'000'000; // negative = relative; 100 ns units -> 2 s
    ::SetThreadpoolTimer(timer, reinterpret_cast<FILETIME *>(&due), 0, 0);

    ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{15000};
    const auto started = std::chrono::steady_clock::now();
    const auto outcome =
        process.execute("echo before-cancel & ping -n 30 127.0.0.1 > nul", limits, token);
    const double elapsed = elapsed_seconds_since(started);
    ::WaitForThreadpoolTimerCallbacks(timer, TRUE);
    ::CloseThreadpoolTimer(timer);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.cancelled);
    MIRAGE_CHECK(!outcome.timed_out);
    MIRAGE_CHECK(outcome.error.code == "cancelled");
    MIRAGE_CHECK(elapsed < 10.0); // ended by the cancellation, far before the 15 s budget
    MIRAGE_CHECK(outcome.standard_output.find("before-cancel") != std::string::npos);
}

/// Per-stream capture budget: output beyond the cap is truncated (flag set,
/// stream bounded, still ok), while a sufficient budget delivers the whole
/// stream untruncated. ASCII-only assertions: cmd emits the session's OEM
/// codepage and CRLF line endings.
void process_output_truncation_and_full_delivery() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ProcessProvider &process = *env.process();

    // 10000 lines of "line-N\r\n" (~120 KB), pure ASCII.
    const char *const loop = "for /l %i in (1,1,10000) do @echo line-%i";

    ProcessLimits capped;
    capped.max_output_bytes = 1024;
    const auto truncated = process.execute(loop, capped, CancelToken{});
    MIRAGE_CHECK(truncated.ok);
    MIRAGE_CHECK(truncated.output_truncated);
    MIRAGE_CHECK(truncated.standard_output.size() <= 1024);
    MIRAGE_CHECK(!truncated.standard_output.empty());
    MIRAGE_CHECK(truncated.standard_output.compare(0, 6, "line-1") == 0);

    ProcessLimits generous;
    generous.max_output_bytes = 512u << 10;
    const auto full = process.execute(loop, generous, CancelToken{});
    MIRAGE_CHECK(full.ok);
    MIRAGE_CHECK(!full.output_truncated);
    MIRAGE_CHECK(full.standard_output.find("line-1\r\n") == 0);
    MIRAGE_CHECK(full.standard_output.find("line-10000") != std::string::npos);
    MIRAGE_CHECK(static_cast<std::size_t>(std::count(full.standard_output.begin(),
                                                     full.standard_output.end(), '\n')) == 10000);
}

/// A command whose direct child exits but leaves a pipe-holding descendant
/// behind stays bounded by the budget: the call cannot report success while
/// the descendant still holds the capture pipes, and the job teardown
/// (TerminateJobObject + KILL_ON_JOB_CLOSE) reaps the whole tree. The
/// survivorship itself has no independent probe on a shared runner (see
/// file header), so the bounded outcome is the asserted contract.
void process_descendant_holding_pipes_is_bounded() {
    WindowsDesktopEnvironment env(Win32Options{true});
    mirage::desktop::ProcessProvider &process = *env.process();

    ProcessLimits limits;
    limits.timeout = std::chrono::milliseconds{3000};
    const auto started = std::chrono::steady_clock::now();
    const auto outcome =
        process.execute("start /b cmd /c ping -n 60 127.0.0.1 > nul", limits, CancelToken{});
    const double elapsed = elapsed_seconds_since(started);

    MIRAGE_CHECK(!outcome.ok);
    MIRAGE_CHECK(outcome.timed_out);
    MIRAGE_CHECK(outcome.error.code == "deadline_exceeded");
    MIRAGE_CHECK(elapsed < 15.0); // beyond the worst-case teardown (kReapWait)
    std::fprintf(stderr, "note: descendant survivorship after the job teardown relies on "
                         "KILL_ON_JOB_CLOSE; a shared-runner tasklist probe is unreliable and "
                         "is deliberately not asserted\n");
    std::fflush(stderr);
}

} // namespace

int main() {
    run_scenario("environment_probe_and_identity", environment_probe_and_identity);

    WindowsDesktopEnvironment env(Win32Options{true});
    if (env.clipboard() != nullptr) {
        run_scenario("clipboard_manufactured_states", clipboard_manufactured_states);
        run_scenario("clipboard_roundtrip_and_independent_observation",
                     clipboard_roundtrip_and_independent_observation);
        run_scenario("clipboard_rejections_keep_state", clipboard_rejections_keep_state);
        run_scenario("clipboard_held_by_peer_is_bounded", clipboard_held_by_peer_is_bounded);
    } else {
        // Recorded by environment_probe_and_identity; loud and honest — run
        // evidence for the clipboard half requires an interactive session.
        std::fprintf(stderr, "clipboard surface unavailable (no interactive desktop?); clipboard "
                             "scenarios cannot run here\n");
        std::fflush(stderr);
    }

    // The process surface is unconditional, so these run in any session.
    run_scenario("process_refusals_before_effects", process_refusals_before_effects);
    run_scenario("process_bare_start_probe", process_bare_start_probe);
    run_scenario("process_start_matrix_probe", process_start_matrix_probe);
    run_scenario("process_normal_execution_and_exit_codes",
                 process_normal_execution_and_exit_codes);
    run_scenario("process_timeout_bounds_the_call", process_timeout_bounds_the_call);
    run_scenario("process_pre_cancelled_returns_promptly", process_pre_cancelled_returns_promptly);
    run_scenario("process_cancel_midflight_tears_down_and_keeps_output",
                 process_cancel_midflight_tears_down_and_keeps_output);
    run_scenario("process_output_truncation_and_full_delivery",
                 process_output_truncation_and_full_delivery);
    run_scenario("process_descendant_holding_pipes_is_bounded",
                 process_descendant_holding_pipes_is_bounded);

    return mirage::testing::finish("win32_clipboard_process_test");
}
