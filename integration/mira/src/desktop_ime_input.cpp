#include "desktop_action_verification.hpp"
#include "desktop_image_supply.hpp"
#include "ime_text_input.hpp"
#include "tool_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mirador/crop.hpp>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
std::string text(const Json &v, const char *key) {
    const auto *p = v.find(key);
    return p && p->as_string() ? *p->as_string() : "";
}
struct Roi {
    int x = 0, y = 0, w = 0, h = 0;
};
std::optional<Roi> roi(const Json &v, int width, int height) {
    Roi out;
    int *values[]{&out.x, &out.y, &out.w, &out.h};
    const char *names[]{"x", "y", "width", "height"};
    for (unsigned i = 0; i < 4; ++i) {
        const auto *p = v.find(names[i]);
        const auto n = p ? p->as_integer() : std::nullopt;
        if (!n || *n < 0 || *n > 16384)
            return {};
        *values[i] = static_cast<int>(*n);
    }
    if (out.w <= 0 || out.h <= 0 || out.x + out.w > width || out.y + out.h > height ||
        std::int64_t(out.w) * out.h > 512 * 1024)
        return {};
    return out;
}
bool overlap(Roi a, Roi b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
Json rectangle_schema() {
    Json::Object props;
    for (const auto *name : {"x", "y", "width", "height"})
        props.emplace_back(name,
                           Json::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 16384}});
    return Json::Object{{"type", "object"},
                        {"properties", std::move(props)},
                        {"required", Json::Array{"x", "y", "width", "height"}},
                        {"additionalProperties", false}};
}
mira::Error failed(const std::string &code, bool effects, bool released,
                   const ImeResult &progress = {}) {
    mira::Error error;
    error.code = code == "cancelled" ? mira::ErrorCode::Cancelled : mira::ErrorCode::PlatformError;
    error.domain = "mirage.ime";
    error.retryable = false;
    error.safe_message = mira::to_json_string(
        Json{Json::Object{{"error_code", code},
                          {"verified", false},
                          {"text_verified", false},
                          {"side_effect_may_have_occurred", effects},
                          {"input_released", released},
                          {"phase", progress.phase},
                          {"verified_bytes", static_cast<std::int64_t>(progress.verified_bytes)},
                          {"internal_actions", std::int64_t(progress.actions)},
                          {"roi_ocr_calls", std::int64_t(progress.reads)}}});
    return error;
}
} // namespace
void DesktopImageTurn::register_ime_tool(mira::BuiltinToolRegistry &registry, ImePlanner planner,
                                         const DesktopAtomToolset::Decorator &decorate) {
    auto *screen = desktop_.environment ? desktop_.environment->screen() : nullptr;
    if (!screen || !screen->desktop_observation_only() ||
        !screen->desktop_interaction_supported() || !desktop_.ocr_image || !planner ||
        !desktop_.actions || (!desktop_.gate && !desktop_.authorize))
        return;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.input.fill";
    spec.version = {1, 0, 0};
    spec.has_side_effects = true;
    spec.description =
        "Replace one visible input field with short specified text, automatically using the IME. "
        "Supply the latest actionable image_scope/epoch, target rectangle, mode_roi showing only "
        "中/英 and candidates_roi covering the candidate popup. All rectangles are in the same "
        "supplied IMAGE pixels; target and candidates must not overlap. The tool focuses and "
        "replaces the entire field, types pinyin/ASCII, recognizes modes and numbered candidates "
        "ONLY through local ROI OCR, pages with = and selects the matching number, then checks "
        "each committed prefix in the target ROI. No VLM recognition, clipboard or Enter submit. "
        "Supports at most 32 basic Han/printable ASCII characters, 256 UTF-8 bytes, 8 pages per "
        "Han character, 120 seconds subject to parent deadline. Shift and = require the IME's "
        "current configuration. Returns text_verified and verification_method=roi_ocr_exact; "
        "this is visual text evidence, not a full application success or semantic value read. "
        "Stops on ambiguity/repeated pages/mismatch; a failure can leave partial text. Never "
        "blindly retry the whole fill. No model-visible candidate images are produced.";
    Json::Object properties{
        {"image_scope", Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
        {"environment_epoch", Json::Object{{"type", "integer"}, {"minimum", 1}}},
        {"text", Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 256}}},
        {"target", rectangle_schema()},
        {"mode_roi", rectangle_schema()},
        {"candidates_roi", rectangle_schema()}};
    spec.parameters_schema = mira::JsonSchema{
        Json{Json::Object{{"type", "object"},
                          {"properties", std::move(properties)},
                          {"required", Json::Array{"image_scope", "environment_epoch", "text",
                                                   "target", "mode_roi", "candidates_roi"}},
                          {"additionalProperties", false}}}};
    mira::BuiltinToolHandler handler = [this, planner = std::move(planner)](const auto &args,
                                                                            const auto &ctx) {
        return fill_text(args, ctx, planner);
    };
    if (decorate) {
        DesktopActionVerification::configure_policy(spec);
        // New wire identity starts at v1 with the current dependency policy;
        // it has no legacy mutation contract to migrate.
        spec.version = {1, 0, 0};
        handler = decorate(spec.wire_name, std::move(handler));
    }
    if (!registry.register_tool(std::move(spec), std::move(handler)))
        throw std::logic_error("IME fill tool registration failed");
    ime_tool_enabled_ = true;
}

mira::Result<Json> DesktopImageTurn::fill_text(const Json &args, const mira::OperationContext &ctx,
                                               const ImePlanner &planner) {
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return failed("owner_mismatch", false, true);
    if (!native_session_ || !native_session_->input_supported() || !native_input_frame_ ||
        !native_frame_ || !image_ || !supplied_image_ || !desktop_.actions || !desktop_.ocr_image)
        return failed("interactive_image_required", false, true);
    if (native_input_recovery_.blocked())
        return failed(native_input_recovery_.reason(), false, true);
    const auto *requested_epoch = args.find("environment_epoch");
    if (!requested_epoch || requested_epoch->as_integer() != static_cast<std::int64_t>(epoch_) ||
        epoch_ != desktop_.actions->epoch() ||
        text(args, "image_scope") != text(metadata_, "image_scope"))
        return failed("stale_observation", false, true);
    const auto authority = desktop_.actions->authority_epoch();
    const auto end = std::min(ctx.deadline.value_or(std::chrono::steady_clock::time_point::max()),
                              std::chrono::steady_clock::now() + std::chrono::seconds(120));
    const desktop::CancelToken bounded([this, ctx, authority, end] {
        return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
               desktop_.actions->authority_epoch() != authority ||
               std::chrono::steady_clock::now() >= end;
    });
    if (bounded.cancelled())
        return failed("cancelled", false, true);
    const auto target = args.find("target"), mode = args.find("mode_roi"),
               candidate = args.find("candidates_roi");
    if (!target || !mode || !candidate)
        return failed("invalid_roi", false, true);
    auto tr = roi(*target, native_frame_->width, native_frame_->height);
    auto mr = roi(*mode, native_frame_->width, native_frame_->height);
    auto cr = roi(*candidate, native_frame_->width, native_frame_->height);
    if (!tr || !mr || !cr || overlap(*tr, *cr))
        return failed("invalid_roi", false, true);
    const int source_w = native_input_frame_->width, source_h = native_input_frame_->height;
    const auto integer = [&](const char *name, std::int64_t fallback) {
        const auto *v = metadata_.find(name);
        return v ? v->as_integer().value_or(fallback) : fallback;
    };
    const auto nx = integer("desktop_per_image_x_numerator", source_w),
               dx = integer("desktop_per_image_x_denominator", native_frame_->width),
               ny = integer("desktop_per_image_y_numerator", source_h),
               dy = integer("desktop_per_image_y_denominator", native_frame_->height),
               ox = integer("origin_x", 0), oy = integer("origin_y", 0);
    if (dx <= 0 || dy <= 0 || nx <= 0 || ny <= 0)
        return failed("invalid_roi", false, true);
    const auto map = [&](Roi r) -> std::optional<Roi> {
        const auto left = ox + std::int64_t(r.x) * nx / dx, top = oy + std::int64_t(r.y) * ny / dy;
        const auto right = ox + (std::int64_t(r.x) + r.w) * nx / dx,
                   bottom = oy + (std::int64_t(r.y) + r.h) * ny / dy;
        if (left < 0 || top < 0 || right > source_w || bottom > source_h || right <= left ||
            bottom <= top || (right - left) * (bottom - top) > 512 * 1024)
            return {};
        return Roi{int(left), int(top), int(right - left), int(bottom - top)};
    };
    tr = map(*tr);
    mr = map(*mr);
    cr = map(*cr);
    if (!tr || !mr || !cr || overlap(*tr, *cr))
        return failed("invalid_roi", false, true);
    const auto desired = text(args, "text");
    if (desired.empty() || desired.size() > 256 || !desktop::is_valid_utf8(desired))
        return failed("invalid_text", false, true);
    auto local = ctx;
    local.deadline = end;
    std::optional<ImePlan> plan;
    try {
        plan = planner(desired, local);
    } catch (...) {
        return failed("pinyin_plan_failed", false, true);
    }
    if (!plan || !valid_ime_plan(desired, *plan) || bounded.cancelled())
        return failed(bounded.cancelled() ? "cancelled" : "invalid_pinyin_plan", false, true);
    auto priority = perception_ ? perception_->pause() : nullptr;
    Json focus{Json::Object{{"image_scope", *args.find("image_scope")},
                            {"environment_epoch", *requested_epoch}}};
    for (const auto *name : {"x", "y", "width", "height"})
        focus.set(name, *target->find(name));
    const auto focus_epoch = epoch_;
    const auto focused = shared_input("click", focus, local);
    if (!focused)
        return focused.error();
    auto lease = desktop_.actions->try_begin(bounded, focus_epoch + 2);
    if (!lease) {
        clear();
        return failed(lease.error().code, true, true);
    }
    bool released = true;
    std::string io_error;
    std::uint64_t sequence = native_newer_than_sequence_;
    std::optional<desktop::ImageFrame> frame;
    const auto permitted = [&](const char *capability) {
        return desktop_.authorize
                   ? desktop_.authorize(
                         capability, "desktop", [&] { return bounded.cancelled(); },
                         ctx.task.to_string(), ctx.operation.to_string())
                   : desktop_.gate(capability, "desktop", [&] { return bounded.cancelled(); });
    };
    ImeIo io;
    io.stopped = [&] { return bounded.cancelled() || !native_session_; };
    io.send = [&](bool key, const std::string &value) {
        if (bounded.cancelled() || !permitted("input.inject") || !permitted("screen.capture")) {
            io_error = bounded.cancelled() ? "cancelled" : "permission_denied";
            return false;
        }
        desktop::SharedInputAction action;
        action.kind =
            key ? desktop::SharedInputAction::Kind::Key : desktop::SharedInputAction::Kind::Text;
        action.value = value;
        action.x = tr->x + tr->w / 2;
        action.y = tr->y + tr->h / 2;
        action.image_width = source_w;
        action.image_height = source_h;
        desktop::InputLimits limits;
        limits.timeout = std::min(std::chrono::milliseconds(key ? 3000 : 15000),
                                  std::chrono::duration_cast<std::chrono::milliseconds>(
                                      end - std::chrono::steady_clock::now()));
        desktop::InputOutcome out;
        try {
            out = native_session_->input(action, limits, bounded);
        } catch (...) {
            out = {false, false, {"provider_exception", "Input failed"}, true, false};
        }
        released = out.input_released;
        if (!released)
            lease.release_failed();
        frame.reset();
        if (!out.ok || !released) {
            io_error = out.error.code.empty() ? "input_failed" : out.error.code;
            if (out.side_effect_may_have_occurred || !released) {
                if (out.error.code == "input_lag" && released) {
                    native_input_recovery_.failed(out, sequence);
                    native_newer_than_sequence_ = sequence;
                } else {
                    native_session_.reset();
                    native_failed_ = true;
                }
            }
        }
        return out.ok && released && !bounded.cancelled();
    };
    io.read = [&](ImeRoi kind) -> std::optional<ImageOcrOutcome> {
        frame.reset(); // Every observation retry requires a strictly newer frame.
        if (bounded.cancelled() || !permitted("screen.capture")) {
            io_error = "permission_denied";
            return {};
        }
        if (!frame) {
            desktop::CaptureOutcome captured;
            try {
                captured = native_session_->capture_presented({desktop_capture_bytes, sequence},
                                                              bounded, desktop_.exclude_pet);
            } catch (...) {
                io_error = "capture_failed";
                return {};
            }
            if (!captured.ok || !valid_image_frame(captured.frame, desktop_capture_bytes) ||
                captured.frame.width != source_w || captured.frame.height != source_h ||
                captured.frame.source_sequence <= sequence || bounded.cancelled()) {
                io_error = "capture_changed_or_failed";
                return {};
            }
            sequence = captured.frame.source_sequence;
            frame = std::move(captured.frame);
        }
        const auto area = kind == ImeRoi::mode ? *mr : kind == ImeRoi::target ? *tr : *cr;
        const mirador::ImageView view{reinterpret_cast<const std::byte *>(frame->pixels.data()),
                                      frame->width,
                                      frame->height,
                                      static_cast<std::int64_t>(frame->stride),
                                      mirador::PixelFormat::kBgra8,
                                      mirador::Rotation::k0,
                                      {}};
        auto cropped = mirador::crop(view, {area.x, area.y, area.w, area.h}, 2 * 1024 * 1024);
        if (!cropped) {
            io_error = "roi_crop_failed";
            return {};
        }
        desktop::ImageFrame patch;
        patch.width = area.w;
        patch.height = area.h;
        patch.stride = std::size_t(area.w) * 4;
        patch.pixels.resize(patch.stride * std::size_t(area.h));
        std::memcpy(patch.pixels.data(), cropped.value().view().data, patch.pixels.size());
        auto out = measure_phase(timings_ ? &timings_->ocr : nullptr,
                                 [&] { return desktop_.ocr_image(patch, bounded, end); });
        for (const auto &r : out.regions)
            if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) ||
                !std::isfinite(r.height) || r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
                r.x + r.width > area.w || r.y + r.height > area.h)
                return {};
        return out;
    };
    ImeResult outcome;
    try {
        outcome = execute_ime_text(*plan, io);
    } catch (...) {
        outcome.error = "ime_provider_exception";
        outcome.phase = "platform";
    }
    native_newer_than_sequence_ = sequence;
    clear(); // Retire the initial scope; never offer private ROI images to a model.
    if (!outcome.ok || bounded.cancelled())
        return failed(bounded.cancelled() ? "cancelled"
                      : !io_error.empty() ? io_error
                                          : outcome.error,
                      true, released, outcome);
    native_input_recovery_.completed();
    return Json{
        Json::Object{{"schema", 1},
                     {"status", "dispatched"},
                     {"verified", false},
                     {"text_verified", true},
                     {"evidence_epoch", static_cast<std::int64_t>(desktop_.actions->epoch() + 1)},
                     {"verification_method", "roi_ocr_exact"},
                     {"input_released", true},
                     {"side_effect_may_have_occurred", true},
                     {"verified_bytes", std::int64_t(outcome.verified_bytes)},
                     {"internal_actions", std::int64_t(outcome.actions)},
                     {"roi_ocr_calls", std::int64_t(outcome.reads)},
                     {"candidate_pages_advanced", std::int64_t(outcome.pages)}}};
}
} // namespace mirage::integration::detail
