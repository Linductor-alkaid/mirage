#include "application_backend.hpp"

#include "win32_util.hpp"

// The COM surface is call-scoped only (nothing is held across calls, so no
// MTA anchor is needed — the uia_backend anchor exists for its registry,
// see uia_backend.hpp); every COM / shell type stays inside this
// translation unit (RULE-01).
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
// The Win32 surface lives in win32_util.hpp; the snapshot walker is shared
// with the M4-03 fallback in windows_desktop_environment.cpp.
#include <tlhelp32.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cwctype>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mirage::platform::windows_backend {
namespace {

using mirage::desktop::ApplicationInfo;
using mirage::desktop::ApplicationLaunchLimits;
using mirage::desktop::ApplicationLaunchOutcome;
using mirage::desktop::ApplicationListLimits;
using mirage::desktop::ApplicationListOutcome;
using mirage::desktop::ApplicationQueryOutcome;
using mirage::desktop::CancelToken;
using mirage::desktop::ProviderError;
using win32_util::error;
using win32_util::last_error_suffix;
using win32_util::UniqueHandle;
using win32_util::utf16_to_utf8;
using win32_util::utf8_to_utf16;

/// Instance registry capacity and scan bounds (RULE-07): the registry, the
/// Start Menu walk and the process snapshot are all fixed caps, so none of
/// them can grow without bound.
constexpr std::size_t kMaxTrackedInstances = 256;
constexpr std::size_t kMaxScannedProcesses = 4096;
constexpr std::size_t kMaxScannedEntries = 4096;
/// Poll slice for the cooperative termination wait: the deadline and the
/// token are observed at least this often (the M2-05 / M4-03 slice).
constexpr std::chrono::milliseconds kPollSlice{25};
/// Working buffer for the shell-link string queries; shortcuts are frozen
/// at this generous bound instead of growing with their content (RULE-07).
constexpr int kShortcutBufferChars = 32768;

// Frozen system GUIDs (shlguid.h, knownfolders.h), defined locally so both
// gate toolchains link identically without the SDK GUID libraries — the
// M4-02 discipline of resolving GUID identity at compile time. These three
// are immutable OS contract surface.
constexpr GUID kShellLinkClsid = {
    0x00021401, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr GUID kFolderCommonStartMenu = {
    0xA4115719, 0xD62E, 0x491D, {0xAA, 0x7C, 0xE7, 0x4B, 0x8B, 0xE3, 0xB0, 0x67}};
constexpr GUID kFolderStartMenu = {
    0x625B53C3, 0xAB48, 0x4EC1, {0xBA, 0x1F, 0xA1, 0xEF, 0x41, 0x46, 0xFC, 0x19}};

/// One call = one COM scope (DEC-017 decision 4), without the uia anchor:
/// nothing in this backend is held across calls, so the scope always pairs
/// its CoUninitialize. S_OK and S_FALSE both own one MTA reference.
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

/// Owning wrapper for one call-scoped COM interface pointer acquired
/// through an out-parameter (the same role as uia_backend's ComPtr, minus
/// the moves nothing here needs).
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
    T *get() const { return ptr_; }
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

/// Case-insensitive UTF-16 name equality (file-system names on Windows).
bool iequals(std::wstring_view left, std::wstring_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::towlower(left[index]) != std::towlower(right[index])) {
            return false;
        }
    }
    return true;
}

std::wstring lowercase(std::wstring_view text) {
    std::wstring out(text);
    for (wchar_t &ch : out) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    return out;
}

/// Last path segment, whatever the separators (process image names and
/// shortcut targets arrive with either form).
std::wstring_view image_basename(std::wstring_view path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

/// The Programs folder of one Start Menu root: the common (machine-wide)
/// tree when `common`, the per-user tree otherwise. Empty when the known
/// folder is unavailable (a stripped-down session) — callers skip the root.
std::wstring programs_root(bool common) {
    PWSTR raw = nullptr;
    const HRESULT hr = ::SHGetKnownFolderPath(common ? kFolderCommonStartMenu : kFolderStartMenu, 0,
                                              nullptr, &raw);
    if (hr != S_OK || raw == nullptr) {
        ::CoTaskMemFree(raw); // a failed call still may return an allocation
        return {};
    }
    std::wstring path(raw);
    ::CoTaskMemFree(raw); // accepts null: a failed call has nothing to free
    path += L"\\Programs";
    return path;
}

/// The launch payload a shortcut carries: target path, argument string,
/// working directory and window state — the same fields a shell launch
/// consumes.
struct ShortcutTarget {
    std::wstring path;
    std::wstring arguments;
    std::wstring work_dir;
    int show_cmd = SW_SHOWNORMAL;
};

/// Resolves one .lnk through the shell link object (COM, fully
/// call-scoped). Returns nullopt with `failure` set when the shortcut
/// cannot be read or has no resolvable file target (a store-app or pure
/// IDList shortcut): such entries still list — a desktop launcher shows
/// them too — but launch cannot carry them and no running instance can be
/// recognized by name.
std::optional<ShortcutTarget> resolve_shortcut(const std::wstring &lnk_path,
                                               ProviderError &failure) {
    ComScope scope;
    if (!scope.ok()) {
        failure = error("io_error", "COM apartment is not available for shortcut resolution");
        return std::nullopt;
    }
    ComRef<IShellLinkW> link;
    if (::CoCreateInstance(kShellLinkClsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IShellLinkW),
                           reinterpret_cast<void **>(link.out())) != S_OK ||
        !link) {
        failure = error("io_error", "shell link object is unavailable");
        return std::nullopt;
    }
    ComRef<IPersistFile> file;
    if (link->QueryInterface(__uuidof(IPersistFile), reinterpret_cast<void **>(file.out())) !=
            S_OK ||
        !file) {
        failure = error("io_error", "shell link file persistence is unavailable");
        return std::nullopt;
    }
    if (file->Load(lnk_path.c_str(), STGM_READ) != S_OK) {
        // COM failures report through their own channel, not last-error:
        // the message stays factual instead of quoting a stale code.
        failure = error("io_error", "shortcut could not be loaded");
        return std::nullopt;
    }
    ShortcutTarget target;
    std::wstring buffer(static_cast<std::size_t>(kShortcutBufferChars), L'\0');
    // Flags 0: the stored path with environment variables expanded (the
    // raw, unexpanded form is the SLGP_RAWPATH variant). S_FALSE means the
    // shortcut carries no file path at all.
    if (link->GetPath(buffer.data(), kShortcutBufferChars, nullptr, 0) != S_OK) {
        failure = error("io_error", "shortcut has no resolvable launch target");
        return std::nullopt;
    }
    target.path.assign(buffer.data());
    if (link->GetArguments(buffer.data(), kShortcutBufferChars) == S_OK) {
        target.arguments.assign(buffer.data());
    }
    if (link->GetWorkingDirectory(buffer.data(), kShortcutBufferChars) == S_OK) {
        target.work_dir.assign(buffer.data());
    }
    // Best effort: a failed query leaves the default window state standing.
    link->GetShowCmd(&target.show_cmd);
    return target;
}

/// One discovered application: the shortcut stem in UTF-8 (what a launcher
/// displays) and the absolute path of the winning .lnk.
struct DiscoveredApp {
    std::string name;
    std::wstring lnk_path;
};

/// Walks both Start Menu Programs trees and maps `relative/path.lnk` ids
/// to entries. The machine tree is walked first, the per-user tree second,
/// so a user shortcut replaces the machine one on the same relative id —
/// the XDG-precedence analog of the Linux backend. Hidden entries are
/// skipped (the NoDisplay analog). The walk is bounded by
/// kMaxScannedEntries; unreadable branches are skipped, not errors — the
/// same tolerance desktop launchers apply.
std::map<std::string, DiscoveredApp> discover_applications() {
    std::map<std::string, DiscoveredApp> by_id;
    const std::wstring roots[2] = {programs_root(true), programs_root(false)};
    for (std::size_t index = 0; index < 2; ++index) {
        const std::wstring &root = roots[index];
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path root_path(root);
        std::error_code fs_error;
        std::filesystem::recursive_directory_iterator it(
            root_path, std::filesystem::directory_options::skip_permission_denied, fs_error);
        if (fs_error) {
            continue; // unreadable root: skipped, not an error
        }
        std::size_t visited = 0;
        while (it != std::filesystem::end(it)) {
            if (++visited > kMaxScannedEntries) {
                break;
            }
            const std::filesystem::directory_entry &entry = *it;
            if (entry.is_directory(fs_error) || fs_error) {
                fs_error.clear();
            } else if (iequals(entry.path().extension().wstring(), L".lnk")) {
                const DWORD attributes = ::GetFileAttributesW(entry.path().c_str());
                const bool hidden = attributes != INVALID_FILE_ATTRIBUTES &&
                                    (attributes & FILE_ATTRIBUTE_HIDDEN) != 0;
                if (!hidden) {
                    DiscoveredApp app;
                    app.name = utf16_to_utf8(entry.path().stem().wstring());
                    app.lnk_path = entry.path().wstring();
                    const std::string id =
                        utf16_to_utf8(entry.path().lexically_relative(root_path).generic_wstring());
                    if (!id.empty() && !app.name.empty()) {
                        if (index == 0) {
                            by_id.emplace(id, std::move(app));
                        } else {
                            by_id.insert_or_assign(id, std::move(app));
                        }
                    }
                }
            }
            it.increment(fs_error);
            if (fs_error) {
                break; // an unreadable branch ends this root's walk
            }
        }
    }
    return by_id;
}

/// Ids are relative paths under a Programs root; only real, non-escaping
/// path components are valid (a refused traversal never touches the file
/// system). Backslashes and colons are refused: the id namespace stays
/// unambiguous (one file, one '/'-separated id) and a drive-letter or
/// drive-relative id can never take the join outside the Start Menu roots
/// (std::filesystem's append replaces the whole path with a rooted right
/// side, and a drive-relative one re-roots to the process CWD).
bool valid_application_id(const std::string &application_id) {
    if (application_id.empty() || application_id.front() == '/' ||
        application_id.find('\\') != std::string::npos ||
        application_id.find(':') != std::string::npos) {
        return false;
    }
    const std::string_view view(application_id);
    std::size_t start = 0;
    for (;;) {
        const std::size_t slash = view.find('/', start);
        const std::string_view segment =
            view.substr(start, slash == std::string_view::npos ? slash : slash - start);
        if (segment.empty() || segment == "." || segment == "..") {
            return false;
        }
        if (slash == std::string_view::npos) {
            return true;
        }
        start = slash + 1;
        if (start == view.size()) {
            return false; // trailing separator
        }
    }
}

/// The .lnk file an application id resolves to: the per-user Programs entry
/// wins over the machine one (the discovery precedence, evaluated by real
/// file existence). nullopt when no visible shortcut carries the id.
std::optional<std::wstring> shortcut_path_by_id(const std::string &application_id) {
    if (!valid_application_id(application_id)) {
        return std::nullopt;
    }
    const std::optional<std::wstring> wide_id = utf8_to_utf16(application_id);
    if (!wide_id.has_value()) {
        return std::nullopt;
    }
    const std::wstring roots[2] = {programs_root(false), programs_root(true)};
    for (const std::wstring &root : roots) {
        if (root.empty()) {
            continue;
        }
        const std::filesystem::path candidate =
            std::filesystem::path(root) / std::filesystem::path(*wide_id);
        const DWORD attributes = ::GetFileAttributesW(candidate.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN)) != 0) {
            continue;
        }
        return candidate.wstring();
    }
    return std::nullopt;
}

/// Every process in one bounded snapshot as (pid, image basename) pairs;
/// the idle process and this process are excluded. Windows keeps no
/// zombies, so every listed entry is live work. nullopt when the snapshot
/// itself fails (the API's failure value is INVALID_HANDLE_VALUE): callers
/// refuse instead of answering from an empty table — a fabricated
/// "not running" is worse than an honest io_error.
std::optional<std::vector<std::pair<DWORD, std::wstring>>> snapshot_processes() {
    std::vector<std::pair<DWORD, std::wstring>> processes;
    UniqueHandle snapshot(::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (snapshot.get() == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (::Process32FirstW(snapshot.get(), &entry) == 0) {
        return std::nullopt;
    }
    const DWORD self = ::GetCurrentProcessId();
    std::size_t visited = 0;
    do {
        if (++visited > kMaxScannedProcesses) {
            break;
        }
        if (entry.th32ProcessID == 0 || entry.th32ProcessID == self) {
            continue;
        }
        processes.emplace_back(entry.th32ProcessID, std::wstring(image_basename(entry.szExeFile)));
    } while (::Process32NextW(snapshot.get(), &entry));
    return processes;
}

/// Creation time of a pid, as the 1601-epoch 64-bit FILETIME value; 0 when
/// the process is gone or unreadable — an unreadable candidate sorts
/// oldest, the M2-05 tolerance for unreadable process attributes.
unsigned long long process_creation_time(DWORD pid) {
    UniqueHandle process(::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
    FILETIME created{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (process == nullptr ||
        ::GetProcessTimes(process.get(), &created, &exit, &kernel, &user) == 0) {
        return 0;
    }
    return (static_cast<unsigned long long>(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}

/// Live pids whose image basename matches, ordered oldest first (creation
/// time, then pid) — "which instance" answers stay deterministic, the
/// oldest live process wins (M2-05). nullopt when the snapshot failed.
std::optional<std::vector<DWORD>> find_live_by_image(const std::wstring &image) {
    std::vector<DWORD> matches;
    if (image.empty()) {
        return matches; // nothing to match: a genuine empty answer
    }
    const std::optional<std::vector<std::pair<DWORD, std::wstring>>> processes =
        snapshot_processes();
    if (!processes.has_value()) {
        return std::nullopt;
    }
    for (const auto &[pid, name] : *processes) {
        if (iequals(name, image)) {
            matches.push_back(pid);
        }
    }
    std::sort(matches.begin(), matches.end(), [](DWORD left, DWORD right) {
        const unsigned long long left_time = process_creation_time(left);
        const unsigned long long right_time = process_creation_time(right);
        if (left_time != right_time) {
            return left_time < right_time;
        }
        return left < right;
    });
    return matches;
}

/// Whether the snapshot currently lists this pid (the "already gone"
/// witness for a foreign pid whose waitable open failed). A failed scan is
/// its own answer — "gone" must never be fabricated from it.
enum class SnapshotPresence { kPresent, kAbsent, kScanFailed };

SnapshotPresence pid_presence(DWORD pid) {
    const std::optional<std::vector<std::pair<DWORD, std::wstring>>> processes =
        snapshot_processes();
    if (!processes.has_value()) {
        return SnapshotPresence::kScanFailed;
    }
    for (const auto &[found, image] : *processes) {
        if (found == pid) {
            return SnapshotPresence::kPresent;
        }
    }
    return SnapshotPresence::kAbsent;
}

/// One instance this backend launched: the owning process handle (closing
/// it never terminates anything) and its pid.
struct TrackedInstance {
    UniqueHandle process;
    DWORD pid = 0;
};

/// Launch registry state (Impl derives from it so the free helpers below
/// can take a reference without naming the private nested type).
struct ApplicationState {
    /// Launched instances by application id. One live instance per id is
    /// enforced ("already_running"), so the table only grows with distinct
    /// ids up to its capacity (RULE-07).
    std::map<std::string, TrackedInstance> instances;
};

/// Launched children that exited on their own must leave the table;
/// called at every provider method entry so the table reflects reality
/// without any background thread (RULE-03). A signaled handle means the
/// process exited; erasing the entry closes the handle — the whole reap,
/// since Windows keeps no zombies.
void reap_locked(ApplicationState &state) {
    for (auto it = state.instances.begin(); it != state.instances.end();) {
        if (::WaitForSingleObject(it->second.process.get(), 0) == WAIT_OBJECT_0) {
            it = state.instances.erase(it);
        } else {
            ++it;
        }
    }
}

/// The backend's view of a live instance for one application: the process
/// it launched itself, or — when that one already exited (possibly after
/// handing off to a relauncher) — the oldest live process matching the
/// shortcut target's image name (Linux has no authoritative application ->
/// process mapping and neither does Windows). Returns the pid, whether it
/// is tracked, and the waitable handle for the tracked case.
struct Instance {
    DWORD pid = 0;
    bool tracked = false;
    HANDLE process = nullptr; // borrowed; the registry (or the caller) owns it
};

/// The backend's view of a live instance for one application: the process
/// it launched itself, or — when that one already exited (possibly after
/// handing off to a relauncher) — the oldest live process matching the
/// shortcut target's image name (Linux has no authoritative application ->
/// process mapping and neither does Windows). Returns the pid, whether it
/// is tracked, and the waitable handle for the tracked case. nullopt means
/// no live instance; a failed name scan sets `scan_failed` (tracked
/// instances are still answered — the scan never runs for them), so a
/// caller never reads a snapshot failure as "not running".
std::optional<Instance> find_instance_locked(ApplicationState &state,
                                             const std::string &application_id,
                                             const std::wstring &image_basename,
                                             bool &scan_failed) {
    scan_failed = false;
    const auto tracked = state.instances.find(application_id);
    if (tracked != state.instances.end()) {
        if (::WaitForSingleObject(tracked->second.process.get(), 0) == WAIT_TIMEOUT) {
            return Instance{tracked->second.pid, true, tracked->second.process.get()};
        }
        state.instances.erase(tracked);
    }
    const std::optional<std::vector<DWORD>> matches = find_live_by_image(image_basename);
    if (!matches.has_value()) {
        scan_failed = true;
        return std::nullopt;
    }
    if (matches->empty()) {
        return std::nullopt;
    }
    return Instance{matches->front(), false, nullptr};
}

struct CloseWindowsContext {
    DWORD pid = 0;
};

/// Posts WM_CLOSE to every top-level window of the instance's process,
/// visible or not (any of them may carry the app's exit path). PostMessage
/// never blocks, so a wedged window cannot stall the provider — the same
/// non-blocking-request discipline as SIGTERM. The console carrier window
/// belongs to conhost rather than to the instance pid, so console
/// instances are honestly unreachable this way (recorded limitation).
BOOL CALLBACK post_close_to_windows(HWND window, LPARAM lparam) {
    auto *context = reinterpret_cast<CloseWindowsContext *>(lparam);
    DWORD pid = 0;
    ::GetWindowThreadProcessId(window, &pid);
    if (pid == context->pid) {
        ::PostMessageW(window, WM_CLOSE, 0, 0);
    }
    return TRUE; // always continue: the instance may own several windows
}

/// The CreateProcess command line for the resolved target: the path quoted
/// when it contains whitespace (CreateProcess reads the quoted first token
/// as the executable), the shortcut's argument string verbatim after it —
/// the same command line a shell launch would produce.
std::wstring target_command_line(const ShortcutTarget &target) {
    const bool quote = target.path.find_first_of(L" \t") != std::wstring::npos;
    std::wstring line;
    if (quote) {
        line += L'"';
    }
    line += target.path;
    if (quote) {
        line += L'"';
    }
    if (!target.arguments.empty()) {
        line += L' ';
        line += target.arguments;
    }
    return line;
}

/// The launch working directory: the shortcut's "Start in" value, falling
/// back to the target's own directory (a shell launch does the same when
/// the field is empty).
std::wstring launch_working_directory(const ShortcutTarget &target) {
    if (!target.work_dir.empty()) {
        return target.work_dir;
    }
    const auto slash = target.path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : target.path.substr(0, slash);
}

/// Starts the resolved target. Returns the owning process handle and pid;
/// on failure sets `failure` and returns nullopt. No job object here — an
/// application must outlive the call (unlike the budgeted command
/// execution of M4-03), and forced teardown is out of the terminate
/// contract's scope. CreateProcess never elevates: a
/// requireAdministrator target fails honestly (the foreground-lock
/// discipline of DEC-017 — a system security boundary is reported, not
/// bypassed).
std::optional<TrackedInstance> spawn_target(const ShortcutTarget &target, ProviderError &failure) {
    std::wstring command_line = target_command_line(target);
    const std::wstring work_dir = launch_working_directory(target);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    // Honor the shortcut's window state (its "Run:" box) like a shell launch.
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = static_cast<WORD>(target.show_cmd);
    PROCESS_INFORMATION info{};
    if (::CreateProcessW(nullptr, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
                         work_dir.empty() ? nullptr : work_dir.c_str(), &startup, &info) == 0) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_ELEVATION_REQUIRED) {
            failure =
                error("io_error", "shortcut target requires elevation (launch does not elevate)");
            return std::nullopt;
        }
        failure = error("io_error", "process creation failed (Win32 error " +
                                        std::to_string(static_cast<unsigned long>(code)) + ")");
        return std::nullopt;
    }
    ::CloseHandle(info.hThread);
    TrackedInstance instance;
    instance.pid = info.dwProcessId;
    instance.process.reset(info.hProcess);
    return instance;
}

} // namespace

struct ApplicationBackend::Impl : ApplicationState {};

ApplicationBackend::~ApplicationBackend() = default;

std::unique_ptr<ApplicationBackend> ApplicationBackend::open() {
    auto backend = std::unique_ptr<ApplicationBackend>(new ApplicationBackend());
    // The launch registry lives in Impl; every provider method dereferences
    // impl_ on entry (reap), so it must exist for every constructed backend
    // (the M2-05 shape).
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

    const std::map<std::string, DiscoveredApp> discovered = discover_applications();
    if (discovered.size() > limits.max_applications) {
        outcome.error =
            error("result_too_large", "application table exceeds the budget of " +
                                          std::to_string(limits.max_applications) + " entries");
        return outcome;
    }
    // One bounded snapshot answers the running flag for every application
    // at once instead of one scan per application (the M2-05 /proc index).
    // A failed snapshot refuses the whole call: every row would otherwise
    // claim "not running" on no evidence.
    const std::optional<std::vector<std::pair<DWORD, std::wstring>>> processes =
        snapshot_processes();
    if (!processes.has_value()) {
        outcome.error = error("io_error", "process snapshot unavailable" + last_error_suffix());
        return outcome;
    }
    std::map<std::wstring, std::vector<DWORD>> image_index;
    for (const auto &[pid, image] : *processes) {
        image_index[lowercase(image)].push_back(pid);
    }

    outcome.applications.reserve(discovered.size());
    for (const auto &[id, app] : discovered) {
        bool running = false;
        ProviderError failure;
        const std::optional<ShortcutTarget> target = resolve_shortcut(app.lnk_path, failure);
        if (target.has_value()) {
            const std::wstring basename = lowercase(image_basename(target->path));
            running = !basename.empty() && image_index.count(basename) != 0;
        }
        // A shortcut that fails to resolve keeps its entry with a false
        // running flag: a vanished or unreadable shortcut degrades its own
        // row, never the whole enumeration (the M2-05 tolerance).
        outcome.applications.push_back({id, app.name, running});
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
    const std::optional<std::wstring> lnk = shortcut_path_by_id(application_id);
    if (!lnk.has_value()) {
        outcome.error = error("not_found", "unknown application id");
        return outcome;
    }
    ProviderError failure;
    const std::optional<ShortcutTarget> target = resolve_shortcut(*lnk, failure);
    const std::wstring basename =
        target.has_value() ? std::wstring(image_basename(target->path)) : std::wstring();
    bool scan_failed = false;
    const std::optional<Instance> instance =
        find_instance_locked(*impl_, application_id, basename, scan_failed);
    if (scan_failed) {
        // A failed scan must not read as "not running" (a tracked hit
        // never reaches the scan, so this refusal only covers foreign
        // lookups).
        outcome.error = error("io_error", "process snapshot unavailable" + last_error_suffix());
        return outcome;
    }
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
    // order: a refused launch must never have reached a CreateProcess.
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
    const std::optional<std::wstring> lnk = shortcut_path_by_id(application_id);
    if (!lnk.has_value()) {
        outcome.error = error("not_found", "unknown application id");
        return outcome;
    }
    ProviderError failure;
    // Best-effort resolution: an unresolvable target must not shadow the
    // already_running answer for a live instance (the fake contract order —
    // the Linux backend answers the same way when the Exec line cannot be
    // parsed). A launch that cannot resolve its target and has no instance
    // fails honestly before the registry state is even consulted.
    const std::optional<ShortcutTarget> target = resolve_shortcut(*lnk, failure);
    const std::wstring basename =
        target.has_value() ? std::wstring(image_basename(target->path)) : std::wstring();
    bool scan_failed = false;
    const std::optional<Instance> live =
        find_instance_locked(*impl_, application_id, basename, scan_failed);
    if (scan_failed) {
        // The single-instance invariant cannot be verified: refusing is the
        // fail-closed answer, spawning on an unverified surface is not one.
        outcome.error = error("io_error", "process snapshot unavailable" + last_error_suffix());
        return outcome;
    }
    if (live.has_value()) {
        outcome.error = error("already_running", "application already has a live instance");
        return outcome;
    }
    if (!target.has_value()) {
        outcome.error = std::move(failure); // set for every nullopt return
        return outcome;
    }
    if (impl_->instances.size() >= kMaxTrackedInstances) {
        outcome.error =
            error("result_too_large", "instance registry reached its capacity of " +
                                          std::to_string(kMaxTrackedInstances) + " entries");
        return outcome;
    }
    std::optional<TrackedInstance> spawned = spawn_target(*target, failure);
    if (!spawned.has_value()) {
        outcome.error = std::move(failure);
        return outcome;
    }
    outcome.instance_id = std::to_string(spawned->pid);
    impl_->instances.emplace(application_id, std::move(*spawned));
    outcome.ok = true;
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
    const std::optional<std::wstring> lnk = shortcut_path_by_id(application_id);
    if (!lnk.has_value()) {
        outcome.error = error("not_found", "unknown application id");
        return outcome;
    }
    ProviderError failure;
    const std::optional<ShortcutTarget> target = resolve_shortcut(*lnk, failure);
    const std::wstring basename =
        target.has_value() ? std::wstring(image_basename(target->path)) : std::wstring();
    bool scan_failed = false;
    const std::optional<Instance> instance =
        find_instance_locked(*impl_, application_id, basename, scan_failed);
    if (!instance.has_value() && scan_failed) {
        // A failed scan must not read as "application has no running
        // instance" (a tracked hit never reaches the scan).
        outcome.error = error("io_error", "process snapshot unavailable" + last_error_suffix());
        return outcome;
    }
    if (!instance.has_value()) {
        outcome.error = error("not_found", "application has no running instance");
        return outcome;
    }
    const DWORD pid = instance->pid;
    const std::string instance_id = std::to_string(pid);

    // Ask the instance to exit: WM_CLOSE to its windows — the TERM
    // equivalent. Never TerminateProcess: forced termination is out of
    // contract scope and a surviving instance is a visible failure
    // (deadline_exceeded), exactly like the SIGTERM discipline of M2-05.
    // Every non-exit outcome carries the surviving instance's id: it stays
    // observable (the additive part of the M2-05 semantics — the instance
    // handle names a still-running process).
    CloseWindowsContext context{pid};
    ::EnumWindows(post_close_to_windows, reinterpret_cast<LPARAM>(&context));

    // Waiting needs a waitable handle. Tracked instances own one; a foreign
    // pid is opened for synchronization. An open failure means either gone
    // (success — the request found nothing left to wait for, the ESRCH
    // precedent) or inaccessible (an honest io_error, never a fabricated
    // ok).
    HANDLE wait_on = instance->process;
    UniqueHandle foreign;
    if (!instance->tracked) {
        foreign.reset(::OpenProcess(SYNCHRONIZE, FALSE, pid));
        if (foreign == nullptr) {
            const SnapshotPresence presence = pid_presence(pid);
            if (presence == SnapshotPresence::kAbsent) {
                outcome.ok = true;
                outcome.instance_id = instance_id;
                return outcome;
            }
            outcome.error = error(
                "io_error", presence == SnapshotPresence::kScanFailed
                                ? std::string("process snapshot unavailable") + last_error_suffix()
                                : std::string("instance handle unavailable") + last_error_suffix());
            outcome.instance_id = instance_id;
            return outcome;
        }
        wait_on = foreign.get();
    }

    const auto deadline = std::chrono::steady_clock::now() + limits.timeout;
    for (;;) {
        if (::WaitForSingleObject(wait_on, static_cast<DWORD>(kPollSlice.count())) ==
            WAIT_OBJECT_0) {
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
            outcome.instance_id = instance_id;
            return outcome;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            outcome.error = error("deadline_exceeded",
                                  "instance ignored the termination request within the budget");
            outcome.instance_id = instance_id;
            return outcome;
        }
    }
}

} // namespace mirage::platform::windows_backend
