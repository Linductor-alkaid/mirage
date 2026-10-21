#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mira/model_digest.hpp>
#include <mirage/desktop/action_coordinator.hpp>

namespace i = mirage::integration;
using Json = mira::JsonValue;
namespace {
Json recovered(i::detail::DesktopActionVerification &verify, const mira::OperationContext &ctx) {
    mira::AgentLoopSpec spec;
    spec.task_id = ctx.task;
    spec.session_id = ctx.session;
    spec.task_epoch = ctx.task_epoch;
    const auto supplied = verify.supply(spec);
    MIRAGE_CHECK(supplied.has_value());
    if (!supplied)
        return Json::Array{};
    for (const auto &item : supplied.value())
        for (const auto &part : item.content)
            if (const auto *text = std::get_if<mira::TextPart>(&part)) {
                constexpr std::string_view prefix = "Untrusted host action recovery facts";
                if (!text->text.starts_with(prefix))
                    continue;
                const auto start = text->text.find("[{\"");
                MIRAGE_CHECK(start != std::string::npos);
                if (start == std::string::npos)
                    return Json::Array{};
                MIRAGE_CHECK(text->text.size() <= 2048);
                const auto payload = text->text.substr(start);
                MIRAGE_CHECK(payload.size() <= 2048);
                const auto parsed = mira::parse_json(payload);
                MIRAGE_CHECK(parsed && parsed.value().as_array());
                if (parsed)
                    return parsed.value();
            }
    return Json::Array{};
}
std::int64_t count(const Json &facts) {
    const auto *array = facts.as_array();
    if (!array || array->empty())
        return 0;
    const auto *value = array->front().find("same_attempt_failures");
    return value && value->as_integer() ? *value->as_integer() : 0;
}
} // namespace

int main() {
    i::DesktopToolContext tools;
    tools.environment = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    auto ctx = mira::make_control_context();
    ctx.session = mira::SessionId::generate();
    ctx.task = mira::TaskId::generate();
    mirage::desktop::CancelToken cancel;
    i::detail::DesktopActionVerification verify(tools, ctx, cancel);
    int handlers = 0;
    const mira::BuiltinToolHandler fail = [&](const auto &, const auto &) -> mira::Result<Json> {
        ++handlers;
        mira::Error result;
        result.code = mira::ErrorCode::PlatformError;
        result.safe_message = mira::to_json_string(Json{Json::Object{
            {"error_code", "invalid_target_bounds"},
            {"field", "target"},
            {"hint", "Fit rectangle in current 1600x1000 IMAGE pixels. Correct coordinates."},
            {"message", "Target rectangle exceeds supplied image."},
            {"side_effect_may_have_occurred", false},
            {"input_released", true}}});
        return result;
    };
    Json args{Json::Object{{"image_scope", "first"},
                           {"environment_epoch", 1},
                           {"x", 16},
                           {"y", 980},
                           {"width", 24},
                           {"height", 26},
                           {"text", "fixture-private-input-must-not-copy"}}};
    for (int attempt = 1; attempt <= 2; ++attempt) {
        const auto failed = verify.execute("desktop.session.click", args, ctx, fail);
        MIRAGE_CHECK(!failed);
        if (!failed) {
            const auto &receipt = failed.error().safe_message;
            MIRAGE_CHECK(receipt.size() <= 512);
            const auto parsed = mira::parse_json(receipt);
            MIRAGE_CHECK(parsed && parsed.value().find("field") && parsed.value().find("hint"));
        }
        // Ordinary input epochs and replacement scopes do not erase the lesson.
        {
            auto lease = tools.actions->try_begin({});
            MIRAGE_CHECK(static_cast<bool>(lease));
        }
        verify.note_observation();
        args.set("image_scope", "scope-after-observation-" + std::to_string(attempt));
        args.set("environment_epoch", static_cast<std::int64_t>(tools.actions->epoch()));
        const auto facts = recovered(verify, ctx);
        MIRAGE_CHECK(facts.as_array() && facts.as_array()->size() == 1);
        MIRAGE_CHECK(count(facts) == attempt);
        const auto encoded = mira::to_json_string(facts);
        MIRAGE_CHECK(encoded.find("fixture-private-input") == std::string::npos);
        MIRAGE_CHECK(encoded.find("correct_arguments_not_recapture") != std::string::npos);
        MIRAGE_CHECK(encoded.find("1600x1000") != std::string::npos);
    }
    MIRAGE_CHECK(handlers == 2);
    const mira::BuiltinToolHandler success = [](const auto &, const auto &) -> mira::Result<Json> {
        return Json{Json::Object{{"status", "dispatched"},
                                 {"input_released", true},
                                 {"side_effect_may_have_occurred", true},
                                 {"verified", false}}};
    };
    // Same intent succeeds after new evidence: the old failure is not permanent.
    MIRAGE_CHECK(verify.execute("desktop.session.click", args, ctx, success).has_value());
    MIRAGE_CHECK(recovered(verify, ctx).as_array()->empty());

    i::detail::DesktopActionVerification archived(tools, ctx, cancel);
    for (int n = 0; n < 40; ++n)
        MIRAGE_CHECK(!archived.execute("desktop.session.click", args, ctx, fail));
    MIRAGE_CHECK(count(recovered(archived, ctx)) == 40);
    // Newest four distinct failed intents, with a hard serialized byte bound.
    for (int n = 0; n < 6; ++n) {
        args.set("x", n);
        MIRAGE_CHECK(!archived.execute("desktop.session.click", args, ctx, fail));
    }
    const auto bounded = recovered(archived, ctx);
    MIRAGE_CHECK(bounded.as_array() && bounded.as_array()->size() == 4);
    MIRAGE_CHECK(mira::to_json_string(bounded).size() <= 2048);
    const mira::BuiltinToolHandler escaped = [](const auto &, const auto &) -> mira::Result<Json> {
        mira::Error result;
        result.code = mira::ErrorCode::PlatformError;
        result.safe_message =
            mira::to_json_string(Json{Json::Object{{"error_code", std::string(128, '\n')},
                                                   {"field", std::string(1024, '\n')},
                                                   {"hint", std::string(2048, '\n')},
                                                   {"side_effect_may_have_occurred", false},
                                                   {"input_released", true}}});
        return result;
    };
    const auto large = archived.execute("desktop.session.click", args, ctx, escaped);
    MIRAGE_CHECK(!large);
    if (!large) {
        MIRAGE_CHECK(large.error().safe_message.size() <= 512);
        const auto parsed = mira::parse_json(large.error().safe_message);
        MIRAGE_CHECK(parsed && parsed.value().find("action_id"));
    }
    tools.actions->takeover();
    MIRAGE_CHECK(tools.actions->resume());
    MIRAGE_CHECK(recovered(archived, ctx).as_array()->empty());
    mira::AgentLoopSpec foreign;
    foreign.task_id = mira::TaskId::generate();
    foreign.session_id = ctx.session;
    foreign.task_epoch = ctx.task_epoch;
    MIRAGE_CHECK(!archived.supply(foreign));
    cancel.request_cancel();
    MIRAGE_CHECK(!archived.execute("desktop.session.click", args, ctx, fail));
    mira::AgentLoopSpec owned;
    owned.task_id = ctx.task;
    owned.session_id = ctx.session;
    owned.task_epoch = ctx.task_epoch;
    MIRAGE_CHECK(!archived.supply(owned));
    return mirage::testing::finish("action_recovery_facts_test");
}
