#pragma once
#include <mira/json.hpp>
#include <mirage/integration/desktop_perception.hpp>

namespace mirage::integration::detail {
// Product evidence packaging only; Mirador owns resampling/perception.
struct PerceptionStoryboard {
    desktop::ImageFrame image;
    mira::JsonValue::Object metadata;
};
mira::Result<PerceptionStoryboard> make_perception_storyboard(const PerceptionView &,
                                                              const std::vector<std::string> &,
                                                              std::chrono::steady_clock::time_point,
                                                              const desktop::CancelToken &);
} // namespace mirage::integration::detail
