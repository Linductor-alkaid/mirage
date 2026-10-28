#include "desktop_image_supply.hpp"
#include <algorithm>
#include <cstdint>
#include <mirage/conversation.hpp>
#include <optional>
#include <set>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
using Clock = std::chrono::steady_clock;
Json tracking_projection(const std::optional<PerceptionTracking> &tracking) {
    if (!tracking)
        return Json{};
    Json::Array tracks;
    std::size_t bytes = 0;
    for (const auto &track : tracking->tracks) {
        Json item{Json::Object{
            {"entity", static_cast<std::int64_t>(track.entity)},
            {"instance_generation", static_cast<std::int64_t>(track.instance_generation)},
            {"state", track.state},
            {"evidence", track.evidence},
            {"verified_sequence", static_cast<std::int64_t>(track.verified_sequence)},
            {"reacquired", track.reacquired},
            {"discontinuous", track.discontinuous},
            {"x", track.bounds.x},
            {"y", track.bounds.y},
            {"width", track.bounds.width},
            {"height", track.bounds.height}}};
        const auto size = mira::to_json_string(item).size();
        if (tracks.size() >= 4 || bytes + size > 650)
            break;
        bytes += size;
        tracks.push_back(std::move(item));
    }
    return Json{
        Json::Object{{"tracks", Json{std::move(tracks)}},
                     {"total_tracks", static_cast<std::int64_t>(tracking->tracks.size())},
                     {"candidate_count", static_cast<std::int64_t>(tracking->candidates.size())},
                     {"source_epoch", static_cast<std::int64_t>(tracking->source_epoch)},
                     {"frame_sequence", static_cast<std::int64_t>(tracking->frame_sequence)},
                     {"verified", static_cast<std::int64_t>(tracking->verified)},
                     {"reused", static_cast<std::int64_t>(tracking->reused)},
                     {"excluded", static_cast<std::int64_t>(tracking->excluded)},
                     {"evicted", static_cast<std::int64_t>(tracking->evicted)},
                     {"pool_bytes", tracking->pool_bytes},
                     {"requires_recognition", tracking->requires_recognition},
                     {"error", tracking->error},
                     {"input_actionable", false},
                     {"content_freshness", "not_established"}}};
}
} // namespace
std::vector<mira::ModelInputItem> DesktopImageTurn::supply_perception() {
    perception_offered_ = 0;
    perception_text_.clear();
    if (!perception_)
        return {};
    const auto view = perception_->view();
    perception_semantics_.sync(view);
    auto semantic_scene = perception_semantics_.project();
    Json::Array events;
    std::size_t bytes = 0;
    for (const auto &e : view.events) {
        if (e.sequence <= perception_cursor_)
            continue;
        const bool preview_available =
            std::any_of(view.previews.begin(), view.previews.end(), [&](const auto &p) {
                return p.first == e.frame_id && !p.second.expired();
            });
        Json event{Json::Object{
            {"cursor", static_cast<std::int64_t>(e.sequence)},
            {"kind", e.kind},
            {"text", e.text},
            {"confirmed", e.confirmed},
            {"entity", static_cast<std::int64_t>(e.entity)},
            {"instance_generation", static_cast<std::int64_t>(e.instance_generation)},
            {"source_epoch", static_cast<std::int64_t>(e.source_epoch)},
            {"frame_id", preview_available ? e.frame_id : std::string{}},
            {"preview_available", preview_available},
            {"age_ms",
             std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - e.observed_at)
                 .count()},
            {"x", e.x},
            {"y", e.y},
            {"width", e.width},
            {"height", e.height}}};
        const auto size = mira::to_json_string(event).size();
        if (events.size() >= 16 || bytes + size > 7000)
            break;
        bytes += size;
        events.push_back(std::move(event));
        perception_offered_ = e.sequence;
    }
    // Supplier input is ephemeral in Mira. Keep a small projection of consumed
    // observations so the next request can still relate a disappearance/update
    // to what was just seen, without replaying the full read journal.
    Json::Array recent;
    std::set<std::pair<std::uint64_t, std::uint64_t>> identities;
    std::size_t recent_bytes = 0;
    for (auto it = view.events.rbegin(); it != view.events.rend(); ++it) {
        if (it->sequence > perception_cursor_ || !it->entity ||
            !identities.emplace(it->entity, it->instance_generation).second)
            continue;
        auto text = it->text;
        conversation::truncate_text(text, 96);
        Json record{Json::Object{
            {"entity", static_cast<std::int64_t>(it->entity)},
            {"instance_generation", static_cast<std::int64_t>(it->instance_generation)},
            {"kind", it->kind},
            {"text", text},
            {"confirmed", it->confirmed},
            {"age_ms",
             std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - it->observed_at)
                 .count()}}};
        const auto size = mira::to_json_string(record).size();
        if (recent.size() >= 8 || recent_bytes + size > 1000)
            break;
        recent_bytes += size;
        recent.push_back(std::move(record));
    }
    Json summary{Json::Object{
        {"state", view.state},
        {"source_epoch", static_cast<std::int64_t>(view.source_epoch)},
        {"resync", view.resync},
        {"error", view.error},
        {"cursor", static_cast<std::int64_t>(perception_offered_)},
        {"newest", static_cast<std::int64_t>(view.newest)},
        {"dropped", static_cast<std::int64_t>(view.dropped)},
        {"filtered", static_cast<std::int64_t>(view.filtered)},
        {"ack_rejected", static_cast<std::int64_t>(view.ack_rejected)},
        {"ack_depth", static_cast<std::int64_t>(view.ack_depth)},
        {"captured_frames", static_cast<std::int64_t>(view.frames)},
        {"analyzed_frames", static_cast<std::int64_t>(view.analyzed)},
        {"skipped_frames", static_cast<std::int64_t>(view.skipped)},
        {"analysis_failed", static_cast<std::int64_t>(view.analysis_failed)},
        {"deferred_frames", static_cast<std::int64_t>(view.deferred_frames)},
        {"recognition_deferred", view.recognition_deferred},
        {"recognition_reason", view.recognition_reason},
        {"tracking", tracking_projection(view.tracking)},
        {"attention",
         view.attention
             ? Json{Json::Object{{"mode", view.attention->mode},
                                 {"active", view.attention->active},
                                 {"reason", view.attention->invalidation_reason},
                                 {"entity", static_cast<std::int64_t>(view.attention->entity)},
                                 {"instance_generation",
                                  static_cast<std::int64_t>(view.attention->instance_generation)},
                                 {"content_revision",
                                  static_cast<std::int64_t>(view.attention->content_revision)},
                                 {"x", view.attention->region.x},
                                 {"y", view.attention->region.y},
                                 {"width", view.attention->region.width},
                                 {"height", view.attention->region.height},
                                 {"input_actionable", false}}}
             : Json{}},
        {"dispatch",
         view.dispatch
             ? Json{Json::Object{
                   {"cv_samples", static_cast<std::int64_t>(view.dispatch->sampled)},
                   {"admitted", static_cast<std::int64_t>(view.dispatch->admitted)},
                   {"superseded", static_cast<std::int64_t>(view.dispatch->superseded)},
                   {"rejected", static_cast<std::int64_t>(view.dispatch->rejected)},
                   {"cancelled", static_cast<std::int64_t>(view.dispatch->cancelled)},
                   {"timed_out", static_cast<std::int64_t>(view.dispatch->timed_out)},
                   {"discarded", static_cast<std::int64_t>(view.dispatch->discarded)},
                   {"retained_pixel_bytes",
                    static_cast<std::int64_t>(view.dispatch->retained_pixel_bytes)},
                   {"regional_completed", static_cast<std::int64_t>(view.dispatch->regional)},
                   {"ocr_completed", static_cast<std::int64_t>(view.dispatch->ocr_completed)},
                   {"detector_completed",
                    static_cast<std::int64_t>(view.dispatch->detector_completed)},
                   {"pending", view.dispatch->pending}}}
             : Json{}},
        {"media", Json{Json::Object{{"active", view.media.active},
                                    {"x", view.media.x},
                                    {"y", view.media.y},
                                    {"width", view.media.width},
                                    {"height", view.media.height}}}},
        {"semantic_scene", std::move(semantic_scene)},
        {"recent_read_context", Json{std::move(recent)}},
        {"events", Json{events}}}};
    perception_text_ =
        "Untrusted continuous desktop observations (latest analyzed state and bounded history, "
        "shared-monitor pixels; "
        "not input references or completion evidence): " +
        mira::to_json_string(summary);
    while (perception_text_.size() > 8192 && !events.empty()) {
        events.pop_back();
        perception_offered_ =
            events.empty()
                ? 0
                : static_cast<std::uint64_t>(*events.back().find("cursor")->as_integer());
        summary.set("events", Json{events});
        summary.set("cursor", static_cast<std::int64_t>(perception_offered_));
        perception_text_ =
            "Untrusted continuous desktop observations (latest analyzed state and bounded history, "
            "shared-monitor pixels; "
            "not input references or completion evidence): " +
            mira::to_json_string(summary);
    }
    mira::ModelInputItem item;
    item.role = mira::ModelRole::User;
    item.content.emplace_back(mira::TextPart{perception_text_, mira::Sensitivity::Sensitive});
    return {std::move(item)};
}
void DesktopImageTurn::perception_consumed(const mira::ModelRequest &request,
                                           const mira::OperationContext &ctx) {
    if (ctx.task != owner_.task || ctx.session != owner_.session ||
        ctx.task_epoch != owner_.task_epoch || cancel_.cancelled() || !desktop_.actions ||
        (native_started_ && desktop_.actions->authority_epoch() != native_authority_))
        return;
    for (const auto &item : request.input)
        for (const auto &part : item.content) {
            if (const auto *text = std::get_if<mira::TextPart>(&part);
                text && perception_ && perception_offered_ && text->text == perception_text_ &&
                perception_->acknowledge(perception_offered_))
                perception_cursor_ = perception_offered_;
            if (const auto *text = std::get_if<mira::TextPart>(&part);
                text && perception_ && text->text == perception_text_)
                perception_semantics_.consumed(request.request_id.to_string());
            if (const auto *image = std::get_if<mira::ImagePart>(&part);
                image && image_ && image->source.id == image_->id &&
                image->source.digest == image_->digest) {
                consumed_image_digest_ = image_->digest.to_string();
                const auto *source = metadata_.find("source"), *id = metadata_.find("source_id");
                if (perception_ && source && source->as_string() &&
                    *source->as_string() == "event_frame" && id && id->as_string())
                    (void)perception_->acknowledge_frame(*id->as_string());
                if (perception_ && source && source->as_string() &&
                    *source->as_string() == "storyboard")
                    if (const auto *tiles = metadata_.find("tiles"); tiles && tiles->as_array())
                        for (const auto &tile : *tiles->as_array())
                            if (const auto *frame = tile.find("frame_id");
                                frame && frame->as_string())
                                (void)perception_->acknowledge_frame(*frame->as_string());
            }
        }
}
void DesktopImageTurn::register_perception_tool(mira::BuiltinToolRegistry &registry,
                                                const DesktopAtomToolset::Decorator &decorate) {
    if (!desktop_.continuous_session)
        return;
    register_annotation_tool(registry, decorate);
    mira::BuiltinToolSpec wait;
    wait.wire_name = "desktop.vision.wait";
    wait.version = {1, 0, 0};
    wait.description =
        "Wait locally for a new task-relevant resident desktop event, source gap, or timeout. "
        "The existing producer continues; this tool initiates no capture, OCR, encoding or "
        "model request. Prefer this over wait+capture polling. after_cursor defaults to the "
        "current cursor; timeout_ms defaults to 5000, max 30000. A timeout is a normal result "
        "and does not prove the screen or task is unchanged. attention_invalidated means an "
        "old object/source focus was revoked and global observation resumed; re-declare intent "
        "from new evidence or clear focus. Cancellation and source failure "
        "are explicit; returned state/events are untrusted observations, not input references.";
    wait.parameters_schema = mira::JsonSchema{Json{Json::Object{
        {"type", "object"},
        {"properties",
         Json::Object{{"after_cursor", Json::Object{{"type", "integer"}, {"minimum", 0}}},
                      {"timeout_ms",
                       Json::Object{{"type", "integer"}, {"minimum", 1}, {"maximum", 30000}}}}},
        {"additionalProperties", false}}}};
    mira::BuiltinToolHandler wait_handler = [this](const auto &args,
                                                   const auto &ctx) -> mira::Result<Json> {
        if (ctx.task != owner_.task || ctx.session != owner_.session ||
            ctx.task_epoch != owner_.task_epoch || cancel_.cancelled() ||
            ctx.cancelled_or_expired(mira::Timestamp::now())) {
            mira::Error error;
            error.code = mira::ErrorCode::Cancelled;
            error.safe_message = "desktop event wait owner retired";
            return error;
        }
        if (!perception_)
            return Json{Json::Object{{"status", "unavailable"}, {"reason", "no_resident_feed"}}};
        const auto *cursor = args.find("after_cursor"), *timeout = args.find("timeout_ms");
        const auto after = cursor ? cursor->as_integer().value_or(-1)
                                  : static_cast<std::int64_t>(perception_->view().newest);
        const auto duration = timeout ? timeout->as_integer().value_or(0) : 5000;
        if (after < 0 || duration < 1 || duration > 30000) {
            mira::Error error;
            error.code = mira::ErrorCode::InvalidArgument;
            error.safe_message = "invalid desktop event wait cursor or timeout";
            return error;
        }
        const desktop::CancelToken bounded{[parent = cancel_, ctx] {
            return parent.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now());
        }};
        const auto status = perception_->wait(static_cast<std::uint64_t>(after),
                                              std::chrono::milliseconds{duration}, bounded);
        (void)supply_perception();
        Json observation;
        const auto begin = perception_text_.find('{');
        if (begin != std::string::npos) {
            const auto parsed = mira::parse_json(perception_text_.substr(begin));
            if (parsed)
                observation = parsed.value();
        }
        return Json{Json::Object{{"status", status},
                                 {"observation", std::move(observation)},
                                 {"input_actionable", false},
                                 {"verified", false}}};
    };
    if (decorate)
        wait_handler = decorate(wait.wire_name, std::move(wait_handler));
    if (!registry.register_tool(std::move(wait), std::move(wait_handler)))
        throw std::logic_error("desktop event wait registration failed");
    mira::BuiltinToolSpec focus;
    focus.wire_name = "desktop.vision.focus";
    focus.version = {1, 0, 0};
    focus.description =
        "Declare this task's attention region in continuous-feed pixels (the same coordinates "
        "events carry). Ordinary events outside it stop reaching your feed; appeared, "
        "disappeared and source resets always arrive, and media_playback/keyframe events inside "
        "it are kept. Use it before desktop.vision.wait when a media or long-running task only "
        "depends on one region, and clear it with width=height=0 when the task moves on. "
        "Alternatively pass only object_ref from the completed request's semantic_scene to "
        "follow the same confirmed object/version. Source/content/instance/unknown evidence "
        "invalidates this binding and restores global events; it never revives automatically. "
        "Use a rectangle for persistent spatial watching across unknown frames. Consumer-local: "
        "it never changes sampling, other tasks or permissions. No capture, no input.";
    focus.parameters_schema = mira::JsonSchema{Json{Json::Object{
        {"type", "object"},
        {"properties",
         Json{Json::Object{
             {"x", Json{Json::Object{{"type", "integer"}, {"minimum", 0}}}},
             {"y", Json{Json::Object{{"type", "integer"}, {"minimum", 0}}}},
             {"width",
              Json{Json::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}}}},
             {"height",
              Json{Json::Object{{"type", "integer"}, {"minimum", 0}, {"maximum", 1000000}}}},
             {"object_ref",
              Json{Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}}}}}},
        {"additionalProperties", false}}}};
    mira::BuiltinToolHandler focus_handler = [this](const auto &args,
                                                    const auto &ctx) -> mira::Result<Json> {
        if (ctx.task != owner_.task || ctx.session != owner_.session ||
            ctx.task_epoch != owner_.task_epoch || cancel_.cancelled() ||
            ctx.cancelled_or_expired(mira::Timestamp::now())) {
            mira::Error error;
            error.code = mira::ErrorCode::Cancelled;
            error.safe_message = "desktop focus owner retired";
            return error;
        }
        if (!perception_)
            return Json{Json::Object{{"applied", false}, {"reason", "no_resident_feed"}}};
        if (const auto *ref = args.find("object_ref")) {
            if (!args.as_object() || args.as_object()->size() != 1 || !ref->as_string() ||
                ref->as_string()->empty() || ref->as_string()->size() > 64)
                return Json{Json::Object{{"applied", false}, {"reason", "invalid_argument"}}};
            if (!desktop_.actions || desktop_.actions->authority_epoch() != native_authority_)
                return Json{Json::Object{{"applied", false}, {"reason", "authority_changed"}}};
            perception_semantics_.sync(perception_->view());
            const auto binding = perception_semantics_.attention_binding(*ref->as_string());
            const bool applied = binding && perception_->focus_object(*binding);
            return Json{Json::Object{
                {"applied", applied},
                {"reason", applied ? "object_attention" : "object_reference_stale_or_unsupported"},
                {"input_actionable", false},
                {"verified", false}}};
        }
        const auto member = [&args](const char *name) -> std::optional<std::int64_t> {
            const auto *value = args.find(name);
            return value && value->is_integer() ? std::optional{*value->as_integer()}
                                                : std::nullopt;
        };
        const auto x = member("x"), y = member("y"), width = member("width"),
                   height = member("height");
        if (!args.as_object() || args.as_object()->size() != 4 || !x || !y || !width || !height ||
            *x < 0 || *y < 0 || *width < 0 || *height < 0 || *x > 1000000 || *y > 1000000 ||
            *width > 1000000 || *height > 1000000 || *x + *width > 1000000 ||
            *y + *height > 1000000)
            return Json{Json::Object{{"applied", false}, {"reason", "invalid_argument"}}};
        const desktop::WindowGeometry region{
            static_cast<std::int32_t>(*x), static_cast<std::int32_t>(*y),
            static_cast<std::int32_t>(*width), static_cast<std::int32_t>(*height)};
        perception_->focus(region);
        const auto attention = perception_->view().attention;
        const bool clearing = *width == 0 || *height == 0;
        const bool applied =
            !attention || (clearing ? attention->mode == "global"
                                    : attention->active && attention->mode == "region");
        return Json{
            Json::Object{{"applied", applied},
                         {"reason", applied ? "region_attention" : "attention_unavailable"},
                         {"focus", Json{Json::Object{{"x", *x},
                                                     {"y", *y},
                                                     {"width", *width},
                                                     {"height", *height},
                                                     {"cleared", *width <= 0 || *height <= 0}}}},
                         {"input_actionable", false},
                         {"verified", false}}};
    };
    if (decorate)
        focus_handler = decorate(focus.wire_name, std::move(focus_handler));
    if (!registry.register_tool(std::move(focus), std::move(focus_handler)))
        throw std::logic_error("desktop vision focus registration failed");
    if (!supply_images_)
        return;
    register_storyboard_tool(registry, decorate);
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.vision.event_frame";
    spec.version = {1, 0, 0};
    spec.description =
        "Read one retained historical event-frame preview by frame_id from the "
        "automatic continuous feed. It is a bounded 640-side preview, never actionable and never "
        "current verification evidence. Expired or consumed previews are unavailable. No new "
        "capture.";
    Json property{Json::Object{{"type", "string"}, {"maxLength", std::int64_t{64}}}};
    Json properties{Json::Object{{"frame_id", std::move(property)}}};
    spec.parameters_schema =
        mira::JsonSchema{Json{Json::Object{{"type", "object"},
                                           {"properties", std::move(properties)},
                                           {"required", Json{Json::Array{Json{"frame_id"}}}},
                                           {"additionalProperties", false}}}};
    mira::BuiltinToolHandler read = [this](const auto &args,
                                           const auto &ctx) -> mira::Result<Json> {
        const auto *id = args.find("frame_id");
        auto fail = [](mira::ErrorCode code, const char *message) -> mira::Result<Json> {
            mira::Error e;
            e.code = code;
            e.domain = "mirage.desktop.perception";
            e.safe_message = message;
            return e;
        };
        if (ctx.task != owner_.task || ctx.session != owner_.session ||
            ctx.task_epoch != owner_.task_epoch || cancel_.cancelled() ||
            ctx.cancelled_or_expired(mira::Timestamp::now()))
            return fail(mira::ErrorCode::Cancelled, "event frame owner retired");
        if (!perception_ || !id || !id->as_string() || id->as_string()->size() > 64 ||
            !args.as_object() || args.as_object()->size() != 1)
            return fail(mira::ErrorCode::InvalidArgument, "frame_id from continuous feed required");
        const auto view = perception_->view();
        for (const auto &[name, weak] : view.previews) {
            if (name != *id->as_string())
                continue;
            const auto preview = weak.lock();
            if (!preview || Clock::now() - preview->observed_at >= std::chrono::seconds(120))
                break;
            const auto now = Clock::now();
            desktop::CancelToken bounded{[this, ctx, now] {
                return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                       Clock::now() - now >= acquire_budget_;
            }};
            auto published = publish_image(
                preview->image, {0, 0, preview->image.width, preview->image.height}, "event_frame",
                name, desktop_.actions->epoch(), now, bounded,
                {{"available", true},
                 {"historical", true},
                 {"input_actionable", false},
                 {"observation_age_ms",
                  std::chrono::duration_cast<std::chrono::milliseconds>(now - preview->observed_at)
                      .count()}});
            return published;
        }
        return Json{
            Json::Object{{"available", false},
                         {"historical", true},
                         {"input_actionable", false},
                         {"reason", "expired_consumed_or_evicted"},
                         {"next", "Use event text; capture current state only if needed."}}};
    };
    if (decorate)
        read = decorate(spec.wire_name, std::move(read));
    if (!registry.register_tool(std::move(spec), std::move(read)))
        throw std::logic_error("event frame registration failed");
}
void DesktopImageTurn::register_annotation_tool(mira::BuiltinToolRegistry &registry,
                                                const DesktopAtomToolset::Decorator &decorate) {
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.vision.annotate";
    spec.version = {1, 0, 0};
    spec.description =
        "Assign one task-meaning label to an object_ref actually supplied in this request's "
        "continuous semantic_scene. Ground meaning in its recognized text/category only; "
        "appearance/relationship dependencies are not supported. A confirmed same-instance "
        "move retains the label; content change suspends it. Annotate only useful missing or "
        "changed meanings, not every frame. Labels are untrusted model inferences, never "
        "accessibility roles, input references, permissions or task-success evidence. "
        "No capture, input, external model call or permanent memory.";
    spec.parameters_schema = mira::JsonSchema{Json{Json::Object{
        {"type", "object"},
        {"properties",
         Json{Json::Object{
             {"object_ref",
              Json{Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}}},
             {"label",
              Json{Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}}}}}}},
        {"required", Json{Json::Array{Json{"object_ref"}, Json{"label"}}}},
        {"additionalProperties", false}}}};
    mira::BuiltinToolHandler annotate = [this](const auto &args,
                                               const auto &ctx) -> mira::Result<Json> {
        auto fail = [](mira::ErrorCode code, const char *message) -> mira::Result<Json> {
            mira::Error e;
            e.code = code;
            e.domain = "mirage.desktop.perception";
            e.safe_message = message;
            return e;
        };
        if (ctx.task != owner_.task || ctx.session != owner_.session ||
            ctx.task_epoch != owner_.task_epoch || cancel_.cancelled() ||
            ctx.cancelled_or_expired(mira::Timestamp::now()) || !desktop_.actions ||
            desktop_.actions->authority_epoch() != native_authority_)
            return fail(mira::ErrorCode::Cancelled, "semantic annotation owner retired or expired");
        const auto *ref = args.find("object_ref"), *label = args.find("label");
        if (!ref || !ref->as_string() || ref->as_string()->empty() ||
            ref->as_string()->size() > 64 || !label || !label->as_string() || !args.as_object() ||
            args.as_object()->size() != 2)
            return fail(mira::ErrorCode::InvalidArgument, "object_ref and label required");
        if (!perception_)
            return fail(mira::ErrorCode::InvalidArgument, "continuous observation is unavailable");
        perception_semantics_.sync(perception_->view());
        const auto result = perception_semantics_.annotate(*ref->as_string(), *label->as_string());
        return Json{Json::Object{{"applied", result.ok},
                                 {"reason", result.ok ? "annotated" : result.error.code},
                                 {"annotation_source", "llm_annotation"},
                                 {"input_actionable", false},
                                 {"dependency_scope", "recognized_text_and_category"}}};
    };
    if (decorate)
        annotate = decorate(spec.wire_name, std::move(annotate));
    if (!registry.register_tool(std::move(spec), std::move(annotate)))
        throw std::logic_error("semantic annotation registration failed");
}
} // namespace mirage::integration::detail
