#include "desktop_image_supply.hpp"
#include "desktop_stability_guard.hpp"
#include "tool_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
std::string field(const Json &v, const char *key) {
    const auto *p = v.find(key);
    return p && p->as_string() ? *p->as_string() : "";
}
mira::Error rejected(std::string code, bool effects = false, bool released = true,
                     const std::string &refreshed_scope = {}) {
    mira::Error e;
    e.code = code == "cancelled"           ? mira::ErrorCode::Cancelled
             : code == "permission_denied" ? mira::ErrorCode::PermissionDenied
                                           : mira::ErrorCode::PlatformError;
    e.domain = "mirage.desktop.shared";
    Json::Object fields{{"error_code", code.substr(0, 64)},
                        {"side_effect_may_have_occurred", effects},
                        {"input_released", released},
                        {"verified", false}};
    if (!refreshed_scope.empty())
        fields.emplace_back("refreshed_image_scope", refreshed_scope);
    e.safe_message = mira::to_json_string(Json{std::move(fields)});
    return e;
}
} // namespace
void DesktopImageTurn::register_shared_input(mira::BuiltinToolRegistry &registry,
                                             const DesktopAtomToolset::Decorator &decorate) {
    for (const auto *operation : {"click", "key", "type_text", "scroll"}) {
        mira::BuiltinToolSpec spec;
        spec.wire_name = std::string("desktop.session.") + operation;
        spec.has_side_effects = true;
        const std::string use =
            std::string_view(operation) == "click"
                ? "Click a visible control, or a clearly non-interactive area "
                  "of the intended window to focus it; no window API is needed. "
            : std::string_view(operation) == "key"
                ? "Send one key chord to the focused UI, such as ctrl+l or enter. "
            : std::string_view(operation) == "type_text"
                ? "Type text into the focused field after confirming focus. "
                : "Scroll a visible UI region to reveal more content. ";
        spec.description =
            use +
            "Wayland complete input on a system-authorized interactive shared monitor. "
            "Requires latest supplied image_scope and environment_epoch; x/y/width/height identify "
            "the target rectangle in IMAGE pixels, revalidated from fresh stream pixels before "
            "input. Every capture or OCR refresh replaces the previous image_scope; use the "
            "latest actionable image, including for key and type_text. "
            "click/scroll use rectangle center. key uses key_name (e.g. ctrl+l, enter); type_text "
            "uses text (printable ASCII, <=4096 bytes, no controls; arbitrary UTF-8 returns "
            "text_transport_unavailable before dispatch on this legacy Portal transport); scroll "
            "uses "
            "vertical_steps/horizontal_steps "
            "(-20..20). No held keys. verify_kind=shared_frame_observed proves released input and "
            "fresh frame only, or shared_text_exists with expected_text checks same-stream OCR. "
            "Neither proves the full task nor focus identity; inspect the new image before "
            "continuing. "
            "A target_pixels_changed rejection already supplies the fresh frame with a new "
            "image_scope in its receipt: retry against that scope without recapturing. "
            "Do not retry uncertain effects.";
        Json::Object props;
        for (const auto *key : {"image_scope", "key_name", "text", "button", "expected_text"})
            props.emplace_back(key, Json{Json::Object{{"type", "string"}, {"maxLength", 4096}}});
        props.emplace_back("verify_kind",
                           Json{Json::Object{{"type", "string"},
                                             {"enum", Json{Json::Array{"shared_frame_observed",
                                                                       "shared_text_exists"}}}}});
        for (const auto *key : {"environment_epoch", "x", "y", "width", "height", "vertical_steps",
                                "horizontal_steps"})
            props.emplace_back(key, Json{Json::Object{{"type", "integer"}}});
        spec.parameters_schema = mira::JsonSchema{Json{
            Json::Object{{"type", "object"},
                         {"properties", Json{std::move(props)}},
                         {"required", Json{Json::Array{"image_scope", "environment_epoch", "x", "y",
                                                       "width", "height", "verify_kind"}}},
                         {"additionalProperties", false}}}};
        const std::string name = operation;
        mira::BuiltinToolHandler handler = [this, name](const auto &args, const auto &ctx) {
            return shared_input(name, args, ctx);
        };
        if (decorate)
            handler = decorate(spec.wire_name, std::move(handler));
        if (!registry.register_tool(std::move(spec), std::move(handler)))
            throw std::runtime_error("shared input tool registration failed");
    }
}

mira::Result<Json> DesktopImageTurn::shared_input(const std::string &operation, const Json &args,
                                                  const mira::OperationContext &ctx) {
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return rejected("owner_mismatch");
    if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()))
        return rejected("cancelled");
    if (!native_session_ || !native_session_->input_supported() || !native_input_frame_ ||
        !image_ || !supplied_image_)
        return rejected("interactive_image_required");
    if (field(args, "image_scope") != field(metadata_, "image_scope"))
        return rejected("stale_image_scope");
    const auto integer = [&](const char *key) -> std::optional<std::int64_t> {
        const auto *v = args.find(key);
        return v ? v->as_integer() : std::nullopt;
    };
    const auto epoch = integer("environment_epoch"), x = integer("x"), y = integer("y"),
               w = integer("width"), h = integer("height");
    if (!epoch || *epoch <= 0 || static_cast<std::uint64_t>(*epoch) != epoch_ ||
        epoch_ != desktop_.actions->epoch() || !x || !y || !w || !h || *x < 0 || *y < 0 ||
        *w <= 0 || *h <= 0 || *x > 16384 || *y > 16384 || *w > 16384 || *h > 16384 ||
        !native_frame_ || *x + *w > native_frame_->width || *y + *h > native_frame_->height ||
        std::chrono::steady_clock::now() - captured_ >= std::chrono::seconds(60))
        return rejected("stale_or_invalid_target");
    const auto kind = field(args, "verify_kind");
    if ((kind != "shared_frame_observed" && kind != "shared_text_exists") ||
        (kind == "shared_text_exists" &&
         (!desktop_.ocr_image || field(args, "expected_text").empty())))
        return rejected("postcondition_unavailable");
    // Validate payloads BEFORE any fresh acquisition or permission UI.
    desktop::SharedInputAction action;
    using Kind = desktop::SharedInputAction::Kind;
    action.kind = operation == "click"       ? Kind::Click
                  : operation == "key"       ? Kind::Key
                  : operation == "type_text" ? Kind::Text
                                             : Kind::Scroll;
    action.value = operation == "click" ? field(args, "button")
                   : operation == "key" ? field(args, "key_name")
                                        : field(args, "text");
    if (operation == "click" && action.value.empty())
        action.value = "left";
    if ((operation == "click" && action.value != "left" && action.value != "middle" &&
         action.value != "right") ||
        (operation == "key" && !desktop::is_valid_key_name(action.value)) ||
        (operation == "type_text" && (action.value.empty() || action.value.size() > 4096 ||
                                      !desktop::is_valid_utf8(action.value))))
        return rejected("invalid_argument");
    if (operation == "type_text")
        for (unsigned char ch : action.value)
            if (ch < 32 || ch == 127)
                return rejected("invalid_argument");
    if (operation == "type_text")
        for (unsigned char ch : action.value)
            if (ch >= 128)
                return rejected("text_transport_unavailable");
    if (operation == "scroll") {
        const auto v = integer("vertical_steps"), horizontal = integer("horizontal_steps");
        if ((!v && !horizontal) || (args.find("vertical_steps") && !v) ||
            (args.find("horizontal_steps") && !horizontal) || v.value_or(0) < -20 ||
            v.value_or(0) > 20 || horizontal.value_or(0) < -20 || horizontal.value_or(0) > 20 ||
            (!v.value_or(0) && !horizontal.value_or(0)))
            return rejected("invalid_argument");
        action.vertical_steps = static_cast<int>(v.value_or(0));
        action.horizontal_steps = static_cast<int>(horizontal.value_or(0));
    }
    const auto authority = native_authority_;
    const auto deadline =
        std::min(ctx.deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds(5)),
                 std::chrono::steady_clock::now() + std::chrono::seconds(5));
    const desktop::CancelToken bounded([this, authority, deadline] {
        return cancel_.cancelled() || desktop_.actions->authority_epoch() != authority ||
               std::chrono::steady_clock::now() >= deadline;
    });
    const auto allowed = [&](const char *capability) {
        return desktop_.authorize
                   ? desktop_.authorize(
                         capability, "desktop", [&] { return bounded.cancelled(); },
                         ctx.task.to_string(), ctx.operation.to_string())
                   : desktop_.gate(capability, "desktop", [&] { return bounded.cancelled(); });
    };
    if (!allowed("input.inject") || !allowed("screen.capture"))
        return rejected("permission_denied");
    if (bounded.cancelled())
        return rejected("cancelled");
    if (captures_ >= 31)
        return rejected("shared_frame_budget_exhausted"); // reserve postcondition frame
    ++captures_;
    desktop::CaptureOutcome fresh;
    try {
        fresh = native_session_->capture({desktop_capture_bytes}, bounded);
    } catch (...) {
        native_session_.reset();
        native_failed_ = true;
        return rejected("capture_failed");
    }
    if (!fresh.ok || !valid_image_frame(fresh.frame, desktop_capture_bytes)) {
        native_session_.reset();
        native_failed_ = true;
        if (fresh.error.code == "cancelled" && !cancel_.cancelled() &&
            desktop_.actions->authority_epoch() == authority &&
            !ctx.cancelled_or_expired(mira::Timestamp::now()) &&
            std::chrono::steady_clock::now() >= deadline)
            return rejected("input_pre_capture_timeout");
        return rejected(fresh.error.code.empty() ? "capture_failed" : fresh.error.code);
    }
    const auto &old = *native_input_frame_;
    if (!valid_image_frame(old, desktop_capture_bytes) || old.width != fresh.frame.width ||
        old.height != fresh.frame.height)
        return rejected("shared_source_changed");
    // M7-18: image pixels map into shared-monitor pixels through the published
    // origin and per-image scale when present (ROI and window crops); full
    // desktop images predate that metadata and keep the frame-ratio mapping.
    const auto metadata_int = [&](const char *key) -> std::optional<std::int64_t> {
        const auto *v = metadata_.find(key);
        return v && v->as_integer() ? std::optional<std::int64_t>{*v->as_integer()} : std::nullopt;
    };
    const std::int64_t num_x = metadata_int("desktop_per_image_x_numerator").value_or(old.width);
    const std::int64_t den_x =
        metadata_int("desktop_per_image_x_denominator").value_or(native_frame_->width);
    const std::int64_t num_y = metadata_int("desktop_per_image_y_numerator").value_or(old.height);
    const std::int64_t den_y =
        metadata_int("desktop_per_image_y_denominator").value_or(native_frame_->height);
    const std::int64_t origin_x = metadata_int("origin_x").value_or(0);
    const std::int64_t origin_y = metadata_int("origin_y").value_or(0);
    if (den_x <= 0 || den_y <= 0)
        return rejected("stale_or_invalid_target");
    const auto to_monitor_x = [&](std::int64_t image_x) {
        return origin_x + std::int64_t(double(image_x) * double(num_x) / double(den_x));
    };
    const auto to_monitor_y = [&](std::int64_t image_y) {
        return origin_y + std::int64_t(double(image_y) * double(num_y) / double(den_y));
    };
    const std::int64_t monitor_left = to_monitor_x(*x);
    const std::int64_t monitor_top = to_monitor_y(*y);
    const std::int64_t monitor_right = to_monitor_x(*x + *w);
    const std::int64_t monitor_bottom = to_monitor_y(*y + *h);
    if (monitor_left < 0 || monitor_top < 0 || monitor_right > old.width ||
        monitor_bottom > old.height || monitor_right - monitor_left <= 0 ||
        monitor_bottom - monitor_top <= 0)
        return rejected("stale_or_invalid_target");
    const int left = static_cast<int>(monitor_left);
    const int top = static_cast<int>(monitor_top);
    const int right = static_cast<int>(monitor_right);
    const int bottom = static_cast<int>(monitor_bottom);
    if (bounded.cancelled())
        return rejected("cancelled");
    // M7-16 / DEC-082: the target must be stable, not byte-identical. Edge
    // animation and same-subject noise (caret, launch pulse) proceed; a moved
    // or replaced subject, a global scene change, or any unprovable comparison
    // rejects exactly like the previous byte comparison.
    if (assess_target_stability(old, fresh.frame, {left, top, right, bottom}) ==
        TargetStability::kChanged) {
        // M7-19: the guard's frame is already a fresh observation — publish it
        // so the model retries against current pixels without a recapture
        // round trip. The receipt carries the new image_scope.
        std::string refreshed;
        if (native_input_enabled_ && !bounded.cancelled()) {
            if (finish_native_publish(
                    fresh.frame, fresh.frame,
                    desktop::WindowGeometry{0, 0, fresh.frame.width, fresh.frame.height}, "desktop",
                    "", fresh.frame.source_sequence, epoch_)) {
                const auto *scope = metadata_.find("image_scope");
                if (scope && scope->as_string())
                    refreshed = *scope->as_string();
            }
        }
        return rejected("target_pixels_changed", false, true, refreshed);
    }
    action.x = double(to_monitor_x(*x + *w / 2));
    action.y = double(to_monitor_y(*y + *h / 2));
    action.image_width = old.width;
    action.image_height = old.height;
    auto lease = desktop_.actions->try_begin(bounded, epoch_);
    if (!lease)
        return rejected(lease.error().code);
    desktop::InputLimits limits;
    limits.max_text_bytes = 4096;
    desktop::InputOutcome out;
    try {
        out = native_session_->input(action, limits, bounded);
    } catch (...) {
        out = {false, false, {"provider_exception", "Input failed"}, true, false};
    }
    if (!out.input_released)
        lease.release_failed();
    if (!out.ok) {
        if (out.side_effect_may_have_occurred) {
            native_session_.reset();
            native_failed_ = true;
        }
        return rejected(out.error.code, out.side_effect_may_have_occurred, out.input_released);
    }
    // DEC-081: only a later source frame can verify this dispatched action.
    native_newer_than_sequence_ = fresh.frame.source_sequence;
    return Json{Json::Object{{"schema", 2},
                             {"status", "dispatched"},
                             {"verified", false},
                             {"side_effect_may_have_occurred", true},
                             {"input_released", true}}};
}

mira::Result<Json> DesktopImageTurn::observe_shared(const std::string &kind,
                                                    const std::string &expected,
                                                    const mira::OperationContext &ctx) {
    if (!native_session_ || !native_session_->input_supported())
        return rejected("shared_session_unavailable");
    auto captured = capture(Json{Json::Object{{"source", "desktop"}, {"interaction", true}}}, ctx);
    if (!captured || !image_ || !native_frame_)
        return captured ? rejected("image_unavailable") : captured.error();
    bool matched = kind == "shared_frame_observed";
    if (kind == "shared_text_exists") {
        if (!desktop_.ocr_image)
            return rejected("ocr_unavailable");
        const auto authority = native_authority_;
        const desktop::CancelToken bounded([this, ctx, authority] {
            return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                   desktop_.actions->authority_epoch() != authority;
        });
        auto read = desktop_.ocr_image(*native_frame_, bounded, ctx.deadline);
        if (!read.ok || read.cancelled || bounded.cancelled() || read.regions.size() > 128)
            return rejected("shared_ocr_failed");
        for (const auto &region : read.regions) {
            if (region.text.size() > 1024 || !desktop::is_valid_utf8(region.text) ||
                !std::isfinite(region.confidence) || region.confidence < 0 ||
                region.confidence > 1 || !std::isfinite(region.x) || !std::isfinite(region.y) ||
                !std::isfinite(region.width) || !std::isfinite(region.height) || region.x < 0 ||
                region.y < 0 || region.width <= 0 || region.height <= 0 ||
                region.x + region.width > native_frame_->width ||
                region.y + region.height > native_frame_->height)
                return rejected("shared_ocr_invalid");
        }
        matched = std::any_of(read.regions.begin(), read.regions.end(), [&](const auto &region) {
            return region.text == expected && region.confidence >= 0.8;
        });
    }
    return Json{Json::Object{{"matched", matched}, {"digest", image_->digest.to_string()}}};
}
} // namespace mirage::integration::detail
