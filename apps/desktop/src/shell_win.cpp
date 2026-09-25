// Windows entry of the Mirage desktop shell (M5-02). With the CEF 152
// bootstrap model (USE_SANDBOX=ON default) this file compiles inside the
// mirage-desktop DLL: bootstrap.exe — copied next to it as
// mirage-desktop.exe — performs the sandbox setup and calls RunWinMain with
// the sandbox information, which flows into CefExecuteProcess/CefInitialize.
// The classic wWinMain path stays for a no-sandbox build of the same code.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

#include "include/cef_command_line.h"
#include "include/cef_sandbox_win.h"

#include <mirage/runtime/ipc/endpoint.hpp>

#include "shell_main.hpp"

namespace {

mirage::desktop_shell::ShellOptions options_from_command_line() {
    mirage::desktop_shell::ShellOptions options;
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
    command_line->InitFromString(::GetCommandLineW());
    if (command_line->HasSwitch("socket")) {
        options.socket_address = command_line->GetSwitchValue("socket").ToString();
    }
    if (command_line->HasSwitch("ui-root")) {
        options.ui_root = command_line->GetSwitchValue("ui-root").ToString();
    }
    if (options.socket_address.empty()) {
        options.socket_address = mirage::runtime::ipc::default_socket_path();
    }
    if (options.ui_root.empty()) {
        options.ui_root = MIRAGE_UI_APP_DIST;
    }
    return options;
}

int run_shell_windows(HINSTANCE instance, void *sandbox_info) {
    const mirage::desktop_shell::ShellOptions options = options_from_command_line();
    return mirage::desktop_shell::run_shell(CefMainArgs(instance), sandbox_info, options);
}

} // namespace

#if defined(CEF_USE_BOOTSTRAP)

// Entry point called by bootstrap.exe when built as a DLL (the sandboxed
// product form; sandbox_info carries bootstrap's sandbox setup).
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance, LPWSTR /*lpCmdLine*/, int /*nCmdShow*/,
                                    void *sandbox_info, cef_version_info_t * /*version_info*/) {
    return run_shell_windows(hInstance, sandbox_info);
}

#else // !defined(CEF_USE_BOOTSTRAP)

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE /*prev*/, PWSTR /*cmd_line*/, int /*cmd_show*/) {
    return run_shell_windows(hInstance, /*sandbox_info=*/nullptr);
}

#endif
