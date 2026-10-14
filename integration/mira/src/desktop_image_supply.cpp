#include "desktop_image_supply.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mirador/resize.hpp>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using mira::ErrorCode;
using mira::JsonValue;
using Geometry = mirage::desktop::WindowGeometry;
mira::Error error(ErrorCode code, std::string message) {
    mira::Error result;
    result.code = code;
    result.domain = "mirage.desktop.image";
    result.safe_message = std::move(message);
    return result;
}
std::string text(const JsonValue &value, const char *field) {
    const auto *item = value.find(field);
    return item && item->as_string() ? *item->as_string() : "";
}
bool same(const Geometry &a, const Geometry &b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
mira::ArtifactRef reference(const mira::ArtifactDescriptor &value) {
    return {value.id, value.digest, value.byte_size, value.media_type, value.sensitivity};
}
JsonValue string_field(const char *description) {
    return JsonValue{JsonValue::Object{
        {"type", "string"}, {"description", description}, {"maxLength", std::int64_t{256}}}};
}
} // namespace

mira::Result<std::vector<std::byte>> DesktopImageSource::fetch(const mira::ArtifactRef &ref) {
    if (ref.media_type != "image/png" || ref.sensitivity != mira::Sensitivity::Sensitive ||
        ref.byte_size == 0 || ref.byte_size > desktop_image_bytes)
        return error(ErrorCode::InvalidArgument, "invalid desktop image reference");
    mira::ArtifactDescriptor descriptor;
    descriptor.id = ref.id;
    descriptor.digest = ref.digest;
    descriptor.byte_size = ref.byte_size;
    descriptor.media_type = ref.media_type;
    descriptor.sensitivity = ref.sensitivity;
    Store store;
    if (!current_.try_load(store) || !store)
        return error(ErrorCode::Unavailable, "desktop image store snapshot unavailable");
    auto opened = store->open(descriptor);
    if (!opened)
        return opened.error();
    if (opened.value().size() != ref.byte_size ||
        mira::digest_bytes(opened.value().bytes()) != ref.digest)
        return error(ErrorCode::InvalidArgument, "desktop image identity mismatch");
    return opened.value().bytes();
}

mira::InputReuseMeta DesktopImageSource::record_input_reuse(const std::string &digest,
                                                            std::uint64_t desktop_epoch) {
    const std::lock_guard lock(reuse_mutex_);
    if (desktop_epoch != reuse_epoch_ || digest != reuse_digest_) {
        reuse_digest_ = digest;
        reuse_epoch_ = desktop_epoch;
        reuse_count_ = 0;
    } else {
        ++reuse_count_;
    }
    return {digest, reuse_count_, "screenshot"};
}

DesktopImageTurn::DesktopImageTurn(std::shared_ptr<DesktopImageSource> source,
                                   DesktopToolContext desktop, mira::OperationContext owner,
                                   mirage::desktop::CancelToken cancel, bool supply_images,
                                   std::chrono::milliseconds acquire_budget,
                                   std::chrono::milliseconds image_max_age)
    : source_(std::move(source)),
      store_(std::make_shared<mira::MemoryArtifactStore>(desktop_image_bytes)),
      desktop_(std::move(desktop)), owner_(std::move(owner)),
      cancel_([owner = owner_, parent = std::move(cancel)] {
          return parent.cancelled() || owner.cancelled_or_expired(mira::Timestamp::now());
      }),
      supply_images_(supply_images), acquire_budget_(acquire_budget),
      image_max_age_(image_max_age) {
    if (!source_->bind(store_))
        throw std::runtime_error("desktop image store admission rejected");
}
DesktopImageTurn::~DesktopImageTurn() {
    native_session_.reset();
    clear();
}
void DesktopImageTurn::clear() {
    if (image_)
        (void)store_->erase({image_->id, "desktop image retired"});
    image_.reset();
    native_frame_.reset();
    native_input_frame_.reset();
    marks_.reset();
    mark_window_.reset();
    region_source_.reset();
    supplied_image_ = false;
    expect_image_ = false;
}

void DesktopImageTurn::register_tool(mira::BuiltinToolRegistry &registry,
                                     const DesktopAtomToolset::Decorator &decorate,
                                     bool allow_mutations) {
    if (!desktop_.environment || !desktop_.environment->screen() || !desktop_.actions ||
        (!desktop_.gate && !desktop_.authorize))
        return;
    const bool observation_only = desktop_.environment->screen()->desktop_observation_only();
    if (observation_only)
        register_native_ocr(registry, decorate);
    if (!supply_images_)
        return;
    native_input_enabled_ = observation_only && allow_mutations &&
                            desktop_.environment->screen()->desktop_interaction_supported();
    if (native_input_enabled_)
        register_shared_input(registry, decorate);
    if (!observation_only) {
        mira::BuiltinToolSpec list;
        list.wire_name = "desktop.screen.list_displays";
        list.description =
            "List display IDs and their global desktop pixel bounds for capture/ROI.";
        list.parameters_schema = mira::JsonSchema{JsonValue{
            JsonValue::Object{{"type", "object"}, {"properties", JsonValue{JsonValue::Object{}}}}}};
        mira::BuiltinToolHandler enumerate = [this](const auto &,
                                                    const auto &ctx) -> mira::Result<JsonValue> {
            if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()))
                return error(ErrorCode::Cancelled, "display enumeration cancelled");
            if (ctx.task != owner_.task || ctx.session != owner_.session ||
                ctx.task_epoch != owner_.task_epoch)
                return error(ErrorCode::InvalidArgument, "display enumeration owner mismatch");
            const auto displays = desktop_.environment->screen()->list_displays({}, cancel_);
            if (!displays.ok)
                return error(ErrorCode::PlatformError, displays.error.code);
            JsonValue::Array values;
            for (const auto &display : displays.displays)
                values.emplace_back(
                    JsonValue::Object{{"id", display.id},
                                      {"origin_x", std::int64_t{display.geometry.x}},
                                      {"origin_y", std::int64_t{display.geometry.y}},
                                      {"width", std::int64_t{display.geometry.width}},
                                      {"height", std::int64_t{display.geometry.height}},
                                      {"primary", display.primary}});
            return JsonValue{JsonValue::Object{{"displays", JsonValue{std::move(values)}},
                                               {"unit", "desktop_pixels"}}};
        };
        if (decorate)
            enumerate = decorate(list.wire_name, std::move(enumerate));
        if (!registry.register_tool(std::move(list), std::move(enumerate)))
            throw std::logic_error("desktop display tool registration rejected");
    }
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.screen.capture";
    spec.version = {1, 5, 0};
    spec.description =
        "Capture an authorized display, window, or display-contained ROI as PNG. "
        "Captures longer than 1600 on a side are downscaled read-only previews with exact "
        "origin/scale metadata; smaller captures (including small ROIs) are 1:1 for precise "
        "grounding. "
        "Only source=roi accepts x/y/width/height (all required, desktop pixels). "
        "Max raw acquisition 32 MiB, ROI/PNG 8 MiB, four acquisitions per turn. "
        "The latest image is supplied to the next model request. "
        "Images expire after 5 seconds or a desktop action; recapture to verify. "
        "When region_actionable is true, image_scope binds click_region to these image pixels; "
        "an already supplied region intent may be revalidated locally for up to 60 seconds.";
    JsonValue::Object properties{{"source", string_field("display, window, or roi")},
                                 {"source_id", string_field("Display or window id")}};
    for (const auto *name : {"x", "y", "width", "height"})
        properties.emplace_back(name,
                                JsonValue{JsonValue::Object{{"type", "integer"},
                                                            {"minimum", std::int64_t{-1048576}},
                                                            {"maximum", std::int64_t{1048576}}}});
    spec.parameters_schema = mira::JsonSchema{JsonValue{
        JsonValue::Object{{"type", "object"},
                          {"properties", JsonValue{std::move(properties)}},
                          {"required", JsonValue{JsonValue::Array{"source", "source_id"}}}}}};
    if (observation_only) {
        spec.description =
            "Capture a read-only native desktop image through system authorization. "
            "source=desktop takes no other fields. source=roi takes x/y/width/height in shared "
            "monitor pixels for a smaller observation of one region. source=window takes "
            "source_id from desktop.window.list and crops that window's rectangle (approximated "
            "from window geometry; overlapping content included). The system may show a sharing "
            "prompt; cancellation/denial or platform failure must be explained, "
            "not retried in this turn. A successful ScreenCast session is reused for at most eight "
            "fresh frames in this task, without another sharing prompt. This image has no "
            "executable coordinates, "
            "display/window IDs or input mapping. No click or typing capability "
            "may be inferred. The image is supplied to the next model request.";
        JsonValue::Object observation_properties{
            {"source", JsonValue{JsonValue::Object{
                           {"type", "string"},
                           {"enum", JsonValue{JsonValue::Array{"desktop", "roi", "window"}}}}}},
            {"source_id", JsonValue{JsonValue::Object{{"type", "string"}, {"maxLength", 256}}}},
            {"x", JsonValue{JsonValue::Object{{"type", "integer"}}}},
            {"y", JsonValue{JsonValue::Object{{"type", "integer"}}}},
            {"width", JsonValue{JsonValue::Object{{"type", "integer"}}}},
            {"height", JsonValue{JsonValue::Object{{"type", "integer"}}}}};
        spec.parameters_schema = mira::JsonSchema{JsonValue{
            JsonValue::Object{{"type", "object"},
                              {"properties", JsonValue{std::move(observation_properties)}},
                              {"required", JsonValue{JsonValue::Array{"source"}}},
                              {"additionalProperties", false}}}};
    }
    if (native_input_enabled_) {
        spec.description =
            "Capture native desktop. For a GUI task needing mouse or keyboard, use source=desktop "
            "with interaction=true on the FIRST capture. This requests keyboard/pointer and "
            "monitor sharing together through system "
            "consent. "
            "Omit interaction for read-only sharing. Session mode cannot change within a task. "
            "source=roi takes x/y/width/height in shared monitor pixels for a smaller observation "
            "of one region; source=window takes source_id from desktop.window.list and crops that "
            "window's rectangle (approximated from window geometry; overlapping content "
            "included). "
            "Only input_actionable=true permits desktop.session tools. No global/window "
            "coordinates. "
            "At most 32 captures in an interactive task, eight in a read-only task.";
        JsonValue::Object interactive_properties{
            {"source", JsonValue{JsonValue::Object{
                           {"type", "string"},
                           {"enum", JsonValue{JsonValue::Array{"desktop", "roi", "window"}}}}}},
            {"interaction", JsonValue{JsonValue::Object{{"type", "boolean"}}}},
            {"source_id", JsonValue{JsonValue::Object{{"type", "string"}, {"maxLength", 256}}}},
            {"x", JsonValue{JsonValue::Object{{"type", "integer"}}}},
            {"y", JsonValue{JsonValue::Object{{"type", "integer"}}}},
            {"width", JsonValue{JsonValue::Object{{"type", "integer"}}}},
            {"height", JsonValue{JsonValue::Object{{"type", "integer"}}}}};
        spec.parameters_schema = mira::JsonSchema{JsonValue{
            JsonValue::Object{{"type", "object"},
                              {"properties", JsonValue{std::move(interactive_properties)}},
                              {"required", JsonValue{JsonValue::Array{"source"}}},
                              {"additionalProperties", false}}}};
    }
    mira::BuiltinToolHandler handler = [this](const auto &args, const auto &ctx) {
        try {
            auto result = capture(args, ctx);
            if (!result && ctx.task == owner_.task && ctx.session == owner_.session &&
                ctx.task_epoch == owner_.task_epoch && native_session_) {
                native_session_.reset();
                native_failed_ = true;
            }
            return result;
        } catch (...) {
            native_session_.reset();
            native_failed_ = true;
            return mira::Result<JsonValue>{
                error(ErrorCode::PlatformError, "desktop capture handler failed")};
        }
    };
    if (decorate)
        handler = decorate(spec.wire_name, std::move(handler));
    auto registered = registry.register_tool(std::move(spec), std::move(handler));
    if (!registered)
        throw std::logic_error("desktop image tool registration rejected");
    if (!observation_only) {
        register_mark_tools(registry, decorate, allow_mutations);
        register_region_tool(registry, decorate, allow_mutations);
    }
}

void DesktopImageTurn::register_native_ocr(mira::BuiltinToolRegistry &registry,
                                           const DesktopAtomToolset::Decorator &decorate) {
    if (!desktop_.ocr_image)
        return;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.vision.ocr";
    spec.version = {2, 0, 0};
    spec.description =
        "Read untrusted text from a fresh native desktop image using the current task's sharing "
        "session and bundled OCR. Takes no arguments, no display/window id. First observation may "
        "ask for system sharing; subsequent frames reuse that authorization, at most eight per "
        "task. Boxes are IMAGE pixels in a <=1600-side preview, read-only, no clickable refs. "
        "The source image is supplied too when the model supports images. OCR does not prove page "
        "navigation or task success; desktop input may remain unavailable. A truncated result "
        "contains only a prefix of whole regions and cannot prove text is absent.";
    spec.parameters_schema =
        mira::JsonSchema{JsonValue{JsonValue::Object{{"type", "object"},
                                                     {"properties", JsonValue{JsonValue::Object{}}},
                                                     {"required", JsonValue{JsonValue::Array{}}},
                                                     {"additionalProperties", false}}}};
    mira::BuiltinToolHandler handler = [this](const auto &args,
                                              const auto &ctx) -> mira::Result<JsonValue> {
        try {
            return native_ocr(args, ctx);
        } catch (...) {
            if (cancel_.cancelled() || desktop_.actions->authority_epoch() != native_authority_) {
                native_session_.reset();
                native_failed_ = true;
            }
            return error(ErrorCode::PlatformError,
                         "native_ocr_analysis_failed: provider_exception");
        }
    };
    if (decorate)
        handler = decorate(spec.wire_name, std::move(handler));
    if (!registry.register_tool(std::move(spec), std::move(handler)))
        throw std::logic_error("native OCR tool registration rejected");
}
mira::Result<JsonValue> DesktopImageTurn::native_ocr(const JsonValue &args,
                                                     const mira::OperationContext &ctx) {
    if (!args.as_object() || !args.as_object()->empty())
        return error(ErrorCode::InvalidArgument,
                     "native OCR accepts no arguments or window/display ids");
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return error(ErrorCode::InvalidArgument, "native OCR owner mismatch");
    auto captured = capture(JsonValue{JsonValue::Object{{"source", "desktop"}}}, ctx);
    if (!captured) {
        native_session_.reset();
        native_failed_ = true;
        return captured.error();
    }
    if (!native_frame_)
        return error(ErrorCode::PlatformError, "native OCR source frame unavailable");
    const auto epoch = epoch_, authority = native_authority_;
    const auto end =
        std::min(ctx.deadline.value_or(std::chrono::steady_clock::now() + std::chrono::seconds(5)),
                 std::chrono::steady_clock::now() + std::chrono::seconds(5));
    const desktop::CancelToken bounded(
        [parent = cancel_, actions = desktop_.actions, ctx, end, epoch, authority] {
            return parent.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                   actions->epoch() != epoch || actions->authority_epoch() != authority ||
                   std::chrono::steady_clock::now() >= end;
        });
    auto read = desktop_.ocr_image(*native_frame_, bounded, end);
    if (cancel_.cancelled() || desktop_.actions->authority_epoch() != authority) {
        native_session_.reset();
        native_failed_ = true;
        return error(ErrorCode::Cancelled, "native OCR sharing authority withdrawn");
    }
    if (bounded.cancelled() || read.cancelled)
        return error(ErrorCode::PlatformError, "native_ocr_expired: obtain a new observation");
    if (!read.ok) {
        std::string code = read.error.code;
        if (code.empty() || code.size() > 64 ||
            !std::all_of(code.begin(), code.end(),
                         [](unsigned char c) { return (c >= 'a' && c <= 'z') || c == '_'; }))
            code = "analysis_failed";
        return error(ErrorCode::PlatformError, "native_ocr_analysis_failed: " + code);
    }
    if (read.regions.size() > 128 || read.model_revision.size() > 256 ||
        read.backend_calls > std::uint64_t(INT64_MAX))
        return error(ErrorCode::ResourceExhausted, "native OCR result budget exceeded");
    const auto total_regions =
        read.total_region_count ? read.total_region_count : read.regions.size();
    if (total_regions < read.regions.size() || total_regions > 512)
        return error(ErrorCode::InvalidArgument, "native OCR source region count invalid");
    JsonValue::Array regions;
    for (const auto &r : read.regions) {
        if (r.text.size() > 1024 || !desktop::is_valid_utf8(r.text) ||
            !std::isfinite(r.confidence) || r.confidence < 0 || r.confidence > 1 ||
            !std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) ||
            !std::isfinite(r.height) || r.x < 0 || r.y < 0 || r.width <= 0 || r.height <= 0 ||
            r.x + r.width > native_frame_->width || r.y + r.height > native_frame_->height)
            return error(ErrorCode::InvalidArgument, "native OCR text or image bounds invalid");
        regions.emplace_back(JsonValue::Object{{"text", r.text},
                                               {"confidence", r.confidence},
                                               {"x", r.x},
                                               {"y", r.y},
                                               {"width", r.width},
                                               {"height", r.height}});
    }
    JsonValue result{JsonValue::Object{
        {"schema", std::int64_t(2)},
        {"source", "ocr"},
        {"source_kind", "desktop"},
        {"image_artifact_id", *metadata_.find("artifact_id")},
        {"width", std::int64_t(native_frame_->width)},
        {"height", std::int64_t(native_frame_->height)},
        {"coordinate_space", "image_pixels"},
        {"actionable", false},
        {"observation_only", true},
        {"verified", false},
        {"model_revision", read.model_revision},
        {"backend_calls", static_cast<std::int64_t>(read.backend_calls)},
        {"environment_epoch", static_cast<std::int64_t>(epoch)},
        {"received_at_ms", *metadata_.find("received_at_ms")},
        {"observed_at_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count()},
        {"truncated", false},
        {"total_region_count", static_cast<std::int64_t>(total_regions)},
        {"region_count", std::int64_t(0)},
        {"regions", JsonValue{JsonValue::Array{}}}}};
    // DEC-081: OCR publishes the replacement interactive scope when its image
    // will be supplied to the model. Observation regions themselves stay non-actionable.
    const bool input_actionable = native_input_enabled_ && supply_images_ && native_session_ &&
                                  native_session_->input_supported();
    result.set("input_actionable", input_actionable);
    if (input_actionable)
        result.set("image_scope", *metadata_.find("image_scope"));
    // Preserve complete regions and the wire budget, including JSON escaping.
    // Reserve the growth of region_count from one to three digits.
    auto bytes = mira::to_json_string(result).size() + 2;
    JsonValue::Array admitted;
    for (auto &region : regions) {
        const auto growth = mira::to_json_string(region).size() + !admitted.empty();
        if (bytes + growth > (16u << 10))
            break;
        bytes += growth;
        admitted.push_back(std::move(region));
    }
    result.set("truncated", admitted.size() != total_regions);
    result.set("region_count", static_cast<std::int64_t>(admitted.size()));
    result.set("regions", JsonValue{std::move(admitted)});
    return result;
}

mira::Result<JsonValue> DesktopImageTurn::capture_desktop(const JsonValue &args,
                                                          const mira::OperationContext &ctx) {
    const auto *interactive = args.find("interaction");
    const bool requested = interactive && interactive->as_boolean() && *interactive->as_boolean();
    const auto source_kind = text(args, "source");
    const bool region_source = source_kind == "roi" || source_kind == "window";
    if ((source_kind != "desktop" && !region_source) || !args.as_object() ||
        (interactive && (!interactive->as_boolean() || !native_input_enabled_)) ||
        (requested && source_kind != "desktop"))
        return error(ErrorCode::InvalidArgument,
                     "native desktop capture requires source=desktop|roi|window");
    std::int64_t roi_x = 0, roi_y = 0, roi_width = 0, roi_height = 0;
    std::string window_id;
    if (source_kind == "desktop") {
        if (args.as_object()->size() != (interactive ? 2u : 1u))
            return error(ErrorCode::InvalidArgument,
                         "source=desktop takes only source and interaction");
    } else if (source_kind == "roi") {
        const auto field_int = [&](const char *key) -> std::optional<std::int64_t> {
            const auto *v = args.find(key);
            return v && v->as_integer() ? std::optional<std::int64_t>{*v->as_integer()}
                                        : std::nullopt;
        };
        const auto x = field_int("x"), y = field_int("y"), width = field_int("width"),
                   height = field_int("height");
        if (args.as_object()->size() != 5u || !x || !y || !width || !height || *x < 0 || *y < 0 ||
            *width <= 0 || *height <= 0 || *x > 1048576 || *y > 1048576 || *width > 1048576 ||
            *height > 1048576 || !text(args, "source_id").empty() || interactive)
            return error(ErrorCode::InvalidArgument,
                         "source=roi requires only x/y/width/height in shared monitor pixels");
        roi_x = *x;
        roi_y = *y;
        roi_width = *width;
        roi_height = *height;
    } else {
        window_id = text(args, "source_id");
        if (args.as_object()->size() != 2u || window_id.empty() || window_id.size() > 256 ||
            interactive)
            return error(ErrorCode::InvalidArgument,
                         "source=window requires source_id from desktop.window.list");
    }
    if (!desktop_.capture_scope.empty())
        return error(ErrorCode::UnsupportedCapability,
                     "native desktop observation cannot satisfy a window capture scope");
    const auto authority = desktop_.actions->authority_epoch();
    if (!authority || (captures_ && native_authority_ != authority)) {
        native_session_.reset();
        native_failed_ = true;
        return error(ErrorCode::Cancelled, "desktop sharing authority was withdrawn");
    }
    if (native_failed_ || (captures_ && (!native_session_ || !native_session_->reusable())))
        return error(
            ErrorCode::ResourceExhausted,
            "native sharing ended or failed; explain the result, no reauthorization in this task");
    if (captures_ && interactive &&
        requested != (native_session_ && native_session_->input_supported()))
        return error(ErrorCode::InvalidArgument, "sharing mode cannot change within this task");
    if (captures_ >= (native_session_ && native_session_->input_supported() ? 32u : 8u))
        return error(ErrorCode::ResourceExhausted, "native desktop frame budget exhausted");
    const auto allowed =
        desktop_.authorize
            ? desktop_.authorize(
                  "screen.capture", "desktop", [this] { return cancel_.cancelled(); },
                  ctx.task.to_string(), ctx.operation.to_string())
            : desktop_.gate("screen.capture", "desktop", [this] { return cancel_.cancelled(); });
    if (!allowed || cancel_.cancelled() || desktop_.actions->authority_epoch() != authority) {
        native_session_.reset();
        native_failed_ = true;
        return error(!allowed ? ErrorCode::PermissionDenied : ErrorCode::Cancelled,
                     "screen.capture permission or sharing authority withdrawn");
    }
    if (!captures_) {
        if (requested) {
            const auto input_allowed =
                desktop_.authorize
                    ? desktop_.authorize(
                          "input.inject", "desktop", [this] { return cancel_.cancelled(); },
                          ctx.task.to_string(), ctx.operation.to_string())
                    : desktop_.gate("input.inject", "desktop",
                                    [this] { return cancel_.cancelled(); });
            if (!input_allowed || cancel_.cancelled() ||
                desktop_.actions->authority_epoch() != authority) {
                native_failed_ = true;
                return error(!input_allowed ? ErrorCode::PermissionDenied : ErrorCode::Cancelled,
                             "input.inject permission denied or authority withdrawn");
            }
        }
        native_session_ = requested
                              ? desktop_.environment->screen()->create_desktop_interaction_session()
                              : desktop_.environment->screen()->create_desktop_capture_session();
        if (requested && !native_session_)
            return error(ErrorCode::UnsupportedCapability, "interactive sharing unavailable");
    }
    native_authority_ = authority;
    ++captures_;
    const auto epoch = desktop_.actions->epoch();
    desktop::CaptureLimits limits;
    limits.max_bytes = desktop_capture_bytes;
    limits.newer_than_sequence = native_newer_than_sequence_;
    const desktop::CancelToken active([this, authority] {
        return cancel_.cancelled() || desktop_.actions->authority_epoch() != authority;
    });
    const auto out = native_session_
                         ? native_session_->capture(limits, active)
                         : desktop_.environment->screen()->capture_desktop(limits, active);
    if (out.ok && limits.newer_than_sequence &&
        out.frame.source_sequence <= limits.newer_than_sequence) {
        native_session_.reset();
        native_failed_ = true;
        return error(ErrorCode::PlatformError, "new_source_frame_required");
    }
    if (!out.ok || out.cancelled || active.cancelled()) {
        native_session_.reset();
        native_failed_ = true;
        return error(out.cancelled || active.cancelled() ? ErrorCode::Cancelled
                                                         : ErrorCode::PlatformError,
                     out.error.code + ": " + out.error.message.substr(0, 240));
    }
    if (desktop_.actions->epoch() != epoch)
        return error(ErrorCode::Cancelled, "native desktop image invalidated before publication");
    auto frame = out.frame;
    if (frame.format != desktop::ImageFormat::Bgra8 || frame.width <= 0 || frame.height <= 0 ||
        frame.stride < std::size_t(frame.width) * 4 ||
        frame.stride > desktop_capture_bytes / std::size_t(frame.height) ||
        frame.pixels.size() != frame.stride * std::size_t(frame.height))
        return error(ErrorCode::InvalidArgument, "invalid native desktop frame");
    // M7-18: region sources crop the fresh shared-monitor frame locally — no
    // extra authorization, no extra stream, one frame budget per capture.
    Geometry area{0, 0, frame.width, frame.height};
    std::string kind = "desktop";
    std::string kind_id;
    if (region_source) {
        if (source_kind == "window") {
            auto *windows = desktop_.environment->window();
            if (windows == nullptr)
                return error(ErrorCode::UnsupportedCapability,
                             "window enumeration is unavailable on this desktop");
            desktop::WindowListLimits unlimited;
            unlimited.max_windows = 4096;
            const auto listed = windows->list_windows(unlimited, active);
            if (!listed.ok)
                return error(ErrorCode::PlatformError,
                             listed.error.code + ": window enumeration failed");
            const auto *match = static_cast<const desktop::WindowInfo *>(nullptr);
            for (const auto &window : listed.windows)
                if (window.id == window_id)
                    match = &window;
            if (match == nullptr)
                return error(ErrorCode::InvalidArgument, "no such window");
            roi_x = match->geometry.x;
            roi_y = match->geometry.y;
            roi_width = match->geometry.width;
            roi_height = match->geometry.height;
            kind_id = window_id;
        }
        // Screen-coordinate geometry can fall outside the shared monitor
        // (multi-monitor setups): intersect, then fail closed on an empty
        // intersection instead of guessing an offset.
        const auto clip_left = std::max<std::int64_t>(roi_x, 0);
        const auto clip_top = std::max<std::int64_t>(roi_y, 0);
        const auto clip_right =
            std::min<std::int64_t>(roi_x + roi_width, static_cast<std::int64_t>(frame.width));
        const auto clip_bottom =
            std::min<std::int64_t>(roi_y + roi_height, static_cast<std::int64_t>(frame.height));
        if (clip_left >= clip_right || clip_top >= clip_bottom)
            return error(ErrorCode::InvalidArgument,
                         "capture region lies outside the shared monitor");
        area = Geometry{static_cast<std::int32_t>(clip_left), static_cast<std::int32_t>(clip_top),
                        static_cast<std::int32_t>(clip_right - clip_left),
                        static_cast<std::int32_t>(clip_bottom - clip_top)};
        desktop::ImageFrame part;
        part.format = frame.format;
        part.width = area.width;
        part.height = area.height;
        part.stride = static_cast<std::size_t>(area.width) * 4;
        part.source_sequence = frame.source_sequence;
        part.pixels.resize(part.stride * std::size_t(area.height));
        for (std::int32_t row = 0; row < area.height; ++row)
            std::memcpy(part.pixels.data() + std::size_t(row) * part.stride,
                        frame.pixels.data() + std::size_t(area.y + row) * frame.stride +
                            std::size_t(area.x) * 4,
                        part.stride);
        frame = std::move(part);
        kind = source_kind;
        if (source_kind == "roi")
            kind_id.clear();
    }
    if (std::max(frame.width, frame.height) > 1600) {
        mirador::ImageView view;
        view.data = reinterpret_cast<const std::byte *>(frame.pixels.data());
        view.width = frame.width;
        view.height = frame.height;
        view.row_stride_bytes = static_cast<std::int64_t>(frame.stride);
        view.format = mirador::PixelFormat::kBgra8;
        const auto longest = std::max(frame.width, frame.height);
        const auto w = std::max(1, static_cast<int>(std::int64_t(frame.width) * 1600 / longest));
        const auto h = std::max(1, static_cast<int>(std::int64_t(frame.height) * 1600 / longest));
        const auto resized = mirador::resize_area(view, w, h, desktop_image_bytes);
        if (!resized.ok())
            return error(ErrorCode::PlatformError, "native desktop preview resize failed");
        const auto pixels = resized.value().view();
        frame.width = w;
        frame.height = h;
        frame.stride = static_cast<std::size_t>(pixels.row_stride_bytes);
        frame.pixels.resize(frame.stride * std::size_t(h));
        std::memcpy(frame.pixels.data(), pixels.data, frame.pixels.size());
    }
    return finish_native_publish(std::move(frame), out.frame, area, kind, kind_id,
                                 out.frame.source_sequence, epoch);
}

mira::Result<JsonValue>
DesktopImageTurn::finish_native_publish(desktop::ImageFrame frame,
                                        const desktop::ImageFrame &raw_frame, const Geometry &area,
                                        const std::string &kind, const std::string &kind_id,
                                        std::uint64_t source_sequence, std::uint64_t epoch) {
    const auto captured = std::chrono::steady_clock::now();
    const desktop::CancelToken bounded([this, captured] {
        return cancel_.cancelled() ||
               std::chrono::steady_clock::now() - captured >= std::chrono::seconds(5);
    });
    auto result = publish_image(frame, area, kind, kind_id, epoch, captured, bounded);
    if (!result)
        return result;
    auto fields = *metadata_.as_object();
    if (kind == "desktop")
        std::erase_if(fields, [](const auto &item) {
            return item.first.starts_with("desktop_per_image_") ||
                   item.first.starts_with("origin_") || item.first.starts_with("scale_");
        });
    metadata_ = JsonValue{std::move(fields)};
    metadata_.set("region_actionable", false);
    metadata_.set("desktop_coordinates_available", false);
    metadata_.set("observation_only", !(native_session_ && native_session_->input_supported()));
    if (native_input_enabled_) {
        metadata_.set("input_actionable", native_session_ && native_session_->input_supported());
        if (native_session_ && native_session_->input_supported())
            metadata_.set("image_scope", mira::OperationId::generate().to_string());
    }
    metadata_.set("received_at_ms", *metadata_.find("captured_at_ms"));
    auto received_fields = *metadata_.as_object();
    std::erase_if(received_fields,
                  [](const auto &field) { return field.first == "captured_at_ms"; });
    metadata_ = JsonValue{std::move(received_fields)};
    metadata_.set("capture_timestamp_known", false);
    metadata_.set("source_sequence",
                  static_cast<std::int64_t>(std::min(source_sequence, std::uint64_t(INT64_MAX))));
    native_newer_than_sequence_ = 0;
    // The guard's comparison baseline is always the RAW FULL frame — never the
    // preview-scaled or region-cropped image the model sees (M7-18/M7-19).
    if (native_session_ && native_session_->input_supported())
        native_input_frame_ = raw_frame;
    if (desktop_.ocr_image || native_input_enabled_)
        native_frame_ = std::move(frame);
    return metadata_;
}

mira::Result<JsonValue> DesktopImageTurn::capture(const JsonValue &args,
                                                  const mira::OperationContext &ctx) {
    if (cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()))
        return error(ErrorCode::Cancelled, "desktop capture cancelled");
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch)
        return error(ErrorCode::InvalidArgument, "desktop capture owner mismatch");
    clear();
    const auto kind = text(args, "source"), id = text(args, "source_id");
    if (desktop_.environment->screen()->desktop_observation_only())
        return capture_desktop(args, ctx);
    // DEC-085 capability honesty: a degraded (Wayland) screen capability
    // must surface as a canonical platform limitation, never as a generic
    // argument or I/O failure the model cannot tell apart from its own
    // mistakes.
    const auto capability = desktop_.environment->screen_capability();
    if ((kind != "display" && kind != "window" && kind != "roi") || id.empty() || id.size() > 256) {
        if (kind == "desktop" && capability != mirage::desktop::ScreenCapability::Full)
            return error(ErrorCode::UnsupportedCapability,
                         "platform_not_supported: full-screen X11 capture is unavailable in this "
                         "Wayland session (root-window reads fail); use the authorized desktop "
                         "observation capture or a window capture, or explain the limitation");
        return error(ErrorCode::InvalidArgument, "invalid capture source");
    }
    if (captures_ == 4)
        return error(ErrorCode::ResourceExhausted, "desktop capture count exceeds 4 per turn");
    const auto allowed = desktop_.authorize ? desktop_.authorize(
                                                  "screen.capture", kind + ":" + id,
                                                  [this] { return cancel_.cancelled(); },
                                                  ctx.task.to_string(), ctx.operation.to_string())
                                            : desktop_.gate("screen.capture", kind + ":" + id,
                                                            [this] { return cancel_.cancelled(); });
    if (!allowed)
        return error(ErrorCode::PermissionDenied, "screen.capture denied before acquisition");
    if (cancel_.cancelled())
        return error(ErrorCode::Cancelled, "desktop capture cancelled");
    const auto start = std::chrono::steady_clock::now();
    const mirage::desktop::CancelToken bounded([this, start] {
        return cancel_.cancelled() || std::chrono::steady_clock::now() - start >= acquire_budget_;
    });
    const auto epoch = desktop_.actions->epoch();
    auto &screen = *desktop_.environment->screen();
    auto *windows = desktop_.environment->window();
    const auto front = windows ? windows->front_window(bounded) : desktop::WindowQueryOutcome{};
    auto geometry = [&]() -> mira::Result<Geometry> {
        if (kind == "window") {
            if (!windows)
                return error(ErrorCode::UnsupportedCapability, "window capture has no geometry");
            const auto listed = windows->list_windows({}, bounded);
            if (!listed.ok)
                return error(ErrorCode::PlatformError, listed.error.code);
            for (const auto &window : listed.windows)
                if (window.id == id)
                    return window.geometry;
        } else {
            const auto listed = screen.list_displays({}, bounded);
            if (!listed.ok)
                return error(ErrorCode::PlatformError, listed.error.code);
            for (const auto &display : listed.displays)
                if (display.id == id)
                    return display.geometry;
        }
        return error(ErrorCode::NotFound, "capture source no longer exists");
    };
    const auto before = geometry();
    if (!before)
        return before.error();
    auto area = before.value();
    if (kind == "roi") {
        for (auto [name, field] : {std::pair{"x", &area.x},
                                   {"y", &area.y},
                                   {"width", &area.width},
                                   {"height", &area.height}}) {
            const auto *value = args.find(name);
            if (!value || !value->as_integer() || *value->as_integer() < -1048576 ||
                *value->as_integer() > 1048576)
                return error(ErrorCode::InvalidArgument, "ROI needs bounded x/y/width/height");
            *field = static_cast<std::int32_t>(*value->as_integer());
        }
        const auto &display = before.value();
        if (area.x < display.x || area.y < display.y ||
            std::int64_t{area.x} + area.width > std::int64_t{display.x} + display.width ||
            std::int64_t{area.y} + area.height > std::int64_t{display.y} + display.height)
            return error(ErrorCode::InvalidArgument, "ROI is outside its declared display");
    } else if (args.find("x") || args.find("y") || args.find("width") || args.find("height")) {
        return error(ErrorCode::InvalidArgument, "ROI fields require source=roi");
    }
    if (area.width <= 0 || area.height <= 0)
        return error(ErrorCode::InvalidArgument, "capture extents must be positive");
    if (!desktop_.capture_scope.empty() &&
        (!front.ok || !front.found || front.window.id != desktop_.capture_scope ||
         area.x < front.window.geometry.x || area.y < front.window.geometry.y ||
         std::int64_t{area.x} + area.width >
             std::int64_t{front.window.geometry.x} + front.window.geometry.width ||
         std::int64_t{area.y} + area.height >
             std::int64_t{front.window.geometry.y} + front.window.geometry.height))
        return error(ErrorCode::InvalidArgument, "capture outside configured window scope");
    if (std::uint64_t{static_cast<unsigned>(area.width)} * static_cast<unsigned>(area.height) >
        (kind == "roi" ? desktop_image_bytes : desktop_capture_bytes) / 4)
        return error(ErrorCode::ResourceExhausted,
                     "capture exceeds raw pixel budget; request a smaller ROI");
    const auto source_displays = screen.list_displays({}, bounded);
    ++captures_;
    desktop::CaptureLimits limits;
    limits.max_bytes = kind == "roi" ? desktop_image_bytes : desktop_capture_bytes;
    const auto outcome = kind == "window"    ? screen.capture_window(id, limits, bounded)
                         : kind == "display" ? screen.capture_display(id, limits, bounded)
                                             : screen.capture_roi(area, limits, bounded);
    if (!outcome.ok || outcome.cancelled) {
        if (!outcome.cancelled && capability != mirage::desktop::ScreenCapability::Full)
            return error(ErrorCode::UnsupportedCapability,
                         "platform_not_supported: direct " + kind +
                             " capture failed in this Wayland session (" + outcome.error.code +
                             "); use the authorized desktop observation capture or explain the "
                             "limitation instead of retrying the same capture");
        return error(outcome.cancelled ? ErrorCode::Cancelled : ErrorCode::PlatformError,
                     outcome.error.code);
    }
    if (bounded.cancelled())
        return error(ErrorCode::Cancelled, "capture cancelled or expired before publication");
    const auto after = geometry();
    if (!after || !same(before.value(), after.value()) || desktop_.actions->epoch() != epoch ||
        outcome.frame.width != area.width || outcome.frame.height != area.height)
        return error(ErrorCode::InvalidArgument, "capture geometry or desktop epoch changed");
    const auto &frame = outcome.frame;
    const auto raw_budget = kind == "roi" ? desktop_image_bytes : desktop_capture_bytes;
    if (frame.format != desktop::ImageFormat::Bgra8 ||
        frame.stride < static_cast<std::size_t>(frame.width) * 4 ||
        frame.stride > raw_budget / static_cast<std::size_t>(frame.height) ||
        frame.pixels.size() != frame.stride * static_cast<std::size_t>(frame.height))
        return error(ErrorCode::InvalidArgument, "invalid source frame");
    const auto publish = [&](const desktop::ImageFrame &preview) -> mira::Result<JsonValue> {
        auto result = publish_image(preview, area, kind, id, epoch, start, bounded);
        if (!result)
            return result;
        const auto after_front =
            windows ? windows->front_window(bounded) : desktop::WindowQueryOutcome{};
        const auto displays = screen.list_displays({}, bounded);
        const auto fits = [&](const desktop::DisplayInfo &d) {
            return area.x >= d.geometry.x && area.y >= d.geometry.y &&
                   std::int64_t{area.x} + area.width <=
                       std::int64_t{d.geometry.x} + d.geometry.width &&
                   std::int64_t{area.y} + area.height <=
                       std::int64_t{d.geometry.y} + d.geometry.height &&
                   (kind == "window" || d.id == id);
        };
        const auto display = std::find_if(displays.displays.begin(), displays.displays.end(), fits);
        const bool stable =
            front.ok && after_front.ok && front.found == after_front.found &&
            (!front.found || (front.window.id == after_front.window.id &&
                              front.window.title == after_front.window.title &&
                              same(front.window.geometry, after_front.window.geometry)));
        auto *input = desktop_.environment->input();
        const bool display_stable =
            displays.ok && display != displays.displays.end() && source_displays.ok &&
            std::any_of(source_displays.displays.begin(), source_displays.displays.end(),
                        [&](const auto &d) {
                            return d.id == display->id && same(d.geometry, display->geometry) &&
                                   d.primary == display->primary;
                        });
        const bool bounded_geometry = area.x >= -1048576 && area.x <= 1048576 &&
                                      area.y >= -1048576 && area.y <= 1048576 &&
                                      area.width <= 65535 && area.height <= 65535;
        const bool source_focused = kind != "window" || (front.found && front.window.id == id &&
                                                         same(front.window.geometry, area));
        const bool actionable = region_tool_enabled_ && source_focused && bounded_geometry &&
                                stable && display_stable && input &&
                                input->complete_actions_supported() && !bounded.cancelled() &&
                                desktop_.actions->epoch() == epoch;
        metadata_.set("region_actionable", actionable);
        if (actionable) {
            RegionSource source;
            source.frame = frame;
            source.area = area;
            source.display = *display;
            source.front = front;
            source.window = kind == "window" ? id : "";
            source.scope = mira::OperationId::generate().to_string();
            source.image_width = preview.width;
            source.image_height = preview.height;
            metadata_.set("image_scope", source.scope);
            metadata_.set("display_id", display->id);
            metadata_.set("window_id", front.found ? front.window.id : "");
            metadata_.set("region_max_age_ms", std::int64_t{60000});
            region_source_ = std::move(source);
        }
        return metadata_;
    };
    // M7-17: large ROIs preview-scale like display/window captures (exact
    // origin/scale metadata keeps click_region mapping); small ROIs stay 1:1.
    if (std::max(frame.width, frame.height) <= 1600)
        return publish(frame);
    if (frame.format != desktop::ImageFormat::Bgra8 ||
        frame.stride < std::size_t(frame.width) * 4 ||
        frame.stride > desktop_capture_bytes / std::size_t(frame.height) ||
        frame.pixels.size() < frame.stride * std::size_t(frame.height))
        return error(ErrorCode::InvalidArgument, "invalid preview source frame");
    mirador::ImageView view;
    view.data = reinterpret_cast<const std::byte *>(frame.pixels.data());
    view.width = frame.width;
    view.height = frame.height;
    view.row_stride_bytes = static_cast<std::int64_t>(frame.stride);
    view.format = mirador::PixelFormat::kBgra8;
    const auto longest = std::max(frame.width, frame.height);
    const auto w = std::max(1, static_cast<int>(std::int64_t(frame.width) * 1600 / longest));
    const auto h = std::max(1, static_cast<int>(std::int64_t(frame.height) * 1600 / longest));
    const auto resized = mirador::resize_area(view, w, h, desktop_image_bytes);
    if (!resized.ok() || bounded.cancelled())
        return error(bounded.cancelled() ? ErrorCode::Cancelled : ErrorCode::ResourceExhausted,
                     "preview resize failed or expired");
    const auto pixels = resized.value().view();
    desktop::ImageFrame preview;
    preview.width = w;
    preview.height = h;
    preview.stride = static_cast<std::size_t>(pixels.row_stride_bytes);
    preview.pixels.resize(preview.stride * std::size_t(h));
    std::memcpy(preview.pixels.data(), pixels.data, preview.pixels.size());
    return publish(preview);
}

mira::Result<JsonValue>
DesktopImageTurn::publish_image(const desktop::ImageFrame &frame, const Geometry &area,
                                const std::string &kind, const std::string &id, std::uint64_t epoch,
                                std::chrono::steady_clock::time_point start,
                                const desktop::CancelToken &bounded, JsonValue::Object extra) {
    const auto png = encode_desktop_png(frame, bounded);
    if (!png)
        return png.error();
    mira::ArtifactWriteSpec write;
    write.media_type = "image/png";
    write.sensitivity = mira::Sensitivity::Sensitive;
    write.max_bytes = desktop_image_bytes;
    // MemoryArtifactStore IDs are content-addressed: retire before re-committing identical pixels.
    clear();
    auto writer = store_->begin(write);
    if (!writer)
        return writer.error();
    const auto written = writer.value().write(png.value());
    if (!written)
        return written.error();
    auto committed = store_->commit(writer.value());
    if (!committed)
        return committed.error();
    if (bounded.cancelled() || desktop_.actions->epoch() != epoch) {
        (void)store_->erase({committed.value().id, "late desktop capture"});
        return error(ErrorCode::Cancelled,
                     "capture cancelled, expired, or invalidated before publish");
    }
    image_ = committed.value();
    captured_ = start;
    epoch_ = epoch;
    metadata_ = JsonValue{JsonValue::Object{
        {"schema", std::int64_t{1}},
        {"source", kind},
        {"source_id", id},
        {"artifact_id", image_->id.to_string()},
        {"sha256", image_->digest.to_string()},
        {"media_type", "image/png"},
        {"sensitivity", "sensitive"},
        {"byte_size", static_cast<std::int64_t>(image_->byte_size)},
        {"width", std::int64_t{frame.width}},
        {"height", std::int64_t{frame.height}},
        {"source_width", std::int64_t{area.width}},
        {"source_height", std::int64_t{area.height}},
        {"coordinate_space", "image_pixels"},
        {"actionable", kind == "som"},
        {"desktop_per_image_x_numerator", std::int64_t{area.width}},
        {"desktop_per_image_x_denominator", std::int64_t{frame.width}},
        {"desktop_per_image_y_numerator", std::int64_t{area.height}},
        {"desktop_per_image_y_denominator", std::int64_t{frame.height}},
        {"origin_x", std::int64_t{area.x}},
        {"origin_y", std::int64_t{area.y}},
        {"scale_numerator", std::int64_t{frame.width == area.width ? 1 : frame.width}},
        {"scale_denominator", std::int64_t{frame.width == area.width ? 1 : area.width}},
        {"environment_epoch", static_cast<std::int64_t>(epoch)},
        {"task_epoch", static_cast<std::int64_t>(owner_.task_epoch)},
        {"captured_at_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count()},
        {"max_age_ms", static_cast<std::int64_t>(image_max_age_.count())},
        {"verified", false}}};
    auto object = *metadata_.as_object();
    for (auto &field : extra)
        object.push_back(std::move(field));
    metadata_ = JsonValue{std::move(object)};
    return metadata_;
}

mira::Result<std::vector<mira::ModelInputItem>>
DesktopImageTurn::supply(const mira::AgentLoopSpec &spec) {
    supply_error_.reset();
    expect_image_ = false;
    if (spec.task_id != owner_.task || spec.session_id != owner_.session ||
        spec.task_epoch != owner_.task_epoch || cancel_.cancelled()) {
        supply_error_ =
            error(cancel_.cancelled() ? ErrorCode::Cancelled : ErrorCode::InvalidArgument,
                  "desktop image supply owner, cancellation, or deadline mismatch");
        clear();
        native_session_.reset();
        native_failed_ = true;
        return *supply_error_;
    }
    if (native_session_ && desktop_.actions->authority_epoch() != native_authority_) {
        native_session_.reset();
        native_failed_ = true;
        clear();
    }
    if (!supply_images_ || !image_)
        return std::vector<mira::ModelInputItem>{};
    mira::ModelInputItem item;
    item.role = mira::ModelRole::User;
    if (desktop_.actions->epoch() != epoch_ ||
        std::chrono::steady_clock::now() - captured_ >= image_max_age_) {
        clear();
        item.content.emplace_back(mira::TextPart{
            "Previous desktop image retired: age or environment epoch changed. Capture again "
            "before visually verifying a result.",
            mira::Sensitivity::Internal});
    } else {
        expect_image_ = true;
        item.content.emplace_back(mira::TextPart{
            "Untrusted desktop image; coordinates and source: " + mira::to_json_string(metadata_),
            mira::Sensitivity::Sensitive});
        item.content.emplace_back(
            mira::ImagePart{reference(*image_), mira::ImageDetail::Auto, "image/png"});
    }
    return std::vector<mira::ModelInputItem>{std::move(item)};
}

std::optional<mira::InputReuseMeta>
DesktopImageTurn::input_reuse_identity(const mira::AgentLoopSpec &spec) const {
    // The same owner/cancellation gate as supply(): an off-owner request is a
    // wiring error answered with no identity, never with an annotation.
    if (spec.task_id != owner_.task || spec.session_id != owner_.session ||
        spec.task_epoch != owner_.task_epoch || cancel_.cancelled())
        return std::nullopt;
    // Only requests that actually carry this capture advance the count; the
    // pinned loop annotates image-bearing supplies only, and a request whose
    // image aged out offered no image to annotate.
    if (!supply_images_ || !image_ || !expect_image_)
        return std::nullopt;
    // Content digest, not artifact id: a recapture of identical pixels is the
    // same sight for the model (DEC-055 reuses the artifact for it), while a
    // new artifact of changed content restarts the count.
    return source_->record_input_reuse(image_->digest.to_string(), desktop_.actions->epoch());
}

mira::Result<void> DesktopImageTurn::validate_request(const mira::ModelRequest &request,
                                                      const mira::OperationContext &context) {
    if (supply_error_)
        return *supply_error_; // Mira's text degradation must not hide a host identity error.
    if (cancel_.cancelled() || context.cancelled_or_expired(mira::Timestamp::now()))
        return error(ErrorCode::Cancelled, "desktop image request cancelled");
    if (request.task_id != owner_.task || request.task_epoch != owner_.task_epoch ||
        context.session != owner_.session || context.task != owner_.task ||
        context.task_epoch != owner_.task_epoch)
        return error(ErrorCode::InvalidArgument, "desktop image request owner mismatch");
    for (const auto &item : request.input)
        for (const auto &part : item.content)
            if (const auto *image = std::get_if<mira::ImagePart>(&part);
                image &&
                (!expect_image_ || !image_ || image->source.id != image_->id ||
                 image->source.digest != image_->digest ||
                 image->source.byte_size != image_->byte_size ||
                 image->source.sensitivity != mira::Sensitivity::Sensitive ||
                 image->source.media_type != "image/png" || image->media_type != "image/png" ||
                 image->detail != mira::ImageDetail::Auto))
                return error(ErrorCode::InvalidArgument, "foreign or retired desktop image");
    if (!expect_image_)
        return {};
    if (!image_ || epoch_ != desktop_.actions->epoch() ||
        std::chrono::steady_clock::now() - captured_ >= image_max_age_)
        return error(ErrorCode::InvalidArgument, "desktop image invalidated before inference");
    for (const auto &item : request.input)
        for (const auto &part : item.content)
            if (const auto *image = std::get_if<mira::ImagePart>(&part);
                image && image->source.id == image_->id && image->source.digest == image_->digest) {
                supplied_image_ = true;
                return {};
            }
    return error(ErrorCode::ResourceExhausted, "required desktop image dropped before inference");
}
} // namespace mirage::integration::detail
