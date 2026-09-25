#pragma once

#include <string>

#include "include/cef_app.h"

#include "desktop_client.hpp"
#include "ui_scheme_handler.hpp"

namespace mirage::desktop_shell {

/// The browser-process CefApp (M5-02): registers the mirage://app scheme in
/// every process, registers the asset factory and creates the product window
/// on context initialization. Per the M5-01 formal policy (DEC-006) the GPU
/// stays enabled and no disable switches are injected; the sandbox is owned
/// by bootstrap.exe on Windows and by the chrome-sandbox helper at packaging
/// time (M5-11) on Linux.
class DesktopApp : public CefApp, public CefBrowserProcessHandler {
  public:
    DesktopApp() = default;

    DesktopApp(CefRefPtr<DesktopClient> client, std::string ui_root)
        : client_(std::move(client)), ui_root_(std::move(ui_root)) {}

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override { return this; }
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;
    void OnBeforeCommandLineProcessing(const CefString &process_type,
                                       CefRefPtr<CefCommandLine> command_line) override;

    // CefBrowserProcessHandler
    void OnContextInitialized() override;

    static constexpr const char *kIndexUrl = "mirage://app/index.html?transport=desktop";
    static constexpr int kWindowWidth = 1280;
    static constexpr int kWindowHeight = 800;

  private:
    CefRefPtr<DesktopClient> client_;
    std::string ui_root_;
#ifdef __linux__
    void *x11_display_ = nullptr; ///< the shell owns the display it mapped
#endif

    IMPLEMENT_REFCOUNTING(DesktopApp);
    DISALLOW_COPY_AND_ASSIGN(DesktopApp);
};

} // namespace mirage::desktop_shell
