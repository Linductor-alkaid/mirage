#include "desktop_visual_knowledge.hpp"
#include "desktop_action_verification.hpp"
#include "desktop_image_supply.hpp"
#include "tool_geometry.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace mirage::integration::detail {
namespace {
using Json = mira::JsonValue;
std::string field(const Json &value, const char *key) {
    const auto *member = value.find(key);
    return member && member->as_string() ? *member->as_string() : "";
}
mira::Error failure(const char *code, bool effects = false) {
    mira::Error result;
    result.code = effects ? mira::ErrorCode::ExecutionUncertain : mira::ErrorCode::InvalidState;
    result.domain = "mirage.desktop.knowledge";
    result.retryable = false;
    result.safe_message = mira::to_json_string(Json{Json::Object{
        {"code", code}, {"verified", false}, {"side_effect_may_have_occurred", effects}}});
    return result;
}
} // namespace

void register_local_visual_knowledge(mira::BuiltinToolRegistry &registry,
                                     const DesktopToolContext &desktop) {
    if (!desktop.environment || !desktop.actions || !desktop.mark || !desktop.ocr ||
        !desktop.with_visual_reference || !desktop.environment->window() ||
        !desktop.environment->screen() || !desktop.environment->input() ||
        !desktop.environment->input()->complete_actions_supported())
        return;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.knowledge.visual_click";
    spec.has_side_effects = true;
    spec.description = "Locally resolve exact OCR text or an installed template ID in the focused "
                       "window and click once. Requires one exact page_text and a new unique "
                       "expected_text postcondition. Fresh references, authorization and pixels "
                       "are checked on every run; no persisted coordinates or automatic retry. "
                       "Returns canonical JSON text with verified evidence.";
    Json::Object properties{
        {"knowledge_schema", Json::Object{{"type", "integer"}, {"minimum", 1}, {"maximum", 1}}},
        {"target_kind",
         Json::Object{{"type", "string"}, {"enum", Json::Array{"ocr", "template"}}}}};
    Json::Array required{"knowledge_schema", "target_kind"};
    for (const auto *key : {"window_title", "page_text", "target", "expected_text"}) {
        properties.emplace_back(
            key, Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 1024}});
        required.emplace_back(key);
    }
    spec.parameters_schema =
        mira::JsonSchema{Json{Json::Object{{"type", "object"},
                                           {"properties", Json{std::move(properties)}},
                                           {"required", Json{std::move(required)}},
                                           {"additionalProperties", false}}}};
    const auto registered = registry.register_tool(
        std::move(spec),
        [desktop](const Json &args, const mira::OperationContext &ctx) -> mira::Result<Json> {
            try {
                auto source = std::make_shared<DesktopImageSource>();
                DesktopImageTurn turn(source, desktop, ctx, {});
                return turn.local_click(args, ctx);
            } catch (...) {
                return failure("knowledge_initialization_failed");
            }
        });
    if (!registered)
        throw std::logic_error("local visual knowledge registration rejected");
}

mira::Result<Json> DesktopImageTurn::local_click(const Json &args,
                                                 const mira::OperationContext &ctx) {
    clear();
    for (const auto *key : {"window_title", "page_text", "target", "expected_text"}) {
        const auto value = field(args, key);
        if (value.empty() || value.size() > 1024 || !desktop::is_valid_utf8(value) ||
            std::any_of(value.begin(), value.end(),
                        [](unsigned char c) { return c < 0x20 || c == 0x7f; }))
            return failure("invalid_argument");
    }
    const auto *schema = args.find("knowledge_schema");
    const auto kind = field(args, "target_kind");
    if (!schema || schema->as_integer() != 1 || (kind != "ocr" && kind != "template") ||
        (kind == "template" && field(args, "target").size() > 256) ||
        field(args, "page_text") == field(args, "expected_text") ||
        (kind == "ocr" && field(args, "target") == field(args, "expected_text")))
        return failure("invalid_argument");
    const auto epoch = desktop_.actions->epoch();
    const auto end = std::min(ctx.deadline.value_or(std::chrono::steady_clock::time_point::max()),
                              std::chrono::steady_clock::now() + std::chrono::seconds{5});
    const auto parent = cancel_;
    cancel_ = desktop::CancelToken{[parent, ctx, end] {
        return parent.cancelled() || ctx.cancelled_or_expired(mira::Timestamp::now()) ||
               std::chrono::steady_clock::now() >= end;
    }};
    auto bounded_context = ctx;
    bounded_context.deadline = end;
    bool entered = false;
    try {
        if (cancel_.cancelled())
            return failure("cancelled_or_expired");
        const auto front = desktop_.environment->window()->front_window(cancel_);
        if (!front.ok || !front.found || front.window.title != field(args, "window_title"))
            return failure("knowledge_page_mismatch");
        const auto &box = front.window.geometry;
        const Json crop{Json::Object{{"x", std::int64_t{box.x}},
                                     {"y", std::int64_t{box.y}},
                                     {"width", std::int64_t{box.width}},
                                     {"height", std::int64_t{box.height}}}};
        const auto fresh = mark(crop, bounded_context);
        if (!fresh)
            return fresh.error();
        if (!mark_window_ || mark_window_->id != front.window.id ||
            mark_window_->title != front.window.title || epoch_ != epoch)
            return failure("knowledge_identity_mismatch");
        std::size_t matches = 0, pages = 0, post = 0, selected = 0;
        bool target_valid = false, page_valid = false;
        const auto &regions = marks_->publish.snapshot.regions;
        for (std::size_t n = 0; n < regions.size(); ++n) {
            const auto &region = regions[n];
            if (!std::isfinite(region.confidence) || region.confidence < 0 || region.confidence > 1)
                return failure("visual_evidence_invalid");
            const bool ocr = region.source == desktop::VisualRegionSource::kOcr;
            const bool visible =
                geometry_contains(box, region.bounds) && marks_->marks[n].selectable;
            if (ocr && region.text == field(args, "expected_text"))
                ++post;
            if (ocr && region.text == field(args, "page_text")) {
                ++pages;
                page_valid = visible && region.confidence >= 0.8;
            }
            const bool matching = kind == "ocr" ? ocr && region.text == field(args, "target")
                                                : !region.template_id.empty() &&
                                                      region.template_id == field(args, "target");
            if (matching) {
                ++matches;
                selected = n;
                target_valid = visible && region.confidence >= (kind == "ocr" ? 0.8 : 0.9);
            }
        }
        if (pages != 1 || !page_valid)
            return failure("knowledge_page_mismatch");
        if (matches != 1 || !target_valid)
            return failure(matches > 1 ? "knowledge_target_ambiguous" : "knowledge_target_missing");
        if (post)
            return failure("knowledge_postcondition_already_present");
        Json action{
            Json::Object{{"window_id", front.window.id},
                         {"visual_scope", marks_->publish.snapshot.scope_ref},
                         {"environment_epoch", static_cast<std::int64_t>(epoch)},
                         {"mark", static_cast<std::int64_t>(marks_->marks[selected].number)},
                         {"verify_kind", "visual_text_exists"},
                         {"expected_text", field(args, "expected_text")},
                         {"expected_page_title", front.window.title},
                         {"expected_page_text", field(args, "page_text")}}};
        DesktopActionVerification verification(desktop_, bounded_context, cancel_);
        entered = true;
        auto result =
            verification.execute("desktop.vision.click", action, bounded_context,
                                 [this](const auto &a, const auto &c) { return click_mark(a, c); });
        if (!result)
            return result.error();
        auto receipt = *result.value().as_object();
        auto proof = *result.value().find("verification");
        // The first capture may precede repaint. Re-read evidence only, never input.
        // Unexpected identity/permission/cancellation failures are not retried.
        for (int attempt = 0; attempt < 2 && field(proof, "reason") == "condition_not_matched" &&
                              !cancel_.cancelled();
             ++attempt) {
            auto fresh_proof =
                verification.recheck(field(result.value(), "action_id"), bounded_context);
            if (!fresh_proof)
                return failure("knowledge_verification_failed_after_dispatch", true);
            proof = std::move(fresh_proof).value();
        }
        if (cancel_.cancelled())
            return failure("knowledge_cancelled_after_dispatch", true);
        if (!proof.find("verified") || proof.find("verified")->as_boolean() != true) {
            auto rejected = failure("knowledge_postcondition_unverified", true);
            rejected.safe_message = mira::canonical_json_string(
                Json{Json::Object{{"code", "knowledge_postcondition_unverified"},
                                  {"reason", field(proof, "reason")},
                                  {"action_id", field(proof, "action_id")},
                                  {"evidence_id", field(proof, "evidence_id")},
                                  {"condition_digest", field(proof, "condition_digest")},
                                  {"verified", false},
                                  {"side_effect_may_have_occurred", true}}});
            return rejected;
        }
        for (auto &[key, member] : receipt) {
            if (key == "verified")
                member = Json{true};
            if (key == "verification")
                member = proof;
        }
        receipt.emplace_back("knowledge_digest", mira::canonical_json_digest(args).to_string());
        return Json{mira::canonical_json_string(Json{std::move(receipt)})};
    } catch (...) {
        return failure("knowledge_observation_failed", entered);
    }
}
} // namespace mirage::integration::detail
