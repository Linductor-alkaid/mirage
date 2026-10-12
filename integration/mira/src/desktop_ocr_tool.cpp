#include "desktop_ocr_tool.hpp"
#include <algorithm>
#include <mira/event_store.hpp>

namespace mirage::integration::detail {
namespace {
mira::Error failure(mira::ErrorCode code, const char *message) {
    mira::Error e;
    e.code = code;
    e.domain = "mirage.desktop.ocr";
    e.safe_message = message;
    return e;
}
bool contains(const desktop::WindowGeometry &outer, const desktop::WindowGeometry &inner) {
    return inner.width > 0 && inner.height > 0 && inner.x >= outer.x && inner.y >= outer.y &&
           std::int64_t{inner.x} + inner.width <= std::int64_t{outer.x} + outer.width &&
           std::int64_t{inner.y} + inner.height <= std::int64_t{outer.y} + outer.height;
}
} // namespace
void register_ocr_tool(mira::BuiltinToolRegistry &registry, const DesktopToolContext &desktop,
                       const desktop::CancelToken &parent,
                       const DesktopAtomToolset::Decorator &decorate) {
    // DEC-075: a native frame has no configured global display. The owned
    // image turn registers the corresponding shared-session OCR path.
    if (desktop.environment && desktop.environment->screen() &&
        desktop.environment->screen()->desktop_observation_only())
        return;
    if (!desktop.ocr || !desktop.actions || (!desktop.authorize && !desktop.gate))
        return;
    using Json = mira::JsonValue;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.vision.ocr";
    spec.description = "Read untrusted OCR text from the configured display via authorized fresh "
                       "capture. Boxes are global desktop coordinates. No input or task "
                       "verification is performed. Direct OCR refs are not actionable; use "
                       "desktop.vision.mark, where available, for scoped numbered selection. "
                       "When a capture scope window is configured, text is limited to that "
                       "focused window.";
    spec.parameters_schema =
        mira::JsonSchema{Json{Json::Object{{"type", "object"},
                                           {"properties", Json{Json::Object{}}},
                                           {"required", Json{Json::Array{}}},
                                           {"additionalProperties", false}}}};
    mira::BuiltinToolHandler handler = [desktop, parent](const auto &arguments,
                                                         const auto &ctx) -> mira::Result<Json> {
        if (!arguments.as_object() || !arguments.as_object()->empty())
            return failure(mira::ErrorCode::InvalidArgument, "OCR accepts no arguments");
        const auto end = std::min(
            ctx.deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds{5}),
            std::chrono::steady_clock::now() + std::chrono::seconds{5});
        const desktop::CancelToken bounded([parent, ctx, end] {
            return parent.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                   std::chrono::steady_clock::now() >= end;
        });
        const AtomCancelProbe cancelled = [bounded] { return bounded.cancelled(); };
        const bool allowed =
            desktop.authorize
                ? desktop.authorize("screen.capture", "ocr:configured-display", cancelled,
                                    ctx.task.to_string(), ctx.operation.to_string())
                : desktop.gate("screen.capture", "ocr:configured-display", cancelled);
        if (bounded.cancelled())
            return failure(mira::ErrorCode::Cancelled, "OCR cancelled or expired");
        if (!allowed)
            return failure(mira::ErrorCode::PermissionDenied, "OCR capture permission denied");
        const auto epoch = desktop.actions->epoch();
        DesktopOcrResult read;
        try {
            read = desktop.ocr(bounded, end);
        } catch (...) {
            return failure(mira::ErrorCode::PlatformError, "OCR analysis failed");
        }
        if (read.cancelled || bounded.cancelled())
            return failure(mira::ErrorCode::Cancelled, "OCR cancelled or expired");
        if (!read.ok) {
            auto issue = failure(mira::ErrorCode::PlatformError, "OCR capture or analysis failed");
            if (!read.error.code.empty() && read.error.code.size() <= 64 &&
                std::all_of(read.error.code.begin(), read.error.code.end(),
                            [](unsigned char c) { return (c >= 'a' && c <= 'z') || c == '_'; }))
                issue.safe_message += ": " + read.error.code;
            return issue;
        }
        if (desktop.actions->epoch() != epoch)
            return failure(mira::ErrorCode::InvalidArgument, "OCR environment changed");
        // DEC-062: a configured capture scope limits returned text to the
        // focused scope window; text observed anywhere else never leaves.
        std::optional<desktop::WindowGeometry> scope_bounds;
        if (!desktop.capture_scope.empty()) {
            auto *window = desktop.environment ? desktop.environment->window() : nullptr;
            const auto front =
                window ? window->front_window(bounded) : desktop::WindowQueryOutcome{};
            if (!front.ok || !front.found || front.window.id != desktop.capture_scope)
                return failure(mira::ErrorCode::InvalidArgument,
                               "OCR capture scope window is not focused");
            scope_bounds = front.window.geometry;
        }
        if (read.snapshot.regions.size() > 128 || read.snapshot.scope_ref.empty())
            return failure(mira::ErrorCode::ResourceExhausted, "OCR result budget exceeded");
        Json::Array regions;
        for (const auto &r : read.snapshot.regions) {
            if (r.text.size() > 1024 || !desktop::is_valid_utf8(r.text))
                return failure(mira::ErrorCode::ResourceExhausted, "OCR text budget invalid");
            if (scope_bounds && !contains(*scope_bounds, r.bounds))
                continue;
            regions.emplace_back(Json::Object{{"ref", r.ref},
                                              {"text", r.text},
                                              {"confidence", r.confidence},
                                              {"x", std::int64_t{r.bounds.x}},
                                              {"y", std::int64_t{r.bounds.y}},
                                              {"width", std::int64_t{r.bounds.width}},
                                              {"height", std::int64_t{r.bounds.height}}});
        }
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        Json::Object fields{{"schema", std::int64_t{1}},
                            {"source", "ocr"},
                            {"display_id", read.display_id},
                            {"visual_scope", read.snapshot.scope_ref},
                            {"model_revision", read.model_revision},
                            {"backend_calls", static_cast<std::int64_t>(read.backend_calls)},
                            {"environment_epoch", static_cast<std::int64_t>(epoch)},
                            {"observed_at_ms", now},
                            {"verified", false},
                            {"regions", Json{std::move(regions)}}};
        if (!desktop.capture_scope.empty())
            fields.emplace_back("window_id", desktop.capture_scope);
        Json result{Json::Object{std::move(fields)}};
        if (mira::to_json_string(result).size() > (16u << 10))
            return failure(mira::ErrorCode::ResourceExhausted, "OCR tool output budget exceeded");
        return result;
    };
    if (decorate)
        handler = decorate(spec.wire_name, std::move(handler));
    if (!registry.register_tool(std::move(spec), std::move(handler)))
        throw std::logic_error("OCR tool registration rejected");
}
} // namespace mirage::integration::detail
