#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <kairo/executor.hpp>
#include <mira/model_digest.hpp>
#include <mirage/desktop/input_session.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <source_location>
#include <stdexcept>
#include <vector>

// Full-frame PNG encode can exceed the tight production budgets under
// ThreadSanitizer (same discipline as the GUI fallback fixture).
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

// DEC-110 / M7-26: model-facing clicks use local automatic approaches.
// Record travel/press order and ensure changed targets, cancellation, legacy
// timing arguments and stale tool versions never dispatch a click.
namespace d = mirage::desktop;
namespace i = mirage::integration;
namespace detail = i::detail;
using Json = mira::JsonValue;

namespace {
void check(bool value, const std::source_location at = std::source_location::current()) {
    ++::mirage::testing::check_count();
    if (!value) {
        ++::mirage::testing::failure_count();
        throw std::runtime_error("visual glide check at line " + std::to_string(at.line()));
    }
}
mira::OperationContext owner() {
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.task_epoch = 5;
    return ctx;
}
mira::ToolExecutionRecord invoke(mira::BuiltinToolRegistry &registry, const std::string &name,
                                 const Json &args, const mira::OperationContext &ctx,
                                 bool legacy_version = false) {
    for (const auto &spec : registry.exposed_tools())
        if (spec.wire_name == name) {
            mira::ToolProposal proposal;
            proposal.tool_id = spec.tool_id;
            proposal.tool_version = legacy_version ? mira::SemanticVersion{3, 0, 0} : spec.version;
            proposal.wire_name = name;
            proposal.has_side_effects = spec.has_side_effects;
            proposal.operation_id = mira::OperationId::generate();
            proposal.provider_call_id.value = "glide-call";
            proposal.arguments = args;
            proposal.arguments_digest = mira::canonical_json_digest(args);
            auto result = registry.execute(proposal, ctx);
            if (!result) {
                mira::ToolExecutionRecord failed;
                failed.failed = true;
                failed.safe_error_summary = result.error().safe_message;
                return failed;
            }
            return result.value();
        }
    throw std::runtime_error("glide tool missing: " + name);
}
void expect_honest_receipt(const mira::ToolExecutionRecord &record, const char *code,
                           bool side_effect) {
    check(record.failed);
    const auto parsed = mira::parse_json(record.safe_error_summary);
    check(static_cast<bool>(parsed));
    if (!parsed)
        return;
    const auto &summary = parsed.value();
    const auto *reported = summary.find("code");
    check(reported && reported->as_string() && *reported->as_string() == code);
    const auto *effect = summary.find("side_effect_may_have_occurred");
    check(effect && effect->as_boolean().value_or(!side_effect) == side_effect);
}

/// Input fixture: every provider surface delegates to the shared fake input;
/// the timed-pointer opt-in is scripted so tests can flip support and travel
/// outcomes. Smooth-move calls are recorded with their full arguments.
class GlideInput final : public d::InputProvider {
  public:
    d::InputProvider *delegate = nullptr;
    bool timed_supported = true;
    bool move_fails = false;
    std::function<void()> after_move;
    d::ProviderError move_error{"deadline_exceeded", "scripted travel failure"};
    struct Move {
        std::int32_t x = 0, y = 0;
        std::chrono::milliseconds duration{}, timeout{};
        std::uint32_t points = 0;
    };
    std::vector<Move> moves;
    int click_calls = 0;
    std::uint32_t last_click_count = 0;
    std::string last_button;

    bool complete_actions_supported() const override { return true; }
    bool bounded_pointer_actions_supported() const override { return true; }
    bool timed_pointer_actions_supported() const override { return timed_supported; }
    std::optional<d::PointerMotionVariation> last_variation;
    d::InputOutcome
    smooth_move_to(std::int32_t x, std::int32_t y, std::chrono::milliseconds duration,
                   std::uint32_t points, const d::InputLimits &limits, const d::CancelToken &,
                   std::optional<d::PointerMotionVariation> variation = std::nullopt) override {
        last_variation = variation;
        moves.push_back({x, y, duration, limits.timeout, points});
        d::InputOutcome out;
        if (move_fails) {
            out.error = move_error;
            out.side_effect_may_have_occurred = true;
            return out;
        }
        if (after_move)
            after_move();
        out.ok = true;
        out.side_effect_may_have_occurred = true;
        return out;
    }
    d::InputOutcome click_sequence_at(std::int32_t x, std::int32_t y, const d::MouseButton &button,
                                      std::uint32_t count, const d::InputLimits &limits,
                                      const d::CancelToken &cancel) override {
        ++click_calls;
        last_click_count = count;
        last_button = button;
        return delegate->click_at(x, y, button, limits, cancel);
    }
    d::InputOutcome click_at(std::int32_t x, std::int32_t y, const d::MouseButton &button,
                             const d::InputLimits &limits, const d::CancelToken &cancel) override {
        return delegate->click_at(x, y, button, limits, cancel);
    }
    d::InputOutcome inject_key(const d::KeySym &key, bool pressed, const d::InputLimits &limits,
                               const d::CancelToken &cancel) override {
        return delegate->inject_key(key, pressed, limits, cancel);
    }
    d::InputOutcome type_text(const std::string &text, const d::InputLimits &limits,
                              const d::CancelToken &cancel) override {
        return delegate->type_text(text, limits, cancel);
    }
    d::InputOutcome pointer_move(std::int32_t x, std::int32_t y, const d::InputLimits &limits,
                                 const d::CancelToken &cancel) override {
        return delegate->pointer_move(x, y, limits, cancel);
    }
    d::InputOutcome pointer_button(const d::MouseButton &button, bool pressed,
                                   const d::InputLimits &limits,
                                   const d::CancelToken &cancel) override {
        return delegate->pointer_button(button, pressed, limits, cancel);
    }
    d::PointerQueryOutcome pointer_position(const d::CancelToken &cancel) override {
        return delegate->pointer_position(cancel);
    }
};

/// DesktopEnvironment whose input provider is the scripted GlideInput.
class GlideEnvironment final : public d::DesktopEnvironment {
  public:
    std::shared_ptr<mirage::testing::FakeDesktopEnvironment> fake;
    GlideInput glide;
    explicit GlideEnvironment(std::shared_ptr<mirage::testing::FakeDesktopEnvironment> base)
        : fake(std::move(base)) {
        glide.delegate = fake->input();
    }
    d::EnvironmentInfo info() const override { return {"glide-fixture", "test"}; }
    d::ScreenProvider *screen() override { return fake->screen(); }
    d::WindowProvider *window() override { return fake->window(); }
    d::InputProvider *input() override { return &glide; }
    // Numbered visual targets only, like the visual marks fixture.
    d::AccessibilityProvider *accessibility() override { return nullptr; }
};

/// Numbered-mark fixture (desktop.vision.click path): OCR pipeline over the
/// fake screen marks one "button" region at frame (20,20,40,20) inside the
/// display/window {-200,-40,128,80}; the mark anchor lands at (-160,-10).
struct MarkFixture {
    std::shared_ptr<GlideEnvironment> env = std::make_shared<GlideEnvironment>(
        std::make_shared<mirage::testing::FakeDesktopEnvironment>());
    std::unique_ptr<i::FakeOcrBackend> ocr;
    d::VisualReferenceRegistry visual;
    std::unique_ptr<i::VisualObservationPipeline> pipeline;
    i::DesktopToolContext tools;
    mira::OperationContext ctx = owner();
    d::CancelToken cancel;
    std::shared_ptr<detail::DesktopImageSource> source =
        std::make_shared<detail::DesktopImageSource>();
    std::unique_ptr<detail::DesktopImageTurn> turn;
    mira::BuiltinToolRegistry registry;

    explicit MarkFixture(kairo::Executor &executor) {
        static int sequence = 0;
        env->fake->displays = {{"d1", {-200, -40, 128, 80}, true}};
        env->fake->windows = {{"w1", "Before", {-200, -40, 128, 80}, true}};
        i::FakeOcrConfig backend;
        backend.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        backend.regions = {{{20, 20, 40, 20}, "button", 0.9f, {}}};
        ocr = std::make_unique<i::FakeOcrBackend>(backend);
        i::VisualObservationPipelineConfig cfg;
        cfg.source_id = "visual-glide-mark-test-" + std::to_string(++sequence);
        cfg.run_ocr = true;
        cfg.refresh_ocr = true;
        cfg.session.ocr_backend = ocr.get();
        pipeline =
            std::make_unique<i::VisualObservationPipeline>(executor, *env->screen(), visual, cfg);
        std::string failure;
        check(pipeline->start(failure));
        tools.environment = env;
        tools.actions = std::make_shared<d::ActionCoordinator>();
        tools.gate = [](const auto &, const auto &, const auto &probe) { return !probe(); };
        tools.mark = [&](const auto &crop, const auto &token, const auto &end) {
            return pipeline->refresh_marked(crop, token, end);
        };
        tools.with_visual_reference = [&](const auto &scope, const auto &ref, const auto &action) {
            return visual.with_reference(scope, ref, action);
        };
        tools.with_visual_references = [&](const auto &scope, const auto &from, const auto &to,
                                           const auto &action) {
            return visual.with_references(scope, from, to, action);
        };
        turn = std::make_unique<detail::DesktopImageTurn>(source, tools, ctx, cancel);
        turn->register_tool(registry, {});
    }
    ~MarkFixture() { pipeline->stop(); }
    mira::ToolExecutionRecord mark() {
        return invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, ctx);
    }
    Json target(const Json &marked) {
        return Json{Json::Object{{"visual_scope", *marked.find("visual_scope")},
                                 {"environment_epoch", *marked.find("environment_epoch")},
                                 {"mark", std::int64_t{1}},
                                 {"window_id", "w1"},
                                 {"verify_kind", "window_focused"}}};
    }
    mira::ToolExecutionRecord click(const Json &args) {
        return invoke(registry, "desktop.vision.click", args, ctx);
    }
};

/// Image-region fixture (desktop.vision.click_region path): a
/// desktop.screen.capture supplies a region-actionable image over the
/// 400x300 fake display; the default rectangle (20,20,20,20) centers on
/// (30,30) global pixels.
struct RegionFixture {
    std::shared_ptr<mirage::testing::FakeDesktopEnvironment> fake =
        std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    std::shared_ptr<GlideEnvironment> env;
    i::DesktopToolContext tools;
    mira::OperationContext ctx = owner();
    d::CancelToken cancel;
    std::unique_ptr<detail::DesktopImageTurn> image;
    mira::BuiltinToolRegistry registry;

    RegionFixture() : env(std::make_shared<GlideEnvironment>(fake)) {
        fake->windows = {{"w1", "Editor", {100, 100, 100, 100}, true}};
        fake->displays = {{"d1", {0, 0, 400, 300}, true}};
        tools.environment = env;
        tools.actions = std::make_shared<d::ActionCoordinator>();
        tools.gate = [](const auto &, const auto &, const auto &probe) { return !probe(); };
        // Full-frame PNG encode stays inside the production budgets except
        // under ThreadSanitizer; mirror the gui fallback fixture's budget.
        const auto budget = std::chrono::milliseconds{MIRAGE_TSAN_SLOW ? 60000 : 5000};
        image = std::make_unique<detail::DesktopImageTurn>(
            std::make_shared<detail::DesktopImageSource>(), tools, ctx, cancel, true, budget,
            budget);
        image->register_tool(registry, {}, true);
    }
    Json capture() {
        const auto result =
            invoke(registry, "desktop.screen.capture",
                   Json{Json::Object{{"source", "display"}, {"source_id", "d1"}}}, ctx);
        check(!result.failed);
        mira::AgentLoopSpec spec;
        spec.task_id = ctx.task;
        spec.session_id = ctx.session;
        spec.task_epoch = ctx.task_epoch;
        auto items = image->supply(spec);
        check(static_cast<bool>(items));
        mira::ModelRequest request;
        request.task_id = ctx.task;
        request.task_epoch = ctx.task_epoch;
        request.input = items.value();
        check(static_cast<bool>(image->validate_request(request, ctx)));
        return result.result;
    }
    Json args(const Json &meta, bool window = false) {
        return Json{
            Json::Object{{"image_scope", *meta.find("image_scope")},
                         {"environment_epoch", *meta.find("environment_epoch")},
                         {"target_context", window ? "window" : "display"},
                         {window ? "window_id" : "display_id", window ? "w1" : "d1"},
                         {"x", window ? 110 : 20},
                         {"y", window ? 110 : 20},
                         {"width", 20},
                         {"height", 20},
                         {"verify_kind", window ? "window_focused" : "foreground_changed"}}};
    }
    mira::ToolExecutionRecord click(const Json &a) {
        return invoke(registry, "desktop.vision.click_region", a, ctx);
    }
};

void click_glide_matrix(kairo::Executor &executor) {
    for (int mode = 0; mode < 7; ++mode) {
        MarkFixture f(executor);
        const auto marked = f.mark();
        check(!marked.failed);
        auto args = f.target(marked.result);
        if (mode == 1)
            f.env->glide.timed_supported = false;
        if (mode == 2)
            f.env->glide.move_fails = true;
        if (mode == 3)
            f.env->glide.after_move = [&] { f.env->fake->windows[0].title = "hover changed"; };
        if (mode == 4)
            f.env->glide.after_move = [&] { f.cancel.request_cancel(); };
        if (mode == 5)
            args.set("glide_ms", 300);
        if (mode == 6) {
            args.set("button", "right");
            args.set("click_count", 3);
        }
        if (mode == 0) {
            const auto old = invoke(f.registry, "desktop.vision.click", args, f.ctx, true);
            check(old.failed && f.env->glide.moves.empty() && f.env->glide.click_calls == 0);
        }
        const auto result = f.click(args);
        const bool success = mode == 0 || mode == 6;
        check(result.failed != success);
        if (mode == 2)
            expect_honest_receipt(result, "deadline_exceeded", true);
        check(f.env->glide.click_calls == (success ? 1 : 0));
        if (success) {
            check(f.env->glide.moves.size() == 1);
            check(f.env->glide.last_variation.has_value());
            const auto &move = f.env->glide.moves[0];
            check(move.x == -160 && move.y == -10 && move.duration.count() >= 120 &&
                  move.duration.count() <= 650 && move.points == 0);
            if (mode == 6)
                check(f.env->glide.last_click_count == 3 && f.env->glide.last_button == "right");
        }
        if (mode == 1 || mode == 5)
            check(f.env->glide.moves.empty());
        const auto &exposed = f.registry.exposed_tools();
        const auto found = std::find_if(exposed.begin(), exposed.end(), [](const auto &tool) {
            return tool.wire_name == "desktop.vision.click";
        });
        check(found != exposed.end() && found->version == mira::SemanticVersion{4, 0, 0});
        check(!found->parameters_schema.root.find("properties")->find("glide_ms"));
    }
}
void region_glide_matrix() {
    for (int mode = 0; mode < 8; ++mode) {
        RegionFixture f;
        auto args = f.args(f.capture());
        if (mode == 1)
            f.env->glide.timed_supported = false;
        if (mode == 2)
            f.env->glide.move_fails = true;
        if (mode == 3)
            f.env->glide.after_move = [&] { f.fake->windows[0].title = "hover changed"; };
        if (mode == 4)
            f.env->glide.after_move = [&] { f.cancel.request_cancel(); };
        if (mode == 5)
            args.set("glide_ms", 300);
        if (mode == 6) {
            args.set("button", "middle");
            args.set("click_count", 2);
        }
        if (mode == 7)
            f.fake->pointer = {30, 30}; // no empty travel at the target
        if (mode == 0) {
            const auto old = invoke(f.registry, "desktop.vision.click_region", args, f.ctx, true);
            check(old.failed && f.env->glide.moves.empty() && f.env->glide.click_calls == 0);
        }
        const auto result = f.click(args);
        const bool success = mode == 0 || mode == 6 || mode == 7;
        check(result.failed != success);
        if (mode == 2)
            expect_honest_receipt(result, "deadline_exceeded", true);
        check(f.env->glide.click_calls == (success ? 1 : 0));
        if (success && mode != 7) {
            check(f.env->glide.moves.size() == 1);
            const auto &move = f.env->glide.moves[0];
            check(move.x == 30 && move.y == 30 && move.duration.count() >= 120 &&
                  move.duration.count() <= 650);
        }
        if (mode == 1 || mode == 5 || mode == 7)
            check(f.env->glide.moves.empty());
        const auto &exposed = f.registry.exposed_tools();
        const auto found = std::find_if(exposed.begin(), exposed.end(), [](const auto &tool) {
            return tool.wire_name == "desktop.vision.click_region";
        });
        check(found != exposed.end() && found->version == mira::SemanticVersion{4, 0, 0});
        check(!found->parameters_schema.root.find("properties")->find("glide_ms"));
    }
}

} // namespace

int main() {
    try {
        std::ifstream golden_file(std::filesystem::path{__FILE__}.parent_path() /
                                  "data/automatic-pointer-motion-v2.json");
        const std::string golden_source{std::istreambuf_iterator<char>{golden_file}, {}};
        const auto golden = mira::parse_json(golden_source);
        check(bool(golden));
        check(golden.value().find("policy_version")->as_integer() ==
              d::pointer_motion_policy::version);
        check(golden.value().find("action_timeout_ms")->as_integer() ==
              d::pointer_motion_policy::action_timeout.count());
        for (const auto &item : *golden.value().find("cases")->as_array()) {
            const auto to = static_cast<std::int32_t>(*item.find("distance")->as_integer());
            const auto width = static_cast<std::int32_t>(*item.find("width")->as_integer());
            check(d::automatic_pointer_duration({0, 0}, {to, 0}, width).count() ==
                  *item.find("duration_ms")->as_integer());
        }
        kairo::Executor executor;
        kairo::ExecutorConfig options;
        options.min_threads = options.max_threads = 3;
        check(static_cast<bool>(executor.initialize(options)));
        click_glide_matrix(executor);
        region_glide_matrix();
        executor.shutdown(true);
    } catch (const std::exception &error) {
        std::fprintf(stderr, "visual glide fixture: %s\n", error.what());
        return 1;
    }
    return mirage::testing::finish("visual_glide_tools");
}
