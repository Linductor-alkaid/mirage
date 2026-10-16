#include "desktop_image_supply.hpp"
#include "desktop_stability_guard.hpp"
#include "tool_geometry.hpp"
#include <algorithm>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
using Geometry = desktop::WindowGeometry;
mira::Error error(mira::ErrorCode code, const char *message) {
    mira::Error e;
    e.code = code;
    e.domain = "mirage.desktop.visual";
    e.safe_message = message;
    return e;
}
mira::Error action_error(const desktop::ProviderError &failure, bool effect = false,
                         bool released = true) {
    auto e = error(mira::ErrorCode::PlatformError, "visual action rejected");
    if (failure.code == "cancelled")
        e.code = mira::ErrorCode::Cancelled;
    if (failure.code == "permission_denied")
        e.code = mira::ErrorCode::PermissionDenied;
    e.safe_message =
        mira::to_json_string(Json{Json::Object{{"code", failure.code},
                                               {"side_effect_may_have_occurred", effect},
                                               {"input_released", released},
                                               {"verified", false}}});
    return e;
}
std::string field(const Json &value, const char *key) {
    const auto *v = value.find(key);
    return v && v->as_string() ? *v->as_string() : "";
}
std::int64_t number(const Json &value, const char *key) {
    const auto *v = value.find(key);
    return v && v->as_integer() ? *v->as_integer() : 0;
}
bool allowed(const DesktopToolContext &desktop, const std::string &capability,
             const std::string &resource, const desktop::CancelToken &cancel,
             const mira::OperationContext &ctx) {
    const AtomCancelProbe probe = [cancel] { return cancel.cancelled(); };
    return desktop.authorize ? desktop.authorize(capability, resource, probe, ctx.task.to_string(),
                                                 ctx.operation.to_string())
                             : desktop.gate && desktop.gate(capability, resource, probe);
}
Json string_schema(std::int64_t maximum = 256) {
    return Json{Json::Object{{"type", "string"}, {"maxLength", maximum}}};
}
Json integer_schema(std::int64_t low, std::int64_t high) {
    return Json{Json::Object{{"type", "integer"}, {"minimum", low}, {"maximum", high}}};
}
} // namespace

void DesktopImageTurn::register_mark_tools(mira::BuiltinToolRegistry &registry,
                                           const DesktopAtomToolset::Decorator &decorate,
                                           bool allow_mutations) {
    if (!desktop_.mark || !desktop_.with_visual_reference)
        return;
    mira::BuiltinToolSpec marks;
    marks.wire_name = "desktop.vision.mark";
    marks.version = {1, 2, 0};
    marks.description = "Fresh authorized OCR/geometry/template regions numbered over the same "
                        "screenshot. Optional "
                        "x/y/width/height crops in global desktop pixels without resizing. "
                        "Numbers are local to this captured analysis. Image supplied next request; "
                        "numbers need visual_scope and "
                        "environment_epoch. Regions are visual facts, not clickable semantics. "
                        "Optional template_id is appearance evidence, not semantic identity. "
                        "Prefer semantic actions when available; expires in 5 seconds.";
    Json::Object properties;
    for (const auto *key : {"x", "y", "width", "height"})
        properties.emplace_back(key, integer_schema(-1048576, 1048576));
    marks.parameters_schema =
        mira::JsonSchema{Json{Json::Object{{"type", "object"},
                                           {"properties", Json{std::move(properties)}},
                                           {"additionalProperties", false}}}};
    mira::BuiltinToolHandler render = [this](const auto &args, const auto &ctx) {
        // A failed refresh cannot leave a previously supplied image actionable.
        clear();
        try {
            return mark(args, ctx);
        } catch (...) {
            clear();
            return mira::Result<Json>{
                error(mira::ErrorCode::PlatformError, "mark rendering failed")};
        }
    };
    if (decorate)
        render = decorate(marks.wire_name, std::move(render));
    if (!registry.register_tool(std::move(marks), std::move(render)))
        throw std::logic_error("visual mark registration rejected");
    auto *input = desktop_.environment->input();
    if (!allow_mutations || !desktop_.environment->window() || !input ||
        !input->complete_actions_supported())
        return;
    mira::BuiltinToolSpec click;
    click.wire_name = "desktop.vision.click";
    click.version = {1, 1, 0};
    click.has_side_effects = true;
    click.description =
        "Click a fully visible number from this turn's latest visual_scope, with "
        "environment_epoch and focused window_id. Requires exact fresh pixels "
        "and unchanged window/display geometry; animation may be rejected. No "
        "raw coordinates. Fix verify_kind before clicking and inspect new "
        "postcondition evidence. visual_text_exists requires expected_text and fresh unique OCR "
        "inside the window at confidence >= 0.8; injection alone does not verify task success.";
    Json::Object target{{"visual_scope", string_schema()},
                        {"window_id", string_schema()},
                        {"mark", integer_schema(1, 128)},
                        {"environment_epoch", integer_schema(1, INT64_MAX)}};
    for (const auto *key :
         {"verify_kind", "expected_title", "expected_role", "expected_name", "expected_text"})
        target.emplace_back(key, string_schema(1024));
    click.parameters_schema = mira::JsonSchema{Json{Json::Object{
        {"type", "object"},
        {"properties", Json{std::move(target)}},
        {"required", Json{Json::Array{"visual_scope", "mark", "environment_epoch", "window_id"}}},
        {"additionalProperties", false}}}};
    mira::BuiltinToolHandler act = [this](const auto &args, const auto &ctx) {
        return click_mark(args, ctx);
    };
    if (decorate)
        act = decorate(click.wire_name, std::move(act));
    if (!registry.register_tool(std::move(click), std::move(act)))
        throw std::logic_error("visual click registration rejected");
    if (!input->bounded_pointer_actions_supported())
        return;
    for (const auto *operation : {"scroll", "drag"}) {
        const bool drag = std::string_view{operation} == "drag";
        if (drag && !desktop_.with_visual_references)
            continue;
        mira::BuiltinToolSpec spec;
        spec.wire_name = std::string{"desktop.vision."} + operation;
        spec.version = {1, 0, 0};
        spec.has_side_effects = true;
        spec.description =
            drag
                ? "Complete left drag between mark and destination_mark in the same fresh "
                  "visual_scope and focused window. Max 2048 pixels, 16 linear segments, 1 second. "
                  "No held-button continuation. Declare a postcondition and verify fresh evidence."
                : "Discrete wheel steps at a fresh mark in the focused window. Positive "
                  "horizontal_steps/vertical_steps means right/down, negative left/up. Total "
                  "absolute steps 1..16, max 1 second. No pixel-distance guarantee. Declare a "
                  "postcondition and verify fresh evidence.";
        Json::Object props{{"visual_scope", string_schema()},
                           {"window_id", string_schema()},
                           {"mark", integer_schema(1, 128)},
                           {"environment_epoch", integer_schema(1, INT64_MAX)}};
        Json::Array required{"visual_scope", "mark", "environment_epoch", "window_id"};
        for (const auto *key :
             {"verify_kind", "expected_title", "expected_role", "expected_name", "expected_text"})
            props.emplace_back(key, string_schema(1024));
        if (drag) {
            props.emplace_back("destination_mark", integer_schema(1, 128));
            required.emplace_back("destination_mark");
        } else {
            for (const auto *key : {"horizontal_steps", "vertical_steps"}) {
                props.emplace_back(key, integer_schema(-16, 16));
                required.emplace_back(key);
            }
        }
        spec.parameters_schema =
            mira::JsonSchema{Json{Json::Object{{"type", "object"},
                                               {"properties", Json{std::move(props)}},
                                               {"required", Json{std::move(required)}},
                                               {"additionalProperties", false}}}};
        mira::BuiltinToolHandler handler =
            [this, operation = std::string{operation}](const auto &args, const auto &ctx) {
                return pointer_mark(args, ctx, operation);
            };
        if (decorate)
            handler = decorate(spec.wire_name, std::move(handler));
        if (!registry.register_tool(std::move(spec), std::move(handler)))
            throw std::logic_error("visual pointer registration rejected");
    }
}

mira::Result<Json> DesktopImageTurn::mark(const Json &args, const mira::OperationContext &ctx) {
    const auto start = std::chrono::steady_clock::now();
    const auto end = std::min(ctx.deadline.value_or(start + std::chrono::seconds{5}),
                              start + std::chrono::seconds{5});
    const desktop::CancelToken bounded([this, ctx, end] {
        return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
               std::chrono::steady_clock::now() >= end;
    });
    if (bounded.cancelled())
        return error(mira::ErrorCode::Cancelled, "mark cancelled or expired");
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return error(mira::ErrorCode::InvalidArgument, "mark owner mismatch");
    std::optional<Geometry> crop;
    if (!args.as_object())
        return error(mira::ErrorCode::InvalidArgument, "invalid mark arguments");
    if (!args.as_object()->empty()) {
        if (args.as_object()->size() != 4)
            return error(mira::ErrorCode::InvalidArgument, "crop requires four fields");
        Geometry area;
        for (const auto &[key, member] : {std::pair{"x", &area.x},
                                          {"y", &area.y},
                                          {"width", &area.width},
                                          {"height", &area.height}}) {
            const auto *v = args.find(key);
            if (!v || !v->as_integer() || *v->as_integer() < -1048576 || *v->as_integer() > 1048576)
                return error(mira::ErrorCode::InvalidArgument, "invalid crop coordinate");
            *member = static_cast<std::int32_t>(*v->as_integer());
        }
        if (area.width <= 0 || area.height <= 0)
            return error(mira::ErrorCode::InvalidArgument, "invalid crop extents");
        crop = area;
    }
    if (captures_ >= 4)
        return error(mira::ErrorCode::ResourceExhausted,
                     "desktop capture count exceeds 4 per turn");
    if (!allowed(desktop_, "screen.capture", "mark:configured-display", bounded, ctx))
        return error(bounded.cancelled() ? mira::ErrorCode::Cancelled
                                         : mira::ErrorCode::PermissionDenied,
                     "mark capture denied");
    if (bounded.cancelled())
        return error(mira::ErrorCode::Cancelled, "mark cancelled during authorization");
    ++captures_;
    const auto epoch = desktop_.actions->epoch();
    auto *window = desktop_.environment->window();
    const auto front = window ? window->front_window(bounded) : desktop::WindowQueryOutcome{};
    // DEC-062: a configured capture scope pins the supplied image to that
    // focused window; the caller crop may only narrow it.
    std::optional<Geometry> area = crop;
    if (!desktop_.capture_scope.empty()) {
        if (!front.ok || !front.found || front.window.id != desktop_.capture_scope)
            return error(mira::ErrorCode::InvalidArgument,
                         "mark capture scope window is not focused");
        if (crop && !geometry_contains(front.window.geometry, *crop))
            return error(mira::ErrorCode::InvalidArgument,
                         "mark crop outside the capture scope window");
        area = crop.value_or(front.window.geometry);
    }
    auto fresh = desktop_.mark(area, bounded, end);
    if (bounded.cancelled() || fresh.capture_cancelled || fresh.analysis_cancelled)
        return error(mira::ErrorCode::Cancelled, "mark cancelled before publication");
    if (!fresh.captured || !fresh.analyzed || !fresh.publish.ok ||
        desktop_.actions->epoch() != epoch) {
        const auto phase = desktop_.actions->epoch() != epoch ? "environment"
                           : !fresh.captured                  ? "capture"
                           : !fresh.analyzed                  ? "analysis"
                                                              : "publication";
        const auto code = desktop_.actions->epoch() != epoch ? "environment_changed"
                          : !fresh.captured                  ? fresh.capture_error.code
                          : !fresh.analyzed                  ? fresh.analysis_error.code
                                                             : "publication_rejected";
        const bool over_budget = code == "capture_too_large" || code == "result_too_large";
        auto failure =
            error(over_budget ? mira::ErrorCode::ResourceExhausted : mira::ErrorCode::PlatformError,
                  "mark observation failed");
        failure.safe_message = mira::to_json_string(Json{Json::Object{
            {"code", code.empty() ? "mark_observation_failed" : code},
            {"stage", phase},
            {"verified", false},
            {"side_effect_may_have_occurred", false},
            {"message", over_budget ? "Mark capture/analysis budget exceeded; use a smaller "
                                      "explicit desktop-pixel ROI in the focused window."
                                    : "Mark observation unavailable; obtain a fresh focused-window "
                                      "observation and inspect the stage/code."}}});
        // Provider codes are not an unbounded transport surface.
        if (failure.safe_message.size() > 512)
            failure.safe_message = "mark observation failed: invalid provider error code";
        return failure;
    }
    if (!valid_image_frame(fresh.frame, desktop_image_bytes, 65535) ||
        !valid_image_frame(fresh.marked_frame, desktop_image_bytes, 65535) ||
        fresh.display_id.empty() || fresh.display_id.size() > 256 ||
        fresh.publish.snapshot.scope_ref.empty() || fresh.publish.snapshot.scope_ref.size() > 128 ||
        fresh.publish.snapshot.regions.size() != fresh.marks.size() || fresh.marks.size() > 128 ||
        fresh.frame.width != fresh.capture_roi.value_or(fresh.display_geometry).width ||
        fresh.frame.height != fresh.capture_roi.value_or(fresh.display_geometry).height ||
        (fresh.capture_roi && !geometry_equals(*fresh.capture_roi, fresh.image_geometry)) ||
        fresh.marked_frame.width != fresh.image_geometry.width ||
        fresh.marked_frame.height != fresh.image_geometry.height ||
        !geometry_contains(fresh.display_geometry, fresh.image_geometry) ||
        !geometry_equals(fresh.image_geometry, area.value_or(fresh.display_geometry)))
        return error(mira::ErrorCode::ResourceExhausted, "invalid or over-budget mark result");
    std::optional<desktop::WindowInfo> observed_window;
    if (front.ok && front.found && window) {
        const auto after = window->front_window(bounded);
        if (after.ok && after.found && after.window.id == front.window.id &&
            after.window.title == front.window.title &&
            geometry_equals(after.window.geometry, front.window.geometry))
            observed_window = after.window;
    }
    Json::Array mapping;
    for (std::size_t i = 0; i < fresh.marks.size(); ++i) {
        const auto &m = fresh.marks[i];
        const auto &r = fresh.publish.snapshot.regions[i];
        if (r.text.size() > 1024 || !desktop::is_valid_utf8(r.text) || r.template_id.size() > 256 ||
            !desktop::is_valid_utf8(r.template_id))
            return error(mira::ErrorCode::ResourceExhausted, "mark region text budget invalid");
        if (m.number != i + 1 || m.ref != r.ref || r.ref.empty() || r.ref.size() > 32)
            return error(mira::ErrorCode::InvalidArgument, "mark reference mapping mismatch");
        mapping.emplace_back(Json::Object{{"mark", std::int64_t{m.number}},
                                          {"ref", m.ref},
                                          {"template_id", r.template_id},
                                          {"selectable", m.selectable},
                                          {"x", std::int64_t{r.bounds.x}},
                                          {"y", std::int64_t{r.bounds.y}},
                                          {"width", std::int64_t{r.bounds.width}},
                                          {"height", std::int64_t{r.bounds.height}}});
    }
    Json::Object extra{{"visual_scope", fresh.publish.snapshot.scope_ref},
                       {"visual_generation", static_cast<std::int64_t>(fresh.visual_generation)},
                       {"frame_sequence", static_cast<std::int64_t>(fresh.frame_sequence)},
                       {"window_id", observed_window ? observed_window->id : ""},
                       {"marks", Json{std::move(mapping)}}};
    auto published = publish_image(fresh.marked_frame, fresh.image_geometry, "som",
                                   fresh.display_id, epoch, start, bounded, std::move(extra));
    if (!published)
        return published.error();
    if (mira::to_json_string(published.value()).size() > (16u << 10)) {
        clear();
        return error(mira::ErrorCode::ResourceExhausted, "mark metadata budget exceeded");
    }
    fresh.marked_frame = {}; // Stored PNG owns the overlay; retain only bounded source pixels.
    marks_ = std::move(fresh);
    mark_window_ = std::move(observed_window);
    return published;
}

mira::Result<Json> DesktopImageTurn::click_mark(const Json &args,
                                                const mira::OperationContext &ctx) {
    return pointer_mark(args, ctx, "click");
}
mira::Result<Json> DesktopImageTurn::pointer_mark(const Json &args,
                                                  const mira::OperationContext &ctx,
                                                  const std::string &operation) {
    const bool drag = operation == "drag", scroll = operation == "scroll";
    const auto end =
        std::min(ctx.deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds{1}),
                 std::chrono::steady_clock::now() + std::chrono::seconds{1});
    const desktop::CancelToken bounded([this, ctx, end] {
        return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
               std::chrono::steady_clock::now() >= end;
    });
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return action_error({"invalid_argument", "visual click owner mismatch"});
    const auto id = field(args, "window_id");
    const auto scope = field(args, "visual_scope");
    const auto selected = number(args, "mark");
    const auto destination = drag ? number(args, "destination_mark") : selected;
    const auto horizontal = number(args, "horizontal_steps");
    const auto vertical = number(args, "vertical_steps");
    if (scroll && (horizontal < -16 || horizontal > 16 || vertical < -16 || vertical > 16 ||
                   !desktop::valid_scroll_steps(static_cast<std::int32_t>(horizontal),
                                                static_cast<std::int32_t>(vertical))))
        return action_error({"invalid_argument", "wheel step budget must be 1..16"});
    const auto claimed_epoch = number(args, "environment_epoch");
    const auto current = [&] {
        return marks_ && mark_window_ && image_ && !id.empty() && id.size() <= 256 &&
               mark_window_->id == id && scope == marks_->publish.snapshot.scope_ref &&
               selected > 0 && selected <= static_cast<std::int64_t>(marks_->marks.size()) &&
               destination > 0 && destination <= static_cast<std::int64_t>(marks_->marks.size()) &&
               claimed_epoch > 0 && static_cast<std::uint64_t>(claimed_epoch) == epoch_ &&
               epoch_ == desktop_.actions->epoch() &&
               (desktop_.capture_scope.empty() || id == desktop_.capture_scope) &&
               std::chrono::steady_clock::now() - captured_ < std::chrono::seconds{5};
    };
    if (bounded.cancelled())
        return action_error({"cancelled", "visual click cancelled"});
    if (!current())
        return action_error({"stale_observation", "visual click needs matching marks"});
    const auto &mark = marks_->marks[static_cast<std::size_t>(selected - 1)];
    const auto &region = marks_->publish.snapshot.regions[static_cast<std::size_t>(selected - 1)];
    const auto &to_mark = marks_->marks[static_cast<std::size_t>(destination - 1)];
    const auto &to_region =
        marks_->publish.snapshot.regions[static_cast<std::size_t>(destination - 1)];
    if (!mark.selectable || !geometry_contains(mark_window_->geometry, region.bounds) ||
        !geometry_contains(marks_->image_geometry, {mark.anchor.x, mark.anchor.y, 1, 1}))
        return action_error({"invalid_argument", "target not fully visible in focused window"});
    if (drag &&
        (!to_mark.selectable || !geometry_contains(mark_window_->geometry, to_region.bounds) ||
         !geometry_contains(marks_->image_geometry, {to_mark.anchor.x, to_mark.anchor.y, 1, 1}) ||
         !desktop::valid_drag_path({mark.anchor.x, mark.anchor.y},
                                   {to_mark.anchor.x, to_mark.anchor.y})))
        return action_error({"invalid_argument", "invalid or over-budget drag destination"});
    if (captures_ >= 4)
        return action_error({"result_too_large", "visual revalidation capture budget exhausted"});
    if (!allowed(desktop_, "input.inject", id, bounded, ctx) ||
        !allowed(desktop_, "screen.capture", "display:" + marks_->display_id, bounded, ctx))
        return action_error({bounded.cancelled() ? "cancelled" : "permission_denied",
                             "visual click authorization failed"});
    if (!current())
        return action_error({"stale_observation", "visual marks expired during authorization"});
    auto lease = desktop_.actions->try_begin(bounded, epoch_);
    if (!lease)
        return action_error(lease.error(), false, lease.error().code != "input_release_failed");
    const auto dispatch_epoch = desktop_.actions->epoch();
    auto *window = desktop_.environment->window();
    auto *screen = desktop_.environment->screen();
    const auto stable_front = [&] {
        const auto f = window->front_window(bounded);
        return f.ok && f.found && f.window.id == id && f.window.title == mark_window_->title &&
               geometry_equals(f.window.geometry, mark_window_->geometry);
    };
    const auto stable_display = [&] {
        const auto list = screen->list_displays({}, bounded);
        return list.ok &&
               std::any_of(list.displays.begin(), list.displays.end(), [&](const auto &d) {
                   return d.id == marks_->display_id &&
                          geometry_equals(d.geometry, marks_->display_geometry) &&
                          d.primary == marks_->display_primary;
               });
    };
    if (!stable_front() || !stable_display())
        return action_error({"stale_observation", "window or display moved"});
    ++captures_;
    const auto fresh = marks_->capture_roi
                           ? screen->capture_roi(*marks_->capture_roi, {}, bounded)
                           : screen->capture_display(marks_->display_id, {}, bounded);
    if (!fresh.ok || fresh.cancelled || bounded.cancelled())
        return action_error({bounded.cancelled() || fresh.cancelled ? "cancelled" : "io_error",
                             "visual revalidation capture failed"});
    const auto &source = marks_->frame;
    if (!valid_image_frame(fresh.frame, desktop_image_bytes, 65535) ||
        fresh.frame.width != source.width || fresh.frame.height != source.height ||
        !stable_display() || !stable_front())
        return action_error({"stale_observation", "visual source pixels or geometry changed"});
    // M7-16 / DEC-082: re-verify the operated marks' region for stability
    // instead of whole-frame byte equality. Animation outside the target, or
    // same-subject noise inside it, proceeds; a moved or replaced target still
    // rejects as stale.
    const auto frame_rect = [&](const Geometry &bounds) {
        return TargetRegion{bounds.x - marks_->image_geometry.x,
                            bounds.y - marks_->image_geometry.y,
                            bounds.x - marks_->image_geometry.x + bounds.width,
                            bounds.y - marks_->image_geometry.y + bounds.height};
    };
    TargetRegion target = frame_rect(region.bounds);
    if (drag) {
        const auto to = frame_rect(to_region.bounds);
        target = {std::min(target.left, to.left), std::min(target.top, to.top),
                  std::max(target.right, to.right), std::max(target.bottom, to.bottom)};
    }
    if (assess_target_stability(source, fresh.frame, target) == TargetStability::kChanged)
        return action_error({"stale_observation", "visual source pixels or geometry changed"});
    desktop::InputOutcome outcome;
    bool entered = false;
    try {
        const auto perform = [&](const auto &active, const desktop::VisualRegionEntry *to) {
            if (!geometry_equals(active.bounds, region.bounds) ||
                (drag && (!to || !geometry_equals(to->bounds, to_region.bounds))) ||
                !stable_front() || bounded.cancelled() ||
                desktop_.actions->epoch() != dispatch_epoch ||
                std::chrono::steady_clock::now() - captured_ >= std::chrono::seconds{5}) {
                outcome.error = {"stale_observation", "visual target changed before injection"};
                return;
            }
            desktop::InputLimits limits;
            limits.timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
                end - std::chrono::steady_clock::now());
            if (drag || scroll)
                limits.timeout =
                    std::min(limits.timeout, std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 captured_ + std::chrono::seconds{5} -
                                                 std::chrono::steady_clock::now()));
            if (limits.timeout.count() <= 0) {
                outcome.cancelled = true;
                outcome.error = {"cancelled", "visual input deadline expired"};
                return;
            }
            entered = true;
            auto *input = desktop_.environment->input();
            const desktop::PointerActionScope pointer_scope{id, mark_window_->geometry};
            const desktop::PointerPoint from{mark.anchor.x, mark.anchor.y};
            if (drag)
                outcome = input->drag_to(pointer_scope, from, {to_mark.anchor.x, to_mark.anchor.y},
                                         limits, bounded);
            else if (scroll)
                outcome =
                    input->scroll_at(pointer_scope, from, static_cast<std::int32_t>(horizontal),
                                     static_cast<std::int32_t>(vertical), limits, bounded);
            else
                outcome = input->click_at(from.x, from.y, "left", limits, bounded);
        };
        const auto resolved =
            drag ? desktop_.with_visual_references(
                       scope, mark.ref, to_mark.ref,
                       [&](const auto &from, const auto &to) { perform(from, &to); })
                 : desktop_.with_visual_reference(
                       scope, mark.ref, [&](const auto &from) { perform(from, nullptr); });
        if (!resolved.code.empty())
            return action_error(resolved);
    } catch (...) {
        if (entered)
            lease.release_failed();
        return action_error({"provider_exception", "visual input failed"}, entered, !entered);
    }
    if (!outcome.input_released) {
        lease.release_failed();
        return action_error({"input_release_failed", "visual input cleanup unconfirmed"},
                            outcome.side_effect_may_have_occurred || outcome.ok, false);
    }
    if (!outcome.ok)
        return action_error(outcome.error, outcome.side_effect_may_have_occurred, true);
    if (bounded.cancelled())
        return action_error({"cancelled", "visual action cancelled during dispatch"}, true, true);
    Json::Object result{{"schema", std::int64_t{2}}, {"status", "dispatched"},
                        {"window_id", id},           {"visual_scope", scope},
                        {"mark", selected},          {"ref", mark.ref},
                        {"verified", false},         {"side_effect_may_have_occurred", true},
                        {"input_released", true}};
    if (drag)
        result.emplace_back("destination_mark", destination);
    if (scroll) {
        result.emplace_back("horizontal_steps", horizontal);
        result.emplace_back("vertical_steps", vertical);
    }
    return Json{std::move(result)};
}
} // namespace mirage::integration::detail
