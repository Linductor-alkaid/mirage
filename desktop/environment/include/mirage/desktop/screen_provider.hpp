#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/provider_error.hpp>

namespace mirage::desktop {

/// Pixel layout of a captured frame. The single canonical format keeps the
/// contract platform-free; backends convert before returning.
enum class ImageFormat {
    Bgra8, ///< 32 bits per pixel, byte order B, G, R, A.
};

/// One captured image. `pixels` holds `stride * height` bytes; `stride >=
/// width * 4` allows padded rows.
struct ImageFrame {
    ImageFormat format = ImageFormat::Bgra8;
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::size_t stride = 0;
    std::vector<std::uint8_t> pixels;
};

/// Budget for one capture (RULE-07). A capture that would exceed `max_bytes`
/// of pixel data is refused — never silently truncated. Must be positive.
struct CaptureLimits {
    std::size_t max_bytes = 8u << 20;
};

struct DisplayInfo {
    std::string id;
    WindowGeometry geometry;
    bool primary = false;
};

struct DisplayListLimits {
    std::size_t max_displays = 16;
};

struct DisplayListOutcome {
    bool ok = false;
    std::vector<DisplayInfo> displays;
    ProviderError error; ///< meaningful only when ok is false
};

struct CaptureOutcome {
    bool ok = false;
    bool cancelled = false;
    ImageFrame frame;    ///< meaningful only when ok
    ProviderError error; ///< meaningful only when ok is false
};

/// Screen capture (design doc section 5): whole displays, windows and
/// arbitrary regions. Capture may surface sensitive on-screen content, so
/// callers judge the `screen.capture` capability through the runtime
/// permission gate (RULE-05, DEC-010) before invoking; the provider itself
/// stays permission-agnostic. Methods are synchronous and bounded; callers
/// decide the execution context.
class ScreenProvider {
  public:
    virtual ~ScreenProvider() = default;

    /// Enumerates displays within the budget; refuses over-budget results
    /// instead of truncating.
    virtual DisplayListOutcome list_displays(const DisplayListLimits &limits,
                                             const CancelToken &cancel) = 0;

    DisplayListOutcome list_displays() { return list_displays(DisplayListLimits{}, CancelToken{}); }

    /// Captures a whole display by id (as reported by list_displays).
    virtual CaptureOutcome capture_display(const std::string &display_id,
                                           const CaptureLimits &limits,
                                           const CancelToken &cancel) = 0;

    /// Captures a window by id (as reported by WindowProvider). Fails closed
    /// when the window is gone or fully occlusion-blocked by the platform.
    virtual CaptureOutcome capture_window(const std::string &window_id, const CaptureLimits &limits,
                                          const CancelToken &cancel) = 0;

    /// Captures a region in global desktop coordinates. Empty or negative
    /// extents are rejected before any capture happens.
    virtual CaptureOutcome capture_roi(const WindowGeometry &roi, const CaptureLimits &limits,
                                       const CancelToken &cancel) = 0;

    /// Same captures under default limits and without cancellation.
    CaptureOutcome capture_display(const std::string &display_id) {
        return capture_display(display_id, CaptureLimits{}, CancelToken{});
    }

    CaptureOutcome capture_window(const std::string &window_id) {
        return capture_window(window_id, CaptureLimits{}, CancelToken{});
    }

    CaptureOutcome capture_roi(const WindowGeometry &roi) {
        return capture_roi(roi, CaptureLimits{}, CancelToken{});
    }
};

} // namespace mirage::desktop
