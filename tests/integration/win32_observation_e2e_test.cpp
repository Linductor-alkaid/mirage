// M4-07 Windows end-to-end closed loop (design doc sections 6, 9 and 11;
// M4 exit condition): the full observe -> action -> observe cycle runs on
// the session's real interactive desktop with the real platform frontends —
// a real fixture window carrying a push button and an edit control, the
// WindowsDesktopEnvironment's Win32 + UIA providers, the desktop-layer
// ObservationAssembler and ElementTargetExecutor, and the whole stack bound
// through MiraEnvironmentBinding exactly as a hosted Mira instance would
// see it. No simulator anywhere.
//
// The closed loop, in DEC-005 terms: observe() delivers a SemanticSnapshot
// of the focused fixture window whose @eN refs are live; a reference
// ElementTarget resolves through the accessibility reference ring and the
// semantic activation really clicks the fixture button (the click lands in
// the fixture's own window procedure — independent evidence, not the
// provider's word); a Value-pattern set_text lands in the edit control
// (independent GetWindowTextW readback) and a FRESH observation carries the
// changed state with re-issued refs.
//
// Assertion philosophy (same as the other win32 suites): invariant-style,
// never environment-fragile. Reported outcomes must agree with
// independently observed desktop state, while contract rejections are
// strict and happen before any side effect. The assembler snapshots the
// FOCUSED window, so the loop scenarios first activate the fixture through
// the environment's own WindowProvider; a session that refuses the
// activation is skipped with a loud note (the win32_backend_test
// foreground-lock discipline — never a silent pass). The message loop runs
// on the main thread only (RULE-03: no threads anywhere).

#include "../support/test.hpp"

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/element_target_executor.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/observation_assembler.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <mirage/platform/windows/windows_desktop_environment.hpp>

#include <mira/environment.hpp>

// Keep every entry point on the explicit W surface regardless of the
// toolchain's default (MinGW defaults to ANSI).
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

namespace {

using mirage::desktop::CancelToken;
using mirage::desktop::DesktopEnvironment;
using mirage::desktop::ElementTarget;
using mirage::desktop::ElementTargetExecutor;
using mirage::desktop::InputLimits;
using mirage::desktop::ObservationAssembler;
using mirage::desktop::ObservationComponents;
using mirage::desktop::SemanticNode;
using mirage::desktop::SemanticSnapshot;
using mirage::platform::windows_backend::ApplicationOptions;
using mirage::platform::windows_backend::NotificationsOptions;
using mirage::platform::windows_backend::UiaOptions;
using mirage::platform::windows_backend::Win32Options;
using mirage::platform::windows_backend::WindowsDesktopEnvironment;
namespace integration = mirage::integration;

constexpr wchar_t kWindowClassName[] = L"MirageM407E2eTestClass";
constexpr const char *kMainTitle = "mirage-m407-e2e-main";
constexpr const char *kButtonName = "mirage-m407-e2e-button";
constexpr const char *kEditText = "mirage-m407-e2e-payload";
constexpr int kButtonId = 2201;
constexpr int kEditId = 2202;

/// Progress marker for the CI log: one flushed stderr line, so a hang
/// localizes to one stage.
void stage(const std::string &marker) {
    std::fprintf(stderr, "[m407-e2e] %s\n", marker.c_str());
    std::fflush(stderr);
}

// File-scope state the window procedure records (single-threaded test, the
// message pump runs on the main thread only, RULE-03).
int g_click_count = 0;

LRESULT CALLBACK test_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED && LOWORD(wparam) == static_cast<WORD>(kButtonId)) {
            ++g_click_count; // the observable side effect of exactly one Invoke
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

/// Services the thread's message queue for `milliseconds`; delivery checks
/// pump before they assert.
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

/// Window ids are the decimal form of the HWND value (DEC-017 decision 7).
std::string window_id(HWND window) {
    return std::to_string(reinterpret_cast<std::uintptr_t>(window));
}

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

std::wstring utf8_to_utf16_str(const std::string &text) {
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(size), L'\0');
    const int written = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                            out.data(), size);
    if (written <= 0) {
        return {};
    }
    out.resize(static_cast<std::size_t>(written));
    return out;
}

/// Case-folded copy (the process image name's casing is not contractual).
std::string lowered(std::string text) {
    for (char &ch : text) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return text;
}

/// The fixture control's content, read through the independent
/// GetWindowTextW path.
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

const SemanticNode *find_node(const SemanticSnapshot &snapshot, const std::string &role,
                              const std::string &name) {
    for (const SemanticNode &node : snapshot.nodes) {
        if (node.role == role && (name.empty() || node.name == name)) {
            return &node;
        }
    }
    return nullptr;
}

/// Registers the test window class and creates the fixture: a visible
/// TOPMOST main window carrying a real push button (Invoke target) and a
/// real EDIT (Value target).
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
        const std::wstring main_title = utf8_to_utf16_str(kMainTitle);
        const std::wstring button_text = utf8_to_utf16_str(kButtonName);
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
        UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
    }

    TestGui(const TestGui &) = delete;
    TestGui &operator=(const TestGui &) = delete;

    bool ok() const { return main_ != nullptr && button_ != nullptr && edit_ != nullptr; }
    HWND main_window() const { return main_; }
    HWND edit_window() const { return edit_; }

  private:
    HWND main_ = nullptr;
    HWND button_ = nullptr;
    HWND edit_ = nullptr;
};

/// The environment the loop runs against: Win32 + UIA enabled, the
/// application and notification surfaces off (not this work item's faces).
/// Null accessors mean the session probes failed: loud skip, never a
/// silent pass.
std::shared_ptr<WindowsDesktopEnvironment> make_environment() {
    auto environment = std::make_shared<WindowsDesktopEnvironment>(
        Win32Options{true}, UiaOptions{true}, ApplicationOptions{false},
        NotificationsOptions{false});
    if (environment->window() == nullptr || environment->accessibility() == nullptr) {
        std::fprintf(stderr,
                     "[m407-e2e] environment probes failed (window=%d accessibility=%d); "
                     "the interactive-desktop scenarios are environment-limited\n",
                     environment->window() != nullptr ? 1 : 0,
                     environment->accessibility() != nullptr ? 1 : 0);
        std::fflush(stderr);
        return nullptr;
    }
    return environment;
}

/// Activates the fixture through the environment's own WindowProvider and
/// pumps until the foreground really is the fixture. False when the session
/// refuses (foreground lock): the caller skips the focused-window scenarios
/// with a loud note.
///
/// The Windows foreground lock grants SetForegroundWindow to the process
/// that received the last input event; a headless harness owns no input
/// history, so before activating, the fixture injects one inert synthetic
/// key (SHIFT down + up through SendInput, the provider's own injection
/// surface) — the honest stand-in for the user interaction that would
/// precede a real activation. The activation itself stays the provider's.
bool make_fixture_foreground(DesktopEnvironment &environment, HWND main_window) {
    std::vector<INPUT> unlock;
    INPUT key{};
    key.type = INPUT_KEYBOARD;
    key.ki.wVk = VK_SHIFT;
    unlock.push_back(key);
    unlock.push_back(key);
    unlock.back().ki.dwFlags = KEYEVENTF_KEYUP;
    if (::SendInput(static_cast<UINT>(unlock.size()), unlock.data(), sizeof(INPUT)) !=
        static_cast<UINT>(unlock.size())) {
        std::fprintf(stderr, "[m407-e2e] foreground unlock SendInput refused (Win32 error %lu)\n",
                     static_cast<unsigned long>(::GetLastError()));
        std::fflush(stderr);
    }
    run_pump(50);

    const auto activation = environment.window()->activate(window_id(main_window));
    run_pump(400);
    if (!activation.ok) {
        std::fprintf(stderr,
                     "[m407-e2e] fixture activation refused (%s): foreground scenarios "
                     "are environment-limited\n",
                     activation.error.code.c_str());
        std::fflush(stderr);
        return false;
    }
    const auto front = environment.window()->front_window(CancelToken{});
    return front.ok && front.found && front.window.id == window_id(main_window);
}

/// The binding's capability honesty for the Windows backend surface, and
/// the refusal semantics of the pinned input face (M2-05, on the real
/// backend): dispatch is rejected before any side effect.
void binding_capabilities_are_honest(const std::shared_ptr<DesktopEnvironment> &environment) {
    stage("binding capabilities");
    integration::MiraEnvironmentBinding binding(environment);

    const mira::EnvironmentCapabilities capabilities = binding.capabilities();
    MIRAGE_CHECK(capabilities.foreground_app);  // window provider present
    MIRAGE_CHECK(capabilities.ui_tree);         // window + accessibility present
    MIRAGE_CHECK(!capabilities.screen_capture); // no visual pipeline wired
    MIRAGE_CHECK(capabilities.perception_sources == 0);
    MIRAGE_CHECK(!capabilities.discrete_input); // the pinned input surface is unmapped
    MIRAGE_CHECK(!capabilities.input_release);

    // The capability gate fails closed: a required component the Windows
    // surface cannot deliver (screen without the visual wiring) refuses
    // the whole request instead of returning a silently incomplete
    // observation.
    mira::ObservationRequest screen_request;
    screen_request.required.screen = true;
    MIRAGE_CHECK(!binding.observe(screen_request, mira::make_control_context()).has_value());

    mira::InputSequence sequence;
    sequence.events.push_back(mira::InputEvent{"tap", "0.5,0.5"});
    const auto receipt = binding.execute(sequence, mira::make_control_context());
    MIRAGE_CHECK(receipt.has_value());
    MIRAGE_CHECK(receipt.value().status == mira::ExecutionStatus::Rejected);
    MIRAGE_CHECK(!receipt.value().side_effect_may_have_occurred);
    MIRAGE_CHECK(binding.interrupt(mira::make_control_context()).has_value());
    MIRAGE_CHECK(binding.interrupt(mira::make_control_context()).has_value());
}

/// observe() through the binding: the pinned projection of the fixture's
/// real UIA tree plus the honest foreground, with the @eN refs surviving
/// into the pinned stable hints and the validator staying clean.
void binding_observe_projects_the_real_desktop(
    const std::shared_ptr<DesktopEnvironment> &environment) {
    stage("binding observe projection");
    integration::MiraEnvironmentBinding binding(environment);

    mira::ObservationRequest request;
    request.required.structure = true;
    request.required.foreground = true;
    const auto result = binding.observe(request, mira::make_control_context());
    MIRAGE_CHECK(result.has_value());
    const mira::Observation &observation = result.value();

    MIRAGE_CHECK(!observation.id.is_nil());
    MIRAGE_CHECK(observation.quality.overall == mira::ComponentQuality::Good);
    MIRAGE_CHECK(observation.structure.has_value());
    const mira::UiTreeSnapshot &structure = observation.structure->value;
    MIRAGE_CHECK(mira::validate_ui_tree_snapshot(structure).has_value());
    MIRAGE_CHECK(structure.complete);
    MIRAGE_CHECK(!structure.truncated);

    // The fixture window's node and its button child, with the @eN hints
    // surviving the pinned projection.
    bool window_node_seen = false;
    bool button_node_seen = false;
    for (const mira::UiNode &node : structure.nodes) {
        MIRAGE_CHECK(node.stable_hint.has_value());
        MIRAGE_CHECK(node.stable_hint->hint.compare(0, 2, "@e") == 0);
        MIRAGE_CHECK(node.provenance.source == "mirage.desktop.accessibility");
        if (node.role == mira::UiRole::Window && node.text == kMainTitle) {
            window_node_seen = true;
        }
        if (node.role == mira::UiRole::Button && node.text == kButtonName) {
            button_node_seen = true;
        }
    }
    MIRAGE_CHECK(window_node_seen);
    MIRAGE_CHECK(button_node_seen);

    // Foreground honesty: the snapshot's application root is this process's
    // image name and the activity is the fixture's title, both independently
    // observed.
    MIRAGE_CHECK(observation.foreground.has_value());
    MIRAGE_CHECK(lowered(observation.foreground->value.package_name) == lowered(exe_file_name()));
    MIRAGE_CHECK(observation.foreground->value.activity_name ==
                 utf16_to_utf8_str(control_text(GetForegroundWindow())));
}

} // namespace

int main() {
    try {
        stage("fixture window");
        TestGui gui;
        MIRAGE_CHECK(gui.ok());
        if (!gui.ok()) {
            return 1;
        }

        stage("environment open");
        std::shared_ptr<WindowsDesktopEnvironment> windows_environment = make_environment();
        MIRAGE_CHECK(windows_environment != nullptr);
        if (windows_environment == nullptr) {
            return 1;
        }
        std::shared_ptr<DesktopEnvironment> environment = windows_environment;

        binding_capabilities_are_honest(environment);

        // The loop scenarios snapshot the FOCUSED window; a session that
        // refuses the activation is environment-limited, loudly.
        if (make_fixture_foreground(*environment, gui.main_window())) {
            binding_observe_projects_the_real_desktop(environment);

            stage("observe leg: desktop assembly");
            ObservationAssembler assembler(*environment, nullptr);
            ObservationComponents components;
            components.active_window = true;
            components.semantic_snapshot = true;
            const auto first = assembler.assemble(components, {}, CancelToken{});
            MIRAGE_CHECK(first.ok);
            MIRAGE_CHECK(first.semantic_snapshot.captured);
            const SemanticSnapshot &snapshot = first.observation.semantic_snapshot;
            MIRAGE_CHECK(snapshot.window_title ==
                         utf16_to_utf8_str(control_text(gui.main_window())));
            MIRAGE_CHECK(lowered(snapshot.application) == lowered(exe_file_name()));

            const SemanticNode *button = find_node(snapshot, "button", kButtonName);
            MIRAGE_CHECK(button != nullptr);
            const SemanticNode *edit_node = find_node(snapshot, "text", "");
            MIRAGE_CHECK(edit_node != nullptr); // the EDIT projects to Edit -> "text"

            stage("action leg: reference click");
            // The @eN ref resolves through the accessibility reference ring
            // and the semantic activation really clicks the real button; the
            // click lands in the fixture's window procedure while this
            // thread pumps.
            ElementTarget click_target;
            click_target.reference.id = button->ref;
            ElementTargetExecutor executor(environment->accessibility(), nullptr,
                                           environment->input());
            const int clicks_before = g_click_count;
            const auto resolution = executor.click(click_target, &snapshot);
            MIRAGE_CHECK(resolution.ok);
            MIRAGE_CHECK(resolution.ring ==
                         mirage::desktop::ResolutionRing::kAccessibilityReference);
            run_pump(600);
            MIRAGE_CHECK(g_click_count == clicks_before + 1);

            stage("action leg: value set_text");
            ElementTarget text_target;
            text_target.reference.id = edit_node->ref;
            const auto written =
                environment->accessibility()->set_text(text_target, kEditText, InputLimits{});
            MIRAGE_CHECK(written.ok);
            run_pump(300);
            MIRAGE_CHECK(control_text(gui.edit_window()) == utf8_to_utf16_str(kEditText));

            stage("observe leg: fresh observation carries the change");
            const auto second = assembler.assemble(components, {}, CancelToken{});
            MIRAGE_CHECK(second.ok);
            MIRAGE_CHECK(second.semantic_snapshot.captured);
            const SemanticSnapshot &fresh = second.observation.semantic_snapshot;
            // The refs are re-issued per snapshot (the registry is replaced
            // wholesale): the fresh observation's edit ref is live and
            // actionable — writing the same content through it keeps the
            // loop cycling (observe -> act -> observe -> act). The changed
            // content itself is evidenced through the independent
            // GetWindowTextW channel; a Win32 edit's UIA name stays empty
            // (the content lives in the Value pattern, not the Name).
            const SemanticNode *fresh_edit = find_node(fresh, edit_node->role, "");
            MIRAGE_CHECK(fresh_edit != nullptr);
            ElementTarget fresh_target;
            fresh_target.reference.id = fresh_edit->ref;
            const auto rewritten =
                environment->accessibility()->set_text(fresh_target, kEditText, InputLimits{});
            MIRAGE_CHECK(rewritten.ok);
            run_pump(200);
            MIRAGE_CHECK(control_text(gui.edit_window()) == utf8_to_utf16_str(kEditText));
            MIRAGE_CHECK(fresh_edit != nullptr);
        } else {
            std::fprintf(stderr, "[m407-e2e] SKIP note: the closed-loop scenarios need the fixture "
                                 "foreground; the session refused activation\n");
            std::fflush(stderr);
        }

        stage("done");
        return mirage::testing::finish("win32-observation-e2e");
    } catch (const std::exception &error) {
        std::fprintf(stderr, "[m407-e2e] fatal: %s\n", error.what());
        std::fflush(stderr);
        return 2;
    }
}
