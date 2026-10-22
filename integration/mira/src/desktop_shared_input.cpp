#include "desktop_action_verification.hpp"
#include "desktop_image_supply.hpp"
#include "desktop_stability_guard.hpp"
#include "tool_click.hpp"
#include "tool_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mirage/desktop/input_cadence.hpp>
#include <mirage/desktop/input_session.hpp>
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
                     const std::string &refreshed_scope = {}, const std::string &invalid_field = {},
                     const std::string &hint = {}, const std::string &message = {}) {
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
    if (!invalid_field.empty())
        fields.emplace_back("field", invalid_field);
    if (!hint.empty())
        fields.emplace_back("hint", hint);
    if (!message.empty())
        fields.emplace_back("message", message);
    e.safe_message = mira::to_json_string(Json{std::move(fields)});
    return e;
}
} // namespace
void DesktopImageTurn::register_shared_input(mira::BuiltinToolRegistry &registry,
                                             const DesktopAtomToolset::Decorator &decorate) {
    for (const auto *operation : {"click", "key", "type_text", "scroll", "drag"}) {
        mira::BuiltinToolSpec spec;
        spec.wire_name = std::string("desktop.session.") + operation;
        spec.version = {4, 0, 0};
        if (std::string_view(operation) == "click" || std::string_view(operation) == "key")
            spec.version = {4, 1, 0};
        spec.has_side_effects = true;
        const std::string use =
            std::string_view(operation) == "click"
                ? "Click a visible control, or a clearly non-interactive area "
                  "of the intended window to focus it; no window API is needed. "
                  "click_count sets consecutive clicks in one action (default 1); "
                  "use 2 to OPEN an item that a single click only selects, 3 for triple-click. "
                  "Do not approximate a double-click with separate tool calls. "
                  "Automatically approaches the target "
                  "along a locally timed minimum-jerk path, then checks fresh target pixels before "
                  "pressing. No model timing parameter. Requires a fresh same-stream cursor "
                  "origin; "
                  "pointer_origin_unknown refuses without clicking. "
            : std::string_view(operation) == "key"
                ? "Send one key chord to the focused UI, such as ctrl+l or enter. "
            : std::string_view(operation) == "type_text"
                ? "Type text into the focused field after confirming focus. "
            : std::string_view(operation) == "drag"
                ? "Complete locally paced curved drag between source rectangle center and "
                  "destination_x/y/width/height rectangle center in the same shared image. "
                  "Max 2048 desktop pixels; automatically approach and revalidate before pressing. "
                : "Scroll a visible UI region to reveal more content. ";
        spec.description =
            use +
            "Wayland complete input on a system-authorized interactive shared monitor. "
            "Requires latest supplied image_scope and environment_epoch; x/y/width/height identify "
            "the target rectangle in IMAGE pixels, revalidated from fresh stream pixels before "
            "input. Rectangle must fit the supplied image: x+width<=image width, "
            "y+height<=image height; source dimensions are not action-image bounds. "
            "Every capture or OCR refresh replaces the previous image_scope; use the "
            "latest actionable image, including for key and type_text. "
            "click/scroll use rectangle center. key uses canonical lowercase key_name (e.g. "
            "ctrl+l, enter, shift; bare ctrl/alt/shift/meta tap and release; meta+ for "
            "Super chords); type_text "
            "uses text (printable ASCII, <=4096 bytes, no controls; arbitrary UTF-8 returns "
            "text_transport_unavailable before dispatch on this legacy Portal transport); scroll "
            "uses "
            "vertical_steps/horizontal_steps "
            "(default 0; positive right/down, negative left/up; total absolute steps 1..16). "
            "click/drag use button=left/middle/right (default left). Scroll and drag approach "
            "and revalidate the target first. Wheel steps, drag motion and key transitions "
            "use local natural timing; text is typed character by character. No timing arguments "
            "or held keys. Click/key accept press=long for one locally timed 1080..1240 ms "
            "complete press (click_count must be 1); default normal is unchanged. No duration "
            "parameter or cross-call hold; release completes before the result/verification. "
            "Text has a 15 second budget; split longer text. "
            "verify_kind=shared_frame_observed proves released input and "
            "fresh frame only, or shared_text_exists with expected_text checks same-stream OCR. "
            "verify_kind is optional. Without it, no post-action frame or OCR is captured; "
            "the receipt reports execution only. Capture at the next state dependency or "
            "task boundary to obtain a fresh actionable image_scope. Requested evidence "
            "does not prove the full task or focus identity. "
            "A target_pixels_changed rejection already supplies the fresh frame with a new "
            "image_scope in its receipt: retry against that scope without recapturing. "
            "Do not retry uncertain effects.";
        Json::Object props;
        if (std::string_view(operation) == "click" || std::string_view(operation) == "key")
            props.emplace_back(
                "press",
                Json{Json::Object{{"type", "string"}, {"enum", Json::Array{"normal", "long"}}}});
        if (std::string_view(operation) == "click") {
            props.emplace_back("click_count", click_count_schema());
        }
        if (std::string_view(operation) == "key")
            props.emplace_back("key_name",
                               Json{Json::Object{{"type", "string"}, {"maxLength", 4096}}});
        if (std::string_view(operation) == "type_text")
            props.emplace_back("text", Json{Json::Object{{"type", "string"}, {"maxLength", 4096}}});
        for (const auto *key : {"image_scope", "expected_text"})
            props.emplace_back(key, Json{Json::Object{{"type", "string"}, {"maxLength", 4096}}});
        if (std::string_view(operation) == "click" || std::string_view(operation) == "drag")
            props.emplace_back("button", button_schema());
        if (std::string_view(operation) == "drag")
            for (const auto *key :
                 {"destination_x", "destination_y", "destination_width", "destination_height"})
                props.emplace_back(
                    key,
                    Json{Json::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 16384}}});
        props.emplace_back("verify_kind",
                           Json{Json::Object{{"type", "string"},
                                             {"enum", Json{Json::Array{"shared_frame_observed",
                                                                       "shared_text_exists"}}}}});
        if (std::string_view(operation) == "scroll")
            for (const auto *key : {"vertical_steps", "horizontal_steps"})
                props.emplace_back(
                    key,
                    Json{Json::Object{
                        {"type", "integer"}, {"minimum", -16}, {"maximum", 16}, {"default", 0}}});
        for (const auto *key : {"environment_epoch", "x", "y", "width", "height"})
            props.emplace_back(key, Json{Json::Object{{"type", "integer"}}});
        Json::Array required{"image_scope", "environment_epoch", "x", "y", "width", "height"};
        if (std::string_view(operation) == "drag")
            for (const auto *key :
                 {"destination_x", "destination_y", "destination_width", "destination_height"})
                required.emplace_back(key);
        spec.parameters_schema =
            mira::JsonSchema{Json{Json::Object{{"type", "object"},
                                               {"properties", Json{std::move(props)}},
                                               {"required", Json{std::move(required)}},
                                               {"additionalProperties", false}}}};
        const std::string name = operation;
        mira::BuiltinToolHandler handler = [this, name](const auto &args, const auto &ctx) {
            return shared_input(name, args, ctx);
        };
        if (decorate) {
            DesktopActionVerification::configure_policy(spec);
            handler = decorate(spec.wire_name, std::move(handler));
        }
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
    if (native_input_fenced_)
        return rejected("input_fenced", false, true, {}, {},
                        "Human review required; only read-only observation remains available.");
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
    if (!epoch || *epoch <= 0)
        return rejected("invalid_argument", false, true, {}, "environment_epoch");
    if (static_cast<std::uint64_t>(*epoch) != epoch_ || epoch_ != desktop_.actions->epoch() ||
        std::chrono::steady_clock::now() - captured_ >= std::chrono::seconds(60))
        return rejected("stale_observation", false, true, {}, "environment_epoch",
                        "Use the latest supplied image scope and epoch; refresh only if expired.");
    if (!native_frame_)
        return rejected("interactive_image_required");
    if (!x || !y || !w || !h || *x < 0 || *y < 0 || *w <= 0 || *h <= 0 || *x > 16384 ||
        *y > 16384 || *w > 16384 || *h > 16384)
        return rejected("invalid_argument", false, true, {}, "target",
                        "Use nonnegative x/y and positive width/height in current IMAGE pixels.");
    const auto invalid_bounds = [&](const char *field_name) {
        return rejected("invalid_target_bounds", false, true, {}, field_name,
                        "Fit rectangle in current " + std::to_string(native_frame_->width) + "x" +
                            std::to_string(native_frame_->height) +
                            " IMAGE pixels. Correct coordinates; recapture alone will not help.",
                        "Target rectangle exceeds supplied image.");
    };
    if (*x + *w > native_frame_->width || *y + *h > native_frame_->height)
        return invalid_bounds("target");
    const auto kind = field(args, "verify_kind");
    if ((!kind.empty() && kind != "shared_frame_observed" && kind != "shared_text_exists") ||
        (kind == "shared_text_exists" &&
         (!desktop_.ocr_image || field(args, "expected_text").empty())))
        return rejected("postcondition_unavailable");
    // Validate payloads BEFORE any fresh acquisition or permission UI.
    const bool drag = operation == "drag";
    const auto clicks = pointer_click_count(args);
    const auto button = pointer_button(args);
    if (!clicks || !button || (operation != "key" && args.find("key_name")) ||
        (operation != "type_text" && args.find("text")) ||
        (operation != "click" && args.find("click_count")) || args.find("glide_ms") ||
        (operation != "click" && !drag && args.find("button")) ||
        (operation != "scroll" && (args.find("horizontal_steps") || args.find("vertical_steps"))))
        return rejected("invalid_argument");
    const auto press = field(args, "press");
    if ((args.find("press") && (operation != "click" && operation != "key")) ||
        (args.find("press") && !args.find("press")->as_string()) ||
        (!press.empty() && press != "normal" && press != "long") ||
        (press == "long" && operation == "click" && *clicks != 1))
        return rejected("invalid_argument");
    desktop::SharedInputAction action;
    action.long_press = press == "long";
    if (action.long_press && !native_session_->timed_press_supported())
        return rejected(
            "timed_press_unavailable", false, true, {}, "press",
            "This shared backend cannot long press; use normal input or another supported route.");
    action.click_count = *clicks;
    using Kind = desktop::SharedInputAction::Kind;
    action.kind = operation == "click"       ? Kind::Click
                  : operation == "key"       ? Kind::Key
                  : operation == "type_text" ? Kind::Text
                  : drag                     ? Kind::Drag
                                             : Kind::Scroll;
    action.value = (operation == "click" || drag) ? *button
                   : operation == "key"           ? field(args, "key_name")
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
        const auto v = wheel_steps(args, "vertical_steps"),
                   horizontal = wheel_steps(args, "horizontal_steps");
        if (!v || !horizontal || !desktop::valid_scroll_steps(*horizontal, *v))
            return rejected("invalid_argument");
        action.vertical_steps = *v;
        action.horizontal_steps = *horizontal;
    }
    auto dx = x, dy = y, dw = w, dh = h;
    if (drag) {
        dx = integer("destination_x");
        dy = integer("destination_y");
        dw = integer("destination_width");
        dh = integer("destination_height");
        if (!dx || !dy || !dw || !dh || *dx < 0 || *dy < 0 || *dw <= 0 || *dh <= 0 || *dx > 16384 ||
            *dy > 16384 || *dw > 16384 || *dh > 16384)
            return rejected("invalid_argument", false, true, {}, "destination");
        if (*dx + *dw > native_frame_->width || *dy + *dh > native_frame_->height)
            return invalid_bounds("destination");
    }
    for (const auto *key :
         {"destination_x", "destination_y", "destination_width", "destination_height"})
        if (!drag && args.find(key))
            return rejected("invalid_argument");
    const auto authority = native_authority_;
    const auto cap = operation == "type_text" ? desktop::input_cadence_policy::text_timeout
                                              : std::chrono::milliseconds{5000};
    const auto deadline = std::min(ctx.deadline.value_or(std::chrono::steady_clock::now() + cap),
                                   std::chrono::steady_clock::now() + cap);
    const desktop::CancelToken bounded([this, ctx, authority, deadline] {
        return cancel_.cancelled() || desktop_.actions->authority_epoch() != authority ||
               ctx.cancelled_or_expired(mira::Timestamp::now()) ||
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
    desktop::CaptureOutcome fresh;
    try {
        fresh = measure_phase(timings_ ? &timings_->capture : nullptr, [&] {
            return native_session_->capture_presented({desktop_capture_bytes}, bounded,
                                                      desktop_.exclude_pet);
        });
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
    const std::int64_t monitor_left = to_monitor_x(std::min(*x, *dx));
    const std::int64_t monitor_top = to_monitor_y(std::min(*y, *dy));
    const std::int64_t monitor_right = to_monitor_x(std::max(*x + *w, *dx + *dw));
    const std::int64_t monitor_bottom = to_monitor_y(std::max(*y + *h, *dy + *dh));
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
                native_capture_args_ =
                    Json{Json::Object{{"source", "desktop"}, {"interaction", true}}};
                native_capture_key_ =
                    mira::canonical_json_digest(Json{Json::Object{{"source", "desktop"}}})
                        .to_string();
                if (fresh.presentation_excluded == false)
                    metadata_.set("pet_presentation",
                                  Json{Json::Object{{"excluded", false},
                                                    {"reason", "unsupported_compositor"},
                                                    {"pixels_may_include_pet", true}}});
                const auto *scope = metadata_.find("image_scope");
                if (scope && scope->as_string())
                    refreshed = *scope->as_string();
            }
        }
        return rejected("target_pixels_changed", false, true, refreshed);
    }
    action.x = double(to_monitor_x(*x + *w / 2));
    action.y = double(to_monitor_y(*y + *h / 2));
    action.destination_x = double(to_monitor_x(*dx + *dw / 2));
    action.destination_y = double(to_monitor_y(*dy + *dh / 2));
    if (drag && !desktop::valid_drag_path(
                    {static_cast<std::int32_t>(action.x), static_cast<std::int32_t>(action.y)},
                    {static_cast<std::int32_t>(action.destination_x),
                     static_cast<std::int32_t>(action.destination_y)}))
        return rejected("invalid_argument");
    action.image_width = old.width;
    action.image_height = old.height;
    const bool pointer_action = operation == "click" || operation == "scroll" || drag;
    if (pointer_action) {
        // Availability is a read-only preflight. Do not retire the current
        // image/epoch merely because the stream lacks cursor metadata.
        try {
            const auto origin = native_session_->pointer_position(bounded);
            if (!origin.ok)
                return rejected(origin.cancelled ? "cancelled" : "pointer_origin_unknown");
            if (origin.image_width != fresh.frame.width ||
                origin.image_height != fresh.frame.height ||
                origin.source_sequence != fresh.frame.source_sequence || origin.position.x < 0 ||
                origin.position.y < 0 || origin.position.x >= origin.image_width ||
                origin.position.y >= origin.image_height)
                return rejected("pointer_origin_unknown");
        } catch (...) {
            return rejected("pointer_query_failed");
        }
    }
    auto lease = desktop_.actions->try_begin(bounded, epoch_);
    if (!lease)
        return rejected(lease.error().code);
    desktop::InputLimits limits;
    limits.max_text_bytes = 4096;
    const auto action_cap = operation == "type_text"
                                ? desktop::input_cadence_policy::text_timeout
                                : desktop::input_cadence_policy::action_timeout;
    const auto execution_end = std::min(deadline, std::chrono::steady_clock::now() + action_cap);
    limits.timeout = std::min(action_cap, std::chrono::duration_cast<std::chrono::milliseconds>(
                                              execution_end - std::chrono::steady_clock::now()));
    desktop::InputOutcome out;
    const auto input_failure = [&](const desktop::InputOutcome &failed) {
        if (failed.side_effect_may_have_occurred) {
            if (failed.error.code == "input_lag" && failed.input_released) {
                native_input_fenced_ = true;
                metadata_.set("input_actionable", false);
                metadata_.set("input_blocked_reason", "input_lag_requires_human_review");
                native_newer_than_sequence_ = fresh.frame.source_sequence;
            } else {
                native_session_.reset();
                native_failed_ = true;
            }
        }
        return rejected(
            failed.error.code, failed.side_effect_may_have_occurred, failed.input_released, {}, {},
            native_input_fenced_ ? "Human review required; sharing is observation-only." : "",
            failed.error.message);
    };
    try {
        bool moved = false;
        if (pointer_action) {
            auto approach = action;
            approach.kind = Kind::Move;
            approach.long_press = false;
            approach.click_count = 1;
            approach.target_width =
                static_cast<std::int32_t>(std::min(to_monitor_x(*x + *w) - to_monitor_x(*x),
                                                   to_monitor_y(*y + *h) - to_monitor_y(*y)));
            const auto travel = native_session_->input(approach, limits, bounded);
            if (!travel.input_released)
                lease.release_failed();
            if (!travel.ok)
                return input_failure(travel);
            moved = travel.side_effect_may_have_occurred;
            if (moved) {
                const auto arrived = measure_phase(timings_ ? &timings_->capture : nullptr, [&] {
                    return native_session_->capture_presented(
                        {desktop_capture_bytes, fresh.frame.source_sequence}, bounded,
                        desktop_.exclude_pet);
                });
                if (!arrived.ok || !valid_image_frame(arrived.frame, desktop_capture_bytes) ||
                    arrived.frame.width != old.width || arrived.frame.height != old.height ||
                    arrived.frame.source_sequence <= fresh.frame.source_sequence ||
                    bounded.cancelled() ||
                    assess_target_stability(old, arrived.frame, {left, top, right, bottom}) ==
                        TargetStability::kChanged)
                    return rejected(bounded.cancelled() ? "cancelled" : "arrival_target_changed",
                                    moved);
                fresh = arrived;
            }
        }
        limits.timeout =
            std::min(action_cap, std::chrono::duration_cast<std::chrono::milliseconds>(
                                     execution_end - std::chrono::steady_clock::now()));
        out = native_session_->input(action, limits, bounded);
        out.side_effect_may_have_occurred |= moved;
    } catch (...) {
        out = {false, false, {"provider_exception", "Input failed"}, true, false};
    }
    if (!out.input_released)
        lease.release_failed();
    if (!out.ok) {
        return input_failure(out);
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
        auto read = measure_phase(timings_ ? &timings_->ocr : nullptr, [&] {
            return desktop_.ocr_image(*native_frame_, bounded, ctx.deadline);
        });
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
    return Json{Json::Object{
        {"matched", matched}, {"digest", image_->digest.to_string()}, {"observation", metadata_}}};
}
} // namespace mirage::integration::detail
