#include "portal_authorization.hpp"
#include <algorithm>

namespace mirage::platform::linux_backend {
namespace {
// Reserved product credential reference, distinct from randomly generated model refs.
constexpr auto reference = "ed9cc9a138fd469a83b602ff3d45d078";
desktop::ProviderError store_error() {
    return {"authorization_store_failed", "无法访问桌面授权钥匙环，请解锁钥匙环后重新授权。"};
}
} // namespace
PortalAuthorization::PortalAuthorization(Store store) : store_(std::move(store)) {
    if (!store_.read)
        store_.read = [] { return read_credential(reference); };
    if (!store_.write)
        store_.write = [](const std::string &token) { return write_credential(reference, token); };
}
void PortalAuthorization::configure(bool remember) {
    if (enabled_.exchange(remember) != remember)
        generation_.fetch_add(1);
}
bool PortalAuthorization::begin() {
    std::lock_guard lock(mutex_);
    if (busy_)
        return false;
    busy_ = true;
    return true;
}
void PortalAuthorization::finish() {
    std::lock_guard lock(mutex_);
    busy_ = false;
}
bool PortalAuthorization::valid_token(const std::string &token) {
    return !token.empty() && token.size() <= 2048 &&
           std::all_of(token.begin(), token.end(),
                       [](unsigned char c) { return c >= 33 && c <= 126; });
}
desktop::ProviderError PortalAuthorization::consume(std::string &token) {
    const auto loaded = store_.read();
    if (loaded.absent)
        return {};
    if (!loaded.ok || !valid_token(loaded.value))
        return store_error();
    // Tokens are single-use. Remove before handing it to the Portal so a crash
    // or failed request cannot resurrect a consumed token on restart.
    if (!store_.write({}).ok)
        return store_error();
    token = loaded.value;
    return {};
}
desktop::ProviderError PortalAuthorization::save(const std::string &token) {
    if (!valid_token(token))
        return {"persistence_not_granted", "系统未保留桌面授权，请重新授权并允许记住选择。"};
    if (!enabled())
        return {"desktop_authorization_changed", "桌面授权已被取消。"};
    return store_.write(token).ok ? desktop::ProviderError{} : store_error();
}
desktop::ProviderError PortalAuthorization::forget() {
    configure(false);
    generation_.fetch_add(1);
    if (!begin())
        return {"desktop_authorization_busy", "授权操作正在取消，请稍后再点忘记授权。"};
    struct Finish {
        PortalAuthorization &s;
        ~Finish() { s.finish(); }
    } finish{*this};
    return store_.write({}).ok ? desktop::ProviderError{} : store_error();
}
desktop::DesktopAuthorizationView PortalAuthorization::view() {
    desktop::DesktopAuthorizationView out{true, enabled(), false, "ask", {}};
    if (!out.remember)
        return out;
    if (!begin()) {
        out.state = "authorizing";
        return out;
    }
    struct Finish {
        PortalAuthorization &s;
        ~Finish() { s.finish(); }
    } finish{*this};
    const auto loaded = store_.read();
    out.remembered = loaded.ok && valid_token(loaded.value);
    out.state = out.remembered ? "remembered" : "needs_authorization";
    if (!loaded.ok && !loaded.absent) {
        out.state = "store_unavailable";
        out.error = store_error();
    }
    return out;
}
} // namespace mirage::platform::linux_backend
