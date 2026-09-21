// Mirage shell PoC：CEF 壳承载——渲染进程实现（DEC-006 M3-06，非产品代码）。
// message router 渲染侧：为每个 V8 context 注入 window.cefQuery / cefQueryCancel，
// 使共享 harness 页面与注入的 ui-check 片段可直达浏览器进程。
#include "poc_app.h"

#include "include/wrapper/cef_helpers.h"

namespace mirage_shell_poc {

PocRendererApp::PocRendererApp() = default;

void PocRendererApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefRefPtr<CefV8Context> context) {
    CEF_REQUIRE_RENDERER_THREAD();
    fprintf(stderr, "[poc-renderer] context created\n");
    fflush(stderr);
    if (!message_router_) {
        CefMessageRouterConfig config;
        message_router_ = CefMessageRouterRendererSide::Create(config);
    }
    message_router_->OnContextCreated(browser, frame, context);
}

void PocRendererApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                       CefRefPtr<CefFrame> frame,
                                       CefRefPtr<CefV8Context> context) {
    CEF_REQUIRE_RENDERER_THREAD();
    if (message_router_) {
        message_router_->OnContextReleased(browser, frame, context);
    }
}

bool PocRendererApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                              CefRefPtr<CefFrame> frame,
                                              CefProcessId source_process,
                                              CefRefPtr<CefProcessMessage> message) {
    return message_router_ &&
           message_router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

}  // namespace mirage_shell_poc
