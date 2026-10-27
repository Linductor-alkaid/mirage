#pragma once
#include <mirage/integration/desktop_perception.hpp>
namespace kairo {
class Executor;
}
namespace mirage::integration {
using ContinuousImageObserver = std::function<PerceptionSample(
    const desktop::ImageFrame &, const std::string &,
    const std::optional<desktop::WindowGeometry> &, const desktop::CancelToken &,
    const std::optional<std::chrono::steady_clock::time_point> &)>;
// Decorates a serialized source. Runtime-owned sources allow only silent grant reuse
// in their background producer; task foreground access still authorizes separately.
PerceptionConnection connect_continuous_desktop(kairo::Executor &,
                                                std::unique_ptr<desktop::DesktopCaptureSession>,
                                                PerceptionSessionOptions, ContinuousImageObserver);

/// DEC-117: service-owned producer; task connections are cancellable leases.
/// stop() must run before shutting down the factory's Executor/analysis worker.
class ResidentDesktopPerception {
  public:
    explicit ResidentDesktopPerception(PerceptionSessionFactory);
    ~ResidentDesktopPerception();
    bool adopt(std::unique_ptr<desktop::DesktopCaptureSession>, PerceptionSessionOptions,
               bool interactive = false);
    /// A failed background admission preserves the caller's authorized foreground source.
    PerceptionConnection connect(std::unique_ptr<desktop::DesktopCaptureSession>,
                                 PerceptionSessionOptions lifetime, PerceptionSessionOptions task,
                                 bool interactive);
    PerceptionConnection attach(bool require_input, PerceptionSessionOptions);
    void invalidate();
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::integration
