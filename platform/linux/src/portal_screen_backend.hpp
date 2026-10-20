#pragma once

#include <memory>
#include <mutex>
#include <mirage/desktop/screen_provider.hpp>

namespace mirage::platform {
struct PresentationContext;
}
namespace mirage::platform::linux_backend {

/// DEC-073: finite synchronous Portal request, driven by the caller's Executor
/// task. Task-owned sharing; DEC-077 input maps frame pixels inside one shared stream.
class PortalAuthorization;
struct PortalPointerState;
class PortalScreenBackend final : public desktop::ScreenProvider {
  public:
    explicit PortalScreenBackend(std::shared_ptr<PortalAuthorization> authorization = {},
                                 std::shared_ptr<PresentationContext> presentation = {});
    ~PortalScreenBackend() override;
    static std::unique_ptr<PortalScreenBackend>
    open(std::shared_ptr<PresentationContext> presentation = {});
    bool desktop_observation_only() const override { return true; }
    std::unique_ptr<desktop::DesktopCaptureSession> create_desktop_capture_session() override;
    bool desktop_interaction_supported() const override;
    void configure_desktop_authorization(bool remember) override;
    desktop::DesktopAuthorizationView desktop_authorization(const desktop::CancelToken &) override;
    desktop::ProviderError manage_desktop_authorization(bool,
                                                        const desktop::CancelToken &) override;
    std::unique_ptr<desktop::DesktopCaptureSession> create_desktop_interaction_session() override;
    desktop::CaptureOutcome capture_desktop(const desktop::CaptureLimits &,
                                            const desktop::CancelToken &) override;
    desktop::DisplayListOutcome list_displays(const desktop::DisplayListLimits &,
                                              const desktop::CancelToken &) override;
    desktop::CaptureOutcome capture_display(const std::string &, const desktop::CaptureLimits &,
                                            const desktop::CancelToken &) override;
    desktop::CaptureOutcome capture_window(const std::string &, const desktop::CaptureLimits &,
                                           const desktop::CancelToken &) override;
    desktop::CaptureOutcome capture_roi(const desktop::WindowGeometry &,
                                        const desktop::CaptureLimits &,
                                        const desktop::CancelToken &) override;

  private:
    std::shared_ptr<PortalAuthorization> authorization_;
    std::shared_ptr<PresentationContext> presentation_;
    std::shared_ptr<PortalPointerState> pointer_state_;
};

/// DEC-110 (M7-26): the task's last dispatched pointer position, owned by the
/// backend (process lifetime) instead of a CastSession, because ordinary
/// captures and input failures recreate the cast freely — the glide origin
/// must survive that churn. One desktop, one mutation writer at a time (the
/// ActionCoordinator lease); the mutex only guards cross-session reads.
struct PortalPointerState {
    std::mutex mutex;
    bool known = false;
    double x = 0, y = 0;
};
} // namespace mirage::platform::linux_backend
