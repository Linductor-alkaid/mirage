#pragma once

#include <memory>
#include <mirage/desktop/screen_provider.hpp>

namespace mirage::platform::linux_backend {

/// DEC-073: finite synchronous Portal request, driven by the caller's Executor
/// task. Task-owned sharing; DEC-077 input maps frame pixels inside one shared stream.
class PortalAuthorization;
class PortalScreenBackend final : public desktop::ScreenProvider {
  public:
    explicit PortalScreenBackend(std::shared_ptr<PortalAuthorization> authorization = {});
    ~PortalScreenBackend() override;
    static std::unique_ptr<PortalScreenBackend> open();
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
};
} // namespace mirage::platform::linux_backend
