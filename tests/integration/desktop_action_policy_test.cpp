#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <kairo/executor.hpp>
#include <stdexcept>

namespace {
namespace d = mirage::desktop;
namespace i = mirage::integration;
using Json = mira::JsonValue;
using Verification = i::detail::DesktopActionVerification;
std::string text(const Json &json, const char *key) {
    const auto *value = json.find(key);
    return value && value->as_string() ? *value->as_string() : "";
}
const Json &unwrap(const mira::Result<Json> &result) {
    if (!result)
        throw std::runtime_error(result.error().safe_message);
    return result.value();
}
struct Fixture {
    std::shared_ptr<mirage::testing::FakeDesktopEnvironment> env =
        std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    i::DesktopToolContext tools;
    mira::OperationContext ctx = mira::make_control_context();
    int inputs = 0, captures = 0;
    bool matched = false;
    Fixture() {
        ctx.task = mira::TaskId::generate();
        ctx.session = mira::SessionId::generate();
        ctx.task_epoch = 1;
        ctx.deadline = std::chrono::steady_clock::now() + std::chrono::minutes{1};
        tools.environment = env;
        tools.actions = std::make_shared<d::ActionCoordinator>();
        env->windows = {{"w1", "Fixture", {0, 0, 100, 100}, true}};
    }
    mira::Result<Json> input(const Json &, const mira::OperationContext &) {
        auto lease = tools.actions->try_begin({});
        if (!lease)
            throw std::runtime_error("fixture lease rejected");
        ++inputs;
        return Json{Json::Object{{"status", "dispatched"},
                                 {"input_released", true},
                                 {"side_effect_may_have_occurred", true},
                                 {"verified", false}}};
    }
    auto handler() {
        return [this](const auto &args, const auto &context) { return input(args, context); };
    }
    auto capture() {
        return [this](const auto &, const auto &, const auto &) -> mira::Result<Json> {
            ++captures;
            return Json{Json::Object{{"matched", matched}, {"digest", "current-frame"}}};
        };
    }
};
void defaults(const Json &golden) {
    Fixture f;
    Verification v(f.tools, f.ctx, {});
    v.set_shared_observation(f.capture());
    const Json args{Json::Object{{"window_id", "w1"}}};
    // More than the active window: complete unverified-business inputs can
    // archive without becoming machine-verified or exhausting the owner.
    for (int n = 0; n < 100; ++n) {
        const auto result = v.execute("desktop.session.key", args, f.ctx, f.handler());
        const auto *proof = unwrap(result).find("verification");
        MIRAGE_CHECK(proof);
        if (!proof)
            return;
        MIRAGE_CHECK(text(*proof, "status") == text(golden, "no_condition_status"));
        MIRAGE_CHECK(text(*proof, "execution_status") == text(golden, "completed_execution"));
        MIRAGE_CHECK(!proof->find("verified")->as_boolean().value_or(true));
        MIRAGE_CHECK(proof->find("policy_version")->as_integer() ==
                     golden.find("policy_version")->as_integer());
        mirage::conversation::Part part;
        part.name = "desktop.session.key";
        part.kind = "tool";
        part.status = "complete";
        part.output = mira::to_json_string(unwrap(result));
        const auto view = mirage::conversation::desktop_evidence(part);
        MIRAGE_CHECK(view && view->status == "not_requested" && view->shared_desktop &&
                     view->scope == "execution_only");
        part.status = "failed";
        MIRAGE_CHECK(!mirage::conversation::desktop_evidence(part));
    }
    MIRAGE_CHECK(f.inputs == 100 && f.captures == 0);
    // Execution is complete without a screenshot; task success still needs
    // post-input feedback. Settlement never captures behind the model.
    MIRAGE_CHECK(!v.settle());
    v.note_observation();
    MIRAGE_CHECK(v.settle());
    MIRAGE_CHECK(v.settle() && f.captures == 0);
    f.tools.actions->takeover();
    MIRAGE_CHECK(!v.settle());
    MIRAGE_CHECK(f.tools.actions->resume());
    MIRAGE_CHECK(!v.settle());
}
void dependencies() {
    Fixture f;
    Verification v(f.tools, f.ctx, {});
    v.set_shared_observation(f.capture());
    const Json condition{
        Json::Object{{"verify_kind", "shared_text_exists"}, {"expected_text", "Saved"}}};
    const auto first = v.execute("desktop.session.click", condition, f.ctx, f.handler());
    const auto id = text(unwrap(first), "action_id");
    MIRAGE_CHECK(!id.empty() && f.captures == 1 && f.inputs == 1);
    Json dependent{Json::Object{{"window_id", "w1"}, {"depends_on", Json::Array{id}}}};
    MIRAGE_CHECK(!v.execute("desktop.input.key", dependent, f.ctx, f.handler()));
    MIRAGE_CHECK(f.inputs == 1);
    // A failed business condition is not a global execution fence.
    const Json independent{Json::Object{{"window_id", "w1"}}};
    MIRAGE_CHECK(v.execute("desktop.input.key", independent, f.ctx, f.handler()));
    MIRAGE_CHECK(f.inputs == 2);
    v.note_observation();
    MIRAGE_CHECK(!v.settle()); // The explicitly requested condition remains unmet.
    f.matched = true;
    const auto repaired = v.recheck(id, f.ctx);
    MIRAGE_CHECK(text(unwrap(repaired), "status") == "verified");
    MIRAGE_CHECK(v.settle() && f.inputs == 2 && f.captures == 2);
    MIRAGE_CHECK(v.execute("desktop.input.key", dependent, f.ctx, f.handler()));
    MIRAGE_CHECK(f.inputs == 3);
    // Another action advances the evidence epoch. A prior match cannot
    // authorize another dependent mutation without new evidence.
    MIRAGE_CHECK(!v.execute("desktop.input.key", dependent, f.ctx, f.handler()));
    MIRAGE_CHECK(f.inputs == 3);
    Json unknown{Json::Object{{"window_id", "w1"}, {"depends_on", Json::Array{"foreign"}}}};
    MIRAGE_CHECK(!v.execute("desktop.input.key", unknown, f.ctx, f.handler()));
    Json duplicate{Json::Object{{"window_id", "w1"}, {"depends_on", Json::Array{id, id}}}};
    MIRAGE_CHECK(!v.execute("desktop.input.key", duplicate, f.ctx, f.handler()));
    auto foreign = f.ctx;
    foreign.task = mira::TaskId::generate();
    MIRAGE_CHECK(!v.recheck(id, foreign));
    f.tools.actions->takeover();
    MIRAGE_CHECK(f.tools.actions->resume());
    MIRAGE_CHECK(!v.recheck(id, f.ctx));
    MIRAGE_CHECK(f.inputs == 3);
}
void uncertainty() {
    for (const bool released : {true, false}) {
        Fixture f;
        Verification v(f.tools, f.ctx, {});
        const Json args{Json::Object{{"window_id", "w1"}}};
        const auto bad = v.execute(
            "desktop.input.key", args, f.ctx,
            [released](const auto &, const auto &) -> mira::Result<Json> {
                mira::Error error;
                error.code = mira::ErrorCode::PlatformError;
                error.safe_message = mira::to_json_string(Json{Json::Object{
                    {"side_effect_may_have_occurred", true}, {"input_released", released}}});
                return error;
            });
        MIRAGE_CHECK(!bad);
        v.note_observation();
        MIRAGE_CHECK(!v.settle());
        MIRAGE_CHECK(!v.execute("desktop.input.key", args, f.ctx, f.handler()));
        MIRAGE_CHECK(f.inputs == 0);
    }
    Fixture f;
    d::CancelToken cancel;
    Verification v(f.tools, f.ctx, cancel);
    cancel.request_cancel();
    MIRAGE_CHECK(
        !v.execute("desktop.input.key", Json::Object{{"window_id", "w1"}}, f.ctx, f.handler()));
    MIRAGE_CHECK(f.inputs == 0 && !v.settle());
}
void unrelated_proof_keeps_fence() {
    Fixture f;
    f.env->applications = {{"app.desktop", "Fixture App", false}};
    f.env->application_instances["app.desktop"] = "instance";
    Verification v(f.tools, f.ctx, {});
    const auto application =
        v.execute("desktop.application.launch", Json::Object{{"application_id", "app.desktop"}},
                  f.ctx, f.handler());
    const auto id = text(unwrap(application), "action_id");
    const Json args{Json::Object{{"window_id", "w1"}}};
    MIRAGE_CHECK(!v.execute(
        "desktop.input.key", args, f.ctx, [](const auto &, const auto &) -> mira::Result<Json> {
            mira::Error error;
            error.code = mira::ErrorCode::PlatformError;
            error.safe_message = R"({"side_effect_may_have_occurred":true,"input_released":true})";
            return error;
        }));
    // Fresh proof of an older application condition says nothing about the
    // newer uncertain key. It must not clear that key's global safety fence.
    MIRAGE_CHECK(text(unwrap(v.recheck(id, f.ctx)), "status") == "verified");
    MIRAGE_CHECK(!v.execute("desktop.input.key", args, f.ctx, f.handler()));
    MIRAGE_CHECK(!v.settle() && f.inputs == 1);

    // Process presence cannot prove release, even when a faulty handler
    // reports the failure without marking the coordinator's lease dirty.
    Verification unreleased(f.tools, f.ctx, {});
    MIRAGE_CHECK(!unreleased.execute(
        "desktop.application.launch", Json::Object{{"application_id", "app.desktop"}}, f.ctx,
        [](const auto &, const auto &) -> mira::Result<Json> {
            mira::Error error;
            error.code = mira::ErrorCode::PlatformError;
            error.safe_message = R"({"side_effect_may_have_occurred":true,"input_released":false})";
            return error;
        }));
    unreleased.note_observation();
    MIRAGE_CHECK(!unreleased.settle());
    MIRAGE_CHECK(!unreleased.execute("desktop.input.key", args, f.ctx, f.handler()));
    MIRAGE_CHECK(f.inputs == 1);
}
void registration(const Json &golden) {
    Fixture f;
    Verification v(f.tools, f.ctx, {});
    mira::BuiltinToolRegistry registry;
    auto decorate = [&](const std::string &name, mira::BuiltinToolHandler handler) {
        return [&, name, handler](const auto &args, const auto &ctx) {
            return Verification::mutation(name) ? v.execute(name, args, ctx, handler)
                                                : handler(args, ctx);
        };
    };
    i::DesktopAtomToolset::register_harness_tools(registry, f.tools, true, {}, decorate);
    v.register_tool(registry, decorate);
    for (const auto &spec : registry.exposed_tools()) {
        if (Verification::mutation(spec.wire_name)) {
            MIRAGE_CHECK(std::int64_t{spec.version.major} ==
                         ((spec.wire_name == "desktop.input.key" ||
                           spec.wire_name == "desktop.input.type_text")
                              ? *golden.find("natural_input_tool_major")->as_integer()
                              : *golden.find("mutation_tool_major")->as_integer()));
            const auto *dependencies =
                spec.parameters_schema.root.find("properties")->find("depends_on");
            MIRAGE_CHECK(dependencies);
            if (dependencies)
                MIRAGE_CHECK(dependencies->find("maxItems")->as_integer() ==
                             golden.find("max_dependencies")->as_integer());
        }
        if (spec.wire_name == "desktop.action.verify")
            MIRAGE_CHECK(std::int64_t{spec.version.major} ==
                         *golden.find("verify_tool_major")->as_integer());
    }
    MIRAGE_CHECK(Verification::observation("desktop.screen.capture"));
    MIRAGE_CHECK(!Verification::observation("desktop.clipboard.read_text"));
    MIRAGE_CHECK(!Verification::observation("desktop.action.verify"));
}
void expiry(const Json &golden) {
    Fixture f;
    f.matched = true;
    Verification v(f.tools, f.ctx, {});
    v.set_shared_observation(f.capture());
    const auto action =
        v.execute("desktop.session.key",
                  Json::Object{{"verify_kind", "shared_text_exists"}, {"expected_text", "Saved"}},
                  f.ctx, f.handler());
    const auto id = text(unwrap(action), "action_id");
    v.note_observation();
    MIRAGE_CHECK(v.settle());
    mira::ModelRequest request;
    request.task_id = f.ctx.task;
    request.task_epoch = f.ctx.task_epoch;
    MIRAGE_CHECK(v.validate_request(request, f.ctx));
    Fixture raw;
    raw.matched = true;
    Verification unconsumed(raw.tools, raw.ctx, {});
    unconsumed.set_shared_observation(raw.capture());
    const auto raw_action = unconsumed.execute(
        "desktop.session.key",
        Json::Object{{"verify_kind", "shared_text_exists"}, {"expected_text", "Saved"}}, raw.ctx,
        raw.handler());
    const auto raw_id = text(unwrap(raw_action), "action_id");
    kairo::Executor executor;
    kairo::ExecutorConfig options;
    options.min_threads = options.max_threads = 1;
    if (!executor.initialize(options))
        throw std::runtime_error("expiry executor initialization failed");
    auto delay =
        executor.submit_delayed(*golden.find("feedback_max_age_ms")->as_integer() + 100, [] {});
    delay.future.get();
    executor.shutdown(true);
    // Freshness is judged at model consumption, not after the model has
    // spent seconds inferring. An unconsumed proof still expires normally.
    MIRAGE_CHECK(v.settle() && f.captures == 1);
    MIRAGE_CHECK(!unconsumed.settle());
    MIRAGE_CHECK(!unconsumed.execute(
        "desktop.input.key", Json::Object{{"window_id", "w1"}, {"depends_on", Json::Array{raw_id}}},
        raw.ctx, raw.handler()));
    MIRAGE_CHECK(raw.captures == 1 && raw.inputs == 1);
    // The consumed condition admits the handler without recapturing. A
    // changed target must still be rejected by its platform preflight.
    int preflight_calls = 0;
    const mira::BuiltinToolHandler preflight = [&](const auto &,
                                                   const auto &) -> mira::Result<Json> {
        ++preflight_calls;
        mira::Error failure;
        failure.code = mira::ErrorCode::InvalidArgument;
        failure.safe_message =
            R"({"status":"rejected","input_released":true,"side_effect_may_have_occurred":false,"reason":"target_pixels_changed"})";
        return failure;
    };
    MIRAGE_CHECK(!v.execute("desktop.input.key",
                            Json::Object{{"window_id", "w1"}, {"depends_on", Json::Array{id}}},
                            f.ctx, preflight));
    MIRAGE_CHECK(preflight_calls == 1 && f.inputs == 1 && f.captures == 1);
    // The request binding is consumed even on a clean preflight rejection.
    MIRAGE_CHECK(!v.execute("desktop.input.key",
                            Json::Object{{"window_id", "w1"}, {"depends_on", Json::Array{id}}},
                            f.ctx, f.handler()));
    MIRAGE_CHECK(f.inputs == 1 && f.captures == 1);
}
} // namespace
int main() {
    try {
        std::ifstream file(std::filesystem::path{__FILE__}.parent_path() /
                           "data/desktop_action_policy_v2.json");
        const std::string source{std::istreambuf_iterator<char>{file}, {}};
        const auto golden = mira::parse_json(source);
        if (!golden)
            throw std::runtime_error("missing policy golden");
        defaults(golden.value());
        dependencies();
        uncertainty();
        unrelated_proof_keeps_fence();
        registration(golden.value());
        expiry(golden.value());
    } catch (const std::exception &error) {
        std::fprintf(stderr, "%s\n", error.what());
        MIRAGE_CHECK(false);
    }
    return mirage::testing::finish("desktop_action_policy_test");
}
