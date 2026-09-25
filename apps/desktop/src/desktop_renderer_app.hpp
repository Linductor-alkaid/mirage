#pragma once

#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"

namespace mirage::desktop_shell {

/// The renderer-process CefApp (M5-02): installs the renderer-side message
/// router (the controlled bridge's JS surface `mirageQuery`) and dispatches
/// the shell's browser → renderer push messages (service events, connection
/// loss) onto the JS hooks the desktop transport registers. No UI logic and
/// no C++ product logic lives in the renderer beyond this plumbing.
class DesktopRendererApp : public CefApp, public CefRenderProcessHandler {
  public:
    DesktopRendererApp() = default;

    static constexpr const char *kEventMessage = "mirage:desktop-event";
    static constexpr const char *kLostMessage = "mirage:desktop-connection-lost";

    // CefApp
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override { return this; }
    void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;

    // CefRenderProcessHandler
    void OnWebKitInitialized() override;
    void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override;
    void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

  private:
    CefRefPtr<CefMessageRouterRendererSide> router_;

    IMPLEMENT_REFCOUNTING(DesktopRendererApp);
    DISALLOW_COPY_AND_ASSIGN(DesktopRendererApp);
};

} // namespace mirage::desktop_shell
