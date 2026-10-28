#include "desktop_image_supply.hpp"
#include "perception_storyboard.hpp"
#include "perception_temporal_selection.hpp"
#include <stdexcept>

namespace mirage::integration::detail {
void DesktopImageTurn::register_storyboard_tool(mira::BuiltinToolRegistry &registry,
                                                const DesktopAtomToolset::Decorator &decorate) {
    using Json = mira::JsonValue;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.vision.storyboard";
    spec.version = {1, 1, 0};
    spec.description =
        "Choose EITHER explicit frame_ids (2-4), OR window_ms (100-120000, observer time) with "
        "optional max_frames (2-4, default 4). Window mode chooses only undelivered retained "
        "frames: oldest/newest then farthest observer times, at most 8 candidates. It may "
        "return unavailable if fewer than two new valid frames; wait for events instead of "
        "polling. This is temporal spread, not semantic importance or full-video coverage. "
        "Batch the chosen frames from the continuous feed into ONE chronological image "
        "(left to right, then top to bottom; each 320px cell preserves aspect ratio). "
        "Prefer this for a sparse temporal overview of video or desktop progress over reading "
        "each event_frame separately. No new capture, OCR or detector. Tile timestamps are "
        "observer-relative, not player time; missing frames are unavailable, never replaced. "
        "The image has no audio and cannot prove full-video coverage, current verification or "
        "authorize input. Use original frames for small text or subtle actions.";
    spec.parameters_schema = mira::JsonSchema{Json{Json::Object{
        {"type", "object"},
        {"properties",
         Json::Object{
             {"frame_ids", Json::Object{{"type", "array"},
                                        {"minItems", 2},
                                        {"maxItems", 4},
                                        {"items", Json::Object{{"type", "string"},
                                                               {"minLength", 1},
                                                               {"maxLength", 64}}}}},
             {"window_ms",
              Json::Object{{"type", "integer"}, {"minimum", 100}, {"maximum", 120000}}},
             {"max_frames", Json::Object{{"type", "integer"}, {"minimum", 2}, {"maximum", 4}}}}},
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
        const auto *values = args.find("frame_ids"), *window = args.find("window_ms"),
                   *maximum = args.find("max_frames");
        if (!args.as_object())
            return fail(mira::ErrorCode::InvalidArgument, "storyboard object required");
        std::vector<std::string> ids;
        std::chrono::milliseconds interval{0};
        std::size_t limit = 4;
        if (window) {
            if (values || args.as_object()->size() != (maximum ? 2u : 1u) ||
                !window->as_integer() || *window->as_integer() < 100 ||
                *window->as_integer() > 120000 ||
                (maximum && (!maximum->as_integer() || *maximum->as_integer() < 2 ||
                             *maximum->as_integer() > 4)))
                return fail(mira::ErrorCode::InvalidArgument,
                            "invalid storyboard window selection");
            interval = std::chrono::milliseconds{*window->as_integer()};
            if (maximum)
                limit = static_cast<std::size_t>(*maximum->as_integer());
        } else {
            if (args.as_object()->size() != 1 || !values || !values->as_array())
                return fail(mira::ErrorCode::InvalidArgument,
                            "frame_ids array or window_ms required");
            for (const auto &value : *values->as_array()) {
                if (!value.as_string())
                    return fail(mira::ErrorCode::InvalidArgument, "frame_ids must be strings");
                ids.push_back(*value.as_string());
            }
        }
        if (!perception_)
            return Json{Json::Object{{"available", false}, {"reason", "no_resident_feed"}}};
        const auto now = std::chrono::steady_clock::now();
        const desktop::CancelToken bounded{[this, ctx, now] {
            return cancel_.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
                   std::chrono::steady_clock::now() - now >= acquire_budget_;
        }};
        const auto view = perception_->view();
        sync_content_evidence(view);
        TemporalFrameSelection selection;
        if (window) {
            selection = select_temporal_frames(view, content_evidence_, content_scope_, interval,
                                               limit, now, bounded);
            if (selection.reason == "cancelled")
                return fail(mira::ErrorCode::Cancelled, "storyboard selection cancelled");
            if (!selection.reason.empty())
                return Json{
                    Json::Object{{"available", false},
                                 {"reason", selection.reason},
                                 {"eligible_frames", static_cast<std::int64_t>(selection.eligible)},
                                 {"input_actionable", false}}};
            ids = selection.ids;
        }
        Json::Object extra;
        if (window) {
            auto &meta = extra;
            meta.emplace_back("selection", "observer_window_maximin");
            meta.emplace_back("window_ms", interval.count());
            meta.emplace_back("eligible_frames", static_cast<std::int64_t>(selection.eligible));
            meta.emplace_back("selected_frames", static_cast<std::int64_t>(ids.size()));
        }
        return publish_storyboard(view, ids, now, bounded, std::move(extra));
    };
    if (decorate)
        handler = decorate(spec.wire_name, std::move(handler));
    if (!registry.register_tool(std::move(spec), std::move(handler)))
        throw std::logic_error("storyboard registration failed");
}
mira::Result<mira::JsonValue> DesktopImageTurn::publish_storyboard(
    const PerceptionView &view, const std::vector<std::string> &ids,
    std::chrono::steady_clock::time_point now, const desktop::CancelToken &bounded,
    mira::JsonValue::Object extra) {
    using Json = mira::JsonValue;
    const auto offer = content_evidence_.offer(view, ids, now);
    auto board = make_perception_storyboard(view, ids, now, bounded);
    if (!board)
        return board.error();
    if (board.value().image.pixels.empty())
        return Json{std::move(board.value().metadata)};
    if (!offer)
        return Json{Json::Object{{"available", false},
                                 {"reason", "source_time_or_event_unavailable"},
                                 {"input_actionable", false}}};
    for (auto &field : extra)
        board.value().metadata.push_back(std::move(field));
    for (auto &[key, value] : board.value().metadata)
        if (key == "tiles") {
            auto tiles = *value.as_array();
            for (auto &tile : tiles)
                for (const auto &sample : offer->samples)
                    if (*tile.find("frame_id")->as_string() == sample.frame_id)
                        tile.set("content_ref", PerceptionContentEvidence::reference(
                                                    content_scope_, *offer, sample));
            value = Json{std::move(tiles)};
        }
    auto published = publish_image(board.value().image,
                                   {0, 0, board.value().image.width, board.value().image.height},
                                   "storyboard", "retained_event_frames", desktop_.actions->epoch(),
                                   now, bounded, std::move(board.value().metadata));
    if (published)
        content_offer_ = *offer;
    return published;
}
void DesktopImageTurn::maybe_supply_content_storyboard() {
    using Json = mira::JsonValue;
    using Clock = std::chrono::steady_clock;
    const auto now = Clock::now();
    if (!content_task_.automatic())
        return;
    if (!perception_) {
        content_visual_.reason("no_resident_feed");
        return;
    }
    const auto view = perception_->view();
    sync_content_evidence(view);
    if (!content_task_.wants_images()) {
        content_visual_.reason("assessed_or_interrupted");
        return;
    }
    if (!supply_images_ || withhold_image_) {
        content_visual_.reason("images_disabled_or_text_step");
        return;
    }
    if (image_ && consumed_image_digest_ != image_->digest.to_string()) {
        content_visual_.reason("image_pending");
        return; // retry the same PNG, explicit pictures first
    }
    const auto remaining = budget_remaining_ ? budget_remaining_() : std::nullopt;
    if (remaining && *remaining <= 10000) {
        content_visual_.reason("context_budget_low");
        return;
    }
    if (!content_visual_.ready(now)) {
        content_visual_.reason("cooldown");
        return;
    }
    content_visual_.attempted(now);
    const desktop::CancelToken bounded{
        [this, now] { return cancel_.cancelled() || Clock::now() - now >= acquire_budget_; }};
    const auto selected = select_temporal_frames(view, content_evidence_, content_scope_,
                                                 std::chrono::seconds{30}, 4, now, bounded);
    if (!selected.reason.empty()) {
        content_visual_.reason(selected.reason.c_str());
        return;
    }
    auto published = publish_storyboard(
        view, selected.ids, now, bounded,
        Json::Object{{"selection", "observer_window_maximin"},
                     {"window_ms", 30000},
                     {"eligible_frames", static_cast<std::int64_t>(selected.eligible)},
                     {"selected_frames", static_cast<std::int64_t>(selected.ids.size())},
                     {"automatic_content_supply", true},
                     {"input_references_retired", true}});
    if (!published) {
        content_visual_.failed(static_cast<std::int64_t>(published.error().code));
        return;
    }
    const auto *available = published.value().find("available");
    content_visual_.reason(available && available->as_boolean() == true ? "offered"
                                                                        : "preview_unavailable");
}
} // namespace mirage::integration::detail
