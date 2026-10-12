#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../support/fake_desktop_environment.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <limits>
#include <mira/model_digest.hpp>
#include <mira/model_provider.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/runtime/persistence/settings.hpp>
#include <poll.h>
#include <source_location>
#include <stdexcept>

namespace detail = mirage::integration::detail;
namespace integration = mirage::integration;
using JsonValue = mira::JsonValue;
namespace {
int checks = 0;
void check(bool condition, const std::source_location location = std::source_location::current()) {
    ++checks;
    if (!condition)
        throw std::runtime_error("desktop image check failed: " + std::to_string(checks) +
                                 " at line " + std::to_string(location.line()));
}
mira::OperationContext context() {
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.task_epoch = 7;
    return ctx;
}
integration::DesktopToolContext desktop() {
    integration::DesktopToolContext tools;
    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->displays = {{"d1", {-120, -40, 128, 64}, true}};
    env->windows = {{"w1", "Fixture", {-100, -30, 32, 16}, true}};
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [](const auto &capability, const auto &, const auto &cancel) {
        return capability == "screen.capture" && !cancel();
    };
    return tools;
}

class NativeEnvironment final : public mirage::desktop::DesktopEnvironment,
                                public mirage::desktop::ScreenProvider {
  public:
    int created = 0, captures = 0, closed = 0, inputs = 0;
    bool interactive = false, pixels_changed = false, release_failed = false,
         uncertain_input = false;
    bool fail = false, revoke = false, throw_capture = false, repeated_source_after_input = false;
    std::function<void()> during;
    mirage::desktop::EnvironmentInfo info() const override { return {"native fixture", "test"}; }
    mirage::desktop::ScreenProvider *screen() override { return this; }
    bool desktop_observation_only() const override { return true; }
    bool desktop_interaction_supported() const override { return interactive; }
    std::unique_ptr<mirage::desktop::DesktopCaptureSession>
    create_desktop_interaction_session() override {
        return std::make_unique<Session>(*this, true);
    }
    struct Session final : mirage::desktop::DesktopCaptureSession {
        NativeEnvironment &env;
        bool live = false, input_mode = false;
        explicit Session(NativeEnvironment &v, bool input = false) : env(v), input_mode(input) {
            ++env.created;
        }
        bool input_supported() const override { return input_mode && live; }
        mirage::desktop::InputOutcome input(const mirage::desktop::SharedInputAction &,
                                            const mirage::desktop::InputLimits &,
                                            const mirage::desktop::CancelToken &cancel) override {
            ++env.inputs;
            if (env.release_failed || env.uncertain_input || cancel.cancelled())
                return {false,
                        cancel.cancelled(),
                        {"fixture_input_failed", "failure"},
                        true,
                        !env.release_failed};
            return {true, false, {}, true, true};
        }
        ~Session() override { ++env.closed; }
        bool reusable() const override { return live; }
        mirage::desktop::CaptureOutcome
        capture(const mirage::desktop::CaptureLimits &,
                const mirage::desktop::CancelToken &cancel) override {
            ++env.captures;
            if (env.throw_capture)
                throw std::runtime_error("native capture failure");
            if (env.during)
                env.during();
            if (env.fail || env.revoke || cancel.cancelled())
                return {false,
                        cancel.cancelled(),
                        {},
                        {cancel.cancelled() ? "cancelled"
                         : env.revoke       ? "portal_session_closed"
                                            : "fixture_failed",
                         "fixture"}};
            live = true;
            return {true,
                    false,
                    {mirage::desktop::ImageFormat::Bgra8, 4, 2, 16,
                     std::vector<std::uint8_t>(
                         32, static_cast<std::uint8_t>(
                                 env.interactive ? (env.pixels_changed ? 2 : 1) : env.captures)),
                     static_cast<std::uint64_t>(env.repeated_source_after_input && env.inputs
                                                   ? env.captures - 1 : env.captures)},
                    {}};
        }
    };
    std::unique_ptr<mirage::desktop::DesktopCaptureSession>
    create_desktop_capture_session() override {
        return std::make_unique<Session>(*this);
    }
    mirage::desktop::DisplayListOutcome
    list_displays(const mirage::desktop::DisplayListLimits &,
                  const mirage::desktop::CancelToken &) override {
        return {};
    }
    mirage::desktop::CaptureOutcome capture_display(const std::string &,
                                                    const mirage::desktop::CaptureLimits &,
                                                    const mirage::desktop::CancelToken &) override {
        return {};
    }
    mirage::desktop::CaptureOutcome capture_window(const std::string &,
                                                   const mirage::desktop::CaptureLimits &,
                                                   const mirage::desktop::CancelToken &) override {
        return {};
    }
    mirage::desktop::CaptureOutcome capture_roi(const mirage::desktop::WindowGeometry &,
                                                const mirage::desktop::CaptureLimits &,
                                                const mirage::desktop::CancelToken &) override {
        return {};
    }
};
void native_session(const std::string &mode) {
    auto env = std::make_shared<NativeEnvironment>();
    auto tools = desktop();
    tools.environment = env;
    bool allow = true;
    int permissions = 0;
    tools.gate = [&](const auto &, const auto &, const auto &) {
        ++permissions;
        return allow;
    };
    auto ctx = context();
    mirage::desktop::CancelToken stop;
    {
        detail::DesktopImageTurn turn(std::make_shared<detail::DesktopImageSource>(), tools, ctx,
                                      stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, false);
        const auto spec = registry.exposed_tools().front();
        auto invoke = [&](const auto &owner) {
            mira::ToolProposal call;
            call.tool_id = spec.tool_id;
            call.tool_version = spec.version;
            call.wire_name = spec.wire_name;
            call.operation_id = mira::OperationId::generate();
            call.provider_call_id.value = "native-session";
            call.arguments = JsonValue{JsonValue::Object{{"source", "desktop"}}};
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            return registry.execute(call, owner);
        };
        auto rejected = [&](const auto &owner) {
            const auto result = invoke(owner);
            return !result || result.value().failed;
        };
        env->fail = mode == "first_failure";
        auto first = invoke(ctx);
        check(first && first.value().failed == env->fail);
        check(env->created == 1 && env->captures == 1);
        if (env->fail) {
            check(env->closed == 1);
            check(rejected(ctx));
            check(env->captures == 1 && env->created == 1);
            return;
        }
        check(env->closed == 0);
        const auto first_id = *first.value().result.find("artifact_id")->as_string();
        if (mode == "budget") {
            for (int n = 1; n < 8; ++n)
                check(!invoke(ctx).value().failed);
            check(rejected(ctx));
            check(env->captures == 8 && env->created == 1 && permissions == 8);
        } else if (mode == "reuse") {
            auto lease = tools.actions->try_begin({});
            check(static_cast<bool>(lease));
            lease = {};
            auto second = invoke(ctx);
            check(second && !second.value().failed);
            check(*second.value().result.find("artifact_id")->as_string() != first_id);
            check(env->captures == 2 && env->created == 1 && permissions == 2);
        } else {
            if (mode == "deny")
                allow = false;
            if (mode == "cancel")
                stop.request_cancel();
            if (mode == "takeover") {
                tools.actions->takeover();
                check(tools.actions->resume());
            }
            if (mode == "revoke")
                env->revoke = true;
            if (mode == "throw")
                env->throw_capture = true;
            if (mode == "during_takeover")
                env->during = [&] { tools.actions->takeover(); };
            if (mode == "foreign") {
                auto foreign = ctx;
                foreign.task = mira::TaskId::generate();
                check(rejected(foreign));
                check(env->captures == 1);
            } else {
                check(rejected(ctx));
                // Cancelled owners close during supply/turn teardown as well.
                mira::AgentLoopSpec spec_owner;
                spec_owner.task_id = ctx.task;
                spec_owner.session_id = ctx.session;
                spec_owner.task_epoch = ctx.task_epoch;
                (void)turn.supply(spec_owner);
                check(env->closed == 1);
                check(rejected(ctx));
                check(env->created == 1);
            }
        }
    }
    check(env->closed == env->created);
    // A second task gets its own session, rather than inheriting authorization.
    env->fail = env->revoke = env->throw_capture = false;
    env->during = {};
    if (mode == "reuse") {
        detail::DesktopImageTurn other(std::make_shared<detail::DesktopImageSource>(), tools,
                                       context(), {});
        check(env->closed == 1);
    }
}

void shared_contracts(const std::string &mode) {
    auto env = std::make_shared<NativeEnvironment>();
    env->interactive = true;
    auto tools = desktop();
    tools.environment = env;
    bool input_allowed = true;
    tools.gate = [&](const auto &cap, const auto &, const auto &cancel) {
        return !cancel() && (cap == "screen.capture" || (input_allowed && cap == "input.inject"));
    };
    tools.ocr_image = [&](const auto &, const auto &, const auto &) {
        integration::ImageOcrOutcome out;
        out.ok = true;
        out.regions = {{mode == "wrong_ocr" ? "other" : "fixture", 0.9, 0, 0, 3, 1}};
        if (mode == "ocr_invalid")
            out.regions.front().confidence = std::numeric_limits<double>::quiet_NaN();
        return out;
    };
    auto ctx = context();
    mirage::desktop::CancelToken stop;
    detail::DesktopActionVerification verification(tools, ctx, stop);
    {
        detail::DesktopImageTurn turn(std::make_shared<detail::DesktopImageSource>(), tools, ctx,
                                      stop);
        verification.set_shared_observation(
            [&](const auto &kind, const auto &expected, const auto &owner) {
                return turn.observe_shared(kind, expected, owner);
            });
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry,
                           [&](const auto &name, auto handler) -> mira::BuiltinToolHandler {
                               return [&, name, handler](const auto &args, const auto &owner) {
                                   return detail::DesktopActionVerification::mutation(name)
                                              ? verification.execute(name, args, owner, handler)
                                              : handler(args, owner);
                               };
                           });
        const auto invoke = [&](const std::string &name, const JsonValue &args,
                                const mira::OperationContext &owner) {
            const auto exposed = registry.exposed_tools();
            const auto found = std::find_if(exposed.begin(), exposed.end(), [&](const auto &spec) {
                return spec.wire_name == name;
            });
            check(found != exposed.end());
            mira::ToolProposal call;
            call.tool_id = found->tool_id;
            call.tool_version = found->version;
            call.wire_name = name;
            call.has_side_effects = found->has_side_effects;
            call.operation_id = mira::OperationId::generate();
            call.provider_call_id.value = "shared";
            call.arguments = args;
            call.arguments_digest = mira::canonical_json_digest(args);
            return registry.execute(call, owner);
        };
        if (mode == "initial_denied") {
            input_allowed = false;
            const auto a = invoke(
                "desktop.screen.capture",
                JsonValue{JsonValue::Object{{"source", "desktop"}, {"interaction", true}}}, ctx);
            check(a && a.value().failed && env->created == 0);
            input_allowed = true;
            const auto b = invoke(
                "desktop.screen.capture",
                JsonValue{JsonValue::Object{{"source", "desktop"}, {"interaction", true}}}, ctx);
            check(b && b.value().failed && env->created == 0);
            return;
        }
        auto captured =
            invoke("desktop.screen.capture",
                   JsonValue{JsonValue::Object{{"source", "desktop"}, {"interaction", true}}}, ctx);
        check(captured && !captured.value().failed && env->created == 1);
        const auto &meta = captured.value().result;
        check(meta.find("input_actionable")->as_boolean() == true &&
              meta.find("observation_only")->as_boolean() == false);
        auto args = JsonValue{JsonValue::Object{
            {"image_scope", *meta.find("image_scope")},
            {"environment_epoch", *meta.find("environment_epoch")},
            {"x", 0},
            {"y", 0},
            {"width", 2},
            {"height", 1},
            {"verify_kind", (mode == "ocr" || mode == "wrong_ocr" || mode == "ocr_invalid")
                                ? "shared_text_exists"
                                : "shared_frame_observed"}}};
        if (mode == "ocr" || mode == "wrong_ocr" || mode == "ocr_invalid")
            args.set("expected_text", "fixture");
        if (mode != "no_image") {
            mira::AgentLoopSpec spec;
            spec.task_id = ctx.task;
            spec.session_id = ctx.session;
            spec.task_epoch = ctx.task_epoch;
            auto supplied = turn.supply(spec);
            check(bool(supplied));
            mira::ModelRequest request;
            request.task_id = ctx.task;
            request.task_epoch = ctx.task_epoch;
            request.input = supplied.value();
            check(bool(turn.validate_request(request, ctx)));
        }
        auto action_ctx = ctx;
        if (mode == "foreign")
            action_ctx.task = mira::TaskId::generate();
        if (mode == "changed")
            env->pixels_changed = true;
        if (mode == "denied")
            input_allowed = false;
        if (mode == "release")
            env->release_failed = true;
        if (mode == "uncertain")
            env->uncertain_input = true;
        if (mode == "takeover")
            tools.actions->takeover();
        if (mode == "stale")
            args.set("image_scope", "foreign-image");
        if (mode == "epoch") {
            auto lease = tools.actions->try_begin({});
            check(bool(lease));
        }
        if (mode == "invalid_crop")
            args.set("width", 5);
        if (mode == "repeated_source")
            env->repeated_source_after_input = true;
        if (mode == "post_failure")
            env->during = [&] {
                if (env->inputs)
                    env->fail = true;
            };
        if (mode == "pretimeout")
            env->during = [&] {
                if (env->captures == 2)
                    (void)::poll(nullptr, 0, 5100);
            };
        if (mode == "cancel")
            stop.request_cancel();
        if (mode == "unicode")
            args.set("text", "fixture中文");
        auto result =
            invoke(mode == "unicode" ? "desktop.session.type_text" : "desktop.session.click", args,
                   action_ctx);
        if (mode == "unicode") {
            check(result && result.value().failed &&
                  result.value().safe_error_summary.find("text_transport_unavailable") !=
                      std::string::npos);
            check(env->captures == 1); // no pre-capture and no provider input
        }
        if (mode == "pretimeout")
            check(result && result.value().failed &&
                  result.value().safe_error_summary.find("input_pre_capture_timeout") !=
                      std::string::npos);
        const bool success = mode == "normal" || mode == "ocr";
        const bool dispatched =
            success || mode == "post_failure" || mode == "wrong_ocr" || mode == "ocr_invalid" ||
            mode == "repeated_source";
        if (dispatched && !success) {
            check(result && !result.value().failed &&
                  result.value().result.find("verified")->as_boolean() == false);
            check(!verification.settle() && env->inputs == 1);
            return;
        }
        if (!result || result.value().failed) {
            if (success)
                std::cerr << "shared " << mode << " failed: "
                          << (result ? result.value().safe_error_summary
                                     : result.error().safe_message)
                          << "\n";
        }
        check(success ? bool(result) && !result.value().failed : !result || result.value().failed);
        check(env->inputs == (success || mode == "release" || mode == "uncertain" ? 1 : 0));
        if (success) {
            check(result.value().result.find("verified")->as_boolean() == true);
            check(verification.settle());
        }
        if (mode == "release")
            check(!tools.actions->input_released() && tools.actions->authority_epoch() == 0);
        if (mode == "uncertain" || mode == "release")
            check(!verification.settle());
    }
    check(env->created == env->closed);
}

class Script final : public mira::IModelProvider {
  public:
    explicit Script(mira::ModelProfile profile) : profile_(std::move(profile)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    std::vector<mira::ModelRequest> requests;
    std::function<void()> after_first;
    bool throw_on_image = false, native = false;
    std::function<void()> after_second;
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        requests.push_back(request);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (requests.size() == 1 || (native && requests.size() == 2)) {
            if (requests.size() == 2) {
                if (throw_on_image)
                    throw std::runtime_error("native model failure");
                if (after_second)
                    after_second();
            }
            const auto tool =
                std::find_if(request.tools.begin(), request.tools.end(), [](const auto &value) {
                    return value.wire_name == "desktop.screen.capture";
                });
            if (!profile_.capabilities.image_input.supported) {
                check(tool == request.tools.end());
                mira::MessageOutput message;
                message.content.emplace_back(mira::OutputTextPart{"text only", {}});
                response.output.emplace_back(std::move(message));
                return response;
            }
            check(tool != request.tools.end());
            mira::ToolCallOutput call;
            call.provider_call_id.value = "capture-" + std::to_string(requests.size());
            call.tool_id = tool->tool_id;
            call.provider_name = tool->wire_name;
            call.arguments = mira::parse_json(native ? R"({"source":"desktop"})"
                                                     : R"({"source":"display","source_id":"d1"})")
                                 .value();
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            response.output.emplace_back(std::move(call));
            if (requests.size() == 1 && after_first)
                after_first();
        } else {
            if (throw_on_image)
                throw std::runtime_error("injected image inference failure");
            mira::MessageOutput message;
            message.content.emplace_back(
                mira::OutputTextPart{"image inspected; business unverified", {}});
            response.output.emplace_back(std::move(message));
        }
        return response;
    }

  private:
    mira::ModelProfile profile_;
};

class NativeOcrScript final : public mira::IModelProvider {
  public:
    mira::ModelProfile profile_;
    std::vector<mira::ModelRequest> requests;
    std::vector<std::string> steps;
    bool images = false, throw_final = false, invalid_args = false, expect_truncated = false;
    explicit NativeOcrScript(const mira::ModelProfile &p) : profile_(p) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        std::size_t image_count = 0;
        const mira::ImagePart *image = nullptr;
        const mira::ToolResultPart *ocr = nullptr;
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                if (const auto *p = std::get_if<mira::ImagePart>(&part)) {
                    ++image_count;
                    image = p;
                }
                if (const auto *p = std::get_if<mira::ToolResultPart>(&part);
                    p && !p->failed && p->result.find("image_artifact_id"))
                    ocr = p;
            }
        if (!images)
            check(image_count == 0);
        if (images && ocr) {
            check(image_count == 1 && image);
            check(image->source.id.to_string() ==
                  *ocr->result.find("image_artifact_id")->as_string());
        }
        if (ocr) {
            check(mira::to_json_string(ocr->result).size() <= (16u << 10));
            check(ocr->result.find("truncated")->as_boolean() == expect_truncated);
            const auto count = ocr->result.find("regions")->as_array()->size();
            check(ocr->result.find("region_count")->as_integer() ==
                  static_cast<std::int64_t>(count));
            if (expect_truncated && ocr->result.find("total_region_count")->as_integer() == 30)
                for (const auto &region : *ocr->result.find("regions")->as_array())
                    check(region.find("text")->as_string()->size() == 1024);
            check(expect_truncated ? ocr->result.find("total_region_count")->as_integer() >
                                         static_cast<std::int64_t>(count)
                                   : ocr->result.find("total_region_count")->as_integer() ==
                                         static_cast<std::int64_t>(count));
            check(ocr->result.find("coordinate_space") && !ocr->result.find("display_id") &&
                  !ocr->result.find("visual_scope"));
            check(ocr->result.find("regions")->as_array()->front().find("text"));
            check(!ocr->result.find("regions")->as_array()->front().find("ref"));
        }
        const auto step = requests.size();
        requests.push_back(request);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (step < steps.size()) {
            const auto &name = steps[step];
            const auto spec = std::find_if(request.tools.begin(), request.tools.end(),
                                           [&](const auto &t) { return t.wire_name == name; });
            check(spec != request.tools.end());
            if (!images)
                check(std::none_of(request.tools.begin(), request.tools.end(), [](const auto &t) {
                    return t.wire_name == "desktop.screen.capture";
                }));
            mira::ToolCallOutput call;
            call.tool_id = spec->tool_id;
            call.provider_name = name;
            call.provider_call_id.value = "native-ocr-" + std::to_string(step);
            call.arguments = name == "desktop.screen.capture"
                                 ? JsonValue{JsonValue::Object{{"source", "desktop"}}}
                             : invalid_args
                                 ? JsonValue{JsonValue::Object{{"window_id", "invented"}}}
                                 : JsonValue{JsonValue::Object{}};
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            response.output.emplace_back(std::move(call));
        } else {
            if (throw_final)
                throw std::runtime_error("native OCR model failure");
            mira::MessageOutput message;
            message.content.emplace_back(
                mira::OutputTextPart{"已读取当前画面的文字，页面任务仍待验证。", {}});
            response.output.emplace_back(std::move(message));
        }
        return response;
    }
};
class SharedScript final : public mira::IModelProvider {
  public:
    mira::ModelProfile profile_;
    int calls = 0;
    std::string first_scope;
    explicit SharedScript(mira::ModelProfile value) : profile_(std::move(value)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        JsonValue metadata;
        std::string host_text;
        for (const auto &item : request.input)
            for (const auto &part : item.content)
                if (const auto *text = std::get_if<mira::TextPart>(&part)) {
                    host_text += text->text + "\n";
                    const std::string prefix = "Untrusted desktop image; coordinates and source: ";
                    if (text->text.starts_with(prefix))
                        metadata = mira::parse_json(text->text.substr(prefix.size())).value();
                }
        if (calls == 0) {
            const auto begin = host_text.find("Available tools for THIS turn (closed set):");
            check(begin != std::string::npos);
            const auto exposed = begin == std::string::npos ? std::string{}
                : host_text.substr(begin, host_text.find('\n', begin) - begin);
            for (const auto &tool : request.tools)
                check(exposed.find(" " + tool.wire_name) != std::string::npos);
            check(exposed.find("desktop.window.list") == std::string::npos);
            check(exposed.find("desktop.session.click") != std::string::npos);
            check(host_text.find("The user need not request these methods by name") !=
                  std::string::npos);
            check(host_text.find("does NOT prevent clicking a visible") != std::string::npos);
            check(host_text.find("first consider an exposed, actionable image-bound click") !=
                  std::string::npos);
            check(host_text.find("If Mirage obscures the browser, ask") == std::string::npos);
            const auto click = std::find_if(request.tools.begin(), request.tools.end(),
                                            [](const auto &tool) {
                                                return tool.wire_name == "desktop.session.click";
                                            });
            check(click != request.tools.end() &&
                  click->description.find("to focus it; no window API is needed") !=
                      std::string::npos);
            check(click->description.find("Every capture or OCR refresh replaces") !=
                  std::string::npos);
        }
        if (calls == 1)
            first_scope = *metadata.find("image_scope")->as_string();
        if (calls == 2) {
            check(*metadata.find("image_scope")->as_string() != first_scope);
            const mira::ToolResultPart *ocr_result = nullptr;
            for (const auto &item : request.input)
                for (const auto &part : item.content)
                    if (const auto *result = std::get_if<mira::ToolResultPart>(&part);
                        result && !result->failed && result->result.find("image_artifact_id"))
                        ocr_result = result;
            check(ocr_result && ocr_result->result.find("input_actionable") &&
                  ocr_result->result.find("input_actionable")->as_boolean() == true);
            check(ocr_result->result.find("image_scope") &&
                  *ocr_result->result.find("image_scope")->as_string() ==
                      *metadata.find("image_scope")->as_string());
        }
        const std::vector<std::string> names = {"desktop.screen.capture", "desktop.vision.ocr", "desktop.session.click",
                                                "desktop.session.key", "desktop.session.type_text",
                                                "desktop.session.scroll"};
        if (calls < static_cast<int>(names.size())) {
            const auto name = names[calls];
            const auto spec = std::find_if(request.tools.begin(), request.tools.end(),
                                           [&](const auto &t) { return t.wire_name == name; });
            check(spec != request.tools.end());
            JsonValue args;
            if (!calls)
                args = JsonValue{JsonValue::Object{{"source", "desktop"}, {"interaction", true}}};
            else if (calls == 1) {
                args = JsonValue{JsonValue::Object{}};
            } else {
                check(metadata.find("input_actionable") &&
                      metadata.find("input_actionable")->as_boolean() == true);
                args = JsonValue{
                    JsonValue::Object{{"image_scope", *metadata.find("image_scope")},
                                      {"environment_epoch", *metadata.find("environment_epoch")},
                                      {"x", 0},
                                      {"y", 0},
                                      {"width", 2},
                                      {"height", 1},
                                      {"verify_kind", "shared_frame_observed"}}};
                if (calls == 3)
                    args.set("key_name", "ctrl+l");
                if (calls == 4)
                    args.set("text", "private-wayland-input");
                if (calls == 5)
                    args.set("vertical_steps", 1);
            }
            mira::ToolCallOutput call;
            call.tool_id = spec->tool_id;
            call.provider_name = name;
            call.provider_call_id.value = "shared-model-" + std::to_string(calls);
            call.arguments = args;
            call.arguments_digest = mira::digest_string(mira::to_json_string(args));
            response.output.emplace_back(std::move(call));
        } else {
            mira::MessageOutput message;
            message.content.emplace_back(
                mira::OutputTextPart{"局部输入与新观察通过，业务仍待验证。", {}});
            response.output.emplace_back(std::move(message));
        }
        ++calls;
        return response;
    }
};
void shared_loop(kairo::Executor &executor) {
    auto env = std::make_shared<NativeEnvironment>();
    env->interactive = true;
    auto tools = desktop();
    tools.environment = env;
    tools.gate = [](const auto &, const auto &, const auto &cancel) { return !cancel(); };
    tools.ocr_image = [](const auto &, const auto &, const auto &) {
        integration::ImageOcrOutcome out;
        out.ok = true;
        out.model_revision = "shared-ocr";
        out.regions = {{"fixture", 0.9, 0, 0, 2, 1}};
        return out;
    };
    std::shared_ptr<SharedScript> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<SharedScript>(profile);
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = "shared-fixture";
    integration::ModelLayer model(executor, config, &provider, tools);
    auto result = executor
                      .submit_auto([&] {
                          return model.complete_harness_turn("", "test shared input", context());
                      })
                      .get();
    if (!result.ok)
        std::cerr << "shared model: " << result.error << "\n";
    check(result.ok && script->calls == 7 && env->inputs == 4 && env->created == env->closed);
    for (const auto &part : result.parts) {
        check(part.input.find("private-wayland-input") == std::string::npos);
        check(part.output.find("private-wayland-input") == std::string::npos);
    }
    model.shutdown();
}

void native_ocr_loop(kairo::Executor &executor, const std::string &mode) {
    auto env = std::make_shared<NativeEnvironment>();
    auto tools = desktop();
    tools.environment = env;
    bool images = mode != "text_only";
    int analyzed = 0;
    mirage::desktop::CancelToken stop;
    tools.gate = [&](const auto &cap, const auto &scope, const auto &cancel) {
        check(cap == "screen.capture" && scope == "desktop");
        return mode != "denied" && !cancel();
    };
    tools.ocr_image = [&](const auto &image, const auto &, const auto &) {
        ++analyzed;
        check(image.width == 4 && image.height == 2 && image.pixels.front() == env->captures);
        if (mode == "throw")
            throw std::runtime_error("fixture OCR");
        if (mode == "cancel")
            stop.request_cancel();
        if (mode == "epoch") {
            auto lease = tools.actions->try_begin({});
            check(static_cast<bool>(lease));
        }
        if (mode == "takeover") {
            tools.actions->takeover();
            check(tools.actions->resume());
        }
        integration::ImageOcrOutcome out;
        out.ok = mode != "failed" && !(mode == "recovery" && analyzed == 1);
        out.error = {"analysis_failed", "fixture failure"};
        out.model_revision = "fixture-ocr-v1";
        out.backend_calls = analyzed;
        out.regions = {{"private-native-text", 0.9, 0, 0, 3, 1}};
        if (mode == "nan")
            out.regions.front().x = std::numeric_limits<double>::quiet_NaN();
        if (mode == "bounds")
            out.regions.front().width = 5;
        if (mode == "regions")
            out.regions.resize(129, out.regions.front());
        if (mode == "text_budget")
            out.regions.front().text.resize(1025, 'x');
        if (mode == "output_budget")
            out.regions.assign(30, {std::string(1024, 'x'), 0.9, 0, 0, 3, 1});
        if (mode == "escaped_budget")
            out.regions.assign(30, {std::string(1024, '\x01'), 0.9, 0, 0, 3, 1});
        if (mode == "candidate_prefix") {
            out.regions.resize(128, out.regions.front());
            out.total_region_count = 160;
        }
        return out;
    };
    std::shared_ptr<NativeOcrScript> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<NativeOcrScript>(profile);
        script->images = images;
        script->throw_final = mode == "model_throw";
        script->invalid_args = mode == "invalid_args";
        script->expect_truncated =
            mode == "output_budget" || mode == "escaped_budget" || mode == "candidate_prefix";
        if (images)
            script->steps.push_back("desktop.screen.capture");
        script->steps.push_back("desktop.vision.ocr");
        if (mode == "recovery")
            script->steps.push_back("desktop.vision.ocr");
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = true;
    config.supports_images = images;
    config.model_selector = "native-ocr-fixture";
    integration::ModelLayer model(executor, config, &provider, tools);
    auto result = executor
                      .submit_auto([&] {
                          return model.complete_harness_turn("", "读取当前浏览器文字", context(),
                                                             "", false, {}, {}, stop);
                      })
                      .get();
    const bool success = mode == "normal" || mode == "text_only" || mode == "recovery" ||
                         mode == "output_budget" || mode == "escaped_budget" ||
                         mode == "candidate_prefix";
    if (result.ok != success)
        std::cerr << "native OCR " << mode << ": " << result.error << '\n';
    check(result.ok == success);
    check(success || result.failed || result.cancelled);
    check(env->created == env->closed);
    if (mode == "normal") {
        check(script && !script->requests.empty());
        const auto &request = script->requests.front();
        check(std::none_of(request.tools.begin(), request.tools.end(), [](const auto &tool) {
            return tool.wire_name.starts_with("desktop.session.");
        }));
        for (const auto &item : request.input)
            for (const auto &part : item.content)
                if (const auto *text = std::get_if<mira::TextPart>(&part))
                    check(text->text.find("Native desktop mouse and keyboard tools ARE exposed") ==
                          std::string::npos);
    }
    check(analyzed == (mode == "denied" || mode == "invalid_args" ? 0
                       : mode == "recovery"                       ? 2
                                                                  : 1));
    for (const auto &part : result.parts) {
        check(part.output.find("private-native-text") == std::string::npos);
        if (success && part.name == "desktop.vision.ocr" && part.status == "complete") {
            check(part.output.find("region_count") != std::string::npos);
            check(part.output.find("image_artifact_id") != std::string::npos);
        }
    }
    model.shutdown();
}

void native_loop(kairo::Executor &executor, const std::string &mode) {
    auto env = std::make_shared<NativeEnvironment>();
    auto tools = desktop();
    tools.environment = env;
    mirage::desktop::CancelToken stop;
    std::shared_ptr<Script> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<Script>(profile);
        script->native = true;
        script->throw_on_image = mode == "model_throw";
        script->after_second = [&] {
            if (mode == "mutation") {
                auto lease = tools.actions->try_begin({});
                check(static_cast<bool>(lease));
            }
            if (mode == "cancel")
                stop.request_cancel();
            if (mode == "takeover") {
                tools.actions->takeover();
                check(tools.actions->resume());
            }
        };
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = "native-vision-fixture";
    integration::ModelLayer model(executor, config, &provider, tools);
    auto result =
        executor
            .submit_auto([&] {
                return model.complete_harness_turn("", "inspect, then obtain a fresh frame",
                                                   context(), "", false, {}, {}, stop);
            })
            .get();
    const bool success = mode == "normal" || mode == "mutation";
    check(result.ok == success);
    check(env->created == 1 && env->closed == 1);
    check(env->captures == (success ? 2 : 1));
    if (success) {
        check(script->requests.size() == 3);
        std::vector<std::string> images;
        for (const auto &request : script->requests)
            for (const auto &item : request.input)
                for (const auto &part : item.content)
                    if (const auto *image = std::get_if<mira::ImagePart>(&part))
                        images.push_back(image->source.id.to_string());
        check(images.size() == 2 && images[0] != images[1]);
        check(result.supplied_input_dropped == 0 && result.supply_degradations == 0);
    } else
        check(result.failed || result.cancelled);
    model.shutdown();
}

void layer(kairo::Executor &executor, const std::string &dialect, bool enabled, bool allowed,
           bool cancelled = false, bool provider_failure = false) {
    auto tools = desktop();
    auto env = std::static_pointer_cast<mirage::testing::FakeDesktopEnvironment>(tools.environment);
    if (!allowed)
        tools.gate = [](const auto &, const auto &, const auto &) { return false; };
    std::shared_ptr<Script> script;
    mirage::desktop::CancelToken token;
    integration::ModelProviderOverride override([&](const auto &profile) {
        script = std::make_shared<Script>(profile);
        script->throw_on_image = provider_failure;
        if (cancelled)
            script->after_first = [&] { token.request_cancel(); };
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = true;
    config.model_selector = "vision-fixture";
    config.dialect = dialect;
    config.supports_images = enabled;
    integration::ModelLayer model(executor, config, &override, tools);
    const auto ctx = context();
    auto result = executor
                      .submit_auto([&] {
                          return model.complete_harness_turn("", "Inspect the authorized display",
                                                             ctx, "", false, {}, {}, token);
                      })
                      .get();
    check(result.ok == (!cancelled && !provider_failure));
    check(!provider_failure || result.failed);
    check(result.supply_degradations == 0 && result.supplied_input_dropped == 0);
    check(env->capture_calls == (enabled && allowed && !cancelled ? 1u : 0u));
    if (enabled && allowed && !cancelled) {
        check(script->requests.size() == 2 && result.supplied_input_items == 1);
        bool image = false, canonical_result = false;
        for (const auto &item : script->requests.back().input)
            for (const auto &part : item.content) {
                if (const auto *value = std::get_if<mira::ImagePart>(&part)) {
                    check(value->source.media_type == "image/png" && value->source.byte_size > 50);
                    check(value->source.sensitivity == mira::Sensitivity::Sensitive);
                    image = true;
                }
                if (const auto *value = std::get_if<mira::ToolResultPart>(&part))
                    canonical_result =
                        !value->failed && value->provider_call_id.value == "capture-1";
            }
        check(image && canonical_result);
        check(result.parts.size() == 1 && result.parts.front().status == "complete");
        check(result.parts.front().output.find("artifact_id") != std::string::npos);
    }
    model.shutdown();
}

void contracts(kairo::Executor &executor) {
    const auto ctx = context();
    auto tools = desktop();
    auto env = std::static_pointer_cast<mirage::testing::FakeDesktopEnvironment>(tools.environment);
    auto source = std::make_shared<detail::DesktopImageSource>();
    mira::ArtifactRef retired;
    {
        detail::DesktopImageTurn turn(source, tools, ctx, {});
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {});
        const auto exposed = registry.exposed_tools();
        check(exposed.size() == 3);
        const auto display_spec =
            std::find_if(exposed.begin(), exposed.end(), [](const auto &tool) {
                return tool.wire_name == "desktop.screen.list_displays";
            });
        check(display_spec != exposed.end());
        mira::ToolProposal displays;
        displays.tool_id = display_spec->tool_id;
        displays.tool_version = display_spec->version;
        displays.wire_name = display_spec->wire_name;
        displays.operation_id = mira::OperationId::generate();
        displays.provider_call_id.value = "displays";
        displays.arguments = mira::parse_json("{}").value();
        displays.arguments_digest = mira::canonical_json_digest(displays.arguments);
        const auto listed = registry.execute(displays, ctx);
        check(listed && !listed.value().failed);
        check(mira::to_json_string(listed.value().result).find("d1") != std::string::npos);
        check(env->capture_calls == 0);
        const auto execute = [&](const char *json, const mira::OperationContext &operation) {
            mira::ToolProposal call;
            call.tool_version = exposed.front().version;
            call.operation_id = mira::OperationId::generate();
            call.tool_id = exposed.front().tool_id;
            call.wire_name = exposed.front().wire_name;
            call.provider_call_id.value = "capture";
            call.arguments = mira::parse_json(json).value();
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            return registry.execute(call, operation);
        };
        auto roi = execute(
            R"({"source":"roi","source_id":"d1","x":-110,"y":-35,"width":8,"height":4})", ctx);
        check(roi && !roi.value().failed);
        check(*roi.value().result.find("origin_x")->as_integer() == -110);
        check(*roi.value().result.find("width")->as_integer() == 8);
        mira::AgentLoopSpec spec;
        spec.task_id = ctx.task;
        spec.session_id = ctx.session;
        spec.task_epoch = ctx.task_epoch;
        auto inputs = turn.supply(spec);
        check(inputs && inputs.value().size() == 1);
        auto image = std::get<mira::ImagePart>(inputs.value().front().content.back());
        retired = image.source;
        const auto bytes = source->fetch(image.source);
        check(bytes && bytes.value().size() == image.source.byte_size);
        auto forged = image.source;
        forged.digest.bytes[0] ^= 1;
        check(!source->fetch(forged));
        mira::ModelRequest request;
        request.output_contract.mode = mira::OutputMode::Text;
        request.generation.max_output_tokens = 1024;
        request.data_policy.store = false;
        request.task_id = ctx.task;
        request.task_epoch = ctx.task_epoch;
        request.input = inputs.value();
        check(static_cast<bool>(turn.validate_request(request, ctx)));
        auto other_session = ctx;
        other_session.session = mira::SessionId::generate();
        check(!turn.validate_request(request, other_session));
        request.input.clear();
        check(!turn.validate_request(request, ctx)); // No silent text continuation after a drop.
        request.input = inputs.value();
        for (const auto &dialect :
             {"openai.responses.v1", "openai.chat-completions.v1", "anthropic.messages.v1"}) {
            mira::ModelProfile profile;
            profile.id = mira::ModelProfileId::generate();
            profile.model_selector = "fixture";
            profile.capabilities.image_input = {true, mira::CapabilityEvidence::FixtureVerified,
                                                ""};
            profile.capabilities.text = {true, mira::CapabilityEvidence::FixtureVerified, ""};
            profile.dialect = *mira::protocol_dialect_from(dialect);
            mira::Result<mira::JsonValue> wire =
                profile.dialect == mira::ProtocolDialect::AnthropicMessagesV1
                    ? mira::AnthropicMessagesV1Mapper{}.encode_request(request, profile, false,
                                                                       *source)
                : profile.dialect == mira::ProtocolDialect::OpenAIResponsesV1
                    ? mira::ResponsesV1Mapper{}.encode_request(request, profile, false, *source)
                    : mira::ChatCompletionsV1Mapper{}.encode_request(request, profile, false,
                                                                     *source);
            if (!wire)
                throw std::runtime_error("dialect fixture: " + wire.error().safe_message);
            check(static_cast<bool>(wire));
            check(mira::to_json_string(wire.value()).find("image/png") != std::string::npos);
        }
        auto window = execute(R"({"source":"window","source_id":"w1"})", ctx);
        check(window && !window.value().failed && !source->fetch(retired));
        auto bad_roi = execute(
            R"({"source":"roi","source_id":"d1","x":-121,"y":-40,"width":2,"height":2})", ctx);
        check(bad_roi && bad_roi.value().failed);
        auto foreign = ctx;
        foreign.session = mira::SessionId::generate();
        const auto mismatch = execute(R"({"source":"display","source_id":"d1"})", foreign);
        check(mismatch && mismatch.value().failed);
        env->displays.front().geometry = {0, 0, 8192, 8192};
        auto large = execute(R"({"source":"display","source_id":"d1"})", ctx);
        check(large && large.value().failed && env->capture_calls == 0);
        tools.actions->takeover();
        check(!turn.validate_request(request, ctx));
        auto invalidated = turn.supply(spec);
        check(invalidated &&
              invalidated.value().empty()); // Failed replacement retires the old source.
        spec.task_epoch++;
        check(!turn.supply(spec) && !turn.validate_request(request, ctx));
    }
    check(!source->fetch(retired));
    // DEC-068: failed preflight attempts do not consume acquisition slots.
    (void)tools.actions->resume();
    env->displays.front().geometry = {0, 0, 3200, 2000};
    {
        detail::DesktopImageTurn turn(source, tools, ctx, {});
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {});
        const auto exposed = registry.exposed_tools().front();
        const auto run = [&](const char *arguments) {
            mira::ToolProposal p;
            p.operation_id = mira::OperationId::generate();
            p.provider_call_id.value = p.operation_id.to_string();
            p.tool_id = exposed.tool_id;
            p.tool_version = exposed.version;
            p.wire_name = exposed.wire_name;
            p.arguments = mira::parse_json(arguments).value();
            p.arguments_digest = mira::canonical_json_digest(p.arguments);
            return registry.execute(p, ctx);
        };
        for (int n = 0; n < 6; ++n)
            check(run(R"({"source":"display","source_id":"d1","width":1600,"height":1000})")
                      .value()
                      .failed);
        auto preview = run(R"({"source":"display","source_id":"d1"})");
        if (!preview)
            throw std::runtime_error(preview.error().safe_message);
        if (preview && preview.value().failed)
            throw std::runtime_error(preview.value().safe_error_summary);
        check(preview && !preview.value().failed);
        const auto &meta = preview.value().result;
        check(*meta.find("width")->as_integer() == 1600 &&
              *meta.find("height")->as_integer() == 1000);
        check(*meta.find("source_width")->as_integer() == 3200 &&
              *meta.find("source_height")->as_integer() == 2000);
        check(!*meta.find("actionable")->as_boolean());
        check(*meta.find("desktop_per_image_x_numerator")->as_integer() == 3200);
        check(*meta.find("desktop_per_image_x_denominator")->as_integer() == 1600);
        check(run(R"({"source":"roi","source_id":"d1","x":0,"y":0,"width":3200,"height":2000})")
                  .value()
                  .failed);
        auto roi =
            run(R"({"source":"roi","source_id":"d1","x":1500,"y":1000,"width":300,"height":200})");
        check(roi && !roi.value().failed && *roi.value().result.find("width")->as_integer() == 300);
    }
    // Identical screenshots must remain readable despite content-addressed IDs.
    env->displays.front().geometry = {-120, -40, 128, 64};
    (void)tools.actions->resume();
    const auto capture_once = [&](detail::DesktopImageTurn &turn,
                                  const mira::OperationContext &operation) {
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {});
        const auto exposed = registry.exposed_tools().front();
        mira::ToolProposal proposal;
        proposal.tool_id = exposed.tool_id;
        proposal.tool_version = exposed.version;
        proposal.wire_name = exposed.wire_name;
        proposal.arguments = mira::parse_json(R"({"source":"display","source_id":"d1"})").value();
        proposal.arguments_digest = mira::canonical_json_digest(proposal.arguments);
        return registry.execute(proposal, operation);
    };
    mira::AgentLoopSpec valid_spec;
    valid_spec.task_id = ctx.task;
    valid_spec.session_id = ctx.session;
    valid_spec.task_epoch = ctx.task_epoch;
    {
        detail::DesktopImageTurn turn(source, tools, ctx, {});
        for (int i = 0; i < 4; ++i) {
            auto captured = capture_once(turn, ctx);
            check(captured && !captured.value().failed);
            auto inputs = turn.supply(valid_spec);
            check(inputs &&
                  static_cast<bool>(source->fetch(
                      std::get<mira::ImagePart>(inputs.value().front().content.back()).source)));
        }
        auto rejected = capture_once(turn, ctx);
        check(rejected && rejected.value().failed);
        check(turn.supply(valid_spec).value().empty());
    }
    { // Verify ordinary image expiry separately from failed replacement retirement.
        detail::DesktopImageTurn turn(source, tools, ctx, {});
        check(capture_once(turn, ctx) && turn.supply(valid_spec));
        auto age = executor.submit_delayed(5100, [] {});
        age.future.get();
        auto expired = turn.supply(valid_spec);
        check(expired && !expired.value().empty() &&
              std::holds_alternative<mira::TextPart>(expired.value().front().content.back()));
    }
    for (const auto &fault : {"cancel", "epoch", "geometry", "exception"}) {
        auto fresh = desktop();
        auto fake =
            std::static_pointer_cast<mirage::testing::FakeDesktopEnvironment>(fresh.environment);
        mirage::desktop::CancelToken stop;
        fake->capture_frame_edit = [&](auto &) {
            if (std::string_view{fault} == "cancel")
                stop.request_cancel();
            if (std::string_view{fault} == "epoch")
                fresh.actions->takeover();
            if (std::string_view{fault} == "geometry")
                fake->displays.front().geometry.x++;
            if (std::string_view{fault} == "exception")
                throw std::runtime_error("fixture capture failure");
        };
        detail::DesktopImageTurn turn(source, fresh, ctx, stop);
        const auto rejected = capture_once(turn, ctx);
        if (std::string_view{fault} == "cancel")
            check(!rejected && rejected.error().code == mira::ErrorCode::Cancelled);
        else
            check(rejected && rejected.value().failed);
        if (std::string_view{fault} != "cancel") {
            const auto supplied = turn.supply(valid_spec);
            check(supplied && supplied.value().empty());
        }
    }
    // Multi-block PNG and padded stride, decoded by the independent Python checker.
    mirage::desktop::ImageFrame frame;
    frame.width = 300;
    frame.height = 100;
    frame.stride = 1204;
    frame.pixels.resize(frame.stride * 100);
    for (std::size_t y = 0; y < 100; ++y)
        for (std::size_t x = 0; x < 300; ++x) {
            const auto offset = y * frame.stride + x * 4;
            frame.pixels[offset] = static_cast<std::uint8_t>(x % 256);
            frame.pixels[offset + 1] = static_cast<std::uint8_t>(y);
            frame.pixels[offset + 2] = 193;
        }
    const auto png = detail::encode_desktop_png(frame, {});
    check(static_cast<bool>(png));
    std::ofstream output("desktop-image-fixture.png", std::ios::binary);
    output.write(reinterpret_cast<const char *>(png.value().data()),
                 static_cast<std::streamsize>(png.value().size()));
    check(static_cast<bool>(output));
    frame.stride = 1;
    check(!detail::encode_desktop_png(frame, {}));
    mirage::desktop::CancelToken stopped;
    stopped.request_cancel();
    check(!detail::encode_desktop_png(frame, stopped));
}

int live() {
    // Opt-in external driver only; key arrives through the child environment.
    const auto get = [](const char *name) {
        const auto *value = std::getenv(name);
        return value ? std::string{value} : std::string{};
    };
    auto tools = desktop();
    auto env = std::static_pointer_cast<mirage::testing::FakeDesktopEnvironment>(tools.environment);
    env->displays = {{"d1", {0, 0, 512, 256}, true}};
    std::ifstream file(get("MIRAGE_IMAGE_RAW"), std::ios::binary);
    std::vector<std::uint8_t> pixels(512 * 256 * 4);
    file.read(reinterpret_cast<char *>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    check(file.gcount() == static_cast<std::streamsize>(pixels.size()));
    env->capture_frame_edit = [pixels = std::move(pixels)](auto &frame) { frame.pixels = pixels; };
    kairo::Executor executor;
    kairo::ExecutorConfig owner;
    owner.min_threads = owner.max_threads = 3;
    check(static_cast<bool>(executor.initialize(owner)));
    integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = get("MIRAGE_IMAGE_MODEL");
    config.endpoint_origin = get("MIRAGE_IMAGE_ORIGIN");
    config.api_prefix = "/anthropic/v1";
    config.dialect = "anthropic.messages.v1";
    config.credential_env = "MIRAGE_IMAGE_KEY";
    config.request_deadline = std::chrono::seconds{90};
    integration::ModelLayer model(executor, config, nullptr, tools);
    check(model.running());
    const auto ctx = context();
    const auto result =
        executor
            .submit_auto([&] {
                return model.complete_harness_turn("", get("MIRAGE_IMAGE_PROMPT"), ctx, "adaptive");
            })
            .get();
    model.shutdown();
    executor.shutdown(true);
    std::cout << mira::to_json_string(mira::JsonValue{mira::JsonValue::Object{
                     {"ok", result.ok},
                     {"cancelled", result.cancelled},
                     {"answer", result.reply_text},
                     {"error", result.error},
                     {"captures", static_cast<std::int64_t>(env->capture_calls)},
                     {"model_steps", std::int64_t{result.model_steps}},
                     {"tool_calls", std::int64_t{result.tool_calls}},
                     {"supplied_input_items", std::int64_t{result.supplied_input_items}},
                     {"supplied_input_dropped", std::int64_t{result.supplied_input_dropped}},
                     {"supply_degradations", std::int64_t{result.supply_degradations}}}})
              << '\n';
    return result.ok ? 0 : 1;
}
} // namespace

int main() {
    try {
        if (const auto *mode = std::getenv("MIRAGE_IMAGE_LIVE");
            mode && std::string_view{mode} == "1")
            return live();
        kairo::Executor executor;
        kairo::ExecutorConfig config;
        config.min_threads = config.max_threads = 3;
        check(static_cast<bool>(executor.initialize(config)));
        executor
            .submit_auto([] {
                for (const auto &mode :
                     {"normal", "ocr", "no_image", "foreign", "changed", "denied", "release",
                      "uncertain", "takeover", "stale", "cancel", "initial_denied", "epoch",
                      "invalid_crop", "post_failure", "wrong_ocr", "ocr_invalid", "unicode",
                      "pretimeout", "repeated_source"})
                    shared_contracts(mode);
                for (const auto &mode : {"reuse", "budget", "deny", "cancel", "takeover", "revoke",
                                         "throw", "foreign", "during_takeover", "first_failure"})
                    native_session(mode);
            })
            .get();
        for (const auto &mode : {"normal", "mutation", "cancel", "takeover", "model_throw"})
            native_loop(executor, mode);
        for (const auto &mode :
             {"normal", "text_only", "recovery", "denied", "failed", "throw", "cancel", "epoch",
              "takeover", "nan", "bounds", "regions", "text_budget", "output_budget",
              "escaped_budget", "candidate_prefix", "invalid_args", "model_throw"})
            native_ocr_loop(executor, mode);
        shared_loop(executor);
        contracts(executor);
        for (const auto &dialect :
             {"openai.responses.v1", "openai.chat-completions.v1", "anthropic.messages.v1"})
            layer(executor, dialect, true, true);
        layer(executor, "openai.responses.v1", false, true);
        layer(executor, "openai.responses.v1", true, false);
        layer(executor, "openai.responses.v1", true, true, true);
        layer(executor, "openai.responses.v1", true, true, false, true);
        mirage::runtime::persistence::LocalSettings settings;
        settings.model.emplace();
        settings.model->supports_images = true;
        const auto decoded = mirage::runtime::persistence::decode_settings(
            mirage::runtime::persistence::encode_settings(settings));
        check(decoded.ok && decoded.settings.model->supports_images);
        auto rejected_tools = desktop();
        auto rejected_env = std::static_pointer_cast<mirage::testing::FakeDesktopEnvironment>(
            rejected_tools.environment);
        integration::ModelProviderOverride rejected_provider(
            [](const auto &profile) { return std::make_shared<Script>(profile); });
        integration::ModelLayerConfig rejected_config;
        rejected_config.enabled = rejected_config.supports_images = true;
        rejected_config.model_selector = "fixture";
        integration::ModelLayer rejected_layer(executor, rejected_config, &rejected_provider,
                                               rejected_tools);
        executor.shutdown(true);
        bool rejected = false;
        try {
            auto future = executor.submit_auto(
                [&] { return rejected_layer.complete_harness_turn("", "capture", context()); });
            (void)future.get();
        } catch (const std::exception &) {
            rejected = true;
        }
        check(rejected);
        check(rejected_env->capture_calls == 0);
        rejected_layer.shutdown();
        std::cout << checks << " desktop image checks passed\n";
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
