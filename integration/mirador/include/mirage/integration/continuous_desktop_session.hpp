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
// Decorates one already-initialized task session. Does not acquire authorization.
PerceptionConnection connect_continuous_desktop(kairo::Executor &,
                                                std::unique_ptr<desktop::DesktopCaptureSession>,
                                                PerceptionSessionOptions, ContinuousImageObserver);
} // namespace mirage::integration
