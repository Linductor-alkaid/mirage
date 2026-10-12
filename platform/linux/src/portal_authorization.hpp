#pragma once
#include <atomic>
#include <functional>
#include <memory>
#include <mirage/desktop/screen_provider.hpp>
#include <mirage/platform/credential_store.hpp>
#include <mutex>

namespace mirage::platform::linux_backend {
// DEC-078: one token rotation at a time. No task queue or background owner.
class PortalAuthorization {
  public:
    struct Store {
        std::function<CredentialResult()> read;
        std::function<CredentialResult(const std::string &)> write;
    };
    explicit PortalAuthorization(Store store = {});
    void configure(bool remember);
    std::uint64_t generation() const { return generation_.load(); }
    bool enabled() const { return enabled_.load(); }
    desktop::DesktopAuthorizationView view();
    bool begin();
    void finish();
    desktop::ProviderError consume(std::string &token);
    desktop::ProviderError save(const std::string &token);
    desktop::ProviderError forget();
    static bool valid_token(const std::string &token);

  private:
    Store store_;
    std::atomic_bool enabled_{false};
    std::atomic_uint64_t generation_{1};
    std::mutex mutex_;
    bool busy_ = false;
};
} // namespace mirage::platform::linux_backend
