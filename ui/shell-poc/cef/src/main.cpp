// Mirage shell PoC：CEF 壳承载——入口（DEC-006 M3-06，非产品代码）。
// 进程形态仿 cefclient：同一可执行文件承载全部 CEF 子进程，main() 经
// CreateCommandLine + InitFromArgv 解析进程类型并分派 App（renderer / zygote
// 子进程需要渲染侧路由，window.cefQuery 才存在；其余子进程用裸 App）。
#include <cstring>
#include <string>

#include "poc_app.h"

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/wrapper/cef_helpers.h"
#if defined(CEF_X11)
#include <X11/Xlib.h>
// Xlib 的 Success 宏不参与本文件，但保持与子模块一致的包含位置；冲突处理
// 仅在用到 Callback::Success 的翻译单元完成。
#endif
#include <unistd.h>

#include <array>

namespace {

// 其余子进程（gpu / utility 等）：无任何 handler 的裸 App。
class PocOtherApp : public CefApp {
  public:
    PocOtherApp() = default;
    IMPLEMENT_REFCOUNTING(PocOtherApp);
};

CefRefPtr<CefApp> MakeApp(const CefRefPtr<CefCommandLine> &command_line) {
    const std::string type = command_line->GetSwitchValue("type");
    if (type.empty()) {
        return new mirage_shell_poc::PocBrowserApp();
    }
    if (type == "renderer" || type == "zygote") {
        // Linux zygote 孵化的子进程类型未定，按 cefclient 惯例交给 renderer app。
        return new mirage_shell_poc::PocRendererApp();
    }
    return new PocOtherApp();
}

#if defined(CEF_X11)
int XErrorHandlerImpl(Display * /*display*/, XErrorEvent *event) {
    LOG(WARNING) << "X error: type=" << event->type << " serial=" << event->serial
                 << " error_code=" << static_cast<int>(event->error_code)
                 << " request_code=" << static_cast<int>(event->request_code);
    return 0;
}

int XIOErrorHandlerImpl(Display * /*display*/) {
    return 0;
}
#endif

}  // namespace

int main(int argc, char *argv[]) {
    CefMainArgs main_args(argc, argv);

    // cefclient 惯例：显式 CreateCommandLine + InitFromArgv（全局命令行单例
    // 在 CefExecuteProcess 之前不可用）。
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
    command_line->InitFromArgv(argc, argv);
    CefRefPtr<CefApp> app = MakeApp(command_line);

    // 子进程（renderer / zygote / gpu / utility）经此执行对应逻辑并返回。
    const int exit_code = CefExecuteProcess(main_args, app, nullptr);
    if (exit_code >= 0) {
        return exit_code;
    }

#if defined(CEF_X11)
    XSetErrorHandler(XErrorHandlerImpl);
    XSetIOErrorHandler(XIOErrorHandlerImpl);
#endif

    CefSettings settings;
    // PoC 关闭 CEF 沙箱（chrome-sandbox setuid 未配置）；产品化阶段按 M5 打包
    // 形态重新评估沙箱与授权，本 PoC 不据此声明安全属性。
    settings.no_sandbox = true;
    settings.log_severity = LOGSEVERITY_INFO;
    // 隔离 profile 目录（须绝对路径），避免默认缓存路径的进程单例行为。
    {
        std::array<char, 4096> exe{};
        const ssize_t len = readlink("/proc/self/exe", exe.data(), exe.size() - 1);
        if (len > 0) {
            exe.data()[len] = '\0';
            std::string exe_dir(exe.data());
            const auto slash = exe_dir.rfind('/');
            std::string profile = slash == std::string::npos
                                      ? std::string("poc-cef-profile")
                                      : exe_dir.substr(0, slash) + "/poc-cef-profile";
            CefString(&settings.root_cache_path).FromString(profile);
        }
    }

    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
        return CefGetExitCode();
    }

    CefRunMessageLoop();
    fprintf(stderr, "[poc] loop quit, shutting down\n");
    fflush(stderr);
    CefShutdown();
    fprintf(stderr, "[poc] shutdown done\n");
    fflush(stderr);

    auto *browser_app = static_cast<mirage_shell_poc::PocBrowserApp *>(app.get());
    browser_app->CloseX11Display();
    return 0;
}
