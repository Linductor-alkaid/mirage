// Mirage shell PoC：CEF 壳承载（DEC-006 M3-06，非产品代码）。
//
// 进程模型与 cefsimple 一致：同一可执行文件承载 browser / render / gpu 等进程，
// main() 按进程类型选择 App。浏览器进程创建自有 X11 窗口并把浏览器作为子窗口嵌入
// （CefWindowInfo::SetAsChild，DEC-006 决策 1 的"应用自有窗口 + 嵌入式渲染引擎"）；
// JS ↔ 原生桥使用 CEF message router（window.cefQuery，桥协议与 Electron main.js、
// harness/bridge-latency.html 三方一致：ping / report / done）。
//
// 命令行（浏览器进程）：
//   poc_cef --url=<本地资产 index.html 或 harness 页面> --out=<结果 JSON 路径>
//           [--page=harness|ui]
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "include/cef_app.h"
#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"
#include "mira/json.hpp"

namespace mirage_shell_poc {

using mira::JsonValue;

// 渲染进程 App：为每个 V8 context 注入 window.cefQuery / cefQueryCancel，
// 共享 harness 页面与 ui-check 注入片段经此直达浏览器进程。
class PocRendererApp : public CefApp, public CefRenderProcessHandler {
  public:
    PocRendererApp();

    PocRendererApp(const PocRendererApp &) = delete;
    PocRendererApp &operator=(const PocRendererApp &) = delete;

    // CefApp
    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return this;
    }

    // CefRenderProcessHandler
    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override;
    void OnContextReleased(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override;
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

  private:
    CefRefPtr<CefMessageRouterRendererSide> message_router_;

    IMPLEMENT_REFCOUNTING(PocRendererApp);
};

struct DisplayInfo {
    unsigned long window = 0;
    unsigned int map_state = 0;
    unsigned int width = 0;
    unsigned int height = 0;
    std::vector<unsigned long> children;
    bool queried = false;
};

// 结果模型：与 ui/shell-poc/electron/main.js 的 result 结构同 schema，
// 由 harness 页面的 report 消息逐步填充，done 时序列化到 --out。
class PocResult {
  public:
    void MergeReport(const JsonValue &report);
    void SetUiCheck(JsonValue check) { ui_check_ = std::move(check); }
    void AddConsole(std::string entry);
    void AddLoadError(std::string entry);
    void SetEmbedInfo(DisplayInfo info) { embed_ = info; }
    void SetContext(std::string page, std::string url);

    [[nodiscard]] JsonValue ToJsonValue() const;
    [[nodiscard]] bool HarnessDone() const { return done_; }
    [[nodiscard]] bool HasUiCheck() const { return ui_check_.is_object(); }

  private:
    std::string shell_ = "cef";
    std::string page_;
    std::string url_;
    JsonValue meta_;
    std::map<std::string, JsonValue> series_;
    std::map<std::string, std::vector<double>> samples_;
    JsonValue ui_check_;
    std::vector<std::string> console_;
    std::vector<std::string> load_errors_;
    DisplayInfo embed_;
    bool done_ = false;

    static constexpr std::size_t kDiagnosticsLimit = 50;
};

// 浏览器进程桥查询 handler：ping 原生回声打点，report 填充结果，done 收敛退出。
class PocRouteHandler : public CefMessageRouterBrowserSide::Handler {
  public:
    class Delegate {
        public:
            virtual void OnHarnessDone() = 0;
            virtual ~Delegate() = default;
    };

    PocRouteHandler(PocResult *result, Delegate *delegate)
        : result_(result), delegate_(delegate) {}

    bool OnQuery(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int64_t query_id,
                 const CefString &request,
                 bool persistent,
                 CefRefPtr<Callback> callback) override;

  private:
    PocResult *result_;
    Delegate *delegate_;
};

// 浏览器进程 client：路由接线 + 生命周期（关闭收敛）+ 加载/控制台诊断 +
// --page=ui 的 DOM 就绪注入检查。
class PocClient
    : public CefClient,
      public CefLifeSpanHandler,
      public CefRequestHandler,
      public CefLoadHandler,
      public CefDisplayHandler {
  public:
    using Delegate = PocRouteHandler::Delegate;

    PocClient(PocResult *result, Delegate *delegate, bool ui_page);

    PocClient(const PocClient &) = delete;
    PocClient &operator=(const PocClient &) = delete;
    void CreateRouter();
    void Launch(const std::string &url, unsigned long parent_window);
    void RequestClose();

    // CefClient
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override;

    // CefLifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    // CefRequestHandler
    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                   TerminationStatus status,
                                   int error_code,
                                   const CefString &error_string) override;

    // CefLoadHandler
    void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser,
                     CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode,
                     const CefString &errorText,
                     const CefString &failedUrl) override;

    // CefDisplayHandler
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString &message,
                          const CefString &source,
                          int line) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString &title) override;

  private:
    static void InjectUiCheck(CefRefPtr<CefBrowser> browser);

    PocResult *result_;
    bool ui_page_ = false;
    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<CefMessageRouterBrowserSide> message_router_;
    // Handler 无引用计数（CefMessageRouterBrowserSide::Handler 为普通多态基类），
    // 以 client 的值成员持有，生命周期随 client。
    std::unique_ptr<PocRouteHandler> route_handler_;

    IMPLEMENT_REFCOUNTING(PocClient);
};

// 本地资产 scheme handler：http://mira.local/<path> → <asset-root>/<path>。
// Vite 构建的 ES module 资产在 CEF 的 file:// 下被 CORS 拦截（crossorigin 模块
// 请求 origin 'null'），本地资产加载需经 scheme handler 以 http 语义供给——
// 这是 M3-06 冻结结论的关键集成成本证据之一。
class PocAssetHandlerFactory : public CefSchemeHandlerFactory {
  public:
    explicit PocAssetHandlerFactory(std::string asset_root)
        : asset_root_(std::move(asset_root)) {}

    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         const CefString &scheme_name,
                                         CefRefPtr<CefRequest> request) override;

  private:
    std::string asset_root_;

    IMPLEMENT_REFCOUNTING(PocAssetHandlerFactory);
};

// 浏览器进程 App：OnContextInitialized 创建 X11 原生窗口并嵌入浏览器。
class PocBrowserApp : public CefApp,
                      public CefBrowserProcessHandler,
                      public PocRouteHandler::Delegate {
  public:
    PocBrowserApp();

    PocBrowserApp(const PocBrowserApp &) = delete;
    PocBrowserApp &operator=(const PocBrowserApp &) = delete;

    // CefApp
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }
    void OnBeforeCommandLineProcessing(
        const CefString &process_type,
        CefRefPtr<CefCommandLine> command_line) override;

    // CefBrowserProcessHandler
    void OnContextInitialized() override;

    // PocRouteHandler::Delegate
    void OnHarnessDone() override;

    [[nodiscard]] bool IsX11Available() const { return x11_available_; }
    [[nodiscard]] unsigned long shell_window() const { return shell_window_; }
    void CloseX11Display();

  private:
    void OpenShellWindow();
    void CollectEmbedInfo();
    void WriteResultFile();

    PocResult result_;
    CefRefPtr<PocClient> client_;
    std::string url_;
    std::string out_path_;
    bool ui_page_ = false;
    bool x11_available_ = false;
    bool result_written_ = false;
    unsigned long shell_window_ = 0;
    void *x11_display_ = nullptr;  // Display*，避免在公共头引入 Xlib

    IMPLEMENT_REFCOUNTING(PocBrowserApp);
};

}  // namespace mirage_shell_poc
