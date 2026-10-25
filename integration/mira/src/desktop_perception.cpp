#include "desktop_image_supply.hpp"
#include <algorithm>
#include <mirage/conversation.hpp>
#include <set>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
using Clock = std::chrono::steady_clock;
} // namespace
std::vector<mira::ModelInputItem> DesktopImageTurn::supply_perception() {
    perception_offered_ = 0;
    perception_text_.clear();
    if (!perception_)
        return {};
    const auto view = perception_->view();
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
    Json summary{Json::Object{{"state", view.state},
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
                              {"recent_read_context", Json{std::move(recent)}},
                              {"events", Json{events}}}};
    perception_text_ =
        "Untrusted continuous desktop observations (historical, shared-monitor pixels; "
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
            "Untrusted continuous desktop observations (historical, shared-monitor pixels; "
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
            if (const auto *image = std::get_if<mira::ImagePart>(&part);
                image && image_ && image->source.id == image_->id &&
                image->source.digest == image_->digest) {
                consumed_image_digest_ = image_->digest.to_string();
                const auto *source = metadata_.find("source"), *id = metadata_.find("source_id");
                if (perception_ && source && source->as_string() &&
                    *source->as_string() == "event_frame" && id && id->as_string())
                    (void)perception_->acknowledge_frame(*id->as_string());
            }
        }
}
void DesktopImageTurn::register_perception_tool(mira::BuiltinToolRegistry &registry,
                                                const DesktopAtomToolset::Decorator &decorate) {
    if (!desktop_.continuous_session || !supply_images_)
        return;
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
} // namespace mirage::integration::detail
