#pragma once

#include <string>

#include "include/cef_request_handler.h"
#include "include/cef_scheme.h"

namespace mirage::desktop_shell {

/// Serves the built product UI (the ui/app vite output) under
/// mirage://app/<path>. The scheme is registered as standard + secure + CORS
/// enabled so the renderer treats it like an application origin (ES modules
/// load; the file:// module restrictions do not apply). Path mapping is
/// bounded: percent-decoded, '..' segments and absolute escapes rejected,
/// every resolution confined to the configured root.
class UiSchemeFactory : public CefSchemeHandlerFactory {
  public:
    static constexpr const char *kScheme = "mirage";
    static constexpr const char *kHost = "app";

    explicit UiSchemeFactory(std::string root) : root_(std::move(root)) {}

    CefRefPtr<CefResourceHandler> Create(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                         const CefString &scheme_name,
                                         CefRefPtr<CefRequest> request) override;

  private:
    std::string root_;
    IMPLEMENT_REFCOUNTING(UiSchemeFactory);
};

} // namespace mirage::desktop_shell
