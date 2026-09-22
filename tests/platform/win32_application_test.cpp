// M4-04 Windows application frontend tests against the session's real
// Start Menu and real helper processes (DEC-017 decision 9 discipline): the
// discovery fixtures are real .lnk shortcuts written into the per-user
// Start Menu Programs tree through the same COM surface the frontend reads
// them with, the launch/terminate fixtures are real processes, and every
// reported outcome must agree with an independently observed channel (the
// process snapshot, the ready-files, the instance pids) — no simulator.
// No interactive desktop is needed beyond a session that can own windows.
//
// Fixture discipline (the m205 pattern, Windows form): the helper binary is
// copied under globally unique names per run, so the backend's image-name
// matching only ever sees the processes this test created. Cleanup is
// total: a run token prefix sweep terminates any surviving helper image,
// the Start Menu fixture directory and the temp tree are removed, and every
// helper carries its own safety timer.
//
// Assertion philosophy: contract rejections (empty/overlong/traversal ids,
// budgets, cancellation, unknown and missing instances) are strict and must
// happen before any side effect, proven by the process snapshot staying
// empty. Platform-fact situations (a shortcut the shell cannot resolve) are
// handled by loud stderr notes, never by silent passes.

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
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace desktop = mirage::desktop;
using mirage::platform::windows_backend::ApplicationOptions;
using mirage::platform::windows_backend::UiaOptions;
using mirage::platform::windows_backend::Win32Options;
using mirage::platform::windows_backend::WindowsDesktopEnvironment;

// Frozen system GUIDs, defined locally like the frontend does (no SDK GUID
// library dependency; identical on both gate toolchains).
constexpr GUID kShellLinkClsid = {
    0x00021401, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kFolderStartMenu = {
    0x625B53C3, 0xAB48, 0x4EC1, {0xBA, 0x1F, 0xA1, 0xEF, 0x41, 0x46, 0xFC, 0x19}};

/// One call = one COM scope (the frontend's call-scoped model; nothing is
/// held across calls).
class ComScope {
  public:
    ComScope() : hr_(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComScope() {
        if (SUCCEEDED(hr_)) {
            ::CoUninitialize();
        }
    }
    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;
    bool ok() const { return SUCCEEDED(hr_); }

  private:
    HRESULT hr_;
};

/// Owning call-scoped COM interface pointer (the frontend's ComRef shape).
template <typename T> class ComRef {
  public:
    ComRef() = default;
    ~ComRef() { reset(); }
    ComRef(const ComRef &) = delete;
    ComRef &operator=(const ComRef &) = delete;
    T **out() {
        reset();
        return &ptr_;
    }
    T *operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    void reset() {
        if (ptr_ != nullptr) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

  private:
    T *ptr_ = nullptr;
};

/// Case-insensitive UTF-16 name equality (image-name matching is
/// case-insensitive on Windows file systems).
bool iequals(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const wchar_t lower_left = static_cast<wchar_t>(std::towlower(left[index]));
        const wchar_t lower_right = static_cast<wchar_t>(std::towlower(right[index]));
        if (lower_left != lower_right) {
            return false;
        }
    }
    return true;
}

std::wstring_view image_basename(std::wstring_view path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

/// The per-user Start Menu root, resolved through the same known-folder API
/// the frontend uses (empty when unavailable — the fixture setup fails
/// loudly in that case instead of faking discovery evidence).
std::wstring start_menu_root() {
    PWSTR raw = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(kFolderStartMenu, 0, nullptr, &raw);
    if (hr != S_OK || raw == nullptr) {
        ::CoTaskMemFree(raw);
        return {};
    }
    std::wstring path(raw);
    ::CoTaskMemFree(raw);
    return path;
}

bool create_shortcut(const std::wstring &lnk_path, const std::wstring &target,
                     const std::wstring &arguments, const std::wstring &work_dir) {
    ComScope scope;
    if (!scope.ok()) {
        return false;
    }
    ComRef<IShellLinkW> link;
    if (::CoCreateInstance(kShellLinkClsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IShellLinkW),
                           reinterpret_cast<void **>(link.out())) != S_OK ||
        !link) {
        return false;
    }
    if (link->SetPath(target.c_str()) != S_OK || link->SetArguments(arguments.c_str()) != S_OK ||
        link->SetWorkingDirectory(work_dir.c_str()) != S_OK) {
        return false;
    }
    ComRef<IPersistFile> file;
    if (link->QueryInterface(__uuidof(IPersistFile), reinterpret_cast<void **>(file.out())) !=
        S_OK) {
        return false;
    }
    return file->Save(lnk_path.c_str(), TRUE) == S_OK;
}

/// Live pids whose image basename matches, from one bounded snapshot — the
/// test's independent observation channel (the same source the backend
/// scans, read on the test side).
std::vector<DWORD> pids_by_image(const std::wstring &image) {
    std::vector<DWORD> matches;
    HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return matches;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot, &entry) != 0) {
        const DWORD self = ::GetCurrentProcessId();
        do {
            if (entry.th32ProcessID != 0 && entry.th32ProcessID != self &&
                iequals(image_basename(entry.szExeFile), image)) {
                matches.push_back(entry.th32ProcessID);
            }
        } while (::Process32NextW(snapshot, &entry));
    }
    ::CloseHandle(snapshot);
    return matches;
}

/// Polls until the predicate holds or the deadline passes (25 ms slices).
template <typename Predicate> bool wait_until(Predicate &&predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        ::Sleep(25);
    }
    return predicate();
}

bool wait_for_file(const std::filesystem::path &file) {
    return wait_until([&] { return std::filesystem::exists(file); });
}

bool wait_image_gone(const std::wstring &image) {
    return wait_until([&] { return pids_by_image(image).empty(); });
}

/// Force-terminates one pid (test-side cleanup of a stuck fixture instance;
/// the provider itself never does this).
bool force_terminate(DWORD pid) {
    HANDLE process = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (process == nullptr) {
        return false;
    }
    const BOOL killed = ::TerminateProcess(process, 1);
    ::WaitForSingleObject(process, 5000);
    ::CloseHandle(process);
    return killed != 0;
}

struct ForeignInstance {
    HANDLE process = nullptr;
    DWORD pid = 0;
};

/// Starts a helper copy directly (NOT through the provider): the foreign
/// instance a user could have started, which the backend can only know by
/// image name.
std::optional<ForeignInstance> spawn_foreign(const std::wstring &copy,
                                             const std::wstring &arguments) {
    std::wstring command_line = L"\"" + copy + L"\"";
    if (!arguments.empty()) {
        command_line += L" " + arguments;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    if (::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
                         &startup, &info) == 0) {
        std::fprintf(stderr, "[win32-app-test] foreign spawn failed %lu\n", ::GetLastError());
        std::fflush(stderr);
        return std::nullopt;
    }
    ::CloseHandle(info.hThread);
    return ForeignInstance{info.hProcess, info.dwProcessId};
}

/// Test-owned handles are force-terminated and closed on destruction, so a
/// failing scenario cannot leak a helper into later scenarios.
struct ForeignTracker {
    std::vector<ForeignInstance> instances;
    ~ForeignTracker() {
        for (const ForeignInstance &instance : instances) {
            ::TerminateProcess(instance.process, 1);
            ::WaitForSingleObject(instance.process, 5000);
            ::CloseHandle(instance.process);
        }
    }
};

/// The run's fixture set: unique helper copies, ready-file paths and the
/// ids the discovery surface must answer with.
struct AppFixtures {
    std::filesystem::path temp_root;
    std::wstring hold_image;
    std::wstring ignore_image;
    std::wstring exit_image;
    std::filesystem::path hold_copy;
    std::filesystem::path ignore_copy;
    std::filesystem::path exit_copy;
    std::filesystem::path hold_ready;
    std::filesystem::path ignore_ready;
    std::filesystem::path foreign_ready;
    std::wstring hold_arguments;
    std::wstring ignore_arguments;
    std::string hold_id = "mirage-m404/hold.lnk";
    std::string ignore_id = "mirage-m404/ignore.lnk";
    std::string exit_id = "mirage-m404/exit.lnk";
    std::string unicode_id;
    std::string hidden_id = "mirage-m404/hidden.lnk";
    std::filesystem::path start_menu_dir;
};

void note_image_state(const char *label, const AppFixtures &fixtures) {
    std::fprintf(stderr, "[win32-app-test] %s: hold=%zu ignore=%zu exit=%zu\n", label,
                 pids_by_image(fixtures.hold_image).size(),
                 pids_by_image(fixtures.ignore_image).size(),
                 pids_by_image(fixtures.exit_image).size());
    std::fflush(stderr);
}

/// Terminates any surviving helper of this run (the total-cleanup sweep;
/// helpers carry their own safety timers as the second net).
void sweep_run_images(const AppFixtures &fixtures) {
    for (const std::wstring &image :
         {fixtures.hold_image, fixtures.ignore_image, fixtures.exit_image}) {
        for (const DWORD pid : pids_by_image(image)) {
            std::fprintf(stderr, "[win32-app-test] sweeping stray helper pid %lu (%ls)\n", pid,
                         image.c_str());
            std::fflush(stderr);
            force_terminate(pid);
        }
    }
}

bool digits_only(const std::string &text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](char ch) { return ch >= '0' && ch <= '9'; });
}

void CALLBACK cancel_timer_callback(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER) {
    static_cast<desktop::CancelToken *>(context)->request_cancel();
}

void run_scenario(const char *name, void (*scenario)(desktop::ApplicationProvider &, AppFixtures &),
                  desktop::ApplicationProvider &app, AppFixtures &fixtures) {
    std::fprintf(stderr, "[win32-app-test] scenario %s\n", name);
    std::fflush(stderr);
    scenario(app, fixtures);
}

// --- scenarios -------------------------------------------------------------

/// The provider is only exposed when the option asks for it; the base-class
/// default (null) is the fail-closed surface.
void accessor_fail_closed_without_option() {
    WindowsDesktopEnvironment env;
    MIRAGE_CHECK(env.application() == nullptr);
}

/// Discovery answers with the fixture entries (and the session's real ones),
/// hidden entries are skipped, and the running flags agree with the
/// independent snapshot observation (nothing launched yet).
void discovery_lists_fixtures_and_skips_hidden(desktop::ApplicationProvider &app,
                                               AppFixtures &fixtures) {
    const desktop::ApplicationListOutcome listing = app.list_applications();
    MIRAGE_CHECK(listing.ok);
    if (!listing.ok) {
        std::fprintf(stderr, "[win32-app-test] discovery failed: %s (%s)\n",
                     listing.error.code.c_str(), listing.error.message.c_str());
        std::fflush(stderr);
        return;
    }

    std::size_t found = 0;
    bool hidden_listed = false;
    bool all_lnk = true;
    bool names_match = true;
    for (const desktop::ApplicationInfo &application : listing.applications) {
        if (application.id.size() < 4 ||
            application.id.compare(application.id.size() - 4, 4, ".lnk") != 0) {
            all_lnk = false;
        }
        if (application.id == fixtures.hidden_id) {
            hidden_listed = true;
        }
        if (application.id == fixtures.hold_id) {
            ++found;
            MIRAGE_CHECK(!application.running);
            if (application.name != "hold") {
                names_match = false;
            }
        } else if (application.id == fixtures.ignore_id || application.id == fixtures.exit_id) {
            ++found;
        } else if (application.id == fixtures.unicode_id) {
            ++found;
            MIRAGE_CHECK(!application.running);
            if (application.name.empty()) {
                names_match = false;
            }
        }
    }
    MIRAGE_CHECK(all_lnk);
    MIRAGE_CHECK(!hidden_listed);
    MIRAGE_CHECK(names_match);
    MIRAGE_CHECK(found == 4); // hold, ignore, exit, unicode — exactly once each
    note_image_state("discovery baseline", fixtures);
}

/// The budget refuses the whole table instead of truncating it (the frozen
/// fail-closed budget semantics); the fixtures alone guarantee a non-empty
/// table, so the refusal does not depend on the session's real inventory.
void budget_refuses_without_truncation(desktop::ApplicationProvider &app, AppFixtures &) {
    desktop::ApplicationListLimits limits;
    limits.max_applications = 0;
    const desktop::ApplicationListOutcome listing =
        app.list_applications(limits, desktop::CancelToken{});
    MIRAGE_CHECK(!listing.ok);
    MIRAGE_CHECK(listing.error.code == "result_too_large");
    MIRAGE_CHECK(listing.applications.empty());
}

/// The frozen refusal order of the fake contract, on the real frontend:
/// cancellation first, then argument validation, all before any process
/// could exist (proven by the snapshot staying empty).
void rejections_happen_before_effects(desktop::ApplicationProvider &app, AppFixtures &fixtures) {
    desktop::ApplicationLaunchLimits limits;
    desktop::CancelToken cancelled;
    cancelled.request_cancel();

    const auto cancelled_launch = app.launch(fixtures.hold_id, limits, cancelled);
    MIRAGE_CHECK(!cancelled_launch.ok);
    MIRAGE_CHECK(cancelled_launch.cancelled);
    MIRAGE_CHECK(cancelled_launch.error.code == "cancelled");
    MIRAGE_CHECK(pids_by_image(fixtures.hold_image).empty()); // canary: nothing spawned

    const auto cancelled_terminate = app.terminate(fixtures.hold_id, limits, cancelled);
    MIRAGE_CHECK(cancelled_terminate.cancelled);
    MIRAGE_CHECK(cancelled_terminate.error.code == "cancelled");

    const auto cancelled_list = app.list_applications(desktop::ApplicationListLimits{}, cancelled);
    MIRAGE_CHECK(!cancelled_list.ok);
    MIRAGE_CHECK(cancelled_list.error.code == "cancelled");

    const auto cancelled_query = app.running_state(fixtures.hold_id, cancelled);
    MIRAGE_CHECK(!cancelled_query.ok);
    MIRAGE_CHECK(cancelled_query.error.code == "cancelled");

    desktop::ApplicationLaunchLimits zero_timeout;
    zero_timeout.timeout = std::chrono::milliseconds::zero();
    const auto zero_launch = app.launch(fixtures.hold_id, zero_timeout, desktop::CancelToken{});
    MIRAGE_CHECK(!zero_launch.ok);
    MIRAGE_CHECK(zero_launch.error.code == "invalid_argument");
    const auto zero_terminate =
        app.terminate(fixtures.hold_id, zero_timeout, desktop::CancelToken{});
    MIRAGE_CHECK(zero_terminate.error.code == "invalid_argument");

    const auto empty_launch = app.launch("");
    MIRAGE_CHECK(!empty_launch.ok);
    MIRAGE_CHECK(empty_launch.error.code == "invalid_argument");
    const auto empty_terminate = app.terminate("");
    MIRAGE_CHECK(empty_terminate.error.code == "invalid_argument");
    const auto empty_query = app.running_state("");
    MIRAGE_CHECK(empty_query.error.code == "invalid_argument");

    desktop::ApplicationLaunchLimits tight;
    tight.max_command_bytes = 4;
    const auto overlong_launch = app.launch(fixtures.hold_id, tight, desktop::CancelToken{});
    MIRAGE_CHECK(overlong_launch.error.code == "invalid_argument");

    MIRAGE_CHECK(pids_by_image(fixtures.hold_image).empty()); // still nothing spawned
}

/// Unknown ids, traversal ids and malformed ids all fail closed with zero
/// side effects — an id can never reach outside the Start Menu roots.
void unknown_and_escaping_ids_fail_closed(desktop::ApplicationProvider &app,
                                          AppFixtures &fixtures) {
    for (const std::string &bad_id :
         {std::string("mirage-m404/missing.lnk"), std::string("../hold.lnk"),
          std::string("mirage-m404/../hold.lnk"), std::string("mirage-m404//hold.lnk"),
          std::string("/mirage-m404/hold.lnk"), std::string("mirage-m404\\hold.lnk"),
          std::string("mirage-m404/hold.lnk/"), std::string("mirage-m404/.")}) {
        const auto launch = app.launch(bad_id);
        MIRAGE_CHECK(!launch.ok);
        MIRAGE_CHECK(launch.error.code == "not_found");
        const auto terminate = app.terminate(bad_id);
        MIRAGE_CHECK(!terminate.ok);
        MIRAGE_CHECK(terminate.error.code == "not_found");
        const auto query = app.running_state(bad_id);
        MIRAGE_CHECK(!query.ok);
        MIRAGE_CHECK(query.error.code == "not_found");
    }
    MIRAGE_CHECK(pids_by_image(fixtures.hold_image).empty());
    MIRAGE_CHECK(pids_by_image(fixtures.exit_image).empty());
}

/// The launch lifecycle against a real process: instance id, running state
/// (query and listing) agree, the duplicate guard holds, the cooperative
/// terminate succeeds through WM_CLOSE, and everything converges.
void launch_state_and_cooperative_terminate(desktop::ApplicationProvider &app,
                                            AppFixtures &fixtures) {
    desktop::ApplicationLaunchLimits limits;
    limits.timeout = std::chrono::milliseconds{15000};

    const auto launched = app.launch(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(launched.ok);
    MIRAGE_CHECK(launched.instance_id.empty() == false);
    MIRAGE_CHECK(digits_only(launched.instance_id));
    if (!launched.ok) {
        std::fprintf(stderr, "[win32-app-test] hold launch failed: %s (%s)\n",
                     launched.error.code.c_str(), launched.error.message.c_str());
        std::fflush(stderr);
        return;
    }
    MIRAGE_CHECK(wait_for_file(fixtures.hold_ready)); // the instance reached its window

    const auto state = app.running_state(fixtures.hold_id);
    MIRAGE_CHECK(state.ok);
    MIRAGE_CHECK(state.running);
    MIRAGE_CHECK(state.instance_id == launched.instance_id);
    MIRAGE_CHECK(pids_by_image(fixtures.hold_image).size() == 1); // independently observed

    const desktop::ApplicationListOutcome listing = app.list_applications();
    MIRAGE_CHECK(listing.ok);
    bool hold_running_in_list = false;
    for (const desktop::ApplicationInfo &application : listing.applications) {
        if (application.id == fixtures.hold_id) {
            hold_running_in_list = application.running;
        }
    }
    MIRAGE_CHECK(hold_running_in_list);

    const auto duplicate = app.launch(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!duplicate.ok);
    MIRAGE_CHECK(duplicate.error.code == "already_running");
    MIRAGE_CHECK(pids_by_image(fixtures.hold_image).size() == 1); // no duplicate spawned

    const auto terminated = app.terminate(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(terminated.ok);
    MIRAGE_CHECK(terminated.instance_id == launched.instance_id);
    MIRAGE_CHECK(wait_image_gone(fixtures.hold_image));

    const auto after = app.running_state(fixtures.hold_id);
    MIRAGE_CHECK(after.ok);
    MIRAGE_CHECK(!after.running);
    MIRAGE_CHECK(after.instance_id.empty());

    const desktop::ApplicationListOutcome settled = app.list_applications();
    MIRAGE_CHECK(settled.ok);
    for (const desktop::ApplicationInfo &application : settled.applications) {
        if (application.id == fixtures.hold_id) {
            MIRAGE_CHECK(!application.running);
        }
    }

    const auto gone = app.terminate(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!gone.ok);
    MIRAGE_CHECK(gone.error.code == "not_found");
}

/// A self-exiting instance leaves no residue: the registry entry is reaped
/// at the next call's entry, the instance is gone, and relaunching works.
void self_exiting_instance_is_reaped(desktop::ApplicationProvider &app, AppFixtures &fixtures) {
    desktop::ApplicationLaunchLimits limits;
    limits.timeout = std::chrono::milliseconds{15000};

    const auto launched = app.launch(fixtures.exit_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(launched.ok);
    MIRAGE_CHECK(wait_image_gone(fixtures.exit_image));

    const auto state = app.running_state(fixtures.exit_id);
    MIRAGE_CHECK(state.ok);
    MIRAGE_CHECK(!state.running);

    const auto relaunched = app.launch(fixtures.exit_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(relaunched.ok); // no already_running: the dead instance was reaped
    MIRAGE_CHECK(wait_image_gone(fixtures.exit_image));
}

/// A foreign same-image instance (not launched through the provider) is
/// visible by name, blocks relaunch, and is cooperatively terminable.
void foreign_instance_is_visible_and_terminable(desktop::ApplicationProvider &app,
                                                AppFixtures &fixtures) {
    desktop::ApplicationLaunchLimits limits;
    limits.timeout = std::chrono::milliseconds{15000};
    ForeignTracker tracker;

    const std::wstring arguments = L"hold 3600 \"" + fixtures.foreign_ready.wstring() + L"\"";
    const std::optional<ForeignInstance> foreign = spawn_foreign(fixtures.hold_copy, arguments);
    MIRAGE_CHECK(foreign.has_value());
    if (!foreign.has_value()) {
        return;
    }
    tracker.instances.push_back(*foreign);
    MIRAGE_CHECK(wait_for_file(fixtures.foreign_ready));

    const auto state = app.running_state(fixtures.hold_id);
    MIRAGE_CHECK(state.ok);
    MIRAGE_CHECK(state.running);
    MIRAGE_CHECK(state.instance_id == std::to_string(foreign->pid));

    const auto duplicate = app.launch(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(!duplicate.ok);
    MIRAGE_CHECK(duplicate.error.code == "already_running");

    const auto terminated = app.terminate(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(terminated.ok);
    MIRAGE_CHECK(terminated.instance_id == std::to_string(foreign->pid));
    MIRAGE_CHECK(wait_image_gone(fixtures.hold_image));

    const auto after = app.running_state(fixtures.hold_id);
    MIRAGE_CHECK(after.ok);
    MIRAGE_CHECK(!after.running);
}

/// An instance that swallows WM_CLOSE survives visibly: deadline_exceeded,
/// running state still true with the same pid — a surviving instance is a
/// visible failure, never a forced kill and never a fabricated success.
void stuck_instance_survives_its_deadline(desktop::ApplicationProvider &app,
                                          AppFixtures &fixtures) {
    desktop::ApplicationLaunchLimits limits;
    limits.timeout = std::chrono::milliseconds{15000};

    const auto launched = app.launch(fixtures.ignore_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(launched.ok);
    if (!launched.ok) {
        return;
    }
    MIRAGE_CHECK(wait_for_file(fixtures.ignore_ready));

    desktop::ApplicationLaunchLimits short_deadline;
    short_deadline.timeout = std::chrono::milliseconds{1500};
    const auto terminated =
        app.terminate(fixtures.ignore_id, short_deadline, desktop::CancelToken{});
    MIRAGE_CHECK(!terminated.ok);
    MIRAGE_CHECK(!terminated.cancelled);
    MIRAGE_CHECK(terminated.error.code == "deadline_exceeded");
    MIRAGE_CHECK(terminated.instance_id == launched.instance_id);

    const auto state = app.running_state(fixtures.ignore_id);
    MIRAGE_CHECK(state.ok);
    MIRAGE_CHECK(state.running);
    MIRAGE_CHECK(state.instance_id == launched.instance_id);

    // The instance is the test's own fixture: cleanup force-terminates it
    // (the provider never does), and the surface then converges.
    MIRAGE_CHECK(digits_only(launched.instance_id));
    MIRAGE_CHECK(force_terminate(std::stoul(launched.instance_id)));
    MIRAGE_CHECK(wait_image_gone(fixtures.ignore_image));
    const auto settled = app.running_state(fixtures.ignore_id);
    MIRAGE_CHECK(settled.ok);
    MIRAGE_CHECK(!settled.running);
}

/// Cancellation during the cooperative wait: the outcome is cancelled, the
/// instance keeps running (tracked and alive), and a regular terminate
/// still succeeds afterwards.
void mid_wait_cancellation_keeps_instance_running(desktop::ApplicationProvider &app,
                                                  AppFixtures &fixtures) {
    desktop::ApplicationLaunchLimits limits;
    limits.timeout = std::chrono::milliseconds{15000};

    const auto launched = app.launch(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(launched.ok);
    if (!launched.ok) {
        return;
    }
    MIRAGE_CHECK(wait_for_file(fixtures.hold_ready));

    desktop::CancelToken token;
    PTP_TIMER timer = ::CreateThreadpoolTimer(&cancel_timer_callback, &token, nullptr);
    MIRAGE_CHECK(timer != nullptr);
    if (timer == nullptr) {
        return;
    }
    LARGE_INTEGER due{};
    due.QuadPart = -2'500'000; // negative = relative; 100 ns units -> 250 ms
    ::SetThreadpoolTimer(timer, reinterpret_cast<FILETIME *>(&due), 0, 0);

    const auto terminated = app.terminate(fixtures.hold_id, limits, token);
    ::WaitForThreadpoolTimerCallbacks(timer, TRUE);
    ::CloseThreadpoolTimer(timer);

    MIRAGE_CHECK(!terminated.ok);
    MIRAGE_CHECK(terminated.cancelled);
    MIRAGE_CHECK(terminated.error.code == "cancelled");

    const auto state = app.running_state(fixtures.hold_id);
    MIRAGE_CHECK(state.ok);
    MIRAGE_CHECK(state.running);
    MIRAGE_CHECK(state.instance_id == launched.instance_id);

    const auto settled = app.terminate(fixtures.hold_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(settled.ok); // the instance stayed tracked and cooperative
    MIRAGE_CHECK(wait_image_gone(fixtures.hold_image));
}

/// A non-ASCII application id resolves end to end: discovery, query and
/// launch carry the UTF-8 id through the UTF-16 file-system boundary.
void unicode_id_resolves_end_to_end(desktop::ApplicationProvider &app, AppFixtures &fixtures) {
    const auto state = app.running_state(fixtures.unicode_id);
    MIRAGE_CHECK(state.ok);
    MIRAGE_CHECK(!state.running);

    desktop::ApplicationLaunchLimits limits;
    limits.timeout = std::chrono::milliseconds{15000};
    const auto launched = app.launch(fixtures.unicode_id, limits, desktop::CancelToken{});
    MIRAGE_CHECK(launched.ok);
    MIRAGE_CHECK(wait_image_gone(fixtures.exit_image)); // its exit-mode target self-exits
    const auto after = app.running_state(fixtures.unicode_id);
    MIRAGE_CHECK(after.ok);
    MIRAGE_CHECK(!after.running);
}

} // namespace

int main() {
    if (::CoInitializeEx(nullptr, COINIT_MULTITHREADED) != S_OK) {
        std::fprintf(stderr, "[win32-app-test] COM MTA unavailable; fixtures cannot be built\n");
        std::fflush(stderr);
        return 1;
    }

    AppFixtures fixtures;
    wchar_t token_text[16]{};
    std::swprintf(token_text, 16, L"%08x", std::random_device{}());
    const std::wstring token(token_text);

    // --- fixture build (a failure here is a loud harness failure, never a
    // skip: discovery evidence cannot be faked) ---
    std::error_code fs_error;
    fixtures.temp_root = std::filesystem::temp_directory_path(fs_error) / (L"mirage-m404-" + token);
    if (fs_error) {
        std::fprintf(stderr, "[win32-app-test] temp directory unavailable\n");
        return 1;
    }
    std::filesystem::create_directories(fixtures.temp_root, fs_error);
    MIRAGE_CHECK(!fs_error);

    wchar_t module_path[4096]{};
    ::GetModuleFileNameW(nullptr, module_path, 4096);
    const std::filesystem::path helper =
        std::filesystem::path(module_path).parent_path() / L"m404_process_helper.exe";
    MIRAGE_CHECK(std::filesystem::exists(helper));

    fixtures.hold_image = L"m404-" + token + L"-hold.exe";
    fixtures.ignore_image = L"m404-" + token + L"-ignore.exe";
    fixtures.exit_image = L"m404-" + token + L"-exit.exe";
    fixtures.hold_copy = fixtures.temp_root / fixtures.hold_image;
    fixtures.ignore_copy = fixtures.temp_root / fixtures.ignore_image;
    fixtures.exit_copy = fixtures.temp_root / fixtures.exit_image;
    fixtures.hold_ready = fixtures.temp_root / (L"hold-" + token + L".ready");
    fixtures.ignore_ready = fixtures.temp_root / (L"ignore-" + token + L".ready");
    fixtures.foreign_ready = fixtures.temp_root / (L"foreign-" + token + L".ready");
    std::filesystem::copy_file(helper, fixtures.hold_copy, fs_error);
    MIRAGE_CHECK(!fs_error);
    std::filesystem::copy_file(helper, fixtures.ignore_copy, fs_error);
    MIRAGE_CHECK(!fs_error);
    std::filesystem::copy_file(helper, fixtures.exit_copy, fs_error);
    MIRAGE_CHECK(!fs_error);
    fixtures.hold_arguments = L"hold 3600 \"" + fixtures.hold_ready.wstring() + L"\"";
    fixtures.ignore_arguments = L"ignore-close 3600 \"" + fixtures.ignore_ready.wstring() + L"\"";

    // "启动" as explicit code units and UTF-8 bytes: the source stays ASCII,
    // so both gate toolchains compile byte-identical fixtures regardless of
    // their execution charsets.
    const std::wstring unicode_leaf = L"m404-\x542F\x52A8.lnk";
    fixtures.unicode_id = "mirage-m404/m404-\xE5\x90\xAF\xE5\x8A\xA8.lnk";

    const std::wstring menu_root = start_menu_root();
    if (menu_root.empty()) {
        std::fprintf(stderr, "[win32-app-test] Start Menu known folder unavailable\n");
        std::fflush(stderr);
        sweep_run_images(fixtures);
        std::filesystem::remove_all(fixtures.temp_root, fs_error);
        return 1;
    }
    fixtures.start_menu_dir = std::filesystem::path(menu_root) / L"Programs" / L"mirage-m404";
    std::filesystem::create_directories(fixtures.start_menu_dir, fs_error);
    if (fs_error) {
        std::fprintf(stderr, "[win32-app-test] cannot create the Start Menu fixture directory\n");
        std::fflush(stderr);
        sweep_run_images(fixtures);
        std::filesystem::remove_all(fixtures.temp_root, fs_error);
        return 1;
    }
    const std::wstring work_dir = fixtures.temp_root.wstring();
    const auto shortcut = [&](const wchar_t *leaf, const std::filesystem::path &target,
                              const std::wstring &arguments) {
        return create_shortcut((fixtures.start_menu_dir / leaf).wstring(), target.wstring(),
                               arguments, work_dir);
    };
    MIRAGE_CHECK(shortcut(L"hold.lnk", fixtures.hold_copy, fixtures.hold_arguments));
    MIRAGE_CHECK(shortcut(L"ignore.lnk", fixtures.ignore_copy, fixtures.ignore_arguments));
    MIRAGE_CHECK(shortcut(L"exit.lnk", fixtures.exit_copy, L"exit"));
    MIRAGE_CHECK(shortcut(unicode_leaf.c_str(), fixtures.exit_copy, L"exit"));
    MIRAGE_CHECK(shortcut(L"hidden.lnk", fixtures.exit_copy, L"exit"));
    ::SetFileAttributesW((fixtures.start_menu_dir / L"hidden.lnk").c_str(), FILE_ATTRIBUTE_HIDDEN);

    // --- scenarios (single shared environment; the provider owns launch
    // state across scenarios on purpose) ---
    accessor_fail_closed_without_option();

    WindowsDesktopEnvironment env{Win32Options{}, UiaOptions{}, ApplicationOptions{true}};
    if (env.application() == nullptr) {
        // The application frontend opens unconditionally; a null accessor
        // here is an implementation defect, not an environment property.
        std::fprintf(stderr, "[win32-app-test] application accessor is null with the option on\n");
        std::fflush(stderr);
        MIRAGE_CHECK(false);
    } else {
        desktop::ApplicationProvider &app = *env.application();
        run_scenario("discovery_lists_fixtures_and_skips_hidden",
                     discovery_lists_fixtures_and_skips_hidden, app, fixtures);
        run_scenario("budget_refuses_without_truncation", budget_refuses_without_truncation, app,
                     fixtures);
        run_scenario("rejections_happen_before_effects", rejections_happen_before_effects, app,
                     fixtures);
        run_scenario("unknown_and_escaping_ids_fail_closed", unknown_and_escaping_ids_fail_closed,
                     app, fixtures);
        run_scenario("launch_state_and_cooperative_terminate",
                     launch_state_and_cooperative_terminate, app, fixtures);
        run_scenario("self_exiting_instance_is_reaped", self_exiting_instance_is_reaped, app,
                     fixtures);
        run_scenario("foreign_instance_is_visible_and_terminable",
                     foreign_instance_is_visible_and_terminable, app, fixtures);
        run_scenario("stuck_instance_survives_its_deadline", stuck_instance_survives_its_deadline,
                     app, fixtures);
        run_scenario("mid_wait_cancellation_keeps_instance_running",
                     mid_wait_cancellation_keeps_instance_running, app, fixtures);
        run_scenario("unicode_id_resolves_end_to_end", unicode_id_resolves_end_to_end, app,
                     fixtures);
    }

    // --- total cleanup: sweep, Start Menu fixtures, temp tree ---
    sweep_run_images(fixtures);
    for (const wchar_t *leaf :
         {L"hold.lnk", L"ignore.lnk", L"exit.lnk", L"hidden.lnk", unicode_leaf.c_str()}) {
        const std::filesystem::path lnk = fixtures.start_menu_dir / leaf;
        ::SetFileAttributesW(lnk.c_str(), FILE_ATTRIBUTE_NORMAL); // hidden fixture deattribution
    }
    std::filesystem::remove_all(fixtures.start_menu_dir, fs_error);
    MIRAGE_CHECK(!std::filesystem::exists(fixtures.start_menu_dir));
    std::filesystem::remove_all(fixtures.temp_root, fs_error);
    MIRAGE_CHECK(!std::filesystem::exists(fixtures.temp_root));

    ::CoUninitialize();
    return mirage::testing::finish("win32_application_test");
}
