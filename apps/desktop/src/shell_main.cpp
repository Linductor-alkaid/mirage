#include "shell_main.hpp"

#include <cstdio>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <executor/executor.hpp>
#include <mirage/runtime/ipc/endpoint.hpp>

#include "bridge_core.hpp"
#include "desktop_app.hpp"
#include "desktop_client.hpp"
#include "desktop_renderer_app.hpp"
#include "shell_session.hpp"

namespace mirage::desktop_shell {

namespace ipc = mirage::runtime::ipc;
namespace {

/// A bare app for CEF subprocess types the shell has no renderer logic for
/// (GPU, utility, ...): only the shared scheme registration, nothing else.
class DesktopOtherApp : public CefApp {
  public:
    DesktopOtherApp() = default;
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override {
        registrar->AddCustomScheme(UiSchemeFactory::kScheme, CEF_SCHEME_OPTION_STANDARD |
                                                                 CEF_SCHEME_OPTION_SECURE |
                                                                 CEF_SCHEME_OPTION_CORS_ENABLED);
    }
    IMPLEMENT_REFCOUNTING(DesktopOtherApp);
    DISALLOW_COPY_AND_ASSIGN(DesktopOtherApp);
};

bool command_line_has_type(const CefRefPtr<CefCommandLine> &command_line, std::string &type) {
    if (!command_line->HasSwitch("type")) {
        return false;
    }
    type = command_line->GetSwitchValue("type").ToString();
    return true;
}

} // namespace

int run_shell(const CefMainArgs &args, void *sandbox_info, const ShellOptions &options) {
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
#if defined(_WIN32)
    command_line->InitFromString(::GetCommandLineW());
#else
    command_line->InitFromArgv(args.argc, args.argv);
#endif

    // Sub-process re-entry: renderers get the bridge plumbing, other helper
    // types only the shared scheme registration. No Executor here — the
    // unique executor owner of this process is the browser process below.
    std::string process_type;
    if (command_line_has_type(command_line, process_type)) {
        CefRefPtr<CefApp> app;
        if (process_type == "renderer") {
            app = new DesktopRendererApp();
        } else {
            app = new DesktopOtherApp();
        }
        return CefExecuteProcess(args, app, sandbox_info);
    }

    // --- browser process -----------------------------------------------------
    executor::Executor executor;
    executor::ExecutorConfig executor_config;
    executor_config.max_threads = 2;
    executor_config.enable_monitoring = true;
    if (!executor.initialize_ex(executor_config)) {
        std::fprintf(stderr, "[mirage-desktop] executor initialization failed\n");
        std::fflush(stderr);
        return 1;
    }

    ShellSession session(executor, options.socket_address, /*on_event=*/{},
                         /*on_lost=*/{});
    BridgeCore bridge([&session](const ipc::Request &body, std::chrono::milliseconds timeout) {
        return session.call(body, timeout);
    });

    CefRefPtr<DesktopClient> client = new DesktopClient(executor, bridge);
    session.set_forwarders(
        [client](const std::string &event_json) { client->forward_event(event_json); },
        [client](const std::string &diagnostic) { client->forward_connection_lost(diagnostic); });

    if (!session.ensure_connected()) {
        // Fail-visible, not fatal: the UI surfaces "unavailable" per request
        // and reconnects lazily when the service comes up.
        std::fprintf(stderr,
                     "[mirage-desktop] service not reachable at '%s' yet; the UI "
                     "will retry on demand\n",
                     options.socket_address.c_str());
        std::fflush(stderr);
    }

    CefRefPtr<DesktopApp> app = new DesktopApp(client, options.ui_root);
    CefSettings settings;
    // The sandbox boundary: bootstrap.exe provides the sandbox information on
    // Windows (CEF_USE_BOOTSTRAP build); a null sandbox_info means the host
    // environment runs the shell outside the sandboxed product form, which
    // CEF records through no_sandbox (M5-01 policy: sandbox on, any
    // deviation is a recorded decision, never a silent default).
    if (!CefInitialize(args, settings, app.get(), sandbox_info)) {
        std::fprintf(stderr, "[mirage-desktop] CEF initialization failed\n");
        std::fflush(stderr);
        session.shutdown();
        executor.shutdown(false);
        return 1;
    }

    // Single-threaded message loop on both platforms: the browser UI thread
    // is this thread, which the executor hops back into via CefPostTask.
    CefRunMessageLoop();

    // Teardown order (AGENTS.md rule 7): stop the task producers (the session
    // loop and the queries it serves), drain the executor pool so no task
    // holds CEF objects afterwards, then shut CEF down on its UI thread.
    session.shutdown();
    executor.shutdown(true);
    CefShutdown();
    return 0;
}

} // namespace mirage::desktop_shell
