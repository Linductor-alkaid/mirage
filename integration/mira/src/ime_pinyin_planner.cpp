#include "ime_pinyin_planner.hpp"
namespace mirage::integration::detail {
ImePlanner make_ime_planner(mira::ModelGateway &gateway, mira::ModelProfileId profile) {
    return [&gateway, profile](const std::string &text,
                               const mira::OperationContext &ctx) -> std::optional<ImePlan> {
        if (ctx.cancelled_or_expired(mira::Timestamp::now()))
            return {};
        mira::ModelRequest request;
        request.contract_version = {2, 0};
        request.request_id = mira::ModelRequestId::generate();
        request.operation_id = mira::OperationId::generate();
        request.task_id = ctx.task;
        request.task_epoch = ctx.task_epoch;
        request.profile_id = profile;
        mira::ModelInputItem system;
        system.role = mira::ModelRole::System;
        system.provenance.source = "mirage.ime.pinyin.v1";
        system.content.emplace_back(mira::TextPart{ime_pinyin_prompt, mira::Sensitivity::Internal});
        mira::ModelInputItem user;
        user.role = mira::ModelRole::User;
        user.content.emplace_back(mira::TextPart{mira::to_json_string(mira::JsonValue{text}),
                                                 mira::Sensitivity::Internal});
        request.input = {std::move(system), std::move(user)};
        request.output_contract.mode = mira::OutputMode::Text;
        request.generation.max_output_tokens = request.budget.max_output_tokens = 2048;
        request.budget.max_requests = 1;
        request.budget.scope = mira::ModelBudgetScope::Request;
        request.data_policy.store = false;
        request.prompt_provenance.system_template_digest = mira::digest_string(ime_pinyin_prompt);
        const auto outcome = gateway.infer(request, ctx);
        if (!outcome || !outcome.value().admitted ||
            ctx.cancelled_or_expired(mira::Timestamp::now()) ||
            outcome.value().response.status != mira::ModelCompletionStatus::Completed)
            return {};
        std::string body;
        for (const auto &item : outcome.value().response.output) {
            if (const auto *message = std::get_if<mira::MessageOutput>(&item)) {
                for (const auto &part : message->content) {
                    if (const auto *value = std::get_if<mira::OutputTextPart>(&part)) {
                        if (value->text.size() > 8192 - body.size())
                            return {};
                        body += value->text;
                    } else
                        return {};
                }
            } else if (!std::holds_alternative<mira::ThinkingPart>(item))
                return {};
            if (body.size() > 8192)
                return {};
        }
        const auto json = mira::parse_json(body);
        const auto *units = json ? json.value().find("units") : nullptr;
        if (!units || !units->as_array() || units->as_array()->size() > 32)
            return {};
        ImePlan plan;
        for (const auto &unit : *units->as_array()) {
            const auto *value = unit.find("text"), *spelling = unit.find("pinyin");
            if (!value || !spelling || !value->as_string() || !spelling->as_string())
                return {};
            plan.push_back({*value->as_string(), *spelling->as_string()});
        }
        return valid_ime_plan(text, plan) ? std::optional<ImePlan>{std::move(plan)} : std::nullopt;
    };
}
} // namespace mirage::integration::detail
