// M4-04 application backend test helper: a tiny stand-in application
// process. The test copies this binary under globally unique names per
// fixture application and points the fixture shortcuts' targets at the
// copies, so the backend's image-name matching sees exactly the processes
// this test created — never a system process that happens to share a name
// (the m205 helper discipline, Windows form).
//
// Usage: <copy> hold|ignore-close|exit [seconds] [ready-file]
//
// hold: creates a top-level (initially hidden) window and pumps messages;
//   WM_CLOSE ends the loop — the cooperative exit path the provider's
//   terminate drives. This is the SIGTERM-responding twin of the m205
//   helper: an application that honors its close request.
// ignore-close: same window, but WM_CLOSE is swallowed — the stuck twin,
//   so the cooperative termination wait runs into its deadline and the
//   instance survives, visible.
// exit: returns immediately (the self-exiting instance of the reap
//   scenario).
//
// With a ready-file path, the file is written AFTER the window exists, so
// a waiter that sees the file knows the instance's windows are enumerable
// (a terminate request before that point would post into the void and only
// burn its wait budget). Every mode bounds its lifetime with a safety
// timer, so stray copies can never outlive a crashed test by more than the
// given budget.
//
// Arguments are parsed from the wide command line, so the ready-file path
// survives non-ASCII user profiles.

#include <windows.h>

#include <shellapi.h>

namespace {

// Whether WM_CLOSE is swallowed (the stuck twin). One window per helper
// process; a plain flag keeps the procedure trivially readable.
bool g_ignore_close = false;

LRESULT CALLBACK window_procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CLOSE:
        // The cooperative exit path; the stuck mode ignores it, which is
        // exactly what the deadline_exceeded scenario needs.
        if (g_ignore_close) {
            return 0;
        }
        ::DestroyWindow(window);
        return 0;
    case WM_TIMER:
        // The safety net: even a stuck helper cannot outlive the bound.
        ::DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    default:
        return ::DefWindowProcW(window, message, wparam, lparam);
    }
}

void write_ready_file(const wchar_t *path) {
    if (path == nullptr || *path == L'\0') {
        return;
    }
    // CreateFileW instead of _wfopen: the ready file only needs to exist,
    // and MSVC raises the CRT's _wfopen as a warnings-as-errors deprecation
    // (C4996) that the MinGW cross gate never sees.
    const HANDLE ready = ::CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (ready != INVALID_HANDLE_VALUE) {
        ::CloseHandle(ready);
    }
}

} // namespace

int main() {
    int count = 0;
    LPWSTR *const args = ::CommandLineToArgvW(::GetCommandLineW(), &count);
    if (args == nullptr || count < 2) {
        return 2;
    }
    const wchar_t *mode = args[1];
    const int seconds = count >= 3 ? ::_wtoi(args[2]) : 3600;
    const wchar_t *ready_file = count >= 4 ? args[3] : nullptr;
    if (seconds <= 0) {
        return 0;
    }
    if (::lstrcmpW(mode, L"exit") == 0) {
        return 0;
    }
    if (::lstrcmpW(mode, L"ignore-close") == 0) {
        g_ignore_close = true;
    } else if (::lstrcmpW(mode, L"hold") != 0) {
        return 2;
    }

    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = window_procedure;
    window_class.hInstance = instance;
    window_class.lpszClassName = L"mirage-m404-helper";
    if (::RegisterClassW(&window_class) == 0) {
        return 2;
    }
    // A top-level window without WS_VISIBLE: the provider's window walk
    // must find hidden windows of the instance too, and a flashing console
    // side window on the CI desktop would be noise.
    const HWND window = ::CreateWindowExW(
        0, L"mirage-m404-helper", L"mirage-m404-helper", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, instance, nullptr);
    if (window == nullptr) {
        return 2;
    }
    // The window exists from here on; a terminate request can now reach it.
    write_ready_file(ready_file);
    ::SetTimer(window, 1, static_cast<UINT>(seconds) * 1000u, nullptr);

    MSG message;
    while (::GetMessageW(&message, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    return 0;
}
