#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/geometry.hpp>
#include <mirage/desktop/input_provider.hpp>
#include <mirage/desktop/presentation_exclusion.hpp>
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
    /// DEC-107: set only when a presentation owner took part in this capture.
    std::optional<bool> presentation_excluded = std::nullopt;
};

using CapturePresentation = std::function<PresentationExclusion(const CancelToken &)>;

/// Finite capture boundary: restore before encoding, analysis or publication.
/// No scheduling or ownership of the caller's execution context lives here.
template <class Capture>
CaptureOutcome capture_with_presentation(const CapturePresentation &acquire,
                                         const CancelToken &cancel, Capture &&capture) {
    if (!acquire)
        return capture();
    PresentationExclusion exclusion;
    CaptureOutcome result;
    try {
        if (cancel.cancelled())
            return {false, true, {}, {"cancelled", "capture presentation cancelled"}};
        exclusion = acquire(cancel);
        if (!exclusion.error.code.empty())
            return {false, exclusion.error.code == "cancelled", {}, exclusion.error};
        if (!exclusion.lease)
            return {false, false, {}, {"pet_exclusion_failed", "capture owner returned no lease"}};
        const auto verified = exclusion.lease->verify(cancel);
        if (!verified.code.empty())
            result = {false, verified.code == "cancelled", {}, verified};
        else {
            result = capture();
            result.presentation_excluded = exclusion.lease->excluded();
            const auto after = exclusion.lease->verify({});
            if (!after.code.empty())
                result = {false, false, {}, after};
        }
    } catch (...) {
        result = {false, false, {}, {"capture_failed", "capture or presentation provider threw"}};
    }
    if (exclusion.lease) {
        try {
            const auto restored = exclusion.lease->release();
            if (!restored.code.empty())
                return {false, false, {}, restored};
        } catch (...) {
            return {false, false, {}, {"pet_restore_failed", "capture presentation cleanup threw"}};
        }
    }
    return result;
}

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
    enum class Kind { Click, Key, Text, Scroll, Drag, Move };
    Kind kind = Kind::Click;
    double x = 0, y = 0;
    std::int32_t image_width = 0, image_height = 0;
    std::string value; // button, key chord or UTF-8 text
    std::int32_t vertical_steps = 0, horizontal_steps = 0;
    std::uint32_t click_count = 1; // DEC-099: Click only, 1..max_click_count within one action.
    double destination_x = 0, destination_y = 0; // Drag endpoint in the same captured image.
    /// DEC-110 (M7-26): Click-only opt-in timed travel to (x, y) along the
    /// frozen minimum-jerk baseline before the press; 0 keeps the direct
    /// dispatch. Needs a known pointer origin (platform read-back or this
    /// session's own last dispatched position); unknown origins refuse the
    /// action before any effect instead of teleporting.
    std::uint32_t glide_ms = 0;
    /// Internal Move step: automatic approach before a click, within the
    /// same action lease. Not exposed as a separate model tool.
    std::int32_t target_width = 0;
};

/// Read-only pointer snapshot in this capture session's FRAME pixels.
/// A successful query belongs to the stated source frame, never global coords
/// or another session's last injected position.
struct SharedPointerQueryOutcome {
    bool ok = false, cancelled = false;
    PointerPoint position;
    std::int32_t image_width = 0, image_height = 0;
    std::uint64_t source_sequence = 0;
    ProviderError error;
};

/// One caller-owned observation session (DEC-074). Calls must be serialized;
/// no object may be shared across tasks. Creation does not authorize capture.
/// Destruction closes platform sharing; a failed capture cannot re-authorize.
class DesktopCaptureSession {
  public:
    virtual ~DesktopCaptureSession() = default;
    /// Read already-acquired stream metadata; no authorization UI or input.
    virtual SharedPointerQueryOutcome pointer_position(const CancelToken &) {
        return {false, false, {}, 0, 0, 0,
                {"pointer_origin_unknown", "Shared cursor metadata is unavailable"}};
    }
    virtual CaptureOutcome capture(const CaptureLimits &, const CancelToken &) = 0;
    /// Backends with authorization/connection setup defer the lease until
    /// pixels can be acquired. The default brackets only the capture call.
    virtual CaptureOutcome capture_presented(const CaptureLimits &limits, const CancelToken &cancel,
                                             const CapturePresentation &presentation) {
        return capture_with_presentation(presentation, cancel,
                                         [&] { return capture(limits, cancel); });
    }
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
