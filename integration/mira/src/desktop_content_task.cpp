#include "desktop_image_supply.hpp"
#include <stdexcept>

namespace mirage::integration::detail {
ContentTaskResult DesktopImageTurn::settle_content_task() {
    if (perception_)
        sync_content_evidence(perception_->view());
    content_task_.sync(content_evidence_, content_scope_);
    return content_task_.result();
}
void DesktopImageTurn::register_content_task_tool(mira::BuiltinToolRegistry &registry,
                                                  const DesktopAtomToolset::Decorator &decorate) {
    using Json = mira::JsonValue;
    mira::BuiltinToolSpec spec;
    spec.wire_name = "desktop.vision.content";
    spec.version = {1, 0, 0};
    spec.description =
        "Declare and assess a content-watching task. begin requires scope observed_samples or "
        "entire_video and requires_audio boolean; declare the user's actual requirement, not a "
        "weaker substitute. Scope cannot be downgraded this turn. After actual frame/storyboard "
        "input, assess with verdict answerable or insufficient, answer (<=1024 bytes), nonempty "
        "limitations (<=512 bytes) and evidence_refs (0-4 content_ref tokens). Only delivered "
        "current-segment references count. Full-video/audio evidence is currently unavailable: "
        "these requirements settle insufficient. status reads current state/recent references; "
        "do not poll it. Source retirement requires same-goal begin to reopen. A declared task "
        "cannot finish successfully without a valid assessment. The host publishes only the "
        "assessed bounded answer, with sparse-coverage disclosure, not a later free-text claim. "
        "Model interpretation remains unverified. No capture, OCR, input or model request.";
    const Json string{Json::Object{{"type", "string"}}};
    const Json refs_schema{Json::Object{
        {"type", "array"},
        {"maxItems", 4},
        {"items", Json{Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}}}}};
    spec.parameters_schema = mira::JsonSchema{Json{Json::Object{
        {"type", "object"},
        {"properties",
         Json{Json::Object{
             {"action", string},
             {"scope", string},
             {"requires_audio", Json{Json::Object{{"type", "boolean"}}}},
             {"verdict", string},
             {"answer", Json{Json::Object{{"type", "string"}, {"maxLength", 1024}}}},
             {"limitations",
              Json{Json::Object{{"type", "string"}, {"minLength", 1}, {"maxLength", 512}}}},
             {"evidence_refs", refs_schema}}}},
        {"required", Json{Json::Array{"action"}}},
        {"additionalProperties", false}}}};
    mira::BuiltinToolHandler handler = [this](const Json &args,
                                              const auto &ctx) -> mira::Result<Json> {
        if (ctx.task != owner_.task || ctx.session != owner_.session ||
            ctx.task_epoch != owner_.task_epoch || cancel_.cancelled() ||
            ctx.cancelled_or_expired(mira::Timestamp::now()) ||
            (native_authority_ && desktop_.actions->authority_epoch() != native_authority_)) {
            mira::Error error;
            error.code = mira::ErrorCode::Cancelled;
            error.safe_message = "content task owner retired";
            return error;
        }
        const auto result = [&](bool applied, const std::string &reason) {
            return Json{
                Json::Object{{"applied", applied},
                             {"reason", reason},
                             {"content_task", content_task_.project()},
                             {"available_refs", content_evidence_.references(content_scope_)},
                             {"verified", false},
                             {"input_actionable", false}}};
        };
        const auto *action = args.find("action");
        if (!args.as_object() || !action || !action->as_string())
            return result(false, "invalid_argument");
        if (!perception_)
            return result(false, "no_resident_feed");
        sync_content_evidence(perception_->view());
        if (*action->as_string() == "status")
            return result(args.as_object()->size() == 1,
                          args.as_object()->size() == 1 ? "status" : "invalid_argument");
        if (*action->as_string() == "begin") {
            const auto *scope = args.find("scope"), *audio = args.find("requires_audio");
            if (args.as_object()->size() != 3 || !scope || !scope->as_string() || !audio ||
                !audio->as_boolean())
                return result(false, "invalid_argument");
            const bool applied =
                content_task_.begin(*scope->as_string(), *audio->as_boolean(), content_evidence_);
            return result(applied, applied ? "content_task_started" : "invalid_or_changed_goal");
        }
        if (*action->as_string() != "assess")
            return result(false, "invalid_argument");
        const auto *verdict = args.find("verdict"), *answer = args.find("answer"),
                   *limits = args.find("limitations"), *refs = args.find("evidence_refs");
        if (args.as_object()->size() != 5 || !verdict || !verdict->as_string() || !answer ||
            !answer->as_string() || !limits || !limits->as_string() || !refs || !refs->as_array() ||
            refs->as_array()->size() > 4)
            return result(false, "invalid_argument");
        std::vector<std::string> citations;
        for (const auto &ref : *refs->as_array()) {
            if (!ref.as_string() || ref.as_string()->empty() || ref.as_string()->size() > 64)
                return result(false, "invalid_argument");
            citations.push_back(*ref.as_string());
        }
        const auto error =
            content_task_.assess(*verdict->as_string(), *answer->as_string(), *limits->as_string(),
                                 citations, content_evidence_, content_scope_);
        return result(error.empty(), error.empty() ? "content_assessed" : error);
    };
    if (decorate)
        handler = decorate(spec.wire_name, std::move(handler));
    if (!registry.register_tool(std::move(spec), std::move(handler)))
        throw std::logic_error("content task registration failed");
}
} // namespace mirage::integration::detail
