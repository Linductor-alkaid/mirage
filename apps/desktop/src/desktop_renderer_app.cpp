#include "desktop_renderer_app.hpp"

#include <string>

#include "include/cef_frame.h"
#include "include/cef_scheme.h"
#include "include/cef_v8.h"

#include "ui_scheme_handler.hpp"

namespace mirage::desktop_shell {

void DesktopRendererApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
    // Same registration as the browser process (the renderer must agree on
    // the scheme's security semantics).
    registrar->AddCustomScheme(UiSchemeFactory::kScheme, CEF_SCHEME_OPTION_STANDARD |
                                                             CEF_SCHEME_OPTION_SECURE |
                                                             CEF_SCHEME_OPTION_CORS_ENABLED);
}

void DesktopRendererApp::OnWebKitInitialized() {
    std::fprintf(stderr, "[mirage-desktop] renderer: webkit initialized\n");
    std::fflush(stderr);
    CefMessageRouterConfig config;
    config.js_query_function = "mirageQuery";
    config.js_cancel_function = "mirageQueryCancel";
    router_ = CefMessageRouterRendererSide::Create(config);
}

void DesktopRendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                          CefRefPtr<CefV8Context> context) {
    std::fprintf(stderr, "[mirage-desktop] renderer: context created\n");
    std::fflush(stderr);
    // Installs the `mirageQuery` JS function for this context — the renderer
    // side of the controlled bridge surface.
    router_->OnContextCreated(browser, frame, context);
}

void DesktopRendererApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                           CefRefPtr<CefV8Context> context) {
    // Drops the context's pending queries.
    router_->OnContextReleased(browser, frame, context);
}

bool DesktopRendererApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                                  CefRefPtr<CefFrame> frame,
                                                  CefProcessId source_process,
                                                  CefRefPtr<CefProcessMessage> message) {
    const std::string name = message->GetName().ToString();
    if (name == kEventMessage || name == kLostMessage) {
        const CefRefPtr<CefListValue> arguments = message->GetArgumentList();
        if (name == kLostMessage) {
            // The transport's connection-lost hook takes no payload.
            frame->ExecuteJavaScript(
                "window.__mirageConnectionLost && window.__mirageConnectionLost();",
                frame->GetURL(), 0);
            return true;
        }
        if (arguments->GetSize() < 1) {
            return true;
        }
        // One encoded protocol-v1 event envelope. The envelope is produced
        // by the pinned-side codec (canonical JSON, a valid JS object
        // literal), so it embeds directly; the hook is the transport's
        // registered callback, absent hooks drop the event.
        const std::string event_json = arguments->GetString(0).ToString();
        frame->ExecuteJavaScript("window.__mirageOnEvent && window.__mirageOnEvent(" + event_json +
                                     ");",
                                 frame->GetURL(), 0);
        return true;
    }
    return router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

} // namespace mirage::desktop_shell
