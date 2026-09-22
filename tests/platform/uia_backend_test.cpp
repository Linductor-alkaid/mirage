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
/// ElementFromHandle semantics probe).
class TestGui {
  public:
    TestGui() {
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
        main_ = CreateWindowExW(WS_EX_TOPMOST, kWindowClassName, L"mirage-uia-backend-test",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 400, 300, nullptr,
                                nullptr, instance, nullptr);
        MIRAGE_CHECK(main_ != nullptr);
        if (main_ != nullptr) {
            button_ = CreateWindowExW(0, L"BUTTON", L"mirage-uia-test-button",
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
        hidden_ =
            CreateWindowExW(0, kWindowClassName, L"mirage-uia-test-hidden", WS_OVERLAPPEDWINDOW, 40,
                            40, 200, 150, nullptr, nullptr, instance, nullptr);
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
/// BN_CLICKED, delivered through the pumped message queue).
void expect_click(AccessibilityProvider &a11y, const ElementTarget &target, const char *scenario) {
    run_pump(50); // flush anything pending before the measurement
    MIRAGE_CHECK(g_click_event != nullptr);
    if (g_click_event == nullptr) {
        return;
    }
    ResetEvent(g_click_event);
    const int before = g_click_count;
    const auto outcome = a11y.activate_element(target);
    MIRAGE_CHECK(outcome.ok);
    if (!outcome.ok) {
        std::fprintf(stderr, "note: %s activation refused: %s (%s)\n", scenario,
                     outcome.error.code.c_str(), outcome.error.message.c_str());
        return;
    }
    const bool delivered = pump_until_event(g_click_event, 5000);
    MIRAGE_CHECK(delivered);
    MIRAGE_CHECK(g_click_count == before + 1);
}

/// All three DEC-005 resolution rings hit the fixture button and click it
/// exactly once; every rejection below refuses before any side effect, so
/// the click counter must not move.
void activation_scenarios(AccessibilityProvider &a11y, const SnapshotRefs &refs) {
    if (!refs.ok || refs.button_ref.empty()) {
        std::fprintf(stderr, "note: no button ref from the fresh snapshot; click scenarios "
                             "cannot run (earlier checks recorded the failures)\n");
        return;
    }

    ElementTarget by_reference;
    by_reference.reference.id = refs.button_ref;
    expect_click(a11y, by_reference, "by-reference");

    ElementTarget by_semantic;
    by_semantic.semantic.role = "button";
    by_semantic.semantic.name = kButtonName;
    expect_click(a11y, by_semantic, "by-semantic");

    ElementTarget by_structural;
    by_structural.structural.path = std::string("/window/") + kMainTitle + "/button/" + kButtonName;
    expect_click(a11y, by_structural, "by-structural");

    // Rejections before effects (click counter frozen across all of them).
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

    ElementTarget missing;
    missing.semantic.role = "button";
    missing.semantic.name = "no-such-button";
    MIRAGE_CHECK(a11y.activate_element(missing).error.code == "not_found");

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

} // namespace

int main() {
    TestGui gui;
    if (!gui.ok()) {
        std::fprintf(stderr, "fixture windows could not be created; interactive desktop "
                             "required for uia_backend_test\n");
        return 1;
    }

    environment_probe_and_identity();

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

    snapshot_rejections(*a11y, gui.main_window());
    const SnapshotRefs first = snapshot_success_and_invariants(*a11y, gui);
    budget_fail_closed(*a11y, gui.main_window(), first);
    const SnapshotRefs fresh = registry_replacement(*a11y, gui.main_window());

    HANDLE clicks = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    MIRAGE_CHECK(clicks != nullptr);
    g_click_event = clicks;
    activation_scenarios(*a11y, fresh);
    g_click_event = nullptr;
    if (clicks != nullptr) {
        CloseHandle(clicks);
    }

    set_text_scenarios(*a11y, fresh, gui);
    cancellation_precedence(*a11y, fresh);
    hidden_window_semantics(*a11y, gui);

    return mirage::testing::finish("uia_backend_test");
}
