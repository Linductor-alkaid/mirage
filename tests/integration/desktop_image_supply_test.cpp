#include "../../integration/mira/src/conversation_capture.hpp"
#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/provider_tool_name.hpp"
#include <array>
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

// Full-frame PNG encode exceeds the production 5 s acquisition budget under
// ThreadSanitizer; keep the production default elsewhere.
#if defined(__SANITIZE_THREAD__)
#define MIRAGE_TSAN_SLOW 1
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define MIRAGE_TSAN_SLOW 1
#endif
#endif
#ifndef MIRAGE_TSAN_SLOW
#define MIRAGE_TSAN_SLOW 0
#endif

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
// M7-17: every exported PNG fixture is verified by the independent Python
// decoder (tests/manual/desktop_png_decode_check.py). The companion .raw file
// carries exactly the RGB bytes this test constructed the frame from — never
// anything produced by the encoder under test.
struct PngFixture {
    std::string png, raw, expectation;
    int width = 0, height = 0;
};
std::vector<PngFixture> png_fixtures;
void export_png_fixture(const std::string &stem, const std::vector<std::byte> &png,
                        const std::vector<std::uint8_t> &expected_rgb, const int width,
                        const int height, const char *expectation) {
    std::ofstream png_file(stem + ".png", std::ios::binary);
    png_file.write(reinterpret_cast<const char *>(png.data()),
                   static_cast<std::streamsize>(png.size()));
    check(static_cast<bool>(png_file));
    std::ofstream raw_file(stem + ".raw", std::ios::binary);
    raw_file.write(reinterpret_cast<const char *>(expected_rgb.data()),
                   static_cast<std::streamsize>(expected_rgb.size()));
    check(static_cast<bool>(raw_file));
    png_fixtures.push_back({stem + ".png", stem + ".raw", expectation, width, height});
}
// A spec-failing business defect is reported after the full run so one defect
// cannot hide the outcome of every other suite in this binary.
std::string deferred_failure;
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
    // M7-16 / DEC-083: `pixels_changed` paints a position-dependent structural
    // pattern (a replaced subject whose frame fingerprint and block diffs both
    // move), while `drift` only shifts brightness uniformly (the subject stays
    // where it was, so the layered guard proceeds).
    int drift = 0;
    std::vector<std::uint8_t> shared_pixels() const {
        std::vector<std::uint8_t> pixels(32, std::uint8_t{0});
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x) {
                const auto value = static_cast<std::uint8_t>(
                    pixels_changed ? (x * 37 + y * 101) & 0xFF : 1 + drift);
                const auto offset =
                    static_cast<std::size_t>(y) * 16 + static_cast<std::size_t>(x) * 4;
                pixels[offset] = pixels[offset + 1] = pixels[offset + 2] = pixels[offset + 3] =
                    value;
            }
        return pixels;
    }
    std::function<void()> during;
    // M7-18: optional window-provider facade (the AT-SPI fallback stand-in)
    // and a configurable session frame, so region sources have room to crop.
    std::vector<mirage::desktop::WindowInfo> native_windows;
    bool provide_windows = false;
    int frame_width = 4, frame_height = 2;
    std::function<std::vector<std::uint8_t>()> session_pixels;
    // Screen coordinates the last dispatched session input carried.
    double last_action_x = -1, last_action_y = -1;
    std::uint32_t last_click_count = 0;
    mirage::desktop::SharedInputAction last_input;
    class NativeWindow final : public mirage::desktop::WindowProvider {
      public:
        explicit NativeWindow(NativeEnvironment &env) : env_(env) {}
        mirage::desktop::WindowListOutcome
        list_windows(const mirage::desktop::WindowListLimits &,
                     const mirage::desktop::CancelToken &) override {
            mirage::desktop::WindowListOutcome outcome;
            outcome.ok = true;
            outcome.windows = env_.native_windows;
            return outcome;
        }
        mirage::desktop::WindowQueryOutcome
        front_window(const mirage::desktop::CancelToken &) override {
            mirage::desktop::WindowQueryOutcome outcome;
            outcome.ok = true;
            for (const auto &window : env_.native_windows)
                if (window.focused) {
                    outcome.found = true;
                    outcome.window = window;
                    break;
                }
            return outcome;
        }
        mirage::desktop::WindowActionOutcome
        activate(const std::string &window_id, const mirage::desktop::CancelToken &) override {
            mirage::desktop::WindowActionOutcome outcome;
            bool hit = false;
            for (auto &window : env_.native_windows) {
                hit |= window.id == window_id;
                window.focused = window.id == window_id;
            }
            if (!hit) {
                outcome.error = {"not_found", "unknown window id"};
                return outcome;
            }
            outcome.ok = true;
            return outcome;
        }
        NativeEnvironment &env_;
    };
    NativeWindow window_provider{*this};
    mirage::desktop::WindowProvider *window() override {
        return provide_windows ? &window_provider : nullptr;
    }
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
        mirage::desktop::InputOutcome input(const mirage::desktop::SharedInputAction &action,
                                            const mirage::desktop::InputLimits &,
                                            const mirage::desktop::CancelToken &cancel) override {
            ++env.inputs;
            env.last_action_x = action.x;
            env.last_action_y = action.y;
            env.last_click_count = action.click_count;
            env.last_input = action;
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
            const auto width = env.frame_width, height = env.frame_height;
            auto pixels =
                env.session_pixels ? env.session_pixels()
                : env.interactive
                    ? env.shared_pixels()
                    : std::vector<std::uint8_t>(std::size_t(width) * std::size_t(height) * 4,
                                                static_cast<std::uint8_t>(env.captures));
            return {true,
                    false,
                    {mirage::desktop::ImageFormat::Bgra8, width, height, std::size_t(width) * 4,
                     std::move(pixels),
                     static_cast<std::uint64_t>(env.repeated_source_after_input && env.inputs
                                                    ? env.captures - 1
                                                    : env.captures)},
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
        mirage::conversation::Timings measured;
        turn.set_timings(&measured);
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
        // The lease variant holds an action lease from before the first
        // capture: the coordinator epoch (2) then equals the fake guard
        // frame's source_sequence (2), so the M7-19 publish path's epoch
        // staleness check can be observed in isolation.
        mirage::desktop::ActionCoordinator::Lease held_lease;
        if (mode == "changed_refresh_lease") {
            held_lease = tools.actions->try_begin({});
            check(bool(held_lease));
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
        if (mode == "changed" || mode == "changed_refresh" || mode == "changed_refresh_lease")
            env->pixels_changed = true;
        if (mode == "uniform_drift")
            env->drift = 30;
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
        if (mode == "double")
            args.set("click_count", 2);
        if (mode == "triple")
            args.set("click_count", 3);
        if (mode == "repeat")
            args.set("click_count", 5);
        if (mode == "click_limit")
            args.set("click_count", static_cast<std::int64_t>(mirage::desktop::max_click_count));
        if (mode == "invalid_click_count")
            args.set("click_count",
                     static_cast<std::int64_t>(mirage::desktop::max_click_count) + 1);
        if (mode == "zero_click_count")
            args.set("click_count", 0);
        if (mode == "negative_click_count")
            args.set("click_count", -1);
        if (mode == "fractional_click_count")
            args.set("click_count", 1.5);
        if (mode == "string_click_count")
            args.set("click_count", "3");
        if (mode == "unicode")
            args.set("text", "fixture中文");
        const bool drag = mode.starts_with("drag");
        if (mode == "right" || mode == "middle")
            args.set("button", mode);
        if (drag) {
            args.set("destination_x", 2);
            args.set("destination_y", 1);
            args.set("destination_width", 1);
            args.set("destination_height", 1);
            args.set("button", "right");
        }
        if (mode == "drag_invalid")
            args.set("destination_x", 99);
        if (mode == "invalid_button")
            args.set("button", "bad");
        if (mode == "button_type")
            args.set("button", 3);
        if (mode == "wheel")
            args.set("horizontal_steps", -16);
        if (mode == "wheel_budget") {
            args.set("horizontal_steps", 16);
            args.set("vertical_steps", 1);
        }
        auto result = invoke(mode == "unicode"           ? "desktop.session.type_text"
                             : drag                      ? "desktop.session.drag"
                             : mode.starts_with("wheel") ? "desktop.session.scroll"
                                                         : "desktop.session.click",
                             args, action_ctx);
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
        if (mode == "changed_refresh" || mode == "changed_refresh_lease") {
            // M7-19: the structural rejection publishes the guard's fresh
            // frame and its receipt carries the new image_scope, so the model
            // retries without another capture round trip. The lease variant
            // holds an action lease taken before the click: the coordinator
            // epoch (2 after try_begin) then coincides with the guard frame's
            // fake source_sequence (2), so the swapped finish_native_publish
            // arguments pass the epoch staleness check and the publish path
            // itself can be observed.
            if (mode == "changed_refresh_lease")
                std::cerr << "changed_refresh_lease receipt: "
                          << (result ? result.value().safe_error_summary : std::string("none"))
                          << "\n";
            check(result && result.value().failed);
            const auto &summary = result.value().safe_error_summary;
            check(summary.find("target_pixels_changed") != std::string::npos);
            check(summary.size() <= 512);
            const auto receipt = mira::parse_json(summary);
            // The model reads refreshed_image_scope from the embedded error
            // JSON inside the canonical receipt's message field.
            std::string refreshed;
            if (receipt && receipt.value().find("message") &&
                receipt.value().find("message")->as_string()) {
                const auto *embedded = receipt.value().find("message")->as_string();
                if (const auto inner = mira::parse_json(*embedded);
                    inner && inner.value().find("refreshed_image_scope") &&
                    inner.value().find("refreshed_image_scope")->as_string())
                    refreshed = *inner.value().find("refreshed_image_scope")->as_string();
            }
            if (mode == "changed_refresh_lease") {
                // Diagnostic: hold an action lease from before the capture so
                // the coordinator epoch coincides with the guard frame's fake
                // source_sequence; report whether the receipt hands the model
                // a readable scope in that case too.
                std::cerr << "changed_refresh_lease: refreshed scope readable="
                          << !refreshed.empty() << "; summary: " << summary << "\n";
                check(env->captures == 2 && env->inputs == 0);
                return;
            }
            if (refreshed.empty()) {
                deferred_failure += "\nM7-19: target_pixels_changed receipt carries no readable "
                                    "refreshed_image_scope for the model; receipt: " +
                                    summary;
                std::cerr << deferred_failure << '\n';
                check(env->captures == 2 && env->inputs == 0);
                return;
            }
            check(refreshed != std::string(*meta.find("image_scope")->as_string()));
            check(env->captures == 2 && env->inputs == 0);
            // The published observation is supplied without any recapture.
            mira::AgentLoopSpec refresh_spec;
            refresh_spec.task_id = ctx.task;
            refresh_spec.session_id = ctx.session;
            refresh_spec.task_epoch = ctx.task_epoch;
            auto refresh_supply = turn.supply(refresh_spec);
            check(refresh_supply && refresh_supply.value().size() == 1 && env->captures == 2);
            mira::ModelRequest refresh_request;
            refresh_request.task_id = ctx.task;
            refresh_request.task_epoch = ctx.task_epoch;
            refresh_request.input = refresh_supply.value();
            check(bool(turn.validate_request(refresh_request, ctx)));
            args.set("image_scope", refreshed);
            const auto retry = invoke("desktop.session.click", args, action_ctx);
            if (retry && retry.value().failed)
                std::cerr << "changed_refresh retry: " << retry.value().safe_error_summary << "\n";
            check(retry && !retry.value().failed);
            check(env->inputs == 1);
            // Only the retry's own guard frame and its postcondition
            // observation ran; no separate recapture call happened.
            check(env->captures == 4 && env->created == 1);
            check(retry.value().result.find("verified")->as_boolean() == true);
            check(verification.settle());
            return;
        }
        // DEC-083: a uniform brightness drift dispatches exactly like `normal`
        // — the guard passes it and the input succeeds, so the post-action
        // verification settles with verified=true; only the structural
        // replacement in `changed` still rejects below.
        const bool success = mode == "normal" || mode == "ocr" || mode == "uniform_drift" ||
                             mode == "double" || mode == "triple" || mode == "repeat" ||
                             mode == "click_limit" || mode == "right" || mode == "middle" ||
                             mode == "drag" || mode == "wheel";
        if (mode == "right" || mode == "middle")
            check(env->last_input.value == mode);
        if (mode == "drag")
            check(env->last_input.kind == mirage::desktop::SharedInputAction::Kind::Drag &&
                  env->last_input.value == "right" && env->last_input.destination_x == 2 &&
                  env->last_input.destination_y == 1);
        if (mode == "wheel")
            check(env->last_input.horizontal_steps == -16 && env->last_input.vertical_steps == 0);
        if (mode == "drag_invalid" || mode == "invalid_button" || mode == "button_type" ||
            mode == "wheel_budget")
            check(env->captures == 1 && env->inputs == 0);
        if (mode == "double")
            check(env->last_click_count == 2);
        if (mode == "triple")
            check(env->last_click_count == 3);
        if (mode == "repeat")
            check(env->last_click_count == 5);
        if (mode == "click_limit")
            check(env->last_click_count == mirage::desktop::max_click_count);
        if (mode.ends_with("click_count"))
            check(env->captures == 1 && env->inputs == 0);
        const bool dispatched = success || mode == "post_failure" || mode == "wrong_ocr" ||
                                mode == "ocr_invalid" || mode == "repeated_source";
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
            const auto *proof = result.value().result.find("verification");
            check(proof && proof->find("evidence_scope") &&
                  *proof->find("evidence_scope")->as_string() ==
                      (mode == "ocr" ? "text_presence" : "fresh_frame"));
            const auto before = measured.capture.calls;
            check(before == 3 && measured.ocr.calls == (mode == "ocr" ? 1u : 0u));
            check(verification.settle());
            check(measured.capture.calls == before + 1 &&
                  measured.ocr.calls == (mode == "ocr" ? 2u : 0u));
        }
        if (mode == "release")
            check(!tools.actions->input_released() && tools.actions->authority_epoch() == 0);
        if (mode == "uncertain" || mode == "release")
            check(!verification.settle());
    }
    check(env->created == env->closed);
}

// M7-17: the compression content matrix. Each case is encoded here — with the
// expected RGB bytes rebuilt from the same construction loop — and exported
// for the independent Python decoder; size expectations separate compressible
// UI content from the incompressible stored fallback.
void png_content_matrix() {
    constexpr int width = 300, height = 100;
    const std::size_t stride = std::size_t(width) * 4 + 7; // padded rows everywhere
    const auto encode_case = [&](const std::string &name, const char *expectation,
                                 const auto &bgra_at) {
        mirage::desktop::ImageFrame frame;
        frame.width = width;
        frame.height = height;
        frame.stride = stride;
        frame.pixels.resize(stride * std::size_t(height));
        std::vector<std::uint8_t> expected_rgb;
        expected_rgb.reserve(std::size_t(width) * std::size_t(height) * 3);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const auto bgra = bgra_at(x, y);
                const auto offset = std::size_t(y) * stride + std::size_t(x) * 4;
                for (std::size_t channel = 0; channel < 4; ++channel)
                    frame.pixels[offset + channel] = bgra[channel];
                // PNG stores RGB; the frame is BGRA.
                expected_rgb.push_back(bgra[2]);
                expected_rgb.push_back(bgra[1]);
                expected_rgb.push_back(bgra[0]);
            }
        const auto png = detail::encode_desktop_png(frame, {});
        check(static_cast<bool>(png));
        const auto raw_bytes = std::uint64_t(expected_rgb.size());
        if (std::string_view{expectation} == "stored_bound") {
            const auto scan_bytes = (std::uint64_t{width} * 3 + 1) * height;
            check(png.value().size() == 57 + 6 + scan_bytes + 5 * ((scan_bytes + 65534) / 65535));
        } else {
            check(std::uint64_t(png.value().size()) <= raw_bytes);
            if (std::string_view{expectation} == "ui_tiny")
                check(std::uint64_t(png.value().size()) * 8 <= raw_bytes);
        }
        export_png_fixture(name, png.value(), expected_rgb, width, height, expectation);
    };
    encode_case("desktop-png-zeros-fixture", "ui_tiny",
                [](int, int) { return std::array<std::uint8_t, 4>{0, 0, 0, 255}; });
    encode_case("desktop-png-gradient-fixture", "raw_or_smaller", [](int x, int y) {
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(x % 256),
                                           static_cast<std::uint8_t>(y), 193, 255};
    });
    encode_case("desktop-png-checker-fixture", "ui_tiny", [](int x, int y) {
        const bool light = (x / 32 + y / 32) % 2 == 0;
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(light ? 200 : 10),
                                           static_cast<std::uint8_t>(light ? 220 : 20),
                                           static_cast<std::uint8_t>(light ? 240 : 30), 255};
    });
    encode_case("desktop-png-stripes-fixture", "ui_tiny", [](int x, int y) {
        // Text-like layout: glyph runs inside line bands over a page
        // background; the 16-pixel horizontal period is what LZ77 eats.
        const bool ink = (y % 10) < 6 && ((x * 37 + y * 151) % 16) < 10;
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(ink ? 24 : 235),
                                           static_cast<std::uint8_t>(ink ? 28 : 238),
                                           static_cast<std::uint8_t>(ink ? 32 : 242), 255};
    });
    unsigned state = 0x9E3779B9u;
    encode_case("desktop-png-noise-fixture", "stored_bound", [&state](int, int) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(state >> 24),
                                           static_cast<std::uint8_t>(state >> 16),
                                           static_cast<std::uint8_t>(state >> 8), 255};
    });
}

// M7-18: Portal-side region sources. The shared fresh frame is cropped
// locally for source=roi / source=window with exact origin/scale metadata,
// one frame budget per capture and fail-closed bounds. The C5 positive click
// (ROI-supplied coordinates dispatching through the stability guard) is
// deferred: a failure is reported after the full run so the remaining
// regression stays observable in the same binary run.
void native_region_sources() {
    auto env = std::make_shared<NativeEnvironment>();
    env->interactive = true;
    env->frame_width = 8;
    env->frame_height = 4;
    // Deterministic BGRA pattern; channel order mirrors ImageFrame (B,G,R,A).
    const auto pixel = [](int x, int y) {
        return std::array<std::uint8_t, 4>{
            static_cast<std::uint8_t>(x * 13 + 3), static_cast<std::uint8_t>(y * 11 + 5),
            static_cast<std::uint8_t>((x * 7 + y * 3) & 0xFF), std::uint8_t{255}};
    };
    env->session_pixels = [&] {
        std::vector<std::uint8_t> pixels(std::size_t(env->frame_width) *
                                         std::size_t(env->frame_height) * 4);
        for (int y = 0; y < env->frame_height; ++y)
            for (int x = 0; x < env->frame_width; ++x)
                for (std::size_t channel = 0; channel < 4; ++channel)
                    pixels[(std::size_t(y) * std::size_t(env->frame_width) + std::size_t(x)) * 4 +
                           channel] = pixel(x, y)[channel];
        return pixels;
    };
    auto tools = desktop();
    tools.environment = env;
    tools.gate = [](const auto &, const auto &, const auto &cancel) { return !cancel(); };
    auto source = std::make_shared<detail::DesktopImageSource>();
    auto ctx = context();
    mirage::desktop::CancelToken stop;
    // C6 first on its own turn: the observation-only schema also lists all
    // three sources. The turn is destroyed before the interactive one so the
    // shared image store stays bound to the live turn.
    const auto source_enum = [](const JsonValue &schema) {
        const auto *properties = schema.find("properties");
        check(properties && properties->as_object());
        const auto source_field =
            std::find_if(properties->as_object()->begin(), properties->as_object()->end(),
                         [](const auto &field) { return field.first == "source"; });
        check(source_field != properties->as_object()->end());
        const auto *values = source_field->second.find("enum");
        check(values && values->as_array() && values->as_array()->size() == 3);
        for (const char *name : {"desktop", "roi", "window"})
            check(std::any_of(values->as_array()->begin(), values->as_array()->end(),
                              [&](const auto &value) {
                                  return value.as_string() && *value.as_string() == name;
                              }));
    };
    {
        detail::DesktopImageTurn observation(source, tools, ctx, stop);
        mira::BuiltinToolRegistry observation_registry;
        observation.register_tool(observation_registry, {}, false);
        const auto observation_exposed = observation_registry.exposed_tools();
        const auto capture_tool = std::find_if(
            observation_exposed.begin(), observation_exposed.end(),
            [](const auto &tool) { return tool.wire_name == "desktop.screen.capture"; });
        check(capture_tool != observation_exposed.end());
        source_enum(capture_tool->parameters_schema.root);
    }
    // Session retirement follows the sharing path, not the tool result: only
    // authorization, stream, and input-transport failures poison the task's
    // session (DEC-074). Argument validation and frame-budget rejections leave
    // an established session reusable, so negatives no longer need isolation.
    const auto run_capture = [](mira::BuiltinToolRegistry &registry,
                                const mira::OperationContext &owner, const JsonValue &args) {
        const auto exposed = registry.exposed_tools();
        const auto tool = std::find_if(exposed.begin(), exposed.end(), [](const auto &spec) {
            return spec.wire_name == "desktop.screen.capture";
        });
        check(tool != exposed.end());
        mira::ToolProposal call;
        call.tool_id = tool->tool_id;
        call.tool_version = tool->version;
        call.wire_name = tool->wire_name;
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "native-region";
        call.arguments = args;
        call.arguments_digest = mira::canonical_json_digest(args);
        return registry.execute(call, owner);
    };
    const auto run_capture_json = [&](mira::BuiltinToolRegistry &registry,
                                      const mira::OperationContext &owner, const char *json) {
        return run_capture(registry, owner, mira::parse_json(json).value());
    };
    const auto run_session_tool = [](mira::BuiltinToolRegistry &registry,
                                     const mira::OperationContext &owner, const char *name,
                                     const JsonValue &args) {
        const auto exposed = registry.exposed_tools();
        const auto tool = std::find_if(exposed.begin(), exposed.end(),
                                       [&](const auto &spec) { return spec.wire_name == name; });
        check(tool != exposed.end());
        mira::ToolProposal call;
        call.tool_id = tool->tool_id;
        call.tool_version = tool->version;
        call.wire_name = name;
        call.has_side_effects = true;
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "region-click";
        call.arguments = args;
        call.arguments_digest = mira::canonical_json_digest(args);
        return registry.execute(call, owner);
    };
    {
        // C1/C2/C3 positives: one healthy session.
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto exposed = registry.exposed_tools();
        const auto capture_tool =
            std::find_if(exposed.begin(), exposed.end(), [](const auto &tool) {
                return tool.wire_name == "desktop.screen.capture";
            });
        check(capture_tool != exposed.end());
        check(capture_tool->version == mira::SemanticVersion{1, 5, 0});
        source_enum(capture_tool->parameters_schema.root);
        // C1: a valid ROI crops the fresh shared frame; exactly one frame
        // budget, one session, no additional authorization.
        const auto roi =
            run_capture_json(registry, ctx, R"({"source":"roi","x":2,"y":1,"width":4,"height":2})");
        if (!roi || roi.value().failed)
            throw std::runtime_error(roi ? roi.value().safe_error_summary
                                         : roi.error().safe_message);
        const auto &meta = roi.value().result;
        check(*meta.find("source")->as_string() == std::string("roi"));
        check(*meta.find("width")->as_integer() == 4 && *meta.find("height")->as_integer() == 2);
        check(*meta.find("source_width")->as_integer() == 4 &&
              *meta.find("source_height")->as_integer() == 2);
        check(*meta.find("origin_x")->as_integer() == 2 &&
              *meta.find("origin_y")->as_integer() == 1);
        check(*meta.find("desktop_per_image_x_numerator")->as_integer() == 4 &&
              *meta.find("desktop_per_image_x_denominator")->as_integer() == 4 &&
              *meta.find("desktop_per_image_y_numerator")->as_integer() == 2 &&
              *meta.find("desktop_per_image_y_denominator")->as_integer() == 2);
        check(*meta.find("scale_numerator")->as_integer() == 1 &&
              *meta.find("scale_denominator")->as_integer() == 1);
        check(env->created == 1 && env->captures == 1);
        {
            // The published image is the crop, pixel for pixel (verified
            // afterwards by the independent decoder against the raw
            // companion).
            mira::AgentLoopSpec spec;
            spec.task_id = ctx.task;
            spec.session_id = ctx.session;
            spec.task_epoch = ctx.task_epoch;
            const auto supplied = turn.supply(spec);
            check(supplied && supplied.value().size() == 1);
            const auto &image = std::get<mira::ImagePart>(supplied.value().front().content.back());
            const auto bytes = source->fetch(image.source);
            check(bytes && bytes.value().size() == image.source.byte_size);
            std::vector<std::uint8_t> expected_rgb;
            for (int y = 1; y < 3; ++y)
                for (int x = 2; x < 6; ++x)
                    for (int channel = 2; channel >= 0; --channel)
                        expected_rgb.push_back(pixel(x, y)[std::size_t(channel)]);
            export_png_fixture("desktop-image-native-roi-fixture", bytes.value(), expected_rgb, 4,
                               2, "pixels_only");
            mira::ModelRequest request;
            request.task_id = ctx.task;
            request.task_epoch = ctx.task_epoch;
            request.input = supplied.value();
            check(static_cast<bool>(turn.validate_request(request, ctx)));
        }
        // C2 (partial): an overhanging ROI clips to the shared monitor.
        const auto partial =
            run_capture_json(registry, ctx, R"({"source":"roi","x":6,"y":2,"width":8,"height":4})");
        check(partial && !partial.value().failed);
        check(*partial.value().result.find("width")->as_integer() == 2 &&
              *partial.value().result.find("height")->as_integer() == 2);
        check(*partial.value().result.find("origin_x")->as_integer() == 6 &&
              *partial.value().result.find("origin_y")->as_integer() == 2);
        // C3: window geometry resolves through the window provider and
        // intersects the shared frame.
        env->provide_windows = true;
        env->native_windows = {{"win1", "Fixture", {1, 0, 4, 2}, true},
                               {"corner", "Overhang", {6, 2, 8, 4}, false}};
        const auto window =
            run_capture_json(registry, ctx, R"({"source":"window","source_id":"win1"})");
        if (!window || window.value().failed)
            std::cerr << "C3 window capture failed: "
                      << (window ? window.value().safe_error_summary : window.error().safe_message)
                      << "\n";
        check(window && !window.value().failed);
        check(*window.value().result.find("source")->as_string() == std::string("window"));
        check(*window.value().result.find("width")->as_integer() == 4 &&
              *window.value().result.find("height")->as_integer() == 2);
        check(*window.value().result.find("origin_x")->as_integer() == 1 &&
              *window.value().result.find("origin_y")->as_integer() == 0);
        const auto overhang =
            run_capture_json(registry, ctx, R"({"source":"window","source_id":"corner"})");
        check(overhang && !overhang.value().failed);
        check(*overhang.value().result.find("width")->as_integer() == 2 &&
              *overhang.value().result.find("height")->as_integer() == 2 &&
              *overhang.value().result.find("origin_x")->as_integer() == 6 &&
              *overhang.value().result.find("origin_y")->as_integer() == 2);
    }
    {
        // C2 (fail-closed): a fully-outside ROI is rejected.
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto outside = run_capture_json(
            registry, ctx, R"({"source":"roi","x":100,"y":100,"width":4,"height":4})");
        check(outside && outside.value().failed &&
              outside.value().safe_error_summary.find("outside the shared monitor") !=
                  std::string::npos);
    }
    {
        // C3 fail-closed: unknown window id, then a null window provider.
        env->provide_windows = true;
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto unknown_window =
            run_capture_json(registry, ctx, R"({"source":"window","source_id":"missing"})");
        check(unknown_window && unknown_window.value().failed &&
              unknown_window.value().safe_error_summary.find("no such window") !=
                  std::string::npos);
    }
    {
        env->provide_windows = false;
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto unsupported =
            run_capture_json(registry, ctx, R"({"source":"window","source_id":"win1"})");
        check(unsupported && unsupported.value().failed &&
              unsupported.value().safe_error_summary.find("window enumeration is unavailable") !=
                  std::string::npos);
    }
    {
        // C4: interaction only combines with source=desktop.
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        check(run_capture_json(
                  registry, ctx,
                  R"({"source":"roi","x":0,"y":0,"width":2,"height":2,"interaction":true})")
                  .value()
                  .failed);
    }
    {
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        check(run_capture_json(registry, ctx,
                               R"({"source":"window","source_id":"win1","interaction":true})")
                  .value()
                  .failed);
    }
    { // C5: click against an ROI supply.
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        // After an ROI supply the model clicks with ROI-image coordinates;
        // the guard must map them through origin/scale into shared pixels.
        // The interactive session is established first (interaction is a
        // source=desktop-only flag).
        const auto interactive =
            run_capture_json(registry, ctx, R"({"source":"desktop","interaction":true})");
        check(interactive && !interactive.value().failed);
        const auto roi_for_click =
            run_capture_json(registry, ctx, R"({"source":"roi","x":2,"y":1,"width":4,"height":2})");
        if (!roi_for_click || roi_for_click.value().failed)
            throw std::runtime_error(roi_for_click ? roi_for_click.value().safe_error_summary
                                                   : roi_for_click.error().safe_message);
        const auto &click_meta = roi_for_click.value().result;
        check(click_meta.find("input_actionable") &&
              click_meta.find("input_actionable")->as_boolean() == true);
        mira::AgentLoopSpec spec;
        spec.task_id = ctx.task;
        spec.session_id = ctx.session;
        spec.task_epoch = ctx.task_epoch;
        const auto click_supply = turn.supply(spec);
        check(click_supply && click_supply.value().size() == 1);
        mira::ModelRequest click_request;
        click_request.task_id = ctx.task;
        click_request.task_epoch = ctx.task_epoch;
        click_request.input = click_supply.value();
        check(static_cast<bool>(turn.validate_request(click_request, ctx)));
        // Negative: coordinates outside the supplied ROI image are rejected
        // before any provider input.
        const auto outside_click =
            run_session_tool(registry, ctx, "desktop.session.click",
                             JsonValue{JsonValue::Object{
                                 {"image_scope", *click_meta.find("image_scope")},
                                 {"environment_epoch", *click_meta.find("environment_epoch")},
                                 {"x", 3},
                                 {"y", 1},
                                 {"width", 2},
                                 {"height", 1},
                                 {"verify_kind", "shared_frame_observed"}}});
        check(outside_click && outside_click.value().failed &&
              outside_click.value().safe_error_summary.find("stale_or_invalid_target") !=
                  std::string::npos);
        check(env->inputs == 0);
        // Positive: an in-image target maps into the cropped area and
        // dispatches.
        const auto captures_before_click = env->captures;
        std::string receipt = "no receipt";
        try {
            const auto inside_click =
                run_session_tool(registry, ctx, "desktop.session.click",
                                 JsonValue{JsonValue::Object{
                                     {"image_scope", *click_meta.find("image_scope")},
                                     {"environment_epoch", *click_meta.find("environment_epoch")},
                                     {"x", 0},
                                     {"y", 0},
                                     {"width", 2},
                                     {"height", 1},
                                     {"verify_kind", "shared_frame_observed"}}});
            receipt = inside_click && inside_click.value().failed
                          ? inside_click.value().safe_error_summary
                          : std::string("dispatched");
            check(inside_click && !inside_click.value().failed);
            check(env->inputs == 1);
            // The dispatched screen coordinates land inside the ROI's monitor
            // area (crop origin {2,1}, extents {4,2}).
            check(env->last_action_x >= 2.0 && env->last_action_x <= 6.0 &&
                  env->last_action_y >= 1.0 && env->last_action_y <= 3.0);
            // Only the click's own guard frame; no extra recapture.
            check(env->captures == captures_before_click + 1);
        } catch (const std::exception &failure) {
            deferred_failure += std::string("\nM7-18 C5 ROI-supplied session click failed: ") +
                                failure.what() + " | receipt: " + receipt;
            std::cerr << deferred_failure << '\n';
        }
    }
}

// 2026-10-18 incident regression (DEC-074 scope): the capture wrapper used to
// retire the task's sharing session on ANY error result, so a pure argument
// validation mistake destroyed a healthy authorized session and every later
// capture/OCR returned "native sharing ended or failed". Session liveness now
// belongs to the failure sites: only real sharing-path failures poison, while
// argument, budget, and staleness rejections leave the session reusable.
void native_session_poison_discipline() {
    auto env = std::make_shared<NativeEnvironment>();
    env->interactive = true;
    env->frame_width = 8;
    env->frame_height = 4;
    env->session_pixels = [&] {
        std::vector<std::uint8_t> pixels(std::size_t(env->frame_width) *
                                         std::size_t(env->frame_height) * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            pixels[i] = pixels[i + 1] = pixels[i + 2] = pixels[i + 3] = std::uint8_t{7};
        return pixels;
    };
    auto tools = desktop();
    tools.environment = env;
    tools.gate = [](const auto &, const auto &, const auto &cancel) { return !cancel(); };
    auto source = std::make_shared<detail::DesktopImageSource>();
    auto ctx = context();
    mirage::desktop::CancelToken stop;
    const auto run_capture = [](mira::BuiltinToolRegistry &registry,
                                const mira::OperationContext &owner, const char *json) {
        const auto exposed = registry.exposed_tools();
        const auto tool = std::find_if(exposed.begin(), exposed.end(), [](const auto &spec) {
            return spec.wire_name == "desktop.screen.capture";
        });
        check(tool != exposed.end());
        mira::ToolProposal call;
        call.tool_id = tool->tool_id;
        call.tool_version = tool->version;
        call.wire_name = tool->wire_name;
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "poison-discipline";
        call.arguments = mira::parse_json(json).value();
        call.arguments_digest = mira::canonical_json_digest(call.arguments);
        return registry.execute(call, owner);
    };
    {
        // (a) Core regression: the incident's exact argument mistake
        // (interaction=true on source=roi) must not retire the established
        // session, and a correct capture must succeed right after. Under the
        // pre-fix wrapper this rejection reset the session, so `recovered`
        // answered "native sharing ended or failed" and `closed` jumped to 1.
        const auto created_base = env->created, closed_base = env->closed,
                   captures_base = env->captures;
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto established =
            run_capture(registry, ctx, R"({"source":"desktop","interaction":true})");
        check(established && !established.value().failed);
        check(env->created == created_base + 1 && env->closed == closed_base);
        const auto malformed =
            run_capture(registry, ctx,
                        R"({"source":"roi","x":0,"y":0,"width":2,"height":1,"interaction":true})");
        check(malformed && malformed.value().failed);
        check(malformed.value().safe_error_summary.find(
                  "interaction=true is only valid with source=desktop") != std::string::npos);
        // The healthy session survived the argument rejection: no provider
        // session was created or closed, and no platform capture ran.
        check(env->created == created_base + 1 && env->closed == closed_base &&
              env->captures == captures_base + 1);
        const auto recovered =
            run_capture(registry, ctx, R"({"source":"roi","x":2,"y":1,"width":4,"height":2})");
        check(recovered && !recovered.value().failed);
        check(*recovered.value().result.find("source")->as_string() == std::string("roi"));
        check(*recovered.value().result.find("width")->as_integer() == 4 &&
              *recovered.value().result.find("height")->as_integer() == 2);
        check(env->created == created_base + 1 && env->closed == closed_base);
        // (b) A fail-closed ROI bounds rejection (raised after the fresh frame
        // was captured) likewise leaves the session reusable.
        const auto outside =
            run_capture(registry, ctx, R"({"source":"roi","x":100,"y":100,"width":4,"height":4})");
        check(outside && outside.value().failed &&
              outside.value().safe_error_summary.find("outside the shared monitor") !=
                  std::string::npos);
        check(env->created == created_base + 1 && env->closed == closed_base);
        const auto after_bounds =
            run_capture(registry, ctx, R"({"source":"roi","x":0,"y":0,"width":2,"height":1})");
        check(after_bounds && !after_bounds.value().failed);
        check(env->created == created_base + 1 && env->closed == closed_base);
        // Another malformed shape (missing ROI field) still does not poison.
        const auto missing =
            run_capture(registry, ctx, R"({"source":"roi","x":0,"y":0,"width":2})");
        check(missing && missing.value().failed &&
              missing.value().safe_error_summary.find("source=roi requires only") !=
                  std::string::npos);
        const auto still_alive =
            run_capture(registry, ctx, R"({"source":"roi","x":2,"y":1,"width":4,"height":2})");
        check(still_alive && !still_alive.value().failed);
        check(env->created == created_base + 1 && env->closed == closed_base);
    }
    {
        // (c) Preservation: a platform capture failure still poisons the
        // session fail-closed; later captures answer with the canonical
        // no-reauthorization receipt and never touch the provider again.
        const auto created_base = env->created, closed_base = env->closed,
                   captures_base = env->captures;
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        env->fail = true;
        const auto failed =
            run_capture(registry, ctx, R"({"source":"desktop","interaction":true})");
        check(failed && failed.value().failed &&
              failed.value().safe_error_summary.find("fixture_failed") != std::string::npos);
        check(env->created == created_base + 1 && env->closed == closed_base + 1);
        env->fail = false;
        const auto blocked =
            run_capture(registry, ctx, R"({"source":"desktop","interaction":true})");
        check(blocked && blocked.value().failed &&
              blocked.value().safe_error_summary.find("native sharing ended or failed") !=
                  std::string::npos);
        check(env->created == created_base + 1 && env->closed == closed_base + 1 &&
              env->captures == captures_base + 1);
    }
    {
        // (d) Wrapper path: a pre-publication epoch race (the environment
        // epoch changes while the frame is captured) returns Cancelled and
        // must NOT poison; the next capture succeeds on the same session.
        const auto created_base = env->created, closed_base = env->closed,
                   captures_base = env->captures;
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto first = run_capture(registry, ctx, R"({"source":"desktop","interaction":true})");
        check(first && !first.value().failed);
        mirage::desktop::ActionCoordinator::Lease lease;
        env->during = [&] { lease = tools.actions->try_begin({}); };
        const auto raced = run_capture(registry, ctx, R"({"source":"desktop","interaction":true})");
        env->during = {};
        check(bool(lease)); // the epoch bump really happened mid-capture
        check(!raced && raced.error().code == mira::ErrorCode::Cancelled);
        check(env->created == created_base + 1 && env->closed == closed_base);
        const auto recovered =
            run_capture(registry, ctx, R"({"source":"desktop","interaction":true})");
        check(recovered && !recovered.value().failed);
        check(env->created == created_base + 1 && env->closed == closed_base &&
              env->captures == captures_base + 3);
    }
    {
        // (e) OCR path: native_ocr's internal capture hitting the same
        // pre-publication epoch race passes the Cancelled error through
        // without poisoning, so OCR and capture stay usable on the session.
        const auto created_base = env->created, closed_base = env->closed;
        tools.ocr_image = [&](const auto &image, const auto &, const auto &) {
            integration::ImageOcrOutcome out;
            out.ok = true;
            out.model_revision = "poison-fixture";
            out.regions = {{"fixture", 0.9, 0, 0, 3, 1}};
            check(image.width == 8 && image.height == 4);
            return out;
        };
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, false);
        const auto run_ocr = [&](const mira::OperationContext &owner) {
            const auto exposed = registry.exposed_tools();
            const auto tool = std::find_if(exposed.begin(), exposed.end(), [](const auto &spec) {
                return spec.wire_name == "desktop.vision.ocr";
            });
            check(tool != exposed.end());
            mira::ToolProposal call;
            call.tool_id = tool->tool_id;
            call.tool_version = tool->version;
            call.wire_name = tool->wire_name;
            call.operation_id = mira::OperationId::generate();
            call.provider_call_id.value = "poison-ocr";
            call.arguments = mira::parse_json("{}").value();
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            return registry.execute(call, owner);
        };
        const auto good = run_ocr(ctx);
        check(good && !good.value().failed && good.value().result.find("image_artifact_id"));
        check(env->created == created_base + 1 && env->closed == closed_base);
        mirage::desktop::ActionCoordinator::Lease lease;
        env->during = [&] { lease = tools.actions->try_begin({}); };
        const auto raced = run_ocr(ctx);
        env->during = {};
        check(bool(lease));
        check(!raced && raced.error().code == mira::ErrorCode::Cancelled);
        // The session survived the OCR staleness passthrough.
        check(env->created == created_base + 1 && env->closed == closed_base);
        const auto next = run_ocr(ctx);
        check(next && !next.value().failed && next.value().result.find("image_artifact_id"));
        const auto capture_after = run_capture(registry, ctx, R"({"source":"desktop"})");
        check(capture_after && !capture_after.value().failed);
        check(env->created == created_base + 1 && env->closed == closed_base);
    }
    check(env->created == env->closed);
}

// DEC-088 capability 1: the persistent desktop source counts consecutively
// re-offered captures across requests and dialog turns; the turn resolves the
// identity only for image-bearing, on-owner requests, keyed by content digest
// (a recapture of identical pixels is the same sight; changed pixels restart).
void input_reuse_identity() {
    auto env = std::make_shared<NativeEnvironment>();
    env->interactive = true;
    env->frame_width = 8;
    env->frame_height = 4;
    const auto pixel = [](int x, int y) {
        return std::array<std::uint8_t, 4>{
            static_cast<std::uint8_t>(x * 13 + 3), static_cast<std::uint8_t>(y * 11 + 5),
            static_cast<std::uint8_t>((x * 7 + y * 3) & 0xFF), std::uint8_t{255}};
    };
    env->session_pixels = [&] {
        std::vector<std::uint8_t> pixels(std::size_t(env->frame_width) *
                                         std::size_t(env->frame_height) * 4);
        for (int y = 0; y < env->frame_height; ++y)
            for (int x = 0; x < env->frame_width; ++x)
                for (std::size_t channel = 0; channel < 4; ++channel)
                    pixels[(std::size_t(y) * std::size_t(env->frame_width) + std::size_t(x)) * 4 +
                           channel] = pixel(x, y)[channel];
        return pixels;
    };
    auto tools = desktop();
    tools.environment = env;
    tools.gate = [](const auto &, const auto &, const auto &cancel) { return !cancel(); };
    auto source = std::make_shared<detail::DesktopImageSource>();
    auto ctx = context();
    mirage::desktop::CancelToken stop;
    mira::AgentLoopSpec spec;
    spec.task_id = ctx.task;
    spec.session_id = ctx.session;
    spec.task_epoch = ctx.task_epoch;
    const auto run_capture = [&](mira::BuiltinToolRegistry &registry, const char *json) {
        const auto exposed = registry.exposed_tools();
        const auto tool = std::find_if(exposed.begin(), exposed.end(), [](const auto &candidate) {
            return candidate.wire_name == "desktop.screen.capture";
        });
        check(tool != exposed.end());
        mira::ToolProposal call;
        call.tool_id = tool->tool_id;
        call.tool_version = tool->version;
        call.wire_name = tool->wire_name;
        call.operation_id = mira::OperationId::generate();
        call.provider_call_id.value = "reuse-capture";
        call.arguments = mira::parse_json(json).value();
        call.arguments_digest = mira::canonical_json_digest(call.arguments);
        return registry.execute(call, ctx);
    };
    std::string digest;
    {
        // One capture re-offered on three requests: the counts run 0, 1, 2.
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto captured =
            run_capture(registry, R"({"source":"roi","x":0,"y":0,"width":4,"height":2})");
        check(captured && !captured.value().failed);
        for (std::uint32_t round = 0; round < 3; ++round) {
            const auto supplied = turn.supply(spec);
            check(supplied.has_value());
            const auto identity = turn.input_reuse_identity(spec);
            check(identity.has_value());
            check(identity->semantic_label == "desktop screenshot");
            check(identity->reuse_count == round);
            check(identity->content_digest.size() == std::size_t{64});
            if (!round)
                digest = identity->content_digest;
            check(identity->content_digest == digest);
        }
    }
    {
        // A second dialog turn on the same persistent source continues the
        // count; a text-only request neither carries an image nor advances it.
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        {
            const auto supplied = turn.supply(spec);
            check(supplied.has_value());
            check(!turn.input_reuse_identity(spec).has_value());
        }
        const auto captured =
            run_capture(registry, R"({"source":"roi","x":0,"y":0,"width":4,"height":2})");
        check(captured && !captured.value().failed);
        const auto supplied = turn.supply(spec);
        check(supplied.has_value());
        const auto identity = turn.input_reuse_identity(spec);
        check(identity.has_value());
        check(identity->reuse_count == 3);
        check(identity->content_digest == digest);
        // An off-owner spec is a wiring error answered without an identity.
        mira::AgentLoopSpec foreign;
        foreign.task_id = mira::TaskId::generate();
        foreign.session_id = ctx.session;
        foreign.task_epoch = ctx.task_epoch;
        check(!turn.input_reuse_identity(foreign).has_value());
    }
    {
        // Changed pixels (different crop) restart the count at zero.
        detail::DesktopImageTurn turn(source, tools, ctx, stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, true);
        const auto captured =
            run_capture(registry, R"({"source":"roi","x":2,"y":1,"width":4,"height":2})");
        check(captured && !captured.value().failed);
        const auto supplied = turn.supply(spec);
        check(supplied.has_value());
        const auto identity = turn.input_reuse_identity(spec);
        check(identity.has_value());
        check(identity->reuse_count == 0);
        check(identity->content_digest != digest);
    }
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
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &value) {
                    return value.wire_name == "desktop_screen_capture";
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
            const auto spec =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &t) {
                    return t.wire_name == mirage::testing::fixture_tool_name(name);
                });
            check(spec != request.tools.end());
            if (!images)
                check(std::none_of(request.tools.begin(), request.tools.end(), [](const auto &t) {
                    return t.wire_name == "desktop_screen_capture";
                }));
            mira::ToolCallOutput call;
            call.tool_id = spec->tool_id;
            call.provider_name = spec->wire_name;
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
            const auto exposed = begin == std::string::npos
                                     ? std::string{}
                                     : host_text.substr(begin, host_text.find('\n', begin) - begin);
            for (const auto &tool : request.tools)
                check(exposed.find(" " + tool.wire_name) != std::string::npos);
            check(exposed.find("desktop_window_list") == std::string::npos);
            check(exposed.find("desktop_session_click") != std::string::npos);
            check(host_text.find("The user need not request these methods by name") !=
                  std::string::npos);
            check(host_text.find("does NOT prevent clicking a visible") != std::string::npos);
            check(host_text.find("first consider an exposed, actionable image-bound click") !=
                  std::string::npos);
            check(host_text.find("If Mirage obscures the browser, ask") == std::string::npos);
            const auto click =
                std::find_if(request.tools.begin(), request.tools.end(), [](const auto &tool) {
                    return tool.wire_name == "desktop_session_click";
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
        const std::vector<std::string> names = {
            "desktop.screen.capture", "desktop.vision.ocr",        "desktop.session.click",
            "desktop.session.key",    "desktop.session.type_text", "desktop.session.scroll"};
        if (calls < static_cast<int>(names.size())) {
            const auto name = names[calls];
            const auto spec =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &t) {
                    return t.wire_name == mirage::testing::fixture_tool_name(name);
                });
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
            call.provider_name = spec->wire_name;
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
            return tool.wire_name.starts_with("desktop_session_");
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
        // The full-frame PNG encode exceeds the production 5 s acquisition
        // budget under ThreadSanitizer; keep the production default elsewhere.
        const auto acquire_budget = std::chrono::seconds{MIRAGE_TSAN_SLOW ? 60 : 5};
        detail::DesktopImageTurn turn(source, tools, ctx, {}, true, acquire_budget);
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
        // M7-17: small ROIs stay 1:1 with exact origin metadata.
        check(*roi.value().result.find("origin_x")->as_integer() == 1500 &&
              *roi.value().result.find("origin_y")->as_integer() == 1000);
        check(*roi.value().result.find("desktop_per_image_x_numerator")->as_integer() == 300 &&
              *roi.value().result.find("desktop_per_image_x_denominator")->as_integer() == 300 &&
              *roi.value().result.find("desktop_per_image_y_numerator")->as_integer() == 200 &&
              *roi.value().result.find("desktop_per_image_y_denominator")->as_integer() == 200);
        check(*roi.value().result.find("scale_numerator")->as_integer() == 1 &&
              *roi.value().result.find("scale_denominator")->as_integer() == 1);
        // M7-17: a long-side >1600 ROI preview-scales exactly like a
        // display/window capture while origin/scale metadata still maps every
        // image pixel back into desktop coordinates.
        auto large_roi =
            run(R"({"source":"roi","source_id":"d1","x":0,"y":0,"width":1920,"height":1080})");
        if (!large_roi || large_roi.value().failed)
            throw std::runtime_error(large_roi ? large_roi.value().safe_error_summary
                                               : large_roi.error().safe_message);
        const auto &large_preview = large_roi.value().result;
        check(*large_preview.find("width")->as_integer() == 1600 &&
              *large_preview.find("height")->as_integer() == 900);
        check(*large_preview.find("source_width")->as_integer() == 1920 &&
              *large_preview.find("source_height")->as_integer() == 1080);
        check(*large_preview.find("origin_x")->as_integer() == 0 &&
              *large_preview.find("origin_y")->as_integer() == 0);
        check(*large_preview.find("desktop_per_image_x_numerator")->as_integer() == 1920 &&
              *large_preview.find("desktop_per_image_x_denominator")->as_integer() == 1600 &&
              *large_preview.find("desktop_per_image_y_numerator")->as_integer() == 1080 &&
              *large_preview.find("desktop_per_image_y_denominator")->as_integer() == 900);
        check(*large_preview.find("scale_numerator")->as_integer() == 1600 &&
              *large_preview.find("scale_denominator")->as_integer() == 1920);
        // click_region semantics survive the preview: the region stays
        // actionable with a fresh scope bound to the published pixels.
        check(large_preview.find("region_actionable") &&
              large_preview.find("region_actionable")->as_boolean() == true &&
              large_preview.find("image_scope"));
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
    // The expected bytes are rebuilt from the construction loop (BGRA -> RGB),
    // independently of the production encoder.
    std::vector<std::uint8_t> expected_rgb;
    expected_rgb.reserve(300u * 100 * 3);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 300; ++x) {
            expected_rgb.push_back(193);
            expected_rgb.push_back(static_cast<std::uint8_t>(y));
            expected_rgb.push_back(static_cast<std::uint8_t>(x % 256));
        }
    check(png.value().size() <= expected_rgb.size()); // compressed never beats raw RGB
    export_png_fixture("desktop-image-fixture", png.value(), expected_rgb, 300, 100,
                       "raw_or_smaller");
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
    std::cout
        << mira::to_json_string(mira::JsonValue{mira::JsonValue::Object{
               {"ok", result.ok},
               {"cancelled", result.cancelled},
               {"answer", result.reply_text},
               {"error", result.error},
               {"captures", static_cast<std::int64_t>(env->capture_calls)},
               {"model_steps", static_cast<std::int64_t>(result.model_steps)},
               {"tool_calls", static_cast<std::int64_t>(result.tool_calls)},
               {"supplied_input_items", static_cast<std::int64_t>(result.supplied_input_items)},
               {"supplied_input_dropped", static_cast<std::int64_t>(result.supplied_input_dropped)},
               {"supply_degradations", static_cast<std::int64_t>(result.supply_degradations)}}})
        << '\n';
    return result.ok ? 0 : 1;
}
// DEC-085 capability honesty fixture: delegates every provider to the fake
// environment but declares a settable screen capability, so the capture
// receipt mapping can be observed for PortalOnly and Full independently.
class CapabilityOverrideEnvironment final : public mirage::desktop::DesktopEnvironment {
  public:
    explicit CapabilityOverrideEnvironment(
        std::shared_ptr<mirage::testing::FakeDesktopEnvironment> inner)
        : inner_(std::move(inner)) {}
    mirage::desktop::EnvironmentInfo info() const override { return inner_->info(); }
    mirage::desktop::FilesystemProvider *filesystem() override { return &inner_->filesystem_; }
    mirage::desktop::ProcessProvider *process() override { return &inner_->process_; }
    mirage::desktop::ApplicationProvider *application() override { return &inner_->application_; }
    mirage::desktop::WindowProvider *window() override { return &inner_->window_; }
    mirage::desktop::AccessibilityProvider *accessibility() override {
        return &inner_->accessibility_;
    }
    mirage::desktop::ScreenProvider *screen() override { return &inner_->screen_; }
    mirage::desktop::InputProvider *input() override { return &inner_->input_; }
    mirage::desktop::ClipboardProvider *clipboard() override { return &inner_->clipboard_; }
    mirage::desktop::NotificationProvider *notification() override {
        return &inner_->notification_;
    }
    mirage::desktop::ScreenCapability screen_capability() override { return capability_; }
    std::shared_ptr<mirage::testing::FakeDesktopEnvironment> inner_;
    mirage::desktop::ScreenCapability capability_ = mirage::desktop::ScreenCapability::Full;
};

// DEC-085 capability honesty: with a degraded (non-Full) screen capability a
// failing direct capture becomes the canonical platform_not_supported receipt
// naming the original error; with Full the plain provider error surfaces
// unchanged. The full-screen argument error degrades the same way.
void capability_receipts() {
    mirage::desktop::ScreenCapability capabilities[] = {
        mirage::desktop::ScreenCapability::PortalOnly, mirage::desktop::ScreenCapability::Full};
    for (const auto capability : capabilities) {
        const bool degraded = capability != mirage::desktop::ScreenCapability::Full;
        auto tools = desktop();
        auto fake =
            std::static_pointer_cast<mirage::testing::FakeDesktopEnvironment>(tools.environment);
        fake->failures.capture_error = true;
        auto env = std::make_shared<CapabilityOverrideEnvironment>(fake);
        env->capability_ = capability;
        tools.environment = env;
        auto ctx = context();
        mirage::desktop::CancelToken stop;
        detail::DesktopImageTurn turn(std::make_shared<detail::DesktopImageSource>(), tools, ctx,
                                      stop);
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {}, false);
        const auto spec = registry.exposed_tools().front();
        const auto invoke = [&](const std::string &source, const std::string &source_id) {
            mira::ToolProposal call;
            call.tool_id = spec.tool_id;
            call.tool_version = spec.version;
            call.wire_name = spec.wire_name;
            call.operation_id = mira::OperationId::generate();
            call.provider_call_id.value = "capability";
            JsonValue::Object fields{{"source", JsonValue{source}}};
            if (!source_id.empty())
                fields.emplace_back("source_id", JsonValue{source_id});
            call.arguments = JsonValue{JsonValue::Object{std::move(fields)}};
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            return registry.execute(call, ctx);
        };
        const auto window = invoke("window", "w1");
        check(window && window.value().failed);
        check(window.value().safe_error_summary.find("io_error") != std::string::npos);
        check((window.value().safe_error_summary.find("platform_not_supported") !=
               std::string::npos) == degraded);
        check((window.value().safe_error_summary.find("window capture failed") !=
               std::string::npos) == degraded);
        const auto display = invoke("display", "d1");
        check(display && display.value().failed);
        check(display.value().safe_error_summary.find("io_error") != std::string::npos);
        check((display.value().safe_error_summary.find("platform_not_supported") !=
               std::string::npos) == degraded);
        // The direct-path schema requires source and source_id; the
        // full-screen receipt guards an out-of-schema source=desktop call
        // that still carries an id.
        const auto full_screen = invoke("desktop", "d1");
        check(full_screen && full_screen.value().failed);
        if (degraded) {
            check(full_screen.value().safe_error_summary.find("platform_not_supported") !=
                  std::string::npos);
            check(full_screen.value().safe_error_summary.find(
                      "full-screen X11 capture is unavailable") != std::string::npos);
        } else {
            check(full_screen.value().safe_error_summary.find("invalid capture source") !=
                  std::string::npos);
        }
    }
}

// DEC-088 capability 5: budget-aware image degradation against the injected
// remaining-budget signal. Above the warn bound the image ships unchanged;
// down to the degrade bound it ships with exactly one skip-analysis notice;
// at or below it the image is withheld for an Internal text description,
// validate_request must not demand the withheld image, and the supply exposes
// no reuse identity. Degraded rounds pause the chain instead of resetting it.
void budget_degradation() {
    auto tools = desktop();
    auto env = std::static_pointer_cast<mirage::testing::FakeDesktopEnvironment>(tools.environment);
    (void)env;
    auto source = std::make_shared<detail::DesktopImageSource>();
    const auto ctx = context();
    mira::AgentLoopSpec spec;
    spec.task_id = ctx.task;
    spec.session_id = ctx.session;
    spec.task_epoch = ctx.task_epoch;
    struct Scan {
        std::size_t images = 0;
        std::size_t warnings = 0;
        std::string text;
    };
    const auto scan = [&](const std::vector<mira::ModelInputItem> &supplied) {
        Scan result;
        for (const auto &item : supplied)
            for (const auto &part : item.content) {
                if (std::holds_alternative<mira::ImagePart>(part))
                    ++result.images;
                if (const auto *value = std::get_if<mira::TextPart>(&part)) {
                    result.text += value->text + "\n";
                    if (value->sensitivity == mira::Sensitivity::Internal &&
                        value->text.find("[Context budget low]") != std::string::npos)
                        ++result.warnings;
                }
            }
        return result;
    };
    const auto capture_once = [&](detail::DesktopImageTurn &turn) {
        mira::BuiltinToolRegistry registry;
        turn.register_tool(registry, {});
        const auto exposed = registry.exposed_tools();
        const auto capture = std::find_if(exposed.begin(), exposed.end(), [](const auto &tool) {
            return tool.wire_name == "desktop.screen.capture";
        });
        check(capture != exposed.end());
        mira::ToolProposal proposal;
        proposal.tool_id = capture->tool_id;
        proposal.tool_version = capture->version;
        proposal.wire_name = capture->wire_name;
        proposal.arguments = mira::parse_json(R"({"source":"display","source_id":"d1"})").value();
        proposal.arguments_digest = mira::canonical_json_digest(proposal.arguments);
        const auto outcome = registry.execute(proposal, ctx);
        check(outcome && !outcome.value().failed);
    };
    // Without any wired signal the image ships unchanged (fail open).
    {
        detail::DesktopImageTurn turn(source, tools, ctx, {});
        capture_once(turn);
        const auto supplied = turn.supply(spec);
        const auto parts = scan(supplied.value());
        check(parts.images == 1 && parts.warnings == 0);
    }
    detail::DesktopImageTurn turn(source, tools, ctx, {});
    capture_once(turn);
    std::optional<std::int64_t> remaining;
    bool wired = false;
    turn.set_budget_remaining([&] { return wired ? remaining : std::nullopt; });
    // Unwired provider and an explicit nullopt both ship the plain image.
    const auto plain = turn.supply(spec);
    auto parts = scan(plain.value());
    check(parts.images == 1 && parts.warnings == 0);
    check(turn.input_reuse_identity(spec).has_value());
    wired = true;
    const auto open = turn.supply(spec);
    parts = scan(open.value());
    check(parts.images == 1 && parts.warnings == 0);
    check(turn.input_reuse_identity(spec).has_value());
    // Healthy budget: unchanged.
    remaining = 25'000;
    const auto healthy = turn.supply(spec);
    parts = scan(healthy.value());
    check(parts.images == 1 && parts.warnings == 0);
    check(turn.input_reuse_identity(spec).has_value());
    // Warn band, both bounds included: image plus exactly one notice.
    for (const auto value : {20'000, 15'000, 10'001}) {
        remaining = value;
        const auto warned = turn.supply(spec);
        parts = scan(warned.value());
        check(parts.images == 1 && parts.warnings == 1);
        check(parts.text.find("Skip detailed image analysis") != std::string::npos);
        check(turn.input_reuse_identity(spec).has_value());
    }
    // Degrade band, boundary included: the image is withheld.
    for (const auto value : {10'000, 8'000}) {
        remaining = value;
        const auto degraded = turn.supply(spec);
        parts = scan(degraded.value());
        check(parts.images == 0 && parts.warnings == 1);
        check(degraded.value().size() == 1 && degraded.value().front().content.size() == 1);
        const auto &notice = std::get<mira::TextPart>(degraded.value().front().content.front());
        check(notice.sensitivity == mira::Sensitivity::Internal);
        check(notice.text.find("[Context budget low]") != std::string::npos);
        check(notice.text.find("withheld") != std::string::npos);
        check(notice.text.find(std::to_string(value)) != std::string::npos);
        check(notice.text.find("Last image: 128x64 pixels.") != std::string::npos);
        check(!turn.input_reuse_identity(spec));
        // The withheld-image request validates without demanding an image.
        mira::ModelRequest request;
        request.task_id = ctx.task;
        request.task_epoch = ctx.task_epoch;
        request.input = degraded.value();
        check(bool(turn.validate_request(request, ctx)));
    }
    // The chain pauses across degraded rounds: the next real offer continues.
    remaining = 25'000;
    const auto resumed = turn.supply(spec);
    parts = scan(resumed.value());
    check(parts.images == 1 && parts.warnings == 0);
    const auto identity = turn.input_reuse_identity(spec);
    check(identity.has_value());
    // Six image offers so far (counts 0..5); the degraded rounds did not
    // advance or reset the chain, so this offer continues at 6.
    check(identity->reuse_count == 6);
}

// Stub provider whose only job is to report a configurable usage line, so the
// ObservedProvider anchor table can be exercised directly.
class UsageStubProvider final : public mira::IModelProvider {
  public:
    mira::ModelProfile profile_;
    std::optional<std::uint64_t> input_tokens;
    mira::UsageQuality quality = mira::UsageQuality::ProviderReported;
    explicit UsageStubProvider(mira::ModelProfile profile) : profile_(std::move(profile)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        response.usage.quality = quality;
        response.usage.input_tokens = input_tokens;
        mira::MessageOutput message;
        message.content.emplace_back(mira::OutputTextPart{"ok", {}});
        response.output.emplace_back(std::move(message));
        return response;
    }
};

// DEC-088 capability 5: the anchored remaining-budget signal fails open
// (nullopt) without a declared window or without an anchor for the session,
// anchors only on provider-reported/exact usage, and then reads
// window − provider input − the DEC-087 reserve.
void anchored_remaining_unit() {
    mira::ModelProfile profile;
    profile.id = mira::ModelProfileId::generate();
    profile.model_selector = "usage-fixture";
    auto stub = std::make_shared<UsageStubProvider>(profile);
    detail::ObservedProvider observed(stub);
    const auto ctx = context();
    const std::string session_key = ctx.session.to_string();
    // No declared window: fail open.
    check(!observed.anchored_remaining(session_key).has_value());
    observed.context_window = 50'000;
    // Declared window but no anchor yet, and unknown sessions stay nullopt.
    check(!observed.anchored_remaining(session_key).has_value());
    check(!observed.anchored_remaining("unknown-session").has_value());
    mira::ModelRequest request;
    request.task_id = ctx.task;
    request.task_epoch = ctx.task_epoch;
    stub->input_tokens = 4'200;
    // Estimated usage must not anchor the session.
    stub->quality = mira::UsageQuality::Estimated;
    check(bool(observed.infer(request, ctx, {})));
    check(!observed.anchored_remaining(session_key).has_value());
    // Provider-reported usage anchors it.
    stub->quality = mira::UsageQuality::ProviderReported;
    check(bool(observed.infer(request, ctx, {})));
    const auto remaining = observed.anchored_remaining(session_key);
    check(remaining.has_value() && *remaining == 50'000 - 4'200 - 1'000);
    check(!observed.anchored_remaining("unknown-session").has_value());
}

// DEC-088 capability 1 end to end: identical pixels keep one content digest,
// so three consecutive supplies annotate the third request through the loop
// identity seam and the count surfaces on DialogCompletion.
class ReuseScript final : public mira::IModelProvider {
  public:
    mira::ModelProfile profile_;
    std::vector<mira::ModelRequest> requests;
    explicit ReuseScript(mira::ModelProfile profile) : profile_(std::move(profile)) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        requests.push_back(request);
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        if (requests.size() <= 3) {
            const auto tool =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &value) {
                    return value.wire_name == "desktop_screen_capture";
                });
            check(tool != request.tools.end());
            mira::ToolCallOutput call;
            call.provider_call_id.value = "reuse-capture-" + std::to_string(requests.size());
            call.tool_id = tool->tool_id;
            call.provider_name = tool->wire_name;
            call.arguments = mira::parse_json(R"({"source":"display","source_id":"d1"})").value();
            call.arguments_digest = mira::canonical_json_digest(call.arguments);
            response.output.emplace_back(std::move(call));
        } else {
            mira::MessageOutput message;
            message.content.emplace_back(mira::OutputTextPart{"observed", {}});
            response.output.emplace_back(std::move(message));
        }
        return response;
    }
};

void reuse_annotation_end_to_end(kairo::Executor &executor) {
    auto tools = desktop();
    std::shared_ptr<ReuseScript> script;
    integration::ModelProviderOverride provider([&](const auto &profile) {
        script = std::make_shared<ReuseScript>(profile);
        return script;
    });
    integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.model_selector = "reuse-fixture";
    integration::ModelLayer model(executor, config, &provider, tools);
    const auto ctx = context();
    auto result =
        executor
            .submit_auto([&] {
                return model.complete_harness_turn("", "capture, then verify the screen", ctx);
            })
            .get();
    check(result.ok);
    check(script && script->requests.size() == 4);
    check(result.supplied_input_reuse_annotations == 1);
    // Only the third image-bearing request (count 2) renders the Internal
    // system note as the last part of the primary user envelope.
    for (std::size_t index = 0; index < script->requests.size(); ++index) {
        bool note = false;
        for (const auto &item : script->requests[index].input)
            for (const auto &part : item.content)
                if (const auto *text = std::get_if<mira::TextPart>(&part))
                    note |= text->sensitivity == mira::Sensitivity::Internal &&
                            text->text.find("[System note: The desktop screenshot above is the "
                                            "same as in the previous 2 turns") != std::string::npos;
        check(index + 1 == script->requests.size() ? note : !note);
    }
    model.shutdown();
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
                for (const auto &mode : {"right",
                                         "middle",
                                         "drag",
                                         "wheel",
                                         "drag_invalid",
                                         "invalid_button",
                                         "button_type",
                                         "wheel_budget",
                                         "normal",
                                         "ocr",
                                         "double",
                                         "triple",
                                         "repeat",
                                         "click_limit",
                                         "invalid_click_count",
                                         "zero_click_count",
                                         "negative_click_count",
                                         "fractional_click_count",
                                         "string_click_count",
                                         "no_image",
                                         "foreign",
                                         "changed",
                                         "uniform_drift",
                                         "changed_refresh",
                                         "changed_refresh_lease",
                                         "denied",
                                         "release",
                                         "uncertain",
                                         "takeover",
                                         "stale",
                                         "initial_denied",
                                         "epoch",
                                         "invalid_crop",
                                         "post_failure",
                                         "wrong_ocr",
                                         "ocr_invalid",
                                         "unicode",
                                         "pretimeout",
                                         "repeated_source"})
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
        budget_degradation();
        anchored_remaining_unit();
        reuse_annotation_end_to_end(executor);
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
        png_content_matrix();
        native_region_sources();
        native_session_poison_discipline();
        input_reuse_identity();
        capability_receipts();
        // Publish the fixture manifest for the independent Python decoder
        // (tests/manual/desktop_png_decode_check.py).
        JsonValue::Array manifest;
        for (const auto &fixture : png_fixtures)
            manifest.emplace_back(
                JsonValue{JsonValue::Object{{"png", fixture.png},
                                            {"raw", fixture.raw},
                                            {"width", static_cast<std::int64_t>(fixture.width)},
                                            {"height", static_cast<std::int64_t>(fixture.height)},
                                            {"expectation", fixture.expectation}}});
        std::ofstream manifest_file("desktop-png-fixtures.json");
        manifest_file << mira::to_json_string(JsonValue{std::move(manifest)});
        check(static_cast<bool>(manifest_file));
        std::cout << checks << " desktop image checks passed\n";
        if (!deferred_failure.empty()) {
            std::cerr << deferred_failure << '\n';
            return 1;
        }
        return 0;
    } catch (const std::exception &failure) {
        std::cerr << failure.what() << '\n';
        return 1;
    }
}
