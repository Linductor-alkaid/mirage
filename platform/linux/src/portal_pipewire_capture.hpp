#pragma once
#include <memory>
#include <mirage/desktop/screen_provider.hpp>
#include <span>
namespace mirage::platform::linux_backend {
enum class PackedPixels { Bgra, Bgrx, Rgba, Rgbx };
// Private CPU-buffer boundary, independently tested before publishing pixels.
desktop::CaptureOutcome copy_portal_pixels(std::span<const std::uint8_t>, std::uint32_t width,
                                           std::uint32_t height, std::int32_t stride, PackedPixels,
                                           const desktop::CaptureLimits &,
                                           const desktop::CancelToken &);
// Bounded metadata decoder: cursor coordinates are frame pixels, not globals.
std::optional<desktop::PointerPoint> decode_portal_cursor(std::span<const std::uint8_t>,
                                                          std::int32_t width, std::int32_t height);
// Takes ownership of the authorized remote fd. Finite caller-owned loop;
// never enables a realtime callback or exposes stream coordinates.
desktop::CaptureOutcome capture_pipewire_frame(int fd, std::uint32_t node,
                                               const desktop::CaptureLimits &,
                                               const desktop::CancelToken &);
// One bounded consumer per task session, progressed only by its Executor task.
// Repeated observations retain the authorized connection; no background loop.
class PortalPipeWireStream {
  public:
    PortalPipeWireStream(int fd, std::uint32_t node);
    ~PortalPipeWireStream();
    std::optional<desktop::PointerPoint> cursor_position() const;
    desktop::CaptureOutcome capture(const desktop::CaptureLimits &, const desktop::CancelToken &,
                                    std::int64_t not_before_ns = 0);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace mirage::platform::linux_backend
