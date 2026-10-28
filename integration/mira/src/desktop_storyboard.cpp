#include "desktop_image_supply.hpp"
#include "perception_storyboard.hpp"
#include <stdexcept>

namespace mirage::integration::detail {
void DesktopImageTurn::register_storyboard_tool(mira::BuiltinToolRegistry &registry,
                                                const DesktopAtomToolset::Decorator &decorate) {
    using Json = mira::JsonValue;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.vision.storyboard";
    spec.version = {1, 0, 0};
    spec.description =
        "Batch 2-4 retained frame_ids from the continuous feed into ONE chronological image "
        "(left to right, then top to bottom; each 320px cell preserves aspect ratio). "
        "Prefer this for a sparse temporal overview of video or desktop progress over reading "
        "each event_frame separately. No new capture, OCR or detector. Tile timestamps are "
        "observer-relative, not player time; missing frames are unavailable, never replaced. "
        "The image has no audio and cannot prove full-video coverage, current verification or "
        "authorize input. Use original frames for small text or subtle actions.";
    spec.parameters_schema = mira::JsonSchema{Json{Json::Object{
        {"type", "object"},
        {"properties",
         Json::Object{{"frame_ids", Json::Object{{"type", "array"},
                                                 {"minItems", 2},
                                                 {"maxItems", 4},
                                                 {"items", Json::Object{{"type", "string"},
                                                                        {"minLength", 1},
                                                                        {"maxLength", 64}}}}}}},
        {"required", Json::Array{Json{"frame_ids"}}},
        {"additionalProperties", false}}}};
    mira::BuiltinToolHandler handler = [this](const auto &args,
                                              const auto &ctx) -> mira::Result<Json> {
        auto fail = [](mira::ErrorCode code, const char *message) -> mira::Result<Json> {
            mira::Error error;
            error.code = code;
            error.safe_message = message;
            return error;
        };
        if (ctx.task != owner_.task || ctx.session != owner_.session ||
            ctx.task_epoch != owner_.task_epoch || cancel_.cancelled() ||
            ctx.cancelled_or_expired(mira::Timestamp::now()) ||
            (native_authority_ && desktop_.actions->authority_epoch() != native_authority_))
            return fail(mira::ErrorCode::Cancelled, "storyboard owner retired");
        const auto *values = args.find("frame_ids");
        if (!args.as_object() || args.as_object()->size() != 1 || !values || !values->as_array())
            return fail(mira::ErrorCode::InvalidArgument, "frame_ids array required");
        std::vector<std::string> ids;
        for (const auto &value : *values->as_array()) {
            if (!value.as_string())
                return fail(mira::ErrorCode::InvalidArgument, "frame_ids must be strings");
            ids.push_back(*value.as_string());
        }
        if (!perception_)
            return Json{Json::Object{{"available", false}, {"reason", "no_resident_feed"}}};
        const auto now = std::chrono::steady_clock::now();
        const desktop::CancelToken bounded{[this, ctx, now] {
            return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                   std::chrono::steady_clock::now() - now >= acquire_budget_;
        }};
        auto board = make_perception_storyboard(perception_->view(), ids, now, bounded);
        if (!board)
            return board.error();
        if (board.value().image.pixels.empty())
            return Json{std::move(board.value().metadata)};
        return publish_image(board.value().image,
                             {0, 0, board.value().image.width, board.value().image.height},
                             "storyboard", "retained_event_frames", desktop_.actions->epoch(), now,
                             bounded, std::move(board.value().metadata));
    };
    if (decorate)
        handler = decorate(spec.wire_name, std::move(handler));
    if (!registry.register_tool(std::move(spec), std::move(handler)))
        throw std::logic_error("storyboard registration failed");
}
} // namespace mirage::integration::detail
