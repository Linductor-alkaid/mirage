#include "layered_visual_sampler.hpp"
#include <algorithm>
#include <cmath>
#include <mira/model_digest.hpp>
#include <mirador/color_convert.hpp>
#include <mirador/crop.hpp>
#include <mirador/resize.hpp>
#include <mirador/status.hpp>

namespace mirage::integration::detail {
namespace {
mirador::Status interrupted(const mirador::ExecutionContext &ctx) {
    if (mirador::is_cancelled(ctx))
        return {mirador::ErrorCode::kCancelled, "sampling cancelled"};
    if (ctx.deadline && std::chrono::steady_clock::now() >= *ctx.deadline)
        return {mirador::ErrorCode::kTimeout, "sampling expired"};
    return mirador::Status::success();
}
mirador::ChangeDetectionParams params() {
    mirador::ChangeDetectionParams p;
    p.fingerprint_early_exit = false;
    return p;
}
bool contains(const desktop::WindowGeometry &a, const desktop::WindowGeometry &b) {
    return b.x >= a.x && b.y >= a.y && std::int64_t(b.x) + b.width <= std::int64_t(a.x) + a.width &&
           std::int64_t(b.y) + b.height <= std::int64_t(a.y) + a.height;
}
bool overlaps(const desktop::WindowGeometry &a, const desktop::WindowGeometry &b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}
bool same_roi(const PerceptionRecognitionWork &a, const PerceptionRecognitionWork &b) {
    return a.roi && b.roi && a.roi->x == b.roi->x && a.roi->y == b.roi->y &&
           a.roi->width == b.roi->width && a.roi->height == b.roi->height;
}
bool same_work(const PerceptionRecognitionWork &a, const PerceptionRecognitionWork &b) {
    return a.ocr == b.ocr && a.detector == b.detector && same_roi(a, b);
}
mirador::Result<mirador::ChangeSignature> regional_signature(const mirador::ImageView &cv,
                                                             const mirador::Frame &source,
                                                             const desktop::WindowGeometry &r) {
    const auto b =
        map_image_bounds(r, source.image.width, source.image.height, cv.width, cv.height);
    auto cropped = mirador::crop(cv, {b.x, b.y, b.width, b.height}, kCvMaxSide * kCvMaxSide);
    if (!cropped)
        return cropped.status();
    return mirador::make_change_signature(cropped.value().view(), params());
}
} // namespace
mirador::Result<mirador::ImageBuffer> cv_work_image(const mirador::ImageView &source) {
    if (auto valid = mirador::validate(source); !valid)
        return valid.status();
    // Prefer an integral reduction step: 1024x512 -> 256x128, rather
    // than paying fractional box overlaps to fill a 320x160 budget.
    const int step = (std::max(source.width, source.height) + kCvMaxSide - 1) / kCvMaxSide;
    const int width = std::max(1, source.width / step);
    const int height = std::max(1, source.height / step);
    // Resize color first: no full-size grayscale temporary or fingerprint scan.
    // Same-size input can be converted directly, avoiding an extra row copy.
    if (width == source.width && height == source.height)
        return mirador::convert_color(source, mirador::PixelFormat::kGray8,
                                      kCvMaxSide * kCvMaxSide);
    auto compact = mirador::resize_area(source, width, height, kCvMaxSide * kCvMaxSide * 4);
    if (!compact)
        return compact.status();
    return mirador::convert_color(compact.value().view(), mirador::PixelFormat::kGray8,
                                  kCvMaxSide * kCvMaxSide);
}
desktop::WindowGeometry map_image_bounds(const desktop::WindowGeometry &b, int fw, int fh, int tw,
                                         int th) {
    if (fw <= 0 || fh <= 0 || tw <= 0 || th <= 0 || b.x < 0 || b.y < 0 || b.width <= 0 ||
        b.height <= 0 || std::int64_t(b.x) + b.width > fw || std::int64_t(b.y) + b.height > fh)
        return {};
    const auto sx = double(tw) / fw, sy = double(th) / fh;
    const int x = std::clamp(int(std::floor(b.x * sx)), 0, tw);
    const int y = std::clamp(int(std::floor(b.y * sy)), 0, th);
    const int right = std::clamp(int(std::ceil((double(b.x) + b.width) * sx)), x, tw);
    const int bottom = std::clamp(int(std::ceil((double(b.y) + b.height) * sy)), y, th);
    return {x, y, right - x, bottom - y};
}
mirador::Frame sampled_frame(const desktop::ImageFrame &image, const std::string &source,
                             std::uint64_t sequence, std::chrono::steady_clock::time_point at) {
    mirador::Frame frame;
    frame.image.data = reinterpret_cast<const std::byte *>(image.pixels.data());
    frame.image.width = image.width;
    frame.image.height = image.height;
    frame.image.row_stride_bytes = static_cast<std::int64_t>(image.stride);
    frame.image.format = mirador::PixelFormat::kBgra8;
    frame.source_id = source;
    frame.sequence = sequence;
    frame.timestamp = at;
    return frame;
}
void LayeredVisualSampler::reset() {
    adjacent_.reset();
    recognized_.reset();
    submitted_.reset();
    recognized_at_ = {};
    media_ = {};
    epoch_ = 0;
    confirming_ = false;
    source_width_ = source_height_ = 0;
    tracking_.reset();
    hints_.clear();
    hints_complete_ = false;
    regional_.reset();
    regional_signature_.reset();
    regional_output_.clear();
}
mirador::Result<void> LayeredVisualSampler::mark_submitted() {
    if (!adjacent_)
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "no sampled request to commit"};
    auto thumbnail = mirador::resize_area(adjacent_->thumbnail.view(), 64, 64, 4096);
    if (!thumbnail) {
        submitted_.reset();
        return thumbnail.status();
    }
    mirador::ChangeSignature copy;
    copy.frame_width = adjacent_->frame_width;
    copy.frame_height = adjacent_->frame_height;
    copy.fingerprint = adjacent_->fingerprint;
    copy.thumbnail = thumbnail.take_value();
    submitted_ = std::move(copy);
    return {};
}
mirador::Result<LayeredVisualSampler::Probe>
LayeredVisualSampler::sample(const mirador::Frame &frame, std::chrono::milliseconds refresh,
                             const mirador::ExecutionContext &ctx) {
    if (auto status = interrupted(ctx); !status)
        return status;
    auto compact = cv_work_image(frame.image);
    if (!compact)
        return compact.status();
    auto cv = frame;
    cv.image = compact.value().view();
    if (source_width_ != frame.image.width || source_height_ != frame.image.height) {
        reset(); // Even when two source layouts shrink to identical CV dimensions.
        source_width_ = frame.image.width;
        source_height_ = frame.image.height;
    }
    auto signature = mirador::make_change_signature(cv.image, params());
    if (!signature)
        return signature.status();
    Probe out;
    out.cv_width = cv.image.width;
    out.cv_height = cv.image.height;
    out.cv_bytes = compact.value().byte_size();
    if (submitted_) {
        auto delta = mirador::detect_change(*submitted_, signature.value(), params());
        if (!delta)
            return delta.status();
        out.same_as_submitted =
            delta.value().classification == mirador::ChangeClassification::kNone;
    }
    out.adjacent.classification = out.cumulative.classification =
        mirador::ChangeClassification::kGlobal;
    out.adjacent.reason = out.cumulative.reason = mirador::ChangeReason::kFirstFrame;
    if (adjacent_) {
        auto change = mirador::detect_change(*adjacent_, signature.value(), params());
        if (!change)
            return change.status();
        out.adjacent = change.take_value();
    }
    if (recognized_) {
        auto change = mirador::detect_change(*recognized_, signature.value(), params());
        if (!change)
            return change.status();
        out.cumulative = change.take_value();
    }
    if (tracking_enabled_ && recognized_) {
        auto tracked = tracking_.sample(cv, out.adjacent, ctx);
        if (!tracked)
            return tracked.status();
        out.tracking = tracked.take_value();
        out.tracking->source_epoch = epoch_;
        for (auto &track : out.tracking->tracks)
            track.bounds = map_image_bounds(track.bounds, cv.image.width, cv.image.height,
                                            frame.image.width, frame.image.height);
        for (auto &candidate : out.tracking->candidates)
            candidate = map_image_bounds(candidate, cv.image.width, cv.image.height,
                                         frame.image.width, frame.image.height);
    }
    // The tracker consumes CV-space reports; scheduling and callers consume
    // recognition-space reports. Map edges, not widths, to cover fractional pixels.
    for (auto *report : {&out.adjacent, &out.cumulative})
        for (auto &r : report->changed_regions) {
            const auto b = map_image_bounds({r.x, r.y, r.width, r.height}, cv.image.width,
                                            cv.image.height, frame.image.width, frame.image.height);
            r = {b.x, b.y, b.width, b.height};
        }
    const auto inside_media = [&](const mirador::ChangeReport &r) {
        return r.classification == mirador::ChangeClassification::kNone ||
               (media_.active && r.classification != mirador::ChangeClassification::kGlobal &&
                !r.changed_regions.empty() &&
                std::all_of(r.changed_regions.begin(), r.changed_regions.end(), [&](const auto &b) {
                    return b.x >= media_.x * frame.image.width &&
                           b.y >= media_.y * frame.image.height &&
                           b.x + b.width <= (media_.x + media_.width) * frame.image.width &&
                           b.y + b.height <= (media_.y + media_.height) * frame.image.height;
                }));
    };
    const bool expired = !recognized_ || frame.timestamp - recognized_at_ >= refresh;
    const bool tracks = out.tracking && out.tracking->requires_recognition;
    const bool changed = out.cumulative.classification != mirador::ChangeClassification::kNone;
    out.recognize =
        expired || confirming_ || tracks ||
        (changed && !(media_.active && inside_media(out.adjacent) && inside_media(out.cumulative)));
    out.reason = confirming_     ? "confirmation_pending"
                 : expired       ? "age_or_bootstrap"
                 : tracks        ? "tracking_revalidation"
                 : out.recognize ? "cumulative_change"
                                 : "sampling_unknown";
    if (out.recognize && !expired && !confirming_ && !tracks &&
        out.cumulative.classification == mirador::ChangeClassification::kPartial &&
        !out.cumulative.changed_regions.empty()) {
        int left = frame.image.width, top = frame.image.height, right = 0, bottom = 0;
        for (const auto &r : out.cumulative.changed_regions) {
            left = std::min(left, r.x);
            top = std::min(top, r.y);
            right = std::max(right, r.x + r.width);
            bottom = std::max(bottom, r.y + r.height);
        }
        desktop::WindowGeometry region{left, top, right - left, bottom - top};
        const int mx = std::max(8, int(std::ceil(4.0 * frame.image.width / cv.image.width)));
        const int my = std::max(8, int(std::ceil(4.0 * frame.image.height / cv.image.height)));
        const auto known = std::find_if(hints_.begin(), hints_.end(), [&](const auto &hint) {
            const auto &b = hint.bounds;
            // DEC-127: carry the 640-wide replay's 8px tolerance as four CV
            // pixels. Fixed source pixels under-pad larger recognition images.
            // This remains routing context, never semantic coverage evidence.
            const desktop::WindowGeometry context{b.x - mx, b.y - my, b.width + 2 * mx,
                                                  b.height + 2 * my};
            if (!hints_complete_ || hint.text == hint.detection || !overlaps(b, region) ||
                !contains(context, region))
                return false;
            return std::none_of(hints_.begin(), hints_.end(), [&](const auto &other) {
                return &other != &hint && overlaps(other.bounds, region);
            });
        });
        if (known != hints_.end()) {
            // Include BOTH actual changed pixels and the complete known box.
            const auto &b = known->bounds;
            const int x = std::min(region.x, b.x), y = std::min(region.y, b.y);
            region = {x, y, std::max(region.x + region.width, b.x + b.width) - x,
                      std::max(region.y + region.height, b.y + b.height) - y};
            out.work.ocr = known->text;
            out.work.detector = known->detection;
        }
        left = std::max(0, region.x - mx);
        top = std::max(0, region.y - my);
        right = std::min(frame.image.width, region.x + region.width + mx);
        bottom = std::min(frame.image.height, region.y + region.height + my);
        if (left < right && top < bottom &&
            std::int64_t(right - left) * (bottom - top) <=
                std::int64_t(frame.image.width) * frame.image.height / 4) {
            out.work.roi = {left, top, right - left, bottom - top};
            out.reason = "regional_change";
            if (regional_ && regional_signature_ && same_roi(*regional_, out.work)) {
                auto current = regional_signature(cv.image, frame, *out.work.roi);
                if (!current)
                    return current.status();
                auto delta =
                    mirador::detect_change(*regional_signature_, current.value(), params());
                if (!delta)
                    return delta.status();
                if (delta.value().classification == mirador::ChangeClassification::kNone) {
                    out.recognize = false;
                    out.reason = "regional_unchanged_unknown";
                }
            }
        } else
            out.work = {}; // bounded full-frame fallback
    }
    if (auto status = interrupted(ctx); !status) {
        tracking_.reset();
        return status;
    }
    adjacent_ = signature.take_value();
    return out;
}
mirador::Result<void> LayeredVisualSampler::recognize(const mirador::Frame &frame,
                                                      const PerceptionSample &sample,
                                                      const mirador::ExecutionContext &ctx) {
    if (!sample.ok || sample.cancelled || sample.skipped || sample.recognition_deferred ||
        !sample.objects_complete || sample.objects.size() > 256 || !sample.source_epoch ||
        (sample.recognition && sample.recognition->roi))
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "incomplete recognition baseline"};
    if (auto status = interrupted(ctx); !status)
        return status;
    auto compact = cv_work_image(frame.image);
    if (!compact)
        return compact.status();
    auto cv = frame;
    cv.image = compact.value().view();
    auto signature = mirador::make_change_signature(cv.image, params());
    if (!signature)
        return signature.status();
    // Rebuild from the actual recognized frame at CV resolution: no backward
    // sequence commit or continuity claim through the OCR waiting interval.
    if (source_width_ &&
        (source_width_ != frame.image.width || source_height_ != frame.image.height))
        reset();
    tracking_.reset();
    if (tracking_enabled_) {
        // Only bounded semantic metadata is copied; no source pixel copy.
        auto mapped = sample;
        for (auto &entry : mapped.objects) {
            auto &b = entry.object.bounds;
            const bool valid = b.x >= 0 && b.y >= 0 && b.width > 0 && b.height > 0 &&
                               std::int64_t(b.x) + b.width <= frame.image.width &&
                               std::int64_t(b.y) + b.height <= frame.image.height;
            if (!valid) {
                b = {};
                continue;
            }
            const double w = double(b.width) * cv.image.width / frame.image.width;
            const double h = double(b.height) * cv.image.height / frame.image.height;
            b = map_image_bounds(b, frame.image.width, frame.image.height, cv.image.width,
                                 cv.image.height);
            if (w < 8 || h < 8)
                b.width = b.height = 0; // Exclude indistinguishable tiny templates.
        }
        auto seeded = tracking_.seed_observed(cv, mapped, ctx);
        if (!seeded.error.empty()) {
            tracking_.reset();
            recognized_.reset();
            return mirador::Status{mirador::ErrorCode::kBackendFailure, seeded.error};
        }
    }
    if (auto status = interrupted(ctx); !status) {
        tracking_.reset();
        return status;
    }
    recognized_ = signature.take_value();
    source_width_ = frame.image.width;
    source_height_ = frame.image.height;
    recognized_at_ = frame.timestamp;
    confirming_ = std::any_of(sample.objects.begin(), sample.objects.end(), [](const auto &o) {
        return o.object.pending || !o.object.identity_confirmed;
    });
    media_ = sample.media;
    epoch_ = sample.source_epoch;
    hints_.clear();
    hints_complete_ = sample.objects.size() <= 32;
    for (const auto &entry : sample.objects) {
        const auto &o = entry.object;
        if (hints_.size() == 32)
            break;
        if (entry.content_observed && o.identity_confirmed && !o.pending && o.bounds.x >= 0 &&
            o.bounds.y >= 0 && o.bounds.width > 0 && o.bounds.height > 0 &&
            std::int64_t(o.bounds.x) + o.bounds.width <= frame.image.width &&
            std::int64_t(o.bounds.y) + o.bounds.height <= frame.image.height) {
            const bool text =
                o.source == desktop::VisualRegionSource::kOcr && !o.text.empty() && o.label.empty();
            const bool detection = o.source == desktop::VisualRegionSource::kDetector &&
                                   o.text.empty() && !o.label.empty();
            hints_.push_back({o.bounds, text, detection});
        } else
            hints_complete_ = false;
    }
    regional_.reset();
    regional_signature_.reset();
    regional_output_.clear();
    return {};
}
mirador::Result<bool> LayeredVisualSampler::recognize_region(const mirador::Frame &frame,
                                                             const PerceptionSample &sample,
                                                             const mirador::ExecutionContext &ctx) {
    if (!recognized_ || sample.source_epoch != epoch_ || !sample.ok || sample.cancelled ||
        sample.skipped || sample.recognition_deferred || sample.objects_complete ||
        !sample.objects.empty() || !sample.recognition || !sample.recognition->roi ||
        sample.events.size() > 32)
        return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                               "invalid regional observation"};
    if (auto status = interrupted(ctx); !status)
        return status;
    if (frame.image.width != source_width_ || frame.image.height != source_height_)
        return mirador::Status{mirador::ErrorCode::kInvalidArgument, "regional layout changed"};
    auto compact = cv_work_image(frame.image);
    if (!compact)
        return compact.status();
    auto signature = regional_signature(compact.value().view(), frame, *sample.recognition->roi);
    if (!signature)
        return signature.status();
    std::string content;
    content.reserve(16 * 1024);
    const auto &roi = *sample.recognition->roi;
    for (const auto &e : sample.events) {
        if (e.kind.size() > 64 || e.text.size() > 256 || e.entity || e.instance_generation ||
            e.confirmed || !std::isfinite(e.x) || !std::isfinite(e.y) || !std::isfinite(e.width) ||
            !std::isfinite(e.height) || e.width <= 0 || e.height <= 0 || e.x < roi.x ||
            e.y < roi.y || e.x + e.width > double(roi.x) + roi.width ||
            e.y + e.height > double(roi.y) + roi.height)
            return mirador::Status{mirador::ErrorCode::kInvalidArgument,
                                   "regional observation must remain unconfirmed"};
        content += std::to_string(e.kind.size()) + ":" + e.kind + std::to_string(e.text.size()) +
                   ":" + e.text + ":" + std::to_string(e.x) + ":" + std::to_string(e.y) + ":" +
                   std::to_string(e.width) + ":" + std::to_string(e.height) + ";";
    }
    const auto digest = mira::digest_string(content).to_string();
    const bool changed =
        !regional_ || !same_work(*regional_, *sample.recognition) || digest != regional_output_;
    if (auto status = interrupted(ctx); !status)
        return status;
    regional_ = sample.recognition;
    regional_signature_ = signature.take_value();
    regional_output_ = digest;
    return changed;
}
} // namespace mirage::integration::detail
