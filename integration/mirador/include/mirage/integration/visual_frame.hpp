#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include <mirador/frame.hpp>
#include <mirador/image_view.hpp>
#include <mirador/pixel_format.hpp>
#include <mirador/result.hpp>

#include <mirage/desktop/screen_provider.hpp>

namespace mirage::integration {

/// Wraps one ScreenProvider capture into a mirador frame (plan item `M3-02`,
/// design doc section 8). The canonical Bgra8 capture layout maps onto
/// mirador's natively defined `PixelFormat::kBgra8` — no pixel conversion
/// happens here; when a backend needs another format, the mirador pipeline
/// converts internally or the caller uses `convert_frame`, which routes
/// through mirador `color_convert` (Mirage never re-implements resampling).
///
/// The frame copies the pixel bytes into a shared owner buffer, so the
/// returned frame (and every derived view) outlives the caller's
/// `ImageFrame`; pixels never escape the visual session (DEC-016 decision 3).
///
/// Errors: kInvalidArgument for an empty source id, non-positive or
/// over-dimensioned extents, a stride below `width * 4`, or a pixel buffer
/// smaller than `stride * height`; kUnsupportedFormat for a capture format
/// other than the canonical Bgra8. The wrapped view is re-validated through
/// `mirador::validate` before being returned.
[[nodiscard]] mirador::Result<mirador::Frame>
to_mirador_frame(const desktop::ImageFrame &frame, std::string source_id, std::uint64_t sequence,
                 std::chrono::steady_clock::time_point timestamp);

/// Converts an existing mirador frame's pixels to `target_format` through
/// mirador's `convert_color` kernel under the explicit `max_bytes` budget
/// (RULE-07: budget exhaustion fails with kBudgetExceeded, never truncates).
/// The returned frame keeps sequence, timestamp, source id and rotation
/// metadata; its pixels live in a new owner buffer held by the frame.
/// Mirage captures are top-down rotation-k0 views, the only shape this
/// adapter produces and consumes.
///
/// Errors: those of `mirador::convert_color` — kInvalidArgument for an
/// invalid source view or undefined target format, kUnsupportedFormat when
/// the conversion matrix has no kernel (for example anything but identity
/// into kNv12), kBudgetExceeded when the request exceeds `max_bytes`.
[[nodiscard]] mirador::Result<mirador::Frame> convert_frame(const mirador::Frame &frame,
                                                            mirador::PixelFormat target_format,
                                                            std::int64_t max_bytes);

} // namespace mirage::integration
