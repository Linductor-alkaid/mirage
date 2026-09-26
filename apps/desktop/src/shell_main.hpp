#pragma once

#include <string>

#include "include/cef_app.h"

namespace mirage::desktop_shell {

/// The shell's launch options (parsed by the platform entry files — argv on
/// POSIX, the Windows command line inside the bootstrap DLL entry).
struct ShellOptions {
    /// Local IPC endpoint of mirage-service (DEC-007). Empty selects the
    /// platform default (default_socket_path()).
    std::string socket_address;
    /// Root of the built UI assets the shell serves under mirage://app/.
    /// Empty selects MIRAGE_UI_APP_DIST (the configure-time build output).
    std::string ui_root;
};

/// Shared shell entry (M5-02): subprocess re-entry first (renderer/GPU
/// processes carry no Executor and no session), then the browser-process
/// lifecycle — executor and session start before CEF, teardown in reverse
/// (session workers stop, the executor drains, then CefShutdown).
///
/// `sandbox_info` is the Windows sandbox object handed over by
/// bootstrap.exe (null on POSIX; a null on Windows means no sandbox, which
/// the shell records through CEF's no_sandbox setting — the M5-01 policy is
/// sandbox-on, and bootstrap.exe supplies it in the product form).
int run_shell(const CefMainArgs &args, void *sandbox_info, const ShellOptions &options);

} // namespace mirage::desktop_shell
