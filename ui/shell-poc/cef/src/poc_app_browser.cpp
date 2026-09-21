// Mirage shell PoC：CEF 壳承载——浏览器进程实现（DEC-006 M3-06，非产品代码）。
#include "poc_app.h"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <utility>

#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/cef_response.h"
#include "include/cef_scheme.h"
#include "include/cef_task.h"
#include "include/cef_version.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#if defined(CEF_X11)
#include <X11/Xlib.h>
// Xlib 的 Success 宏与 CefMessageRouterBrowserSide::Callback::Success 冲突。
#undef Success
#endif

namespace mirage_shell_poc {
namespace {

[[nodiscard]] int64_t NowUs() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::microseconds>(now).count();
}

void EnsureParentDirectory(const std::string &path) {
    const auto slash = path.rfind('/');
    if (slash == std::string::npos) {
        return;
    }
    const std::string parent = path.substr(0, slash);
    // 结果目录层级固定（results/），逐级创建足矣。
    std::string partial;
    for (std::size_t i = 0; i <= parent.size(); ++i) {
        if (i == parent.size() || parent[i] == '/') {
            if (!partial.empty()) {
                mkdir(partial.c_str(), 0755);
            }
        }
        partial.push_back(parent[i]);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// PocResult
// ---------------------------------------------------------------------------

void PocResult::SetContext(std::string page, std::string url) {
    page_ = std::move(page);
    url_ = std::move(url);
}

void PocResult::MergeReport(const JsonValue &report) {
    if (!report.is_object()) {
        return;
    }
    const auto *kind = report.find("kind");
    if (kind && kind->is_string()) {
        if (*kind->as_string() == "meta") {
            meta_ = report;
        } else if (*kind->as_string() == "series") {
            if (const auto *label = report.find("label"); label && label->is_string()) {
                series_[*label->as_string()] = report;
            }
        } else if (*kind->as_string() == "samples") {
            const auto *label = report.find("label");
            const auto *offset = report.find("offset");
            const auto *values = report.find("values");
            if (label && label->is_string() && offset && offset->is_number() && values &&
                values->is_array()) {
                auto &target = samples_[*label->as_string()];
                std::size_t base = static_cast<std::size_t>(*offset->as_number());
                for (const auto &value : *values->as_array()) {
                    const double item = value.as_number().value_or(0.0);
                    if (base < target.size()) {
                        target[base] = item;
                    } else {
                        target.push_back(item);
                    }
                    ++base;
                }
            }
        } else if (*kind->as_string() == "ui-check") {
            ui_check_ = report;
        }
    }
    if (const auto *op = report.find("op"); op && op->is_string() && *op->as_string() == "done") {
        done_ = true;
    }
}

void PocResult::AddConsole(std::string entry) {
    if (console_.size() < kDiagnosticsLimit) {
        console_.push_back(std::move(entry));
    }
}

void PocResult::AddLoadError(std::string entry) {
    if (load_errors_.size() < kDiagnosticsLimit) {
        load_errors_.push_back(std::move(entry));
    }
}

JsonValue PocResult::ToJsonValue() const {
    JsonValue::Object series;
    for (const auto &[label, value] : series_) {
        series.push_back({label, value});
    }
    JsonValue::Object samples;
    for (const auto &[label, values] : samples_) {
        JsonValue::Array array;
        array.reserve(values.size());
        for (const double item : values) {
            array.push_back(item);
        }
        samples.push_back({label, JsonValue(std::move(array))});
    }

    JsonValue::Array console_array;
    for (const auto &entry : console_) {
        console_array.push_back(entry);
    }
    JsonValue::Array load_error_array;
    for (const auto &entry : load_errors_) {
        load_error_array.push_back(entry);
    }
    JsonValue::Object diagnostics;
    diagnostics.push_back({"console", JsonValue(std::move(console_array))});
    diagnostics.push_back({"load_errors", JsonValue(std::move(load_error_array))});

    JsonValue::Object embed;
    embed.push_back({"queried", embed_.queried});
    if (embed_.queried) {
        embed.push_back({"shell_window", static_cast<std::int64_t>(embed_.window)});
        embed.push_back({"map_state", static_cast<std::int64_t>(embed_.map_state)});
        embed.push_back({"width", static_cast<std::int64_t>(embed_.width)});
        embed.push_back({"height", static_cast<std::int64_t>(embed_.height)});
        JsonValue::Array children;
        for (const unsigned long child : embed_.children) {
            children.push_back(static_cast<std::int64_t>(child));
        }
        embed.push_back({"children", JsonValue(std::move(children))});
    }

    JsonValue::Object root;
    root.push_back({"shell", shell_});
    root.push_back({"page", page_});
    root.push_back({"url", url_});
    root.push_back({"cef_version", std::string(CEF_VERSION)});
    root.push_back({"meta", meta_});
    root.push_back({"series", JsonValue(std::move(series))});
    root.push_back({"samples", JsonValue(std::move(samples))});
    root.push_back({"ui_check", ui_check_});
    root.push_back({"diagnostics", JsonValue(std::move(diagnostics))});
    root.push_back({"window_embed", JsonValue(std::move(embed))});
    return JsonValue(std::move(root));
}

// ---------------------------------------------------------------------------
// PocRouteHandler
// ---------------------------------------------------------------------------

bool PocRouteHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                              CefRefPtr<CefFrame> /*frame*/,
                              int64_t /*query_id*/,
                              const CefString &request,
                              bool /*persistent*/,
                              CefRefPtr<Callback> callback) {
    const int64_t enter_us = NowUs();

    const std::string text = request.ToString();
    auto parsed = mira::parse_json(text);
    if (!parsed) {
        callback->Failure(1, "payload is not valid JSON");
        return true;
    }
    const JsonValue &value = parsed.value();
    const auto *op = value.find("op");
    if (!op || !op->is_string()) {
        callback->Failure(1, "missing op");
        return true;
    }
    const std::string &op_name = *op->as_string();
    if (op_name != "ping") {
        // ping 每样本 50 次，只打印非 ping 控制消息，避免 4 万行日志。
        fprintf(stderr, "[poc-query] %s (%zu bytes)\n", op_name.c_str(), text.size());
        fflush(stderr);
    }

    if (op_name == "ping") {
        const auto *index = value.find("i");
        const auto *size = value.find("size");
        JsonValue::Object pong;
        pong.push_back({"op", std::string("pong")});
        pong.push_back({"i", index ? index->as_integer().value_or(-1) : std::int64_t{-1}});
        pong.push_back({"size", size ? size->as_integer().value_or(0) : std::int64_t{0}});
        pong.push_back({"enter_us", enter_us});
        pong.push_back({"leave_us", NowUs()});
        callback->Success(mira::to_json_string(JsonValue(std::move(pong))));
        return true;
    }
    if (op_name == "report") {
        result_->MergeReport(value);
        callback->Success(mira::to_json_string(
            JsonValue(JsonValue::Object{{"op", std::string("report-ack")}})));
        return true;
    }
    if (op_name == "done") {
        result_->MergeReport(value);
        callback->Success(mira::to_json_string(
            JsonValue(JsonValue::Object{{"op", std::string("done-ack")}})));
        delegate_->OnHarnessDone();
        return true;
    }
    callback->Failure(2, "unknown op: " + op_name);
    return true;
}

// ---------------------------------------------------------------------------
// PocClient
// ---------------------------------------------------------------------------

PocClient::PocClient(PocResult *result, Delegate *delegate, bool ui_page)
    : result_(result), ui_page_(ui_page),
      route_handler_(std::make_unique<PocRouteHandler>(result_, delegate)) {}

void PocClient::CreateRouter() {
    CEF_REQUIRE_UI_THREAD();
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterBrowserSide::Create(config);
    message_router_->AddHandler(route_handler_.get(), false);
}

void PocClient::Launch(const std::string &url, unsigned long parent_window) {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window_info;
    // 应用自有 X11 窗口内的子窗口嵌入（窗口嵌入 PoC 目标本体）。
    window_info.SetAsChild(parent_window, CefRect(0, 0, 1280, 800));

    CefBrowserSettings browser_settings;
    CefBrowserHost::CreateBrowser(window_info, this, url, browser_settings, nullptr, nullptr);
}

bool PocClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefProcessId source_process,
                                         CefRefPtr<CefProcessMessage> message) {
    return message_router_ &&
           message_router_->OnProcessMessageReceived(browser, frame, source_process, message);
}

void PocClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_ = browser;
}

void PocClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    fprintf(stderr, "[poc] OnBeforeClose\n");
    fflush(stderr);
    if (message_router_) {
        message_router_->OnBeforeClose(browser);
    }
    if (browser_ && browser_ == browser) {
        browser_ = nullptr;
    }
    if (!browser_) {
        CefQuitMessageLoop();
    }
}

void PocClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                          TerminationStatus status,
                                          int error_code,
                                          const CefString &error_string) {
    fprintf(stderr, "[poc] render process terminated status=%d err=%d %s\n", status, error_code,
            error_string.ToString().c_str());
    fflush(stderr);
    result_->AddLoadError("render process terminated: status=" + std::to_string(status) +
                          " error_code=" + std::to_string(error_code) + " " +
                          error_string.ToString());
    if (!browser_ || browser_ == browser) {
        RequestClose();
    }
}

void PocClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          int /*httpStatusCode*/) {
    fprintf(stderr, "[poc] load end main=%d url=%s\n", frame->IsMain() ? 1 : 0,
            frame->GetURL().ToString().c_str());
    fflush(stderr);
    // 诊断：把桥可见性写入 window.title，经 OnTitleChange 回传 stderr
    // （仅 harness 页面；ui 页面的 title 本身是检查对象）。
    if (!ui_page_) {
        frame->ExecuteJavaScript(
            "document.title = 'BRIDGE=' + (typeof window.cefQuery) + '/' + "
            "(typeof window.mirageBridge);",
            frame->GetURL(), 0);
    }
    if (!ui_page_) {
        return;
    }
    // 产品 UI 页面就绪检查延迟 8s：给 React 挂载与 mock 域初始化留时间，之后经
    // window.cefQuery 回传 DOM 就绪证据并触发 done（协议与 harness 一致）。
    CefPostDelayedTask(TID_UI, base::BindOnce(&PocClient::InjectUiCheck, browser_), 8000);
}

void PocClient::InjectUiCheck(CefRefPtr<CefBrowser> browser) {
    if (!browser) {
        return;
    }
    CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (!frame) {
        return;
    }
    const char *snippet =
        "(function(){"
        "function send(payload){"
        "  try{window.cefQuery({request:JSON.stringify(payload),persistent:false,"
        "    onSuccess:function(){},onFailure:function(){}});}catch(e){}"
        "}"
        "var root=document.getElementById('app')||document.getElementById('root');"
        "send({op:'report',kind:'ui-check',ready:document.readyState,"
        "  title:document.title,rootChildren:root?root.children.length:-1,"
        "  bodyChildren:document.body?document.body.children.length:-1,"
        "  protocol:location.protocol,"
        "  assetScripts:document.querySelectorAll('script[src]').length,"
        "  stylesheets:document.querySelectorAll('link[rel=stylesheet]').length});"
        "send({op:'done'});"
        "})()";
    frame->ExecuteJavaScript(snippet, frame->GetURL(), 0);
}

void PocClient::OnLoadError(CefRefPtr<CefBrowser> /*browser*/,
                            CefRefPtr<CefFrame> frame,
                            ErrorCode errorCode,
                            const CefString &errorText,
                            const CefString &failedUrl) {
    if (errorCode == ERR_ABORTED) {
        return;
    }
    fprintf(stderr, "[poc] load error code=%d text=%s url=%s\n", errorCode,
            errorText.ToString().c_str(), failedUrl.ToString().c_str());
    fflush(stderr);
    result_->AddLoadError("load error code=" + std::to_string(errorCode) + " text=" +
                          errorText.ToString() + " url=" + failedUrl.ToString());
    if (frame->IsMain() && !ui_page_) {
        // 主资源加载失败时 harness 无法驱动 done，主动收敛避免挂死。
        RequestClose();
    }
}

bool PocClient::OnConsoleMessage(CefRefPtr<CefBrowser> /*browser*/,
                                 cef_log_severity_t level,
                                 const CefString &message,
                                 const CefString &source,
                                 int line) {
    fprintf(stderr, "[poc-console] level=%d %s (%s:%d)\n", level, message.ToString().c_str(),
            source.ToString().c_str(), line);
    fflush(stderr);
    result_->AddConsole("level=" + std::to_string(level) + " " + message.ToString() + " (" +
                        source.ToString() + ":" + std::to_string(line) + ")");
    return false;
}

void PocClient::OnTitleChange(CefRefPtr<CefBrowser> /*browser*/, const CefString &title) {
    fprintf(stderr, "[poc] title=%s\n", title.ToString().c_str());
    fflush(stderr);
}

void PocClient::RequestClose() {
    if (!browser_) {
        CefQuitMessageLoop();
        return;
    }
    CefPostTask(TID_UI,
                base::BindOnce(
                    [](CefRefPtr<PocClient> self) {
                        fprintf(stderr, "[poc] close task runs, browser=%d\n",
                                self->browser_ ? 1 : 0);
                        fflush(stderr);
                        if (self->browser_) {
                            self->browser_->GetHost()->CloseBrowser(true);
                        } else {
                            CefQuitMessageLoop();
                        }
                    },
                    CefRefPtr<PocClient>(this)));
    // 看门狗：关闭链路若被挂起的 renderer 任务拖住，3s 后强制退出消息循环，
    // 保证 PoC 进程总是干净返回（结果文件在此之前已写盘）。
    CefPostDelayedTask(TID_UI, CefCreateClosureTask(base::BindOnce(&CefQuitMessageLoop)), 3000);
}

// ---------------------------------------------------------------------------
// PocBrowserApp
// ---------------------------------------------------------------------------

PocBrowserApp::PocBrowserApp() = default;

void PocBrowserApp::OnBeforeCommandLineProcessing(const CefString &process_type,
                                                  CefRefPtr<CefCommandLine> command_line) {
    if (!process_type.ToString().empty()) {
        return;
    }
    // 壳 PoC 的窗口嵌入路径是 X11（SetAsChild 父窗口 + cef_get_xdisplay）；
    // Wayland 会话下 Chromium 默认自选 wayland ozone，与本路径不兼容，强制 x11
    // （XWayland 承载）。
    command_line->AppendSwitchWithValue("ozone-platform", "x11");
}

void PocBrowserApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();
    fprintf(stderr, "[poc] OnContextInitialized\n");
    fflush(stderr);

    CefRefPtr<CefCommandLine> command_line = CefCommandLine::GetGlobalCommandLine();
    url_ = command_line->GetSwitchValue("url");
    out_path_ = command_line->GetSwitchValue("out");
    ui_page_ = command_line->HasSwitch("page") &&
               command_line->GetSwitchValue("page") == "ui";
    if (url_.empty()) {
        url_ = "about:blank";
    } else if (url_.find("://") == std::string::npos && url_ != "about:blank") {
        // 本地资产以路径传入时归一为 file:// URL（DEC-006 PoC 的本地资产加载面）。
        url_ = "file://" + url_;
    }
    result_.SetContext(ui_page_ ? "ui" : "harness", url_);

    // 本地资产供给：http://mira.local/<path> → --asset-root 目录（file:// 的
    // 模块资产被 CORS 拦截，见 PocAssetHandlerFactory 注释）。
    const std::string asset_root = command_line->GetSwitchValue("asset-root");
    if (!asset_root.empty()) {
        CefRegisterSchemeHandlerFactory("http", "mira.local",
                                        new PocAssetHandlerFactory(asset_root));
    }

    OpenShellWindow();
    if (shell_window_ == 0) {
        result_.AddLoadError("failed to open shell window: x11 unavailable");
        CefPostTask(TID_UI,
                    CefCreateClosureTask(base::BindOnce(&CefQuitMessageLoop)));
        return;
    }

    client_ = new PocClient(&result_, /*delegate=*/this, ui_page_);
    client_->CreateRouter();
    fprintf(stderr, "[poc] launching browser shell_window=%lu url=%s\n", shell_window_,
            url_.c_str());
    fflush(stderr);
    client_->Launch(url_, shell_window_);
}

void PocBrowserApp::OpenShellWindow() {
#if defined(CEF_X11)
    Display *display = XOpenDisplay(nullptr);
    if (!display) {
        x11_available_ = false;
        return;
    }
    x11_display_ = display;
    x11_available_ = true;
    const int screen = DefaultScreen(display);
    Window window = XCreateSimpleWindow(display, RootWindow(display, screen), 64, 64, 1280, 800,
                                        0, BlackPixel(display, screen),
                                        WhitePixel(display, screen));
    XStoreName(display, window, "shell-poc-cef");
    XSelectInput(display, window, StructureNotifyMask);
    XMapWindow(display, window);
    XFlush(display);
    shell_window_ = static_cast<unsigned long>(window);
#else
    x11_available_ = false;
#endif
}

void PocBrowserApp::OnHarnessDone() {
    fprintf(stderr, "[poc] done: on_ui=%d\n", CefCurrentlyOn(TID_UI) ? 1 : 0);
    fflush(stderr);
    CollectEmbedInfo();
    WriteResultFile();
    if (client_) {
        client_->RequestClose();
    } else {
        CefQuitMessageLoop();
    }
}

void PocBrowserApp::CollectEmbedInfo() {
#if defined(CEF_X11)
    auto *display = static_cast<Display *>(x11_display_);
    if (!display || shell_window_ == 0) {
        return;
    }
    DisplayInfo info;
    info.window = shell_window_;
    XWindowAttributes attributes{};
    if (XGetWindowAttributes(display, static_cast<Window>(shell_window_), &attributes)) {
        info.map_state = static_cast<unsigned int>(attributes.map_state);
        info.width = static_cast<unsigned int>(attributes.width);
        info.height = static_cast<unsigned int>(attributes.height);
    }
    Window root_return = 0;
    Window parent_return = 0;
    Window *children = nullptr;
    unsigned int child_count = 0;
    if (XQueryTree(display, static_cast<Window>(shell_window_), &root_return, &parent_return,
                   &children, &child_count)) {
        for (unsigned int i = 0; i < child_count; ++i) {
            info.children.push_back(static_cast<unsigned long>(children[i]));
        }
        if (children) {
            XFree(children);
        }
        info.queried = true;
    }
    result_.SetEmbedInfo(std::move(info));
#endif
}

void PocBrowserApp::WriteResultFile() {
    if (result_written_ || out_path_.empty()) {
        return;
    }
    EnsureParentDirectory(out_path_);
    std::ofstream out(out_path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        return;
    }
    out << mira::to_json_string(result_.ToJsonValue());
    out.flush();
    result_written_ = out.good();
}

// ---------------------------------------------------------------------------
// PocAssetHandlerFactory
// ---------------------------------------------------------------------------

namespace {

std::string MimeTypeForPath(const std::string &path) {
    const auto dot = path.rfind('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }
    const std::string ext = path.substr(dot + 1);
    if (ext == "html") {
        return "text/html";
    }
    if (ext == "js" || ext == "mjs") {
        return "text/javascript";
    }
    if (ext == "css") {
        return "text/css";
    }
    if (ext == "json") {
        return "application/json";
    }
    if (ext == "svg") {
        return "image/svg+xml";
    }
    if (ext == "png") {
        return "image/png";
    }
    if (ext == "woff2") {
        return "font/woff2";
    }
    if (ext == "woff") {
        return "font/woff";
    }
    return "application/octet-stream";
}

// 同步全量读取实现（PoC 资产 < 1MB，无需流式/范围请求）。
class PocAssetResourceHandler : public CefResourceHandler {
  public:
    explicit PocAssetResourceHandler(std::string body) : body_(std::move(body)) {}

    bool Open(CefRefPtr<CefRequest> /*request*/,
              bool &handle_request,
              CefRefPtr<CefCallback> /*callback*/) override {
        handle_request = true;
        return true;
    }

    void GetResponseHeaders(CefRefPtr<CefResponse> response,
                            int64_t &response_length,
                            CefString &redirectUrl) override {
        response->SetStatus(200);
        response->SetMimeType(mime_);
        response->SetStatusText("OK");
        response_length = static_cast<int64_t>(body_.size());
    }

    bool Read(void *data_out,
              int bytes_to_read,
              int &bytes_read,
              CefRefPtr<CefResourceReadCallback> /*callback*/) override {
        const std::size_t remaining = body_.size() - offset_;
        bytes_read = static_cast<int>(std::min(remaining, static_cast<std::size_t>(bytes_to_read)));
        if (bytes_read > 0) {
            memcpy(data_out, body_.data() + offset_, static_cast<std::size_t>(bytes_read));
            offset_ += static_cast<std::size_t>(bytes_read);
        }
        return bytes_read > 0;
    }

    void SetMime(std::string mime) { mime_ = std::move(mime); }

    void Cancel() override {}

  private:
    std::string body_;
    std::string mime_;
    std::size_t offset_ = 0;

    IMPLEMENT_REFCOUNTING(PocAssetResourceHandler);
};

}  // namespace

CefRefPtr<CefResourceHandler> PocAssetHandlerFactory::Create(CefRefPtr<CefBrowser> /*browser*/,
                                                             CefRefPtr<CefFrame> /*frame*/,
                                                             const CefString & /*scheme_name*/,
                                                             CefRefPtr<CefRequest> request) {
    const std::string url = request->GetURL().ToString();
    const auto scheme_end = url.find("://");
    const auto path_start = url.find('/', scheme_end == std::string::npos ? 0 : scheme_end + 3);
    std::string path = path_start == std::string::npos ? std::string("/") : url.substr(path_start);
    if (path == "/" || path.empty()) {
        path = "/index.html";
    }
    // 防目录穿越：拒绝任何 ".." 段。
    if (path.find("..") != std::string::npos) {
        return nullptr;
    }
    const std::string file_path = asset_root_ + path;
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        return nullptr;
    }
    std::string body((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    CefRefPtr<PocAssetResourceHandler> handler =
        new PocAssetResourceHandler(std::move(body));
    handler->SetMime(MimeTypeForPath(path));
    return handler;
}

void PocBrowserApp::CloseX11Display() {
#if defined(CEF_X11)
    if (x11_display_) {
        XCloseDisplay(static_cast<Display *>(x11_display_));
        x11_display_ = nullptr;
    }
#endif
}

}  // namespace mirage_shell_poc
