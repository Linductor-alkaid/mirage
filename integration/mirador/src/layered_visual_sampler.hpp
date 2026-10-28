#pragma once
#include "visual_object_tracking.hpp"
#include <mirador/change_detection.hpp>

namespace mirage::integration::detail {
// DEC-127: private, bounded CV work plane; recognition never consumes this image.
inline constexpr int kCvMaxSide = 320;
mirador::Result<mirador::ImageBuffer> cv_work_image(const mirador::ImageView &);
desktop::WindowGeometry map_image_bounds(const desktop::WindowGeometry &, int from_width,
                                         int from_height, int to_width, int to_height);
// Capture worker only. Uses public Mirador algorithms, owns no source pixels.
class LayeredVisualSampler {
    std::optional<mirador::ChangeSignature> adjacent_, recognized_, submitted_;
    std::chrono::steady_clock::time_point recognized_at_{};
    PerceptionMediaActivity media_;
    VisualObjectTracking tracking_;
    std::uint64_t epoch_ = 0;
    bool tracking_enabled_;
    bool confirming_ = false;
    int source_width_ = 0, source_height_ = 0;
    struct Hint {
        desktop::WindowGeometry bounds;
        bool text = false, detection = false;
    };
    std::vector<Hint> hints_; // <=32 confirmed bounds, including ambiguous blockers
    bool hints_complete_ = false;
    std::optional<PerceptionRecognitionWork> regional_;
    std::optional<mirador::ChangeSignature> regional_signature_;
    std::string regional_output_;

  public:
    struct Probe {
        mirador::ChangeReport adjacent, cumulative;
        std::optional<PerceptionTracking> tracking;
        bool recognize = true;
        bool same_as_submitted = false;
        std::string reason;
        PerceptionRecognitionWork work;
        int cv_width = 0, cv_height = 0;
        std::int64_t cv_bytes = 0;
    };
    explicit LayeredVisualSampler(bool tracking) : tracking_enabled_(tracking) {}
    void reset();
    mirador::Result<void> mark_submitted();
    mirador::Result<Probe> sample(const mirador::Frame &, std::chrono::milliseconds refresh,
                                  const mirador::ExecutionContext &);
    // Accept only actual complete recognition on these exact original pixels.
    mirador::Result<void> recognize(const mirador::Frame &, const PerceptionSample &,
                                    const mirador::ExecutionContext &);
    // Commits only the regional gate; true means output changed. Full-frame
    // recognition age/identity/templates/coverage are deliberately untouched.
    mirador::Result<bool> recognize_region(const mirador::Frame &, const PerceptionSample &,
                                           const mirador::ExecutionContext &);
};
// Borrowed for synchronous CV only; no session or tracker retains the view.
mirador::Frame sampled_frame(const desktop::ImageFrame &, const std::string &, std::uint64_t,
                             std::chrono::steady_clock::time_point);
} // namespace mirage::integration::detail
