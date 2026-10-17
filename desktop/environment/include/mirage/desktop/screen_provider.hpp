#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/input_provider.hpp>
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
    /// DEC-081: session-local source frame sequence; zero means not provided.
    std::uint64_t source_sequence = 0;
};

/// Budget for one capture (RULE-07). A capture that would exceed `max_bytes`
/// of pixel data is refused — never silently truncated. Must be positive.
struct CaptureLimits {
    std::size_t max_bytes = 8u << 20;
    /// Optional watermark for a session reporting nonzero source_sequence.
    /// A result must be newer; callers reject zero/older sequence themselves.
    std::uint64_t newer_than_sequence = 0;
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

/// DEC-078: application preference and recoverable grant, never a live input lease.
struct DesktopAuthorizationView {
    bool supported = false;
    bool remember = false;
    bool remembered = false;
    std::string state = "unsupported";
    ProviderError error;
};

/// DEC-077: stream-local complete input. Coordinates refer to a captured image,
/// never to global desktop pixels. No held-press API is exposed.
struct SharedInputAction {
    enum class Kind { Click, Key, Text, Scroll, Drag };
    Kind kind = Kind::Click;
    double x = 0, y = 0;
    std::int32_t image_width = 0, image_height = 0;
    std::string value; // button, key chord or UTF-8 text
    std::int32_t vertical_steps = 0, horizontal_steps = 0;
    std::uint32_t click_count = 1; // DEC-099: Click only, 1..max_click_count within one action.
    double destination_x = 0, destination_y = 0; // Drag endpoint in the same captured image.
};

/// One caller-owned observation session (DEC-074). Calls must be serialized;
/// no object may be shared across tasks. Creation does not authorize capture.
/// Destruction closes platform sharing; a failed capture cannot re-authorize.
class DesktopCaptureSession {
  public:
    virtual ~DesktopCaptureSession() = default;
    virtual CaptureOutcome capture(const CaptureLimits &, const CancelToken &) = 0;
    virtual bool reusable() const { return false; }
    virtual bool input_supported() const { return false; }
    virtual InputOutcome input(const SharedInputAction &, const InputLimits &,
                               const CancelToken &) {
        return {false, false, {"unsupported_capability", "shared input unavailable"}, false, true};
    }
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

    /// A native shared desktop image has no executable global coordinates
    /// or window/display IDs (DEC-073). Task input requires a separately authorized
    /// interaction session (DEC-077); never infer it from captured pixels.
    virtual bool desktop_observation_only() const { return false; }
    virtual std::unique_ptr<DesktopCaptureSession> create_desktop_capture_session() { return {}; }
    virtual bool desktop_interaction_supported() const { return false; }
    virtual void configure_desktop_authorization(bool) {}
    virtual DesktopAuthorizationView desktop_authorization(const CancelToken &) { return {}; }
    /// remember=true requests one system grant; false forgets the stored grant.
    /// Synchronous bounded work; callers own the Executor context and cancellation.
    virtual ProviderError manage_desktop_authorization(bool, const CancelToken &) {
        return {"unsupported_capability", "remembered desktop authorization unavailable"};
    }
    virtual std::unique_ptr<DesktopCaptureSession> create_desktop_interaction_session() {
        return {};
    }
    virtual CaptureOutcome capture_desktop(const CaptureLimits &, const CancelToken &) {
        return {false, false, {}, {"unsupported_capability", "desktop observation unavailable"}};
    }

    /// Enumerates displays within the budget; refuses over-budget results
    /// instead of truncating.
    virtual DisplayListOutcome list_displays(const DisplayListLimits &limits,
                                             const CancelToken &cancel) = 0;

    DisplayListOutcome list_displays() { return list_displays(DisplayListLimits{}, CancelToken{}); }

    /// Captures a whole display by id (as reported by list_displays).
    virtual CaptureOutcome capture_display(const std::string &display_id,
                                           const CaptureLimits &limits,
                                           const CancelToken &cancel) = 0;

    /// Captures a window by id (as reported by WindowProvider): the
    /// on-screen content at the window's current bounds, exactly what a
    /// viewer sees there — content overlapping the window is included,
    /// because X has no honest per-window occlusion query. Fails closed when
    /// the window is gone or not viewable (unmapped windows have nothing on
    /// screen to capture).
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
