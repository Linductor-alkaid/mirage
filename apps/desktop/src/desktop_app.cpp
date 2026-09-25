#include "desktop_app.hpp"

#include <cstdio>

#include "include/cef_browser.h"
#include "include/wrapper/cef_helpers.h"

#ifdef __linux__
#include <X11/Xlib.h>

#include "include/cef_types.h"
#endif

namespace mirage::desktop_shell {

void DesktopApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
    // Standard + secure + CORS: the UI is an application origin, ES modules
    // load normally and no file:// restrictions apply. Both processes agree
    // on this registration (the renderer app returns the same set).
    registrar->AddCustomScheme(UiSchemeFactory::kScheme, CEF_SCHEME_OPTION_STANDARD |
                                                             CEF_SCHEME_OPTION_SECURE |
                                                             CEF_SCHEME_OPTION_CORS_ENABLED);
}

void DesktopApp::OnBeforeCommandLineProcessing(const CefString & /*process_type*/,
                                               CefRefPtr<CefCommandLine> /*command_line*/) {
    // M5-01 formal policy: GPU enabled, sandbox enabled — nothing to inject.
    // Deliberately empty; recorded in DEC-019 so the absence is explicit.
}

void DesktopApp::OnContextInitialized() {
    CEF_REQUIRE_UI_THREAD();

    std::fflush(stderr);

    // The router asserts UI-thread affinity, so it is created here and not
    // in the client's (main-thread) constructor.
    client_->init_router();

    CefRegisterSchemeHandlerFactory(UiSchemeFactory::kScheme, UiSchemeFactory::kHost,
                                    new UiSchemeFactory(ui_root_));

    CefWindowInfo window_info;
    CefBrowserSettings settings;
#ifdef _WIN32
    window_info.SetAsPopup(/*parent=*/nullptr, "Mirage");
#elif defined(__linux__)

    std::fflush(stderr);
    Display *display = XOpenDisplay(nullptr);
    if (display == nullptr) {
        std::fprintf(stderr, "[mirage-desktop] cannot open the X display\n");
        std::fflush(stderr);
        CefQuitMessageLoop();
        return;
    }
    x11_display_ = display;
    Window window = XCreateSimpleWindow(
        display, XDefaultRootWindow(display), /*x=*/0, /*y=*/0, static_cast<unsigned>(kWindowWidth),
        static_cast<unsigned>(kWindowHeight),
        /*border_width=*/1, XBlackPixel(display, XDefaultScreen(display)),
        XWhitePixel(display, XDefaultScreen(display)));
    XMapWindow(display, window);
    XFlush(display);
    window_info.SetAsChild(window, CefRect(0, 0, kWindowWidth, kWindowHeight));
#else
#error "The desktop shell has no window path for this platform yet"
#endif

    CefBrowserHost::CreateBrowser(window_info, client_.get(), kIndexUrl, settings,
                                  /*request_context=*/nullptr, /*extra_info=*/nullptr);

    std::fflush(stderr);
}

} // namespace mirage::desktop_shell
