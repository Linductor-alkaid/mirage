#include "desktop_image_supply.hpp"
#include "tool_click.hpp"
#include "tool_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <mirador/transform.hpp>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
using Geometry = desktop::WindowGeometry;
std::string field(const Json &v, const char *key) {
    const auto *p = v.find(key);
    return p && p->as_string() ? *p->as_string() : "";
}
mira::Error failure(const std::string &code, bool effect = false, bool released = true) {
    mira::Error e;
    e.code = code == "cancelled"           ? mira::ErrorCode::Cancelled
             : code == "permission_denied" ? mira::ErrorCode::PermissionDenied
                                           : mira::ErrorCode::PlatformError;
    e.domain = "mirage.desktop.region";
    e.safe_message =
        mira::to_json_string(Json{Json::Object{{"code", code},
                                               {"side_effect_may_have_occurred", effect},
                                               {"input_released", released},
                                               {"verified", false}}});
    if (e.safe_message.size() > 512)
        e.safe_message =
            R"({"code":"provider_error","side_effect_may_have_occurred":true,"input_released":false,"verified":false})";
    return e;
}
Json string_schema() { return Json{Json::Object{{"type", "string"}, {"maxLength", 1024}}}; }
} // namespace

void DesktopImageTurn::register_region_tool(mira::BuiltinToolRegistry &registry,
                                            const DesktopAtomToolset::Decorator &decorate,
                                            bool mutations) {
    auto *input = desktop_.environment->input();
    if (!mutations || !input || !input->complete_actions_supported() ||
        !desktop_.environment->window())
        return;
    for (const auto *operation : {"click", "scroll", "drag"}) {
        const bool click = std::string_view(operation) == "click";
        const bool drag = std::string_view(operation) == "drag";
        if (!click && !input->bounded_pointer_actions_supported())
            continue;
        mira::BuiltinToolSpec spec;
        spec.wire_name = std::string("desktop.vision.") + operation + "_region";
        spec.version = {1, 0, 0};
        spec.has_side_effects = true;
        spec.description =
            std::string(operation) +
            " at a visible IMAGE rectangle from the latest supplied screen.capture "
            "(region_actionable=true). Requires image_scope, environment_epoch and target_context. "
            "click accepts button=left/middle/right (default left) and click_count=1..32 (default "
            "1); "
            "scroll accepts horizontal_steps/vertical_steps (default 0), positive right/down, "
            "total "
            "absolute steps 1..16. drag accepts button and destination_x/y/width/height rectangle; "
            "complete 16-segment linear drag between rectangle centers, max 2048 desktop pixels. "
            "scroll/drag require target_context=window, window_id, both targets in the same "
            "focused window. "
            "click also permits target_context=display and display_id for desktop/panel/menu. "
            "Rechecks source geometry/focus and fresh pixels of both targets before input; "
            "max one second, no held-button continuation. Supplied intent expires after 60 "
            "seconds. "
            "Declare verify_kind before input; foreground_changed proves UI progress only. "
            "Inspect fresh postcondition evidence before reporting task success.";
        Json::Object props;
        if (click)
            props.emplace_back("click_count", click_count_schema());
        if (click || drag)
            props.emplace_back("button", button_schema());
        if (std::string_view(operation) == "scroll")
            for (const auto *key : {"horizontal_steps", "vertical_steps"})
                props.emplace_back(
                    key,
                    Json{Json::Object{
                        {"type", "integer"}, {"minimum", -16}, {"maximum", 16}, {"default", 0}}});
        if (drag)
            for (const auto *key :
                 {"destination_x", "destination_y", "destination_width", "destination_height"})
                props.emplace_back(
                    key,
                    Json{Json::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 65535}}});
        for (const auto *key :
             {"image_scope", "target_context", "window_id", "display_id", "verify_kind",
              "expected_title", "expected_role", "expected_name", "expected_text"})
            props.emplace_back(key, string_schema());
        for (const auto *key : {"x", "y", "width", "height"})
            props.emplace_back(
                key, Json{Json::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 65535}}});
        props.emplace_back(
            "environment_epoch",
            Json{Json::Object{{"type", "integer"}, {"minimum", 1}, {"maximum", INT64_MAX}}});
        Json::Array required{
            "image_scope", "environment_epoch", "target_context", "x", "y", "width",
            "height",      "verify_kind"};
        if (drag)
            for (const auto *key :
                 {"destination_x", "destination_y", "destination_width", "destination_height"})
                required.emplace_back(key);
        spec.parameters_schema =
            mira::JsonSchema{Json{Json::Object{{"type", "object"},
                                               {"properties", Json{std::move(props)}},
                                               {"required", Json{std::move(required)}},
                                               {"additionalProperties", false}}}};
        mira::BuiltinToolHandler handler =
            [this, operation = std::string(operation)](const auto &args, const auto &ctx) {
                try {
                    return pointer_region(args, ctx, operation);
                } catch (...) {
                    // Input exceptions are caught inside pointer_region and fence their lease.
                    return mira::Result<Json>{failure("observation_exception")};
                }
            };
        if (decorate)
            handler = decorate(spec.wire_name, std::move(handler));
        if (!registry.register_tool(std::move(spec), std::move(handler)))
            throw std::logic_error("region tool registration rejected");
    }
    region_tool_enabled_ = true;
}

mira::Result<Json> DesktopImageTurn::pointer_region(const Json &args,
                                                    const mira::OperationContext &ctx,
                                                    const std::string &operation) {
    const bool drag = operation == "drag", scroll = operation == "scroll";
    const auto clicks = pointer_click_count(args);
    const auto button = pointer_button(args);
    const auto horizontal = wheel_steps(args, "horizontal_steps");
    const auto vertical = wheel_steps(args, "vertical_steps");
    if (!clicks || !button || !horizontal || !vertical ||
        ((drag || scroll) && args.find("click_count")) || (scroll && args.find("button")) ||
        (!scroll && (args.find("horizontal_steps") || args.find("vertical_steps"))) ||
        (scroll && !desktop::valid_scroll_steps(*horizontal, *vertical)))
        return failure("invalid_argument");
    for (const auto *key :
         {"destination_x", "destination_y", "destination_width", "destination_height"})
        if (!drag && args.find(key))
            return failure("invalid_argument");
    const auto end =
        std::min(ctx.deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds{1}),
                 std::chrono::steady_clock::now() + std::chrono::seconds{1});
    const desktop::CancelToken bounded([this, ctx, end] {
        return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
               std::chrono::steady_clock::now() >= end;
    });
    if (bounded.cancelled())
        return failure("cancelled");
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return failure("owner_mismatch");
    const auto *claimed = args.find("environment_epoch");
    const auto current = [&] {
        return region_source_ && image_ && supplied_image_ && claimed && claimed->as_integer() &&
               *claimed->as_integer() > 0 &&
               static_cast<std::uint64_t>(*claimed->as_integer()) == epoch_ &&
               epoch_ == desktop_.actions->epoch() &&
               field(args, "image_scope") == region_source_->scope &&
               std::chrono::steady_clock::now() - captured_ < std::chrono::seconds{60};
    };
    if (!current())
        return failure("stale_observation");
    const auto &source = *region_source_;
    const auto map_box = [&](bool destination) -> std::optional<Geometry> {
        Geometry box;
        for (auto [key, value] :
             {std::pair{"x", &box.x}, std::pair{"y", &box.y}, std::pair{"width", &box.width},
              std::pair{"height", &box.height}}) {
            const auto *v = args.find(destination ? (std::string("destination_") + key) : key);
            if (!v || !v->as_integer() || *v->as_integer() < 0 || *v->as_integer() > 65535)
                return std::nullopt;
            *value = static_cast<std::int32_t>(*v->as_integer());
        }
        if (!geometry_contains({0, 0, source.image_width, source.image_height}, box))
            return std::nullopt;
        // Mirador owns the coordinate transform; rounding encloses the source pixel area.
        const auto transform =
            mirador::compose(mirador::make_scale(double(source.area.width) / source.image_width,
                                                 double(source.area.height) / source.image_height,
                                                 mirador::CoordinateSpaceId::kModelInput,
                                                 mirador::CoordinateSpaceId::kFrame),
                             mirador::make_translation(source.area.x, source.area.y,
                                                       mirador::CoordinateSpaceId::kFrame,
                                                       mirador::CoordinateSpaceId::kDisplay));
        if (!transform.ok())
            return std::nullopt;
        const auto mapped = mirador::transform_rect(
            transform.value(), {static_cast<float>(box.x), static_cast<float>(box.y),
                                static_cast<float>(box.width), static_cast<float>(box.height)});
        const auto left = static_cast<std::int32_t>(std::floor(mapped.x));
        const auto top = static_cast<std::int32_t>(std::floor(mapped.y));
        const Geometry area{left, top,
                            static_cast<std::int32_t>(std::ceil(mapped.x + mapped.width)) - left,
                            static_cast<std::int32_t>(std::ceil(mapped.y + mapped.height)) - top};
        if (!geometry_contains(source.area, area) ||
            std::uint64_t{static_cast<unsigned>(area.width)} * static_cast<unsigned>(area.height) >
                desktop_image_bytes / 4)
            return std::nullopt;
        return area;
    };
    const auto start = map_box(false);
    const auto destination = drag ? map_box(true) : start;
    if (!start || !destination)
        return failure("invalid_argument");
    const desktop::PointerPoint from{start->x + start->width / 2, start->y + start->height / 2};
    const desktop::PointerPoint to{destination->x + destination->width / 2,
                                   destination->y + destination->height / 2};
    if (drag && !desktop::valid_drag_path(from, to))
        return failure("invalid_argument");
    // Revalidate the entire corridor, including both endpoint rectangles, in one capture.
    const auto left = std::min(start->x, destination->x), top = std::min(start->y, destination->y);
    const Geometry area{
        left, top,
        static_cast<std::int32_t>(std::max(std::int64_t{start->x} + start->width,
                                           std::int64_t{destination->x} + destination->width) -
                                  left),
        static_cast<std::int32_t>(std::max(std::int64_t{start->y} + start->height,
                                           std::int64_t{destination->y} + destination->height) -
                                  top)};
    if (std::uint64_t(area.width) * area.height > desktop_image_bytes / 4)
        return failure("region_budget_exceeded");
    const auto context = field(args, "target_context");
    const auto id = field(args, "window_id");
    const auto display = field(args, "display_id");
    if (((drag || scroll) && context != "window") ||
        (context != "window" && context != "display") ||
        (context == "window" && (!source.front.found || id != source.front.window.id ||
                                 !geometry_contains(source.front.window.geometry, area))) ||
        (context == "display" && (!id.empty() || display != source.display.id)) ||
        (!desktop_.capture_scope.empty() &&
         (!source.front.found || source.front.window.id != desktop_.capture_scope ||
          !geometry_contains(source.front.window.geometry, area))))
        return failure("invalid_target_context");
    const auto authorize = [&](const std::string &capability, const std::string &resource) {
        const AtomCancelProbe probe = [&] { return bounded.cancelled(); };
        return desktop_.authorize
                   ? desktop_.authorize(capability, resource, probe, ctx.task.to_string(),
                                        ctx.operation.to_string())
                   : desktop_.gate && desktop_.gate(capability, resource, probe);
    };
    if (!authorize("input.inject", context == "window" ? id : "display:" + display) ||
        !authorize("screen.capture", "display:" + source.display.id))
        return failure(bounded.cancelled() ? "cancelled" : "permission_denied");
    if (!current() || bounded.cancelled())
        return failure(bounded.cancelled() ? "cancelled" : "stale_observation");
    auto lease = desktop_.actions->try_begin(bounded, epoch_);
    if (!lease)
        return failure(lease.error().code, false, lease.error().code != "input_release_failed");
    const auto dispatch_epoch = desktop_.actions->epoch();
    auto *window = desktop_.environment->window();
    auto *screen = desktop_.environment->screen();
    const auto stable = [&] {
        if (bounded.cancelled() || desktop_.actions->epoch() != dispatch_epoch)
            return false;
        const auto front = window->front_window(bounded);
        if (!front.ok || front.found != source.front.found ||
            (front.found &&
             (front.window.id != source.front.window.id ||
              front.window.title != source.front.window.title ||
              !geometry_equals(front.window.geometry, source.front.window.geometry))))
            return false;
        const auto displays = screen->list_displays({}, bounded);
        if (!displays.ok ||
            std::none_of(displays.displays.begin(), displays.displays.end(), [&](const auto &d) {
                return d.id == source.display.id &&
                       geometry_equals(d.geometry, source.display.geometry) &&
                       d.primary == source.display.primary;
            }))
            return false;
        if (!source.window.empty() && (!front.found || front.window.id != source.window))
            return false;
        return true;
    };
    if (!stable())
        return failure(bounded.cancelled() ? "cancelled" : "stale_observation");
    const auto fresh = measure_phase(timings_ ? &timings_->capture : nullptr,
                                     [&] { return screen->capture_roi(area, {}, bounded); });
    if (!fresh.ok || fresh.cancelled || bounded.cancelled())
        return failure(bounded.cancelled() || fresh.cancelled ? "cancelled" : "capture_failed");
    if (!valid_image_frame(source.frame, desktop_capture_bytes) ||
        !valid_image_frame(fresh.frame, desktop_image_bytes) || fresh.frame.width != area.width ||
        fresh.frame.height != area.height)
        return failure("invalid_frame");
    for (std::int32_t row = 0; row < area.height; ++row) {
        const auto offset =
            static_cast<std::size_t>(area.y - source.area.y + row) * source.frame.stride +
            static_cast<std::size_t>(area.x - source.area.x) * 4;
        const auto begin = source.frame.pixels.begin() + static_cast<std::ptrdiff_t>(offset);
        if (bounded.cancelled())
            return failure("cancelled");
        if (!std::equal(begin, begin + static_cast<std::ptrdiff_t>(area.width) * 4,
                        fresh.frame.pixels.begin() +
                            static_cast<std::ptrdiff_t>(row) * fresh.frame.stride))
            return failure("region_changed");
    }
    if (!stable() || std::chrono::steady_clock::now() - captured_ >= std::chrono::seconds{60})
        return failure(bounded.cancelled() ? "cancelled" : "stale_observation");
    desktop::InputLimits limits;
    limits.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - std::chrono::steady_clock::now());
    if (limits.timeout.count() <= 0)
        return failure("cancelled");
    desktop::InputOutcome outcome;
    try {
        auto *input = desktop_.environment->input();
        const desktop::PointerActionScope scope{source.front.window.id,
                                                source.front.window.geometry};
        if (drag)
            outcome = input->drag_with_button(scope, from, to, *button, limits, bounded);
        else if (scroll)
            outcome = input->scroll_at(scope, from, *horizontal, *vertical, limits, bounded);
        else
            outcome = input->click_sequence_at(from.x, from.y, *button, *clicks, limits, bounded);
    } catch (...) {
        lease.release_failed();
        return failure("provider_exception", true, false);
    }
    if (!outcome.input_released) {
        lease.release_failed();
        return failure("input_release_failed", outcome.ok || outcome.side_effect_may_have_occurred,
                       false);
    }
    if (!outcome.ok)
        return failure(outcome.error.code, outcome.side_effect_may_have_occurred);
    if (bounded.cancelled())
        return failure("cancelled", true);
    return Json{Json::Object{{"schema", 2},
                             {"status", "dispatched"},
                             {"window_id", id},
                             {"display_id", source.display.id},
                             {"image_scope", source.scope},
                             {"verified", false},
                             {"side_effect_may_have_occurred", true},
                             {"input_released", true}}};
}
} // namespace mirage::integration::detail
