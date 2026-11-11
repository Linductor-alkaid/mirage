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

// DEC-125/128: independent capture/CV owner and serial
// recognition observer. The observer executes the selected capabilities and ROI,
// uses capture time in its Mirador frame, and binds generation into source policy.
// Regional results must have recognition.roi and remain incomplete/unconfirmed.
using ContinuousImageRecognizer = std::function<PerceptionSample(
    const desktop::ImageFrame &, const std::string &,
    const std::optional<desktop::WindowGeometry> &, const desktop::CancelToken &,
    std::chrono::steady_clock::time_point deadline,
    std::chrono::steady_clock::time_point captured_at, std::uint64_t generation,
    const PerceptionRecognitionWork &)>;
PerceptionConnection connect_layered_desktop(kairo::Executor &,
                                             std::unique_ptr<desktop::DesktopCaptureSession>,
                                             PerceptionSessionOptions, ContinuousImageRecognizer,
                                             std::chrono::milliseconds recognition_refresh,
                                             bool object_tracking = false);

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
