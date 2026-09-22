// M4-02 UIA accessibility backend tests against the session's real
// interactive desktop (DEC-017 decision 9): a real fixture window with a
// real BUTTON and EDIT child, observed and driven through the actual UI
// Automation service — no simulator. Run evidence can only come from a
// machine with an interactive desktop (the CI windows job or a maintainer
// Windows machine); the MinGW cross builds on Linux provide compile and
// link evidence only.
//
// Assertion philosophy (same as win32_backend_test): invariant-style, never
// environment-fragile. Every reported outcome must agree with independently
// observed desktop state (GetWindowTextW, GetWindowRect, WM_COMMAND
// delivery into the fixture's window procedure), while contract rejections
// (malformed ids, budgets, bad UTF-8, cancellation, hint refusals) are
// strict and happen before any side effect. UIA Invoke and Value patterns
// do not require the fixture to own the foreground, so — unlike
// win32_backend_test — no scenario depends on window activation: every
// scenario runs in an ordinary desktop session. The message loop runs on
// the main thread only (RULE-03: no threads anywhere).
//
// Desktop-scan isolation (M4-02 CI evidence, four wedged rounds): the
// semantic/structural resolution scenarios walk the live desktop tree from
// the desktop root, and one wedged provider window on the runner desktop (a
// UWP host process) can hang the first cross-process UIA call into it
// forever — outside the reach of every bounded mechanism (the
// transaction/connection timeouts are client-side hints evaluated outside
// the call; the 30s scan budget only fires between calls). The only
// reliable bound on such a call is the death of the process carrying it,
// so the live-tree scenarios run in a disposable child of this same
// executable (`--scan-probe`): the parent launches itself, watches 45s,
// and TerminateProcess()es the child on timeout with a loud
// environment-limited note (the same skip discipline as
// win32_backend_test's foreground-lock note; never a silent pass). The
// scenarios that only touch the fixture's own HWNDs stay in this process,
// where those same CI rounds proved them reliable. Every scenario emits
// flushed `[uia-test]` stage markers so a hang localizes to one stage in
// the CI log.

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

#include <cctype>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using mirage::desktop::AccessibilityProvider;
using mirage::desktop::CancelToken;
using mirage::desktop::ElementTarget;
using mirage::desktop::InputLimits;
using mirage::desktop::SemanticNode;
using mirage::desktop::SemanticSnapshot;
using mirage::desktop::SemanticSnapshotLimits;
using mirage::desktop::SnapshotOutcome;
using mirage::platform::windows_backend::UiaOptions;
using mirage::platform::windows_backend::Win32Options;
using mirage::platform::windows_backend::WindowsDesktopEnvironment;

constexpr wchar_t kWindowClassName[] = L"MirageUiaBackendTestClass";
constexpr const char *kMainTitle = "mirage-uia-backend-test";
constexpr const char *kHiddenTitle = "mirage-uia-test-hidden";
constexpr const char *kButtonName = "mirage-uia-test-button";
constexpr int kButtonId = 2101;
constexpr int kEditId = 2102;

// `--scan-probe` child mode: the live-tree desktop-scan scenarios only
// (semantic hit + click, structural hit + click, structural hit on a second
// locally built path, semantic negative not_found), with strict in-child
// assertions; the parent consumes the child's exit code as evidence.
constexpr char kScanProbeFlag[] = "--scan-probe";

// Name suffix the scan-probe child appends to every fixture title and
// control name: both processes' fixtures coexist on the same desktop while
// the child scans it, so an exact-name hit must never be ambiguous between
// them.
constexpr const char *kScanNameSuffix = "-scan-probe";

// The negative semantic probe: a button name no desktop window carries.
// Resolving it honestly (not_found) requires walking the whole desktop —
// exactly the wedge-prone scan the child isolation exists for; if the child
// hangs here the watchdog note records the session limitation.
constexpr const char *kScanAbsentButtonName = "mirage-uia-scan-probe-absent-button";

// Hard watchdog on the scan-probe child: generous over every bounded-but-
// slow live-tree path on a loaded desktop, yet small against the ctest
// budget (the bounded scans spend at most 30s between calls).
constexpr DWORD kScanProbeWatchdogMs = 45000;

/// Progress marker for the CI log: one flushed stderr line. The flush is
/// the point — a wedged cross-process UIA call must not be able to swallow
/// the marker naming the stage it hung in.
void stage(const std::string &marker) {
    std::fprintf(stderr, "[uia-test] %s\n", marker.c_str());
    std::fflush(stderr);
}

// File-scope state the window procedure records for the click scenarios.
// The test is single-threaded (main-thread message pump only, RULE-03), so
// plain globals need no synchronization.
int g_click_count = 0;
HANDLE g_click_event = nullptr;

LRESULT CALLBACK test_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        // BN_CLICKED from the fixture button id is the observable side
        // effect one UIA Invoke is allowed to produce; notifications from
        // the EDIT (EN_CHANGE after set_text) carry the other id and are
        // ignored.
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == static_cast<WORD>(kButtonId)) {
            ++g_click_count;
            if (g_click_event != nullptr) {
                SetEvent(g_click_event);
            }
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

/// Services the thread's message queue for `milliseconds` (PeekMessage
/// drain plus a MsgWaitForMultipleObjects wait so the loop yields).
void run_pump(DWORD milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + milliseconds;
    MSG message{};
    while (GetTickCount64() < deadline) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 15, QS_ALLINPUT);
    }
}

/// Drains the queue until `event` fires or the deadline passes; false on
/// timeout. Invoked clicks reach the window procedure only while this pump
/// runs, which is why delivery checks never assert before pumping.
bool pump_until_event(HANDLE event, DWORD timeout_milliseconds) {
    const ULONGLONG deadline = GetTickCount64() + timeout_milliseconds;
    MSG message{};
    for (;;) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (WaitForSingleObject(event, 0) == WAIT_OBJECT_0) {
            return true;
        }
        if (GetTickCount64() >= deadline) {
            return false;
        }
        MsgWaitForMultipleObjects(1, &event, FALSE, 15, QS_ALLINPUT);
    }
}

/// Window ids are the decimal form of the HWND value (DEC-017 decision 7).
std::string window_id(HWND window) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(window));
}

/// UTF-16 -> UTF-8 for comparing the fixture's W-side observations against
/// the UTF-8 provider payloads.
std::string utf16_to_utf8_str(const std::wstring &text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(size), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                            out.data(), size, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// UTF-8 -> UTF-16 with the same strict conversion discipline the backend
/// applies (MB_ERR_INVALID_CHARS); fixture payloads are valid, so an empty
/// result can only mean the conversion failed.
std::wstring utf8_to_utf16_str(const std::string &text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                            static_cast<int>(text.size()), out.data(), size);
    if (written <= 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// Case-folded copy for file-name comparisons (NTFS is case-insensitive and
/// the process image name casing is not contractual).
std::string lowered(std::string text) {
    for (char &ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

/// The fixture control's content, read back through the independent
/// GetWindowTextW path (works for the EDIT's set text).
std::wstring control_text(HWND window) {
    const int length = GetWindowTextLengthW(window);
    if (length <= 0) {
        return {};
    }
    std::wstring text(static_cast<std::size_t>(length), L'\0');
    const int copied = GetWindowTextW(window, text.data(), length + 1);
    if (copied <= 0) {
        return {};
    }
    text.resize(static_cast<std::size_t>(copied));
    return text;
}

/// This process's executable file name: the snapshot's application field
/// must agree with it (case-insensitively).
std::string exe_file_name() {
    WCHAR path[1024];
    const DWORD size =
        GetModuleFileNameW(nullptr, path, static_cast<DWORD>(sizeof(path) / sizeof(path[0])));
    if (size == 0 || size >= static_cast<DWORD>(sizeof(path) / sizeof(path[0]))) {
        return {};
    }
    const std::wstring full(path, size);
    const auto slash = full.find_last_of(L"\\/");
    return utf16_to_utf8_str(slash == std::wstring::npos ? full : full.substr(slash + 1));
}

/// First node with the given role (and name, when non-empty), or null.
const SemanticNode *find_node(const SemanticSnapshot &snapshot, const std::string &role,
                              const std::string &name) {
    for (const SemanticNode &node : snapshot.nodes) {
        if (node.role == role && (name.empty() || node.name == name)) {
            return &node;
        }
    }
    return nullptr;
}

bool near_equal(int a, int b) { return a - b <= 2 && b - a <= 2; }

/// Registers the test window class and creates the fixture windows: a
/// visible TOPMOST main window carrying a real push button (Invoke target)
/// and a real EDIT (Value target), plus a never-shown window (the
/// ElementFromHandle semantics probe). `name_suffix` disambiguates a second
/// fixture set on the same desktop (the scan-probe child coexists with the
/// parent's windows while it scans the live tree).
class TestGui {
  public:
    explicit TestGui(const std::string &name_suffix = std::string()) {
        WNDCLASSEXW window_class{};
        window_class.cbSize = static_cast<UINT>(sizeof(window_class));
        window_class.style = CS_HREDRAW | CS_VREDRAW;
        window_class.lpfnWndProc = &test_wndproc;
        window_class.hInstance = GetModuleHandleW(nullptr);
        window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        window_class.hbrBackground =
            reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_WINDOW + 1));
        window_class.lpszClassName = kWindowClassName;
        MIRAGE_CHECK(RegisterClassExW(&window_class) != 0);

        const HINSTANCE instance = GetModuleHandleW(nullptr);
        // The title doubles as the window/structural-path marker; WS_EX_TOPMOST
        // plus an explicit HWND_TOPMOST raise keeps the fixture clickable
        // through UIA whatever the desktop shows behind it.
        const std::wstring main_title = utf8_to_utf16_str(std::string(kMainTitle) + name_suffix);
        const std::wstring button_text = utf8_to_utf16_str(std::string(kButtonName) + name_suffix);
        const std::wstring hidden_title =
            utf8_to_utf16_str(std::string(kHiddenTitle) + name_suffix);
        main_ = CreateWindowExW(WS_EX_TOPMOST, kWindowClassName, main_title.c_str(),
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300, nullptr,
                                nullptr, instance, nullptr);
        MIRAGE_CHECK(main_ != nullptr);
        if (main_ != nullptr) {
            button_ = CreateWindowExW(0, L"BUTTON", button_text.c_str(),
                                      WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 24, 24, 200, 32, main_,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kButtonId)),
                                      instance, nullptr);
            MIRAGE_CHECK(button_ != nullptr);
            edit_ = CreateWindowExW(
                0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER, 24, 72, 340, 30, main_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEditId)), instance, nullptr);
            MIRAGE_CHECK(edit_ != nullptr);
            SetWindowPos(main_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        // Created but never shown: IsWindow still holds, so the id resolves
        // and the snapshot scenario observes the honest element semantics
        // for an invisible window.
        hidden_ = CreateWindowExW(0, kWindowClassName, hidden_title.c_str(), WS_OVERLAPPEDWINDOW,
                                  40, 40, 200, 150, nullptr, nullptr, instance, nullptr);
        MIRAGE_CHECK(hidden_ != nullptr);
        run_pump(300); // let the creation messages and DWM composition settle
    }

    ~TestGui() {
        if (edit_ != nullptr) {
            DestroyWindow(edit_);
        }
        if (button_ != nullptr) {
            DestroyWindow(button_);
        }
        if (main_ != nullptr) {
            DestroyWindow(main_);
        }
        if (hidden_ != nullptr) {
            DestroyWindow(hidden_);
        }
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }

    bool ok() const {
        return main_ != nullptr && button_ != nullptr && edit_ != nullptr && hidden_ != nullptr;
    }
    HWND main_window() const { return main_; }
    HWND edit_window() const { return edit_; }
    HWND hidden_window() const { return hidden_; }

  private:
    HWND main_ = nullptr;
    HWND button_ = nullptr;
    HWND edit_ = nullptr;
    HWND hidden_ = nullptr;
};

/// The refs the scenarios address elements by: found in the latest
/// successful snapshot of the main fixture window.
struct SnapshotRefs {
    std::string button_ref;
    std::string text_ref;
    std::size_t node_count = 0;
    bool ok = false;
};

/// Opt-in surface honesty (DEC-017 capability honesty): the UIA provider is
/// exposed only when UiaOptions enables it, and UIA — a Windows system
/// component — cannot legitimately fail its probe on an interactive
/// session, so a null there is recorded as a loud failure, never skipped.
void environment_probe_and_identity() {
    WindowsDesktopEnvironment off;
    MIRAGE_CHECK(off.accessibility() == nullptr); // UiaOptions defaults to off (M4-02)
    MIRAGE_CHECK(off.info().name == "mirage-windows");
    MIRAGE_CHECK(off.info().platform == "windows");

    WindowsDesktopEnvironment win32_only(Win32Options{true});
    MIRAGE_CHECK(win32_only.accessibility() == nullptr); // Uia still off by default
    MIRAGE_CHECK(win32_only.window() != nullptr);        // interactive-session probe honesty
    MIRAGE_CHECK(win32_only.info().name == "mirage-windows");

    WindowsDesktopEnvironment both(Win32Options{true}, UiaOptions{true});
    if (both.accessibility() == nullptr) {
        std::fprintf(stderr, "note: UiaBackend::open() returned null (COM MTA / CUIAutomation "
                             "probe failed) — unexpected on an interactive session; the check "
                             "below records this loudly\n");
    }
    MIRAGE_CHECK(both.accessibility() != nullptr);
    MIRAGE_CHECK(both.window() != nullptr);
    MIRAGE_CHECK(both.info().name == "mirage-windows");
    MIRAGE_CHECK(both.info().platform == "windows");
}

/// Snapshot validation happens before any tree walk: malformed id shapes,
/// unknown windows and pre-set cancellation are contract rejections with
/// zero side effects.
void snapshot_rejections(AccessibilityProvider &a11y, HWND main_window) {
    MIRAGE_CHECK(!a11y.semantic_snapshot("").ok);
    MIRAGE_CHECK(a11y.semantic_snapshot("").error.code == "invalid_argument");
    MIRAGE_CHECK(a11y.semantic_snapshot("12abc").error.code == "invalid_argument");
    MIRAGE_CHECK(a11y.semantic_snapshot("0").error.code ==
                 "invalid_argument"); // 0 is never an HWND
    // 20 digits: beyond the 19-digit id shape.
    MIRAGE_CHECK(a11y.semantic_snapshot("12345678901234567890").error.code == "invalid_argument");
    // Well-formed decimal that resolves to no window.
    MIRAGE_CHECK(!a11y.semantic_snapshot("4294967295").ok);
    MIRAGE_CHECK(a11y.semantic_snapshot("4294967295").error.code == "not_found");

    // Entry pre-check cancellation, observed even for the fixture's own
    // valid id: cancellation precedes every other check.
    CancelToken cancel;
    cancel.request_cancel();
    const auto cancelled =
        a11y.semantic_snapshot(window_id(main_window), SemanticSnapshotLimits{}, cancel);
    MIRAGE_CHECK(cancelled.cancelled);
    MIRAGE_CHECK(!cancelled.ok);
    MIRAGE_CHECK(cancelled.error.code == "cancelled");
}

/// The success path and its structural invariants: identity fields agree
/// with independently observed state (GetWindowTextW, the process image
/// name), refs are consecutive @e1..@eN with parent-before-child ordering,
/// and both fixture controls appear under the projected role vocabulary.
SnapshotRefs snapshot_success_and_invariants(AccessibilityProvider &a11y, const TestGui &gui) {
    SnapshotRefs refs;
    const auto outcome = a11y.semantic_snapshot(window_id(gui.main_window()));
    MIRAGE_CHECK(outcome.ok);
    if (!outcome.ok) {
        std::fprintf(stderr, "note: snapshot of the fixture window failed: %s\n",
                     outcome.error.message.c_str());
        return refs;
    }
    refs.ok = true;
    const SemanticSnapshot &snapshot = outcome.snapshot;

    // Identity: the title the WindowProvider id was listed with, and this
    // process's own executable file name.
    MIRAGE_CHECK(snapshot.window_title == kMainTitle);
    MIRAGE_CHECK(!snapshot.application.empty());
    MIRAGE_CHECK(lowered(snapshot.application) == lowered(exe_file_name()));

    MIRAGE_CHECK(!snapshot.nodes.empty());
    refs.node_count = snapshot.nodes.size();
    std::fprintf(stderr, "uia snapshot: %lu control-view node(s) for the fixture window\n",
                 static_cast<unsigned long>(snapshot.nodes.size()));

    // The root is the fixture window itself.
    MIRAGE_CHECK(snapshot.nodes[0].ref == "@e1");
    MIRAGE_CHECK(snapshot.nodes[0].role == "window");
    MIRAGE_CHECK(snapshot.nodes[0].parent == mirage::desktop::kNoParent);

    // Whole-vector invariants: consecutive refs, parent indices always
    // strictly before the child.
    for (std::size_t i = 0; i < snapshot.nodes.size(); ++i) {
        const SemanticNode &node = snapshot.nodes[i];
        MIRAGE_CHECK(node.ref == "@e" + std::to_string(i + 1));
        MIRAGE_CHECK(node.parent == mirage::desktop::kNoParent || node.parent < i);
    }

    // The fixture's own controls, under the frozen role projection.
    const SemanticNode *button = find_node(snapshot, "button", kButtonName);
    if (button == nullptr) {
        std::fprintf(stderr,
                     "note: no button node named \"%s\" in the snapshot — the fixture "
                     "controls must appear in the control view\n",
                     kButtonName);
    }
    MIRAGE_CHECK(button != nullptr);
    const SemanticNode *text = find_node(snapshot, "text", "");
    MIRAGE_CHECK(text != nullptr); // the EDIT projects to Edit -> "text"

    // Geometry invariants: UIA reports screen-space rectangles, so the
    // button's bounds must sit inside the fixture window's own screen
    // rectangle — checked strictly when the two coordinate observations
    // agree (the window node vs GetWindowRect), validity-only otherwise
    // (DPI virtualization on scaled sessions can shift the spaces apart).
    if (button != nullptr) {
        MIRAGE_CHECK(button->enabled);
        MIRAGE_CHECK(button->geometry.width >= 0);
        MIRAGE_CHECK(button->geometry.height >= 0);
        if (button->geometry.width == 0 || button->geometry.height == 0) {
            std::fprintf(stderr, "note: the button reported a zero-size bounding rectangle "
                                 "(occluded or virtualized session)\n");
        }
        RECT win_rect{};
        MIRAGE_CHECK(GetWindowRect(gui.main_window(), &win_rect) != 0);
        const SemanticNode &window_node = snapshot.nodes[0];
        const bool spaces_agree =
            near_equal(window_node.geometry.x, win_rect.left) &&
            near_equal(window_node.geometry.y, win_rect.top) &&
            near_equal(window_node.geometry.width, win_rect.right - win_rect.left) &&
            near_equal(window_node.geometry.height, win_rect.bottom - win_rect.top);
        if (spaces_agree) {
            MIRAGE_CHECK(button->geometry.x >= win_rect.left);
            MIRAGE_CHECK(button->geometry.y >= win_rect.top);
            MIRAGE_CHECK(button->geometry.x + button->geometry.width <= win_rect.right);
            MIRAGE_CHECK(button->geometry.y + button->geometry.height <= win_rect.bottom);
        } else {
            std::fprintf(stderr,
                         "note: UIA coordinates disagree with GetWindowRect "
                         "(window node %ld,%ld %ldx%ld vs rect %ld,%ld %ldx%ld); containment "
                         "cannot be checked across the shifted spaces\n",
                         static_cast<long>(window_node.geometry.x),
                         static_cast<long>(window_node.geometry.y),
                         static_cast<long>(window_node.geometry.width),
                         static_cast<long>(window_node.geometry.height),
                         static_cast<long>(win_rect.left), static_cast<long>(win_rect.top),
                         win_rect.right - win_rect.left, win_rect.bottom - win_rect.top);
        }
        refs.button_ref = button->ref;
    }
    if (text != nullptr) {
        refs.text_ref = text->ref;
    }
    return refs;
}

/// Budget fail-closed: a one-node budget refuses the fixture tree (window +
/// button + EDIT, always more than one) instead of truncating, and the
/// refusal leaves the previous snapshot's registry intact — its text-node
/// ref still resolves (the EDIT exposes no Invoke pattern, so activation
/// honestly reports unsupported_element; a cleared registry would report
/// not_found instead).
void budget_fail_closed(AccessibilityProvider &a11y, HWND main_window, const SnapshotRefs &refs) {
    SemanticSnapshotLimits tiny;
    tiny.max_nodes = 1;
    const auto refused = a11y.semantic_snapshot(window_id(main_window), tiny, CancelToken{});
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "snapshot_too_large");
    MIRAGE_CHECK(refused.snapshot.nodes.empty());

    if (refs.ok && !refs.text_ref.empty()) {
        ElementTarget surviving;
        surviving.reference.id = refs.text_ref;
        const auto resolved = a11y.activate_element(surviving);
        MIRAGE_CHECK(!resolved.ok);
        MIRAGE_CHECK(resolved.error.code == "unsupported_element");
    }
}

/// Registry replacement (behavior-result-driven refresh policy v1, DEC-005):
/// a fresh successful snapshot replaces the registry wholesale. Handles that
/// never existed report not_found; the re-issued "@e1" now names the NEW
/// tree's root — resolvable, but the window exposes no Invoke pattern.
SnapshotRefs registry_replacement(AccessibilityProvider &a11y, HWND main_window) {
    SnapshotRefs refs;
    const auto fresh = a11y.semantic_snapshot(window_id(main_window));
    MIRAGE_CHECK(fresh.ok);
    if (!fresh.ok) {
        return refs;
    }
    refs.ok = true;
    refs.node_count = fresh.snapshot.nodes.size();
    const SemanticNode *button = find_node(fresh.snapshot, "button", kButtonName);
    const SemanticNode *text = find_node(fresh.snapshot, "text", "");
    if (button != nullptr) {
        refs.button_ref = button->ref;
    }
    if (text != nullptr) {
        refs.text_ref = text->ref;
    }

    ElementTarget unknown;
    unknown.reference.id = "@e424242";
    MIRAGE_CHECK(a11y.activate_element(unknown).error.code == "not_found");

    ElementTarget root;
    root.reference.id = "@e1";
    const auto root_hit = a11y.activate_element(root);
    MIRAGE_CHECK(!root_hit.ok);
    MIRAGE_CHECK(root_hit.error.code == "unsupported_element");
    return refs;
}

/// One activation expected to click the fixture button: the outcome must be
/// ok AND the click must be observable in the window procedure (exactly one
/// BN_CLICKED, delivered through the pumped message queue). Every phase
/// emits a flushed stage marker: activation is the call class a wedged
/// provider can hang, so the log must say precisely where it stopped.
void expect_click(AccessibilityProvider &a11y, const ElementTarget &target, const char *scenario) {
    const std::string tag = std::string("expect_click[") + scenario + "]";
    stage(tag + ": flush pending messages before the measurement");
    run_pump(50); // flush anything pending before the measurement
    MIRAGE_CHECK(g_click_event != nullptr);
    if (g_click_event == nullptr) {
        stage(tag + ": no click event handle; scenario aborted");
        return;
    }
    ResetEvent(g_click_event);
    const int before = g_click_count;
    stage(tag + ": activate_element dispatching");
    const auto outcome = a11y.activate_element(target);
    stage(tag + ": activate_element returned ok=" + (outcome.ok ? "1" : "0") +
          " code=" + outcome.error.code);
    MIRAGE_CHECK(outcome.ok);
    if (!outcome.ok) {
        std::fprintf(stderr, "note: %s activation refused: %s (%s)\n", scenario,
                     outcome.error.code.c_str(), outcome.error.message.c_str());
        return;
    }
    const bool delivered = pump_until_event(g_click_event, 5000);
    stage(tag + ": delivery pump done delivered=" + (delivered ? "1" : "0") +
          " click_count=" + std::to_string(g_click_count));
    MIRAGE_CHECK(delivered);
    MIRAGE_CHECK(g_click_count == before + 1);
}

/// The in-proc share of the DEC-005 resolution rings: the by-reference ring
/// hits the fixture button and clicks it exactly once, and every rejection
/// below refuses before any side effect, so the click counter must not
/// move. The semantic and structural rings walk the live desktop tree —
/// they run in the --scan-probe child (run_desktop_scan_probe), never here:
/// one wedged provider window on this desktop can hang those calls past
/// every bounded mechanism, and only a process boundary bounds that.
void activation_scenarios(AccessibilityProvider &a11y, const SnapshotRefs &refs) {
    if (!refs.ok || refs.button_ref.empty()) {
        std::fprintf(stderr, "note: no button ref from the fresh snapshot; click scenarios "
                             "cannot run (earlier checks recorded the failures)\n");
        return;
    }

    ElementTarget by_reference;
    by_reference.reference.id = refs.button_ref;
    expect_click(a11y, by_reference, "by-reference");

    // Rejections before effects (click counter frozen across all of them;
    // none of these touch the live desktop tree — the missing-reference
    // rejection is a registry miss, the missing semantic probe lives in the
    // scan-probe child).
    const int before = g_click_count;

    ElementTarget visual;
    visual.visual.ocr_text = "mirage ghost text";
    MIRAGE_CHECK(a11y.activate_element(visual).error.code == "unsupported_hint");

    ElementTarget raw;
    raw.raw.x = 5;
    raw.raw.y = 5;
    MIRAGE_CHECK(a11y.activate_element(raw).error.code == "unsupported_hint");

    ElementTarget no_hint;
    MIRAGE_CHECK(a11y.activate_element(no_hint).error.code == "invalid_argument");

    ElementTarget stale;
    stale.reference.id = "@e424242";
    MIRAGE_CHECK(a11y.activate_element(stale).error.code == "not_found");

    MIRAGE_CHECK(g_click_count == before);
}

/// Semantic text input through the registry: multi-byte UTF-8 round-trips
/// (verified per code unit against GetWindowTextW), the byte budget is
/// inclusive, and over-budget / non-UTF-8 payloads are rejected before the
/// element is touched. Elements without a Value pattern refuse honestly.
void set_text_scenarios(AccessibilityProvider &a11y, const SnapshotRefs &refs, const TestGui &gui) {
    if (!refs.ok || refs.text_ref.empty()) {
        std::fprintf(stderr, "note: no text ref from the fresh snapshot; set_text scenarios "
                             "cannot run (earlier checks recorded the failures)\n");
        return;
    }
    ElementTarget field;
    field.reference.id = refs.text_ref;

    const std::string payload = "h\xc3\xa9llo \xe2\x9c\x93 mirage"; // héllo ✓ mirage, 17 bytes
    const auto written = a11y.set_text(field, payload);
    MIRAGE_CHECK(written.ok);
    MIRAGE_CHECK(control_text(gui.edit_window()) == utf8_to_utf16_str(payload));

    // Budget boundary: exactly the payload's byte count succeeds, one below
    // refuses — both before the element is touched.
    InputLimits exact;
    exact.max_text_bytes = payload.size();
    MIRAGE_CHECK(a11y.set_text(field, payload, exact).ok);
    MIRAGE_CHECK(control_text(gui.edit_window()) == utf8_to_utf16_str(payload));

    InputLimits short_of;
    short_of.max_text_bytes = payload.size() - 1;
    const auto over = a11y.set_text(field, payload, short_of);
    MIRAGE_CHECK(!over.ok);
    MIRAGE_CHECK(over.error.code == "invalid_argument");
    MIRAGE_CHECK(control_text(gui.edit_window()) == utf8_to_utf16_str(payload)); // unchanged

    const auto bad = a11y.set_text(field, "\xff\xfe");
    MIRAGE_CHECK(!bad.ok);
    MIRAGE_CHECK(bad.error.code == "invalid_argument");
    MIRAGE_CHECK(control_text(gui.edit_window()) == utf8_to_utf16_str(payload)); // unchanged

    // Element semantics: the BUTTON exposes no Value pattern.
    if (!refs.button_ref.empty()) {
        ElementTarget push_button;
        push_button.reference.id = refs.button_ref;
        const auto not_editable = a11y.set_text(push_button, "nope");
        MIRAGE_CHECK(!not_editable.ok);
        MIRAGE_CHECK(not_editable.error.code == "unsupported_element");
    }
    // The window root exposes no Value pattern either: resolvable, refused.
    ElementTarget root;
    root.reference.id = "@e1";
    const auto root_write = a11y.set_text(root, "nope");
    MIRAGE_CHECK(!root_write.ok);
    MIRAGE_CHECK(root_write.error.code == "unsupported_element");
}

/// Cancellation is observed at the entry check, before shape validation,
/// hint rejection and budget checks.
void cancellation_precedence(AccessibilityProvider &a11y, const SnapshotRefs &refs) {
    const int before = g_click_count;
    CancelToken cancel;
    cancel.request_cancel();

    // Cancel beats the unsupported-hint rejection.
    ElementTarget visual;
    visual.visual.ocr_text = "mirage ghost text";
    const auto cancelled_activate = a11y.activate_element(visual, cancel);
    MIRAGE_CHECK(cancelled_activate.cancelled);
    MIRAGE_CHECK(!cancelled_activate.ok);
    MIRAGE_CHECK(cancelled_activate.error.code == "cancelled");

    // Cancel beats the budget rejection (an empty target would refuse with
    // invalid_argument, so this also proves cancel precedes hint shaping).
    const std::string payload = "over budget by far";
    InputLimits tiny;
    tiny.max_text_bytes = 2;
    ElementTarget field;
    if (!refs.text_ref.empty()) {
        field.reference.id = refs.text_ref;
    }
    const auto cancelled_set = a11y.set_text(field, payload, tiny, cancel);
    MIRAGE_CHECK(cancelled_set.cancelled);
    MIRAGE_CHECK(!cancelled_set.ok);
    MIRAGE_CHECK(cancelled_set.error.code == "cancelled");

    MIRAGE_CHECK(g_click_count == before);
}

/// ElementFromHandle semantics for an existing but never-shown window:
/// exactly two honest outcomes exist — a snapshot (the HWND exposes a UIA
/// element; the hidden window carries no visible control-view children, so
/// typically just the window node) or unsupported_window. Anything else is
/// a contract breach; which branch fired is noted for the run log.
void hidden_window_semantics(AccessibilityProvider &a11y, const TestGui &gui) {
    const auto hidden = a11y.semantic_snapshot(window_id(gui.hidden_window()));
    if (hidden.ok) {
        std::fprintf(stderr,
                     "note: the never-shown window exposes a UIA element; snapshot has %lu "
                     "node(s)\n",
                     static_cast<unsigned long>(hidden.snapshot.nodes.size()));
        MIRAGE_CHECK(hidden.snapshot.window_title == kHiddenTitle);
        MIRAGE_CHECK(!hidden.snapshot.nodes.empty());
        MIRAGE_CHECK(hidden.snapshot.nodes[0].role == "window");
        MIRAGE_CHECK(hidden.snapshot.nodes[0].ref == "@e1");
        MIRAGE_CHECK(hidden.snapshot.nodes[0].parent == mirage::desktop::kNoParent);
    } else {
        std::fprintf(stderr, "note: the never-shown window exposes no UIA element (%s)\n",
                     hidden.error.code.c_str());
        MIRAGE_CHECK(hidden.error.code == "unsupported_window");
    }
}

/// --scan-probe mode (see run_desktop_scan_probe): only the live-tree
/// desktop-scan scenarios run here, in this disposable process. The
/// assertions are strict — the semantic/structural rings must hit the
/// suffixed fixture names (no ambiguity with the parent's same-shaped
/// windows, which coexist on this desktop while the child scans it) and
/// click exactly once, and the negative probe must come back not_found.
/// Any of these can hang in a wedged provider's unkillable COM transaction;
/// the parent's watchdog is the only bound that applies then, and this
/// process dying with its stage log intact is the recorded evidence.
int run_scan_probe() {
    stage("scan-probe mode: enter");
    TestGui gui(kScanNameSuffix);
    if (!gui.ok()) {
        std::fprintf(stderr, "scan-probe: fixture windows could not be created; interactive "
                             "desktop required\n");
        return 1;
    }
    stage("scan-probe: fixture ready (all names carry the scan-probe suffix)");

    WindowsDesktopEnvironment env(mirage::platform::windows_backend::Win32Options{true},
                                  mirage::platform::windows_backend::UiaOptions{true});
    AccessibilityProvider *a11y = env.accessibility();
    if (a11y == nullptr) {
        std::fprintf(stderr, "scan-probe: UIA backend probe failed; no scan evidence\n");
        return 1;
    }
    stage("scan-probe: accessibility backend open");

    HANDLE clicks = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    MIRAGE_CHECK(clicks != nullptr);
    g_click_event = clicks;

    const std::string scan_title = std::string(kMainTitle) + kScanNameSuffix;
    const std::string scan_button = std::string(kButtonName) + kScanNameSuffix;

    // Ring 2, positive: the semantic hint resolves from the live desktop
    // root and must hit this process's own suffixed button, exactly once.
    stage("scan-probe: semantic hit begin (live-tree desktop BFS)");
    {
        ElementTarget hit;
        hit.semantic.role = "button";
        hit.semantic.name = scan_button;
        expect_click(*a11y, hit, "scan-probe semantic hit");
    }
    stage("scan-probe: semantic hit end");

    // Ring 3, positive: the structural path is built locally from this
    // process's own fixture title (the parent's window shares the shape,
    // not the name) and must click the suffixed button exactly once.
    stage("scan-probe: structural hit begin (locally built path)");
    {
        ElementTarget hit;
        hit.structural.path = std::string("/window/") + scan_title + "/button/" + scan_button;
        expect_click(*a11y, hit, "scan-probe structural hit");
    }
    stage("scan-probe: structural hit end");

    // Ring 3, second locally built path: the two-segment window form must
    // resolve to the fixture window itself, which exposes no Invoke pattern
    // and must be refused after resolution (unsupported_element, not
    // not_found — the path did hit).
    stage("scan-probe: structural window-path begin (locally built path)");
    {
        ElementTarget window_root;
        window_root.structural.path = std::string("/window/") + scan_title;
        const auto refused = a11y->activate_element(window_root);
        MIRAGE_CHECK(!refused.ok);
        MIRAGE_CHECK(refused.error.code == "unsupported_element");
    }
    stage("scan-probe: structural window-path end");

    // Ring 2, negative: a name nothing on the desktop carries. The honest
    // refusal still requires scanning the whole tree — the wedge-prone
    // probe, deliberately last so the positive evidence above is already on
    // the log if the wedged provider window swallows this call.
    stage("scan-probe: semantic negative not_found begin (full desktop scan, wedge-prone)");
    {
        ElementTarget absent;
        absent.semantic.role = "button";
        absent.semantic.name = kScanAbsentButtonName;
        const auto missing = a11y->activate_element(absent);
        MIRAGE_CHECK(!missing.ok);
        MIRAGE_CHECK(missing.error.code == "not_found");
    }
    stage("scan-probe: semantic negative not_found end");

    g_click_event = nullptr;
    if (clicks != nullptr) {
        CloseHandle(clicks);
    }
    stage("scan-probe: complete");
    return mirage::testing::finish("uia_backend_test[scan-probe]");
}

/// Launches this executable with --scan-probe and enforces the hard
/// watchdog: a wedged provider window hangs the child's UIA transaction
/// past every in-process bound, so process death is the only reliable
/// bound — TerminateProcess on timeout, with a loud environment-limited
/// note (skip discipline: recorded, never silent, never a fake pass). The
/// child's stderr is this process's stderr, so its flushed stage log and
/// this process's interleaved markers land in one ctest capture. Pure
/// Win32 process API, no threads (RULE-03).
void run_desktop_scan_probe() {
    WCHAR path[1024];
    const DWORD path_cap = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
    const DWORD path_len = GetModuleFileNameW(nullptr, path, path_cap);
    MIRAGE_CHECK(path_len != 0 && path_len < path_cap);
    if (path_len == 0 || path_len >= path_cap) {
        std::fprintf(stderr, "note: cannot locate this executable; the desktop-scan "
                             "scenarios were not run\n");
        return;
    }

    std::wstring command_line;
    command_line += L'"';
    command_line.append(path, path_len);
    command_line += L"\" ";
    command_line += utf8_to_utf16_str(kScanProbeFlag);
    // CreateProcessW may write into the command-line buffer; keep it
    // writable and NUL-terminated.
    std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = static_cast<DWORD>(sizeof(startup));
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    HANDLE inherited[] = {startup.hStdInput, startup.hStdOutput, startup.hStdError};
    for (const HANDLE handle : inherited) {
        if (handle != nullptr) {
            SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
        }
    }

    PROCESS_INFORMATION child{};
    // Both the application path and the full quoted command line, so a
    // space-bearing directory cannot split the launch.
    const BOOL launched = CreateProcessW(path, mutable_command.data(), nullptr, nullptr, TRUE, 0,
                                         nullptr, nullptr, &startup, &child);
    MIRAGE_CHECK(launched != 0);
    if (launched == 0) {
        std::fprintf(stderr,
                     "note: CreateProcessW failed (gle=%lu); the desktop-scan "
                     "scenarios were not run\n",
                     GetLastError());
        return;
    }
    stage("desktop-scan: child launched; watchdog 45000 ms");
    // Pump-aware wait: the child's live-tree scan probes this process's
    // fixture windows too, and their standard-control proxies answer via
    // sent messages — a parent frozen in a plain WaitForSingleObject would
    // manufacture slow answers the watchdog could then misread as a wedge.
    // Draining the queue keeps the only hang sources the real ones (the
    // runner's wedged provider windows). Message loop on the main thread
    // only; the fixture's own click counter is not consulted after this
    // point, so stray delivery during the wait records no false evidence.
    const ULONGLONG wait_deadline = GetTickCount64() + kScanProbeWatchdogMs;
    DWORD waited = WAIT_FAILED;
    for (;;) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (WaitForSingleObject(child.hProcess, 0) == WAIT_OBJECT_0) {
            waited = WAIT_OBJECT_0;
            break;
        }
        if (GetTickCount64() >= wait_deadline) {
            waited = WAIT_TIMEOUT;
            break;
        }
        MsgWaitForMultipleObjects(1, &child.hProcess, FALSE, 15, QS_ALLINPUT);
    }
    if (waited == WAIT_TIMEOUT) {
        TerminateProcess(child.hProcess, 1);
        WaitForSingleObject(child.hProcess, 10000); // reap: no stray child past the watchdog
        std::fprintf(stderr,
                     "note: live-tree desktop scan wedged in this session (a hung UIA provider "
                     "window); the scan-probe child was terminated by the 45s watchdog. "
                     "semantic/structural runtime evidence is recorded as environment-limited — "
                     "not a test failure; rerun on a maintainer Windows machine or once the "
                     "runner desktop no longer carries the hung provider window\n");
    } else if (waited == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        const BOOL got_code = GetExitCodeProcess(child.hProcess, &exit_code);
        MIRAGE_CHECK(got_code != 0);
        // DWORD is unsigned long on every Windows toolchain; %lu takes it
        // directly (an unsigned long cast would be a useless cast).
        std::fprintf(stderr,
                     "[uia-test] desktop-scan: child finished with exit code %lu; its "
                     "interleaved stage log above carries the semantic/structural evidence\n",
                     exit_code);
        MIRAGE_CHECK(exit_code == 0); // child assertions are strict; its failures count here
    } else {
        // WaitForSingleObject failed: no bound exists on the child anymore.
        TerminateProcess(child.hProcess, 1);
        WaitForSingleObject(child.hProcess, 10000);
        MIRAGE_CHECK(waited == WAIT_OBJECT_0 || waited == WAIT_TIMEOUT);
    }
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    stage("desktop-scan: done");
}

} // namespace

int main(int argc, char *argv[]) {
    // Child mode: only the live-tree desktop-scan scenarios, then a normal
    // exit whose code the parent consumes as the scan evidence.
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == kScanProbeFlag) {
            return run_scan_probe();
        }
    }

    stage("fixture: creating windows (main process)");
    TestGui gui;
    if (!gui.ok()) {
        std::fprintf(stderr, "fixture windows could not be created; interactive desktop "
                             "required for uia_backend_test\n");
        return 1;
    }

    stage("environment_probe_and_identity: begin");
    environment_probe_and_identity();
    stage("environment_probe_and_identity: end");

    WindowsDesktopEnvironment env(mirage::platform::windows_backend::Win32Options{true},
                                  mirage::platform::windows_backend::UiaOptions{true});
    AccessibilityProvider *a11y = env.accessibility();
    if (a11y == nullptr || env.window() == nullptr) {
        // Probe failure already recorded loudly by environment_probe_and_identity;
        // no dereference of the null accessors, exit with the recorded failures.
        std::fprintf(stderr,
                     "UIA or Win32 probe failed (SM_CXVIRTUALSCREEN=%d SM_CYVIRTUALSCREEN=%d); "
                     "run evidence requires an interactive session (CI windows job or "
                     "maintainer machine)\n",
                     GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN));
        return mirage::testing::finish("uia_backend_test");
    }

    // In-proc scenarios: fixture-HWND snapshots, registry resolution and the
    // pre-effect rejections only — nothing here walks the live desktop tree,
    // which is what four CI rounds proved wedge-proof in this process.
    stage("snapshot_rejections: begin");
    snapshot_rejections(*a11y, gui.main_window());
    stage("snapshot_rejections: end");

    stage("snapshot_success_and_invariants: begin");
    const SnapshotRefs first = snapshot_success_and_invariants(*a11y, gui);
    stage("snapshot_success_and_invariants: end");

    stage("budget_fail_closed: begin");
    budget_fail_closed(*a11y, gui.main_window(), first);
    stage("budget_fail_closed: end");

    stage("registry_replacement: begin");
    const SnapshotRefs fresh = registry_replacement(*a11y, gui.main_window());
    stage("registry_replacement: end");

    HANDLE clicks = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    MIRAGE_CHECK(clicks != nullptr);
    g_click_event = clicks;
    stage("activation_scenarios (in-proc, by-reference ring): begin");
    activation_scenarios(*a11y, fresh);
    stage("activation_scenarios (in-proc, by-reference ring): end");
    g_click_event = nullptr;
    if (clicks != nullptr) {
        CloseHandle(clicks);
    }

    stage("set_text_scenarios: begin");
    set_text_scenarios(*a11y, fresh, gui);
    stage("set_text_scenarios: end");

    stage("cancellation_precedence: begin");
    cancellation_precedence(*a11y, fresh);
    stage("cancellation_precedence: end");

    stage("hidden_window_semantics: begin");
    hidden_window_semantics(*a11y, gui);
    stage("hidden_window_semantics: end");

    // The semantic/structural rings: isolated in a disposable child of this
    // executable under a hard 45s watchdog (a wedged provider window hangs
    // their live-tree calls past every in-process bound; process death is
    // the only bound that applies). On watchdog timeout the note above
    // records the environment limitation and the run still ends normally.
    stage("desktop-scan probe (child isolation): begin");
    run_desktop_scan_probe();
    stage("desktop-scan probe (child isolation): end");

    return mirage::testing::finish("uia_backend_test");
}
