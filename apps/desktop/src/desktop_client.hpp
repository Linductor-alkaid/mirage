#pragma once

#include <string>
#include <vector>

#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"

#include "bridge_core.hpp"

namespace executor {
class Executor;
}

namespace mirage::desktop_shell {

/// The browser-process CefClient (M5-02): owns the browser-side message
/// router (the controlled renderer bridge), the browser set for event
/// fan-out, and the lifespan discipline — the last window closing quits the
/// message loop. Thread surfaces:
/// - router callbacks and browser-set access run on the CEF UI thread;
/// - forward_event / forward_connection_lost are callable from any thread
///   and hop to the CEF UI thread internally.
class DesktopClient : public CefClient, public CefLifeSpanHandler {
  public:
    DesktopClient(executor::Executor &executor, BridgeCore &bridge);
    ~DesktopClient() override;

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    /// Forwards one encoded service event to every live browser's main
    /// frame (renderer process message → the JS event hook).
    void forward_event(const std::string &event_json);

    /// Notifies the renderer that the Local IPC session was lost (the
    /// transport surfaces it as a connection loss; the UI decides on
    /// reconnection and resync).
    void forward_connection_lost(const std::string &diagnostic);

    /// Creates the browser-side message router. Must run on the CEF UI
    /// thread (the router asserts it) — DesktopApp::OnContextInitialized
    /// calls it before the first browser is created.
    void init_router();

  private:
    class BridgeHandler;
    friend class BridgeHandler;

    /// Posts one callable to the CEF UI thread; the task classes live in the
    /// cpp (refcounted CefTask, no base::Bind dependency).
    void post_to_ui(std::function<void()> work);

    CefRefPtr<CefMessageRouterBrowserSide> router_;
    std::unique_ptr<BridgeHandler> bridge_handler_;
    std::vector<CefRefPtr<CefBrowser>> browsers_; // CEF UI thread only
    executor::Executor &executor_;
    BridgeCore &bridge_;

    IMPLEMENT_REFCOUNTING(DesktopClient);
};

} // namespace mirage::desktop_shell
