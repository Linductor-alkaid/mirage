#include "../../integration/mira/src/desktop_action_verification.hpp"
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../support/fake_desktop_environment.hpp"
#include "../support/provider_tool_name.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <mira/model_digest.hpp>
#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/model_layer.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>
#include <source_location>
#include <stdexcept>

namespace d = mirage::desktop;
namespace i = mirage::integration;
namespace detail = i::detail;
using Json = mira::JsonValue;
namespace {
int checks = 0;
void check(bool value, const std::source_location where = std::source_location::current()) {
    ++checks;
    if (!value)
        throw std::runtime_error("visual marks check at line " + std::to_string(where.line()));
}
// M7-16 / DEC-083 injection helper: a 4px-cell checkerboard replaces the
// subject's texture inside the rect, so the frame fingerprint and the block
// diffs both move (a replaced control). The old uniform redraws only shifted
// brightness — the gradient-based guard legitimately passes those, so
// rejection scenarios must inject structure inside the operated region.
void paint_structural(mirage::desktop::ImageFrame &frame, int x0, int y0, int x1, int y1) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto value = static_cast<std::uint8_t>(((x / 4 + y / 4) % 2) ? 230 : 25);
            const auto offset =
                static_cast<std::size_t>(y) * frame.stride + static_cast<std::size_t>(x) * 4;
            frame.pixels[offset] = frame.pixels[offset + 1] = frame.pixels[offset + 2] = value;
            frame.pixels[offset + 3] = 255;
        }
}
mira::OperationContext owner() {
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.task_epoch = 3;
    return ctx;
}
mira::ToolExecutionRecord invoke(mira::BuiltinToolRegistry &registry, const std::string &name,
                                 const Json &args, const mira::OperationContext &ctx) {
    for (const auto &spec : registry.exposed_tools())
        if (spec.wire_name == name) {
            mira::ToolProposal proposal;
            proposal.tool_id = spec.tool_id;
            proposal.tool_version = spec.version;
            proposal.wire_name = name;
            proposal.has_side_effects = spec.has_side_effects;
            proposal.operation_id = mira::OperationId::generate();
            proposal.provider_call_id.value = "visual-call";
            proposal.arguments = args;
            proposal.arguments_digest = mira::canonical_json_digest(args);
            auto result = registry.execute(proposal, ctx);
            if (!result && result.error().code == mira::ErrorCode::Cancelled) {
                mira::ToolExecutionRecord cancelled;
                cancelled.failed = true;
                cancelled.safe_error_summary = result.error().safe_message;
                return cancelled;
            }
            if (!result)
                std::cerr << result.error().safe_message << "\n";
            check(static_cast<bool>(result));
            return result.value();
        }
    throw std::runtime_error("visual tool missing: " + name);
}
class FaultInput final : public d::InputProvider {
  public:
    d::InputProvider *delegate = nullptr;
    int mode = 0, calls = 0;
    bool paths = false;
    d::PointerActionScope last_scope;
    d::PointerPoint from, to;
    int horizontal = 0, vertical = 0;
    std::function<void()> after_pointer;
    std::uint32_t last_click_count = 0;
    std::string last_button;
    d::InputOutcome drag_with_button(const d::PointerActionScope &scope, d::PointerPoint start,
                                     d::PointerPoint finish, const d::MouseButton &button,
                                     const d::InputLimits &limits,
                                     const d::CancelToken &cancel) override {
        last_button = button;
        return drag_to(scope, start, finish, limits, cancel);
    }
    d::InputOutcome click_sequence_at(std::int32_t x, std::int32_t y, const d::MouseButton &button,
                                      std::uint32_t count, const d::InputLimits &limits,
                                      const d::CancelToken &cancel) override {
        // Adapter fixture records forwarding; platform tests cover physical repetition.
        last_click_count = count;
        last_button = button;
        return click_at(x, y, button, limits, cancel);
    }
    bool complete_actions_supported() const override { return mode != 4; }
    bool bounded_pointer_actions_supported() const override { return paths; }
    d::InputOutcome pointer_result(const d::CancelToken &cancel) {
        ++calls;
        if (mode == 1)
            throw std::runtime_error("pointer exception");
        d::InputOutcome out;
        out.side_effect_may_have_occurred = true;
        out.input_released = mode != 2;
        if (mode == 2 || mode == 3)
            out.error = {"io_error", "pointer fault"};
        else if (mode == 5) {
            auto stopping = cancel;
            stopping.request_cancel();
            out.cancelled = true;
            out.error = {"cancelled", "running cancellation"};
        } else {
            out.ok = true;
            if (after_pointer)
                after_pointer();
        }
        return out;
    }
    d::InputOutcome scroll_at(const d::PointerActionScope &scope, d::PointerPoint at,
                              std::int32_t h, std::int32_t v, const d::InputLimits &,
                              const d::CancelToken &cancel) override {
        last_scope = scope;
        from = to = at;
        horizontal = h;
        vertical = v;
        return pointer_result(cancel);
    }
    d::InputOutcome drag_to(const d::PointerActionScope &scope, d::PointerPoint start,
                            d::PointerPoint end, const d::InputLimits &,
                            const d::CancelToken &cancel) override {
        last_scope = scope;
        from = start;
        to = end;
        return pointer_result(cancel);
    }
    d::InputOutcome click_at(std::int32_t x, std::int32_t y, const d::MouseButton &button,
                             const d::InputLimits &limits, const d::CancelToken &cancel) override {
        ++calls;
        if (mode == 5) {
            delegate->pointer_move(x, y, limits, cancel);
            delegate->pointer_button(button, true, limits, cancel);
            auto running = cancel;
            running.request_cancel();
            const auto released = delegate->pointer_button(button, false, limits, d::CancelToken{});
            d::InputOutcome out;
            out.cancelled = true;
            out.error = {"cancelled", "injected running cancellation"};
            out.side_effect_may_have_occurred = true;
            out.input_released = released.ok;
            return out;
        }
        if (mode == 1)
            throw std::runtime_error("injected input exception");
        if (mode == 2 || mode == 3) {
            d::InputOutcome out;
            out.error = {"io_error", "injected input failure"};
            out.input_released = mode == 3;
            out.side_effect_may_have_occurred = true;
            return out;
        }
        return delegate->click_at(x, y, button, limits, cancel);
    }
    d::InputOutcome inject_key(const d::KeySym &, bool, const d::InputLimits &,
                               const d::CancelToken &) override {
        return {};
    }
    d::InputOutcome type_text(const std::string &, const d::InputLimits &,
                              const d::CancelToken &) override {
        return {};
    }
    d::InputOutcome pointer_move(std::int32_t, std::int32_t, const d::InputLimits &,
                                 const d::CancelToken &) override {
        return {};
    }
    d::InputOutcome pointer_button(const d::MouseButton &, bool, const d::InputLimits &,
                                   const d::CancelToken &) override {
        return {};
    }
    d::PointerQueryOutcome pointer_position(const d::CancelToken &) override { return {}; }
};
class Environment final : public d::DesktopEnvironment {
  public:
    mirage::testing::FakeDesktopEnvironment fake;
    FaultInput injector;
    Environment() { injector.delegate = fake.input(); }
    d::EnvironmentInfo info() const override { return {"visual-fixture", "test"}; }
    d::ScreenProvider *screen() override { return fake.screen(); }
    d::WindowProvider *window() override { return fake.window(); }
    d::InputProvider *input() override { return &injector; }
    // Deliberately no AccessibilityProvider: numbered visual targets only.
};
struct Fixture {
    std::shared_ptr<Environment> env = std::make_shared<Environment>();
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
    int mark_calls = 0;
    bool allow_capture = true, allow_input = true;
    std::function<void()> on_authorize;
    explicit Fixture(kairo::Executor &executor, bool mutations = true, std::string label = "button",
                     std::string capture_scope = {}, bool scoped_geometry = false,
                     bool regional_capture = false, bool pointer_paths = false) {
        static int sequence = 0;
        env->injector.paths = pointer_paths;
        env->fake.displays = {{"d1", {-200, -40, 128, 80}, true}};
        env->fake.windows = {{"w1", "Before", {-200, -40, 128, 80}, true}};
        if (scoped_geometry) {
            // DEC-062: the display is strictly larger than the scope window,
            // so the pre-change whole-display default would supply pixels the
            // window scope must withhold; the fake OCR region stays inside.
            env->fake.displays = {{"d1", {-200, -40, 240, 140}, true}};
            env->fake.windows = {{"w1", "Before", {-190, -30, 128, 80}, true}};
        }
        i::FakeOcrConfig backend;
        backend.info.accepted_formats = {mirador::PixelFormat::kBgra8};
        backend.regions = {{{20, 20, 40, 20}, std::move(label), 0.9f, {}}};
        if (pointer_paths)
            backend.regions.push_back({{80, 20, 20, 20}, "destination", 0.9f, {}});
        ocr = std::make_unique<i::FakeOcrBackend>(backend);
        i::VisualObservationPipelineConfig cfg;
        cfg.source_id = "visual-mark-test-" + std::to_string(++sequence);
        cfg.run_ocr = true;
        cfg.roi_capture = regional_capture;
        cfg.refresh_ocr = true;
        cfg.session.ocr_backend = ocr.get();
        pipeline =
            std::make_unique<i::VisualObservationPipeline>(executor, *env->screen(), visual, cfg);
        std::string failure;
        check(pipeline->start(failure));
        tools.environment = env;
        tools.actions = std::make_shared<d::ActionCoordinator>();
        tools.gate = [this](const auto &cap, const auto &, const auto &probe) {
            if (on_authorize)
                on_authorize();
            return !probe() && (cap == "screen.capture" ? allow_capture : allow_input);
        };
        tools.mark = [this](const auto &crop, const auto &token, const auto &end) {
            ++mark_calls;
            return pipeline->refresh_marked(crop, token, end);
        };
        tools.with_visual_reference = [this](const auto &scope, const auto &ref,
                                             const auto &action) {
            return visual.with_reference(scope, ref, action);
        };
        tools.with_visual_references = [this](const auto &scope, const auto &from, const auto &to,
                                              const auto &action) {
            return visual.with_references(scope, from, to, action);
        };
        // DEC-062: the turn copies its context, so the scope must be set
        // before the turn exists.
        tools.capture_scope = std::move(capture_scope);
        turn = std::make_unique<detail::DesktopImageTurn>(source, tools, ctx, cancel);
        turn->register_tool(registry, {}, mutations);
    }
    ~Fixture() { pipeline->stop(); }
    mira::ToolExecutionRecord mark(const Json &args = Json{Json::Object{}}) {
        return invoke(registry, "desktop.vision.mark", args, ctx);
    }
    Json target(const Json &marked) {
        return Json{Json::Object{{"visual_scope", *marked.find("visual_scope")},
                                 {"environment_epoch", *marked.find("environment_epoch")},
                                 {"mark", std::int64_t{1}},
                                 {"window_id", "w1"},
                                 {"verify_kind", "window_focused"}}};
    }
    mira::ToolExecutionRecord click(const Json &target) {
        return invoke(registry, "desktop.vision.click", target, ctx);
    }
};
void contracts(kairo::Executor &executor) {
    for (const auto count : {1u, 2u, 3u, 5u, d::max_click_count}) {
        Fixture f(executor);
        const auto marked = f.mark();
        auto args = f.target(marked.result);
        args.set("click_count", static_cast<std::int64_t>(count));
        args.set("button", count % 2 ? "right" : "middle");
        check(!f.click(args).failed);
        check(f.env->injector.last_button == (count % 2 ? "right" : "middle"));
        check(f.env->injector.calls == 1 && f.env->injector.last_click_count == count);
    }
    for (const auto count : {0u, d::max_click_count + 1}) {
        Fixture f(executor);
        const auto marked = f.mark();
        auto args = f.target(marked.result);
        args.set("click_count", static_cast<std::int64_t>(count));
        check(f.click(args).failed && f.env->injector.calls == 0);
    }
    {
        Fixture f(executor);
        check(f.env->accessibility() == nullptr);
        auto marked = f.mark();
        check(!marked.failed);
        check(*marked.result.find("origin_x")->as_integer() == -200);
        check(marked.result.find("marks")->as_array()->size() == 1);
        check(*marked.result.find("marks")->as_array()->front().find("selectable")->as_boolean());
        mira::AgentLoopSpec spec;
        spec.task_id = f.ctx.task;
        spec.session_id = f.ctx.session;
        spec.task_epoch = f.ctx.task_epoch;
        auto supplied = f.turn->supply(spec);
        check(supplied && supplied.value().size() == 1);
        const auto image = std::get<mira::ImagePart>(supplied.value().front().content.back());
        check(image.source.sensitivity == mira::Sensitivity::Sensitive);
        check(static_cast<bool>(f.source->fetch(image.source)));
        auto clicked = f.click(f.target(marked.result));
        check(!clicked.failed && !*clicked.result.find("verified")->as_boolean());
        check(f.env->fake.pointer.x == -160 && f.env->fake.pointer.y == -10);
        check(f.env->fake.input_log ==
              std::vector<std::string>{"move -160,-10", "press left", "release left"});
        check(f.click(f.target(marked.result)).failed && f.env->injector.calls == 1);
        auto retired = f.turn->supply(spec);
        check(retired && !f.source->fetch(image.source));
    }
    {
        Fixture f(executor);
        auto crop = mira::parse_json(R"({"x":-185,"y":-25,"width":50,"height":30})").value();
        auto marked = f.mark(crop);
        check(!marked.failed && *marked.result.find("width")->as_integer() == 50);
        check(!f.click(f.target(marked.result)).failed);
        check(f.env->fake.pointer.x == -160 && f.env->fake.pointer.y == -10);
    }
    {
        Fixture f(executor);
        auto marked =
            f.mark(mira::parse_json(R"({"x":-170,"y":-25,"width":40,"height":30})").value());
        check(!marked.failed);
        check(!*marked.result.find("marks")->as_array()->front().find("selectable")->as_boolean());
        check(f.click(f.target(marked.result)).failed && f.env->injector.calls == 0);
    }
    for (int scenario = 0; scenario < 13; ++scenario) {
        Fixture f(executor);
        auto marked = f.mark();
        check(!marked.failed);
        auto target = f.target(marked.result);
        switch (scenario) {
        case 0:
            f.visual.clear();
            break;
        case 1:
            check(f.visual.publish(f.visual.current()).ok);
            break; // Same @v1, new scope.
        case 2:
            // Structural replacement of the marked subject (the fake OCR
            // region {20,20,40,20} in frame coordinates).
            f.env->fake.capture_frame_edit = [](auto &frame) {
                paint_structural(frame, 20, 20, 60, 40);
            };
            break;
        case 3:
            f.env->fake.windows.front().geometry.x += 1;
            break;
        case 4:
            f.env->fake.displays.front().geometry.width += 1;
            break;
        case 5:
            f.env->fake.windows.front().focused = false;
            break;
        case 6:
            f.allow_input = false;
            break;
        case 7:
            f.allow_capture = false;
            break;
        case 8:
            f.cancel.request_cancel();
            break;
        case 9:
            f.tools.actions->takeover();
            break;
        case 10:
            f.on_authorize = [&] { f.tools.actions->takeover(); };
            break;
        case 12:
            f.env->fake.displays.front().primary = false;
            break;
        case 11:
            f.ctx.deadline = std::chrono::steady_clock::now() - std::chrono::seconds{1};
            break;
        }
        check(f.click(target).failed && f.env->injector.calls == 0);
    }
    for (int race = 0; race < 2; ++race) {
        Fixture f(executor);
        auto marked = f.mark();
        check(!marked.failed);
        f.env->fake.capture_frame_edit = [&](auto &) {
            if (race == 0)
                f.env->fake.windows.front().geometry.y += 1;
            else
                f.tools.actions->takeover();
        };
        check(f.click(f.target(marked.result)).failed && f.env->injector.calls == 0);
    }
    for (const int failure : {1, 2, 3, 5}) {
        Fixture f(executor);
        auto marked = f.mark();
        check(!marked.failed);
        f.env->injector.mode = failure;
        detail::DesktopActionVerification verification(f.tools, f.ctx, f.cancel);
        mira::BuiltinToolRegistry wrapped;
        f.turn->register_tool(wrapped,
                              [&](const std::string &name, mira::BuiltinToolHandler handler) {
                                  return [&, name, handler](const auto &args, const auto &ctx) {
                                      return detail::DesktopActionVerification::mutation(name)
                                                 ? verification.execute(name, args, ctx, handler)
                                                 : handler(args, ctx);
                                  };
                              });
        auto failed = invoke(wrapped, "desktop.vision.click", f.target(marked.result), f.ctx);
        check(failed.failed && !verification.settle());
        check(f.tools.actions->input_released() == (failure == 3 || failure == 5));
        check(invoke(wrapped, "desktop.vision.click", f.target(marked.result), f.ctx).failed);
        check(f.env->injector.calls == 1); // Uncertain action cannot be resent.
    }
    {
        Fixture f(executor);
        f.allow_capture = false;
        check(f.mark().failed && f.mark_calls == 0 && f.env->fake.capture_calls == 0);
        f.allow_capture = true;
        auto marked = f.mark();
        check(!marked.failed);
        auto foreign = f.ctx;
        foreign.session = mira::SessionId::generate();
        check(invoke(f.registry, "desktop.vision.click", f.target(marked.result), foreign).failed);
        check(f.env->injector.calls == 0);
        check(f.mark(mira::parse_json(R"({"x":0})").value()).failed);
        check(f.click(f.target(marked.result)).failed); // Failed mark retired prior image.
    }
    {
        Fixture f(executor);
        for (int k = 0; k < 4; ++k)
            check(!f.mark().failed);
        check(f.mark().failed && f.mark_calls == 4);
        f.pipeline->stop();
        check(!f.pipeline->refresh_marked({}, {}, {}).analyzed);
    }
    {
        Fixture f(executor);
        auto marked = f.mark();
        check(!marked.failed);
        auto expired = executor.submit_delayed(5050, [] {});
        expired.future.get();
        check(f.click(f.target(marked.result)).failed && f.env->injector.calls == 0);
        check(f.env->fake.capture_calls == 1);
    }
    {
        Fixture f(executor, true, std::string(1025, 'x'));
        check(f.mark().failed && f.env->injector.calls == 0);
    }
    {
        Fixture f(executor);
        f.env->injector.mode = 4;
        mira::BuiltinToolRegistry unsupported;
        f.turn->register_tool(unsupported, {});
        const auto exposed = unsupported.exposed_tools();
        check(std::none_of(exposed.begin(), exposed.end(),
                           [](const auto &t) { return t.wire_name == "desktop.vision.click"; }));
    }
    {
        Fixture f(executor, false);
        const auto exposed = f.registry.exposed_tools();
        check(std::none_of(exposed.begin(), exposed.end(),
                           [](const auto &t) { return t.wire_name == "desktop.vision.click"; }));
    }
}
// Actual pinned geometry extraction, with no OCR/detection backend or supplied boxes.
void geometric_marks(kairo::Executor &executor) {
    mirage::testing::FakeDesktopEnvironment env;
    env.displays = {{"d1", {-200, -40, 240, 140}, true}};
    env.capture_frame_edit = [](auto &frame) {
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x) {
                const bool left = x >= 25 && x <= 85 && y >= 35 && y <= 95;
                const bool right = x >= 145 && x <= 205 && y >= 35 && y <= 95;
                const bool frame_border = ((x == 2 || x == 237) && y >= 2 && y <= 137) ||
                                          ((y == 2 || y == 137) && x >= 2 && x <= 237);
                const auto value =
                    static_cast<std::uint8_t>(left || right || frame_border ? 0 : 255);
                const auto offset = static_cast<std::size_t>(y) * frame.stride + x * 4;
                frame.pixels[offset] = frame.pixels[offset + 1] = frame.pixels[offset + 2] = value;
            }
    };
    d::VisualReferenceRegistry registry;
    i::VisualObservationPipelineConfig disabled;
    disabled.source_id = "geometry-disabled";
    i::VisualObservationPipeline off(executor, *env.screen(), registry, disabled);
    std::string failure;
    check(off.start(failure));
    const auto none = off.refresh_marked({}, {}, {});
    check(none.analyzed && none.marks.empty());
    off.stop();
    auto config = disabled;
    config.source_id = "geometry-enabled";
    config.geometry_marks = true;
    i::FakeOcrConfig empty_config;
    empty_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    empty_config.regions = {{{0, 0, 240, 140}, "", 0.99f, {}}};
    i::FakeOcrBackend empty_text(empty_config);
    config.run_ocr = true;
    config.refresh_ocr = true;
    config.session.ocr_backend = &empty_text;
    i::VisualObservationPipeline on(executor, *env.screen(), registry, config);
    check(on.start(failure));
    const auto marked = on.refresh_marked({}, {}, {});
    if (!marked.analyzed)
        std::cerr << marked.analysis_error.message << '\n';
    check(marked.analyzed && marked.marks.size() == 2);
    for (std::size_t n = 0; n < marked.marks.size(); ++n) {
        const auto &region = marked.publish.snapshot.regions[n];
        check(region.source == d::VisualRegionSource::kGeometry);
        check(region.text.empty() && region.template_id.empty());
        check(marked.marks[n].selectable && marked.marks[n].ref == region.ref);
        check(region.bounds.x < 10 && region.bounds.y < 0);
    }
    // Ordinary observation stays free of geometry even when marked mode is enabled.
    check(on.refresh(true, {}, {}).publish.snapshot.regions.empty());
    const auto clipped = on.refresh_marked(d::WindowGeometry{-175, -5, 30, 60}, {}, {});
    check(clipped.analyzed);
    check(std::none_of(clipped.marks.begin(), clipped.marks.end(),
                       [](const auto &m) { return m.selectable; }));
    d::CancelToken cancel;
    cancel.request_cancel();
    const auto captures = env.capture_calls;
    check(on.refresh_marked({}, cancel, {}).capture_cancelled);
    check(env.capture_calls == captures);
    check(!on.refresh_marked({}, {}, std::chrono::steady_clock::now()).analyzed);
    on.stop();
    check(!on.running() && !on.refresh_marked({}, {}, {}).analyzed);
    // The proposal budget fails the whole analysis rather than dropping a candidate.
    i::VisualSessionHost host(executor, "geometry-budget");
    check(host.start(failure));
    auto pixels = env.screen()->capture_display("d1", {}, {});
    i::VisualAnalysisRequest request;
    request.frame =
        i::to_mirador_frame(pixels.frame, "geometry-budget", 1, std::chrono::steady_clock::now())
            .value();
    request.geometry.run = true;
    request.geometry.proposal.max_proposals = 1;
    const auto rejected = host.submit(std::move(request)).get();
    check(rejected.outcome.kind == i::VisualAnalysisOutcome::Kind::kFailed &&
          rejected.outcome.code == mirador::ErrorCode::kBudgetExceeded && !rejected.snapshot);
    for (const double invalid : {0.0, -0.1, 1.1, std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()}) {
        i::VisualAnalysisRequest bad;
        bad.frame = i::to_mirador_frame(pixels.frame, "geometry-budget", 2,
                                        std::chrono::steady_clock::now())
                        .value();
        bad.geometry.run = true;
        bad.geometry.max_fused_area_fraction = invalid;
        check(host.submit(std::move(bad)).get().outcome.kind ==
              i::VisualAnalysisOutcome::Kind::kRejected);
    }
    host.stop();
}
// MIRADOR-20261016-001 / DEC-060: a 2px desktop outline must produce a
// selectable whole-region mark; the old product profile returned no lines.
void thin_outline_marks(kairo::Executor &executor) {
    mirage::testing::FakeDesktopEnvironment env;
    env.displays = {{"d1", {-200, -40, 300, 180}, true}};
    bool visible = true;
    env.capture_frame_edit = [&](auto &frame) {
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x) {
                const bool inside = x >= 70 && x < 220 && y >= 50 && y < 130;
                const bool edge = x < 72 || x >= 218 || y < 52 || y >= 128;
                const auto value = static_cast<std::uint8_t>(visible && inside && edge ? 0 : 255);
                const auto offset = static_cast<std::size_t>(y) * frame.stride + x * 4;
                frame.pixels[offset] = frame.pixels[offset + 1] = frame.pixels[offset + 2] = value;
            }
    };
    d::VisualReferenceRegistry registry;
    i::VisualObservationPipelineConfig config;
    config.source_id = "thin-outline-acceptance";
    config.geometry_marks = true;
    i::VisualObservationPipeline pipeline(executor, *env.screen(), registry, config);
    std::string error;
    check(pipeline.start(error));
    const auto marked = pipeline.refresh_marked({}, {}, {});
    check(marked.analyzed && marked.marks.size() == 1 && marked.marks.front().selectable);
    const auto &bounds = marked.publish.snapshot.regions.front().bounds;
    // Proposals bound fitted edge centers, not every outer ink pixel.
    check(std::abs(bounds.x + 130) <= 2 && std::abs(bounds.y - 10) <= 2 &&
          std::abs(bounds.width - 150) <= 4 && std::abs(bounds.height - 80) <= 4);
    visible = false;
    const auto removed = pipeline.refresh_marked({}, {}, {});
    check(removed.analyzed && removed.marks.empty());
    pipeline.stop();
}
void configured_template_marks(kairo::Executor &executor) {
    mirage::testing::FakeDesktopEnvironment env;
    env.displays = {{"d1", {-200, -40, 240, 140}, true}};
    bool swapped = false;
    env.capture_frame_edit = [&](auto &frame) {
        for (int y = 0; y < frame.height; ++y)
            for (int x = 0; x < frame.width; ++x) {
                const int start = x < 120 ? 25 : 145;
                const bool button = x >= start && x <= start + 60 && y >= 35 && y <= 95;
                const bool inner = x >= start + 15 && x <= start + 45 && y >= 50 && y <= 80;
                const bool cross = (x >= 120) != swapped;
                const bool glyph = inner && (std::abs((x - start) - (y - 35)) <= 1 ||
                                             (cross && std::abs((x - start) + (y - 35) - 60) <= 1));
                const auto value = static_cast<std::uint8_t>(button && !glyph ? 0 : 255);
                const auto offset = static_cast<std::size_t>(y) * frame.stride + x * 4;
                frame.pixels[offset] = frame.pixels[offset + 1] = frame.pixels[offset + 2] = value;
            }
    };
    const auto pixels = env.screen()->capture_display("d1", {}, {}).frame;
    const auto asset = [&](int x, const std::string &id) {
        i::VisualTemplateAsset result;
        result.template_id = id;
        auto &p = result.pixels;
        p.width = p.height = 62;
        p.stride = 62 * 4;
        for (int y = 34; y < 96; ++y) {
            const auto begin =
                pixels.pixels.begin() + static_cast<std::ptrdiff_t>(y * pixels.stride + x * 4);
            p.pixels.insert(p.pixels.end(), begin, begin + 62 * 4);
        }
        return result;
    };
    i::VisualObservationPipelineConfig config;
    config.source_id = "configured-templates";
    config.geometry_marks = true;
    config.session.template_assets = {asset(24, "line"), asset(144, "cross")};
    d::VisualReferenceRegistry registry;
    i::VisualObservationPipeline pipeline(executor, *env.screen(), registry, config);
    std::string failure;
    check(pipeline.start(failure));
    auto first = pipeline.refresh_marked({}, {}, {});
    check(first.analyzed);
    const auto matched = [&](const auto &snapshot, const std::string &id) -> d::VisualRegionEntry {
        std::vector<d::VisualRegionEntry> matches;
        for (const auto &region : snapshot.regions)
            if (region.template_id == id)
                matches.push_back(region);
        if (matches.size() != 1)
            std::cerr << "configured match " << id << " count=" << matches.size() << '\n';
        check(matches.size() == 1);
        check(matches.front().source == d::VisualRegionSource::kTemplate &&
              matches.front().text.empty());
        return matches.front();
    };
    check(matched(first.publish.snapshot, "line").bounds.x < -100);
    check(matched(first.publish.snapshot, "cross").bounds.x > -100);
    swapped = true;
    auto moved = pipeline.refresh_marked({}, {}, {});
    check(moved.analyzed && first.publish.snapshot.scope_ref != moved.publish.snapshot.scope_ref);
    check(matched(moved.publish.snapshot, "cross").bounds.x < -100);
    check(matched(moved.publish.snapshot, "line").bounds.x > -100);
    check(pipeline.refresh(true, {}, {}).publish.snapshot.regions.empty());
    d::CancelToken cancel;
    cancel.request_cancel();
    check(pipeline.refresh_marked({}, cancel, {}).capture_cancelled);
    check(!pipeline.refresh_marked({}, {}, std::chrono::steady_clock::now()).analyzed);
    pipeline.stop();
    check(!pipeline.running() && !pipeline.refresh_marked({}, {}, {}).analyzed);

    // Equal visual appearances under different IDs must remain geometric evidence.
    auto ambiguous = config;
    ambiguous.source_id = "ambiguous-templates";
    ambiguous.session.template_assets[1].pixels = ambiguous.session.template_assets[0].pixels;
    i::VisualObservationPipeline duplicate(executor, *env.screen(), registry, ambiguous);
    check(duplicate.start(failure));
    auto uncertain = duplicate.refresh_marked({}, {}, {});
    check(uncertain.analyzed && !uncertain.marks.empty());
    for (const auto &r : uncertain.publish.snapshot.regions)
        check(r.template_id.empty() && r.source == d::VisualRegionSource::kGeometry);
    duplicate.stop();
    i::FakeOcrConfig ocr_config;
    ocr_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    ocr_config.regions = {{{0, 0, 240, 140}, "caption", 0.99f, {}}};
    i::FakeOcrBackend ocr(ocr_config);
    auto mixed_config = config;
    mixed_config.source_id = "conflicting-templates";
    mixed_config.run_ocr = mixed_config.refresh_ocr = true;
    mixed_config.session.ocr_backend = &ocr;
    i::VisualObservationPipeline conflict(executor, *env.screen(), registry, mixed_config);
    check(conflict.start(failure));
    const auto merged = conflict.refresh_marked({}, {}, {});
    check(merged.analyzed && merged.publish.snapshot.regions.size() == 1);
    check(merged.publish.snapshot.regions.front().template_id.empty());
    check(merged.publish.snapshot.regions.front().source == d::VisualRegionSource::kOcr &&
          merged.publish.snapshot.regions.front().text == "caption");
    conflict.stop();
    auto enlarged_config = mixed_config;
    enlarged_config.source_id = "enlarged-template";
    enlarged_config.session.template_assets.resize(1);
    i::VisualObservationPipeline enlarged(executor, *env.screen(), registry, enlarged_config);
    check(enlarged.start(failure));
    const auto oversized = enlarged.refresh_marked({}, {}, {});
    check(oversized.analyzed && oversized.publish.snapshot.regions.size() == 1 &&
          oversized.publish.snapshot.regions.front().template_id.empty());
    enlarged.stop();
    ocr_config.regions = {{{24, 34, 62, 62}, "caption", 0.99f, {}}};
    i::FakeOcrBackend single_ocr(ocr_config);
    mixed_config.source_id = "mixed-template-ocr";
    mixed_config.session.ocr_backend = &single_ocr;
    i::VisualObservationPipeline mixed(executor, *env.screen(), registry, mixed_config);
    check(mixed.start(failure));
    const auto combined = mixed.refresh_marked({}, {}, {});
    check(combined.analyzed);
    bool preserved = false;
    for (const auto &r : combined.publish.snapshot.regions)
        if (r.source == d::VisualRegionSource::kOcr && r.text == "caption") {
            check(r.template_id == "cross");
            preserved = true;
        }
    check(preserved);
    mixed.stop();
    for (int mode = 0; mode < 6; ++mode) {
        auto bad = config.session;
        if (mode == 0)
            bad.template_assets[1].template_id = bad.template_assets[0].template_id;
        if (mode == 1)
            bad.template_assets[0].pixels.pixels.pop_back();
        if (mode == 2)
            bad.template_assets.resize(17, bad.template_assets.front());
        if (mode == 3)
            bad.templates.max_bytes = 1;
        if (mode == 4)
            bad.template_assets[0].template_id = "bad identity";
        if (mode == 5)
            bad.templates.thumb_side = std::numeric_limits<std::int32_t>::max();
        i::VisualSessionHost host(executor, "invalid-assets-" + std::to_string(mode), bad);
        check(!host.start(failure) && !failure.empty());
    }
    i::VisualSessionHost host(executor, "template-admission", config.session);
    check(host.start(failure));
    auto request = i::VisualAnalysisRequest{};
    request.frame =
        i::to_mirador_frame(pixels, "template-admission", 1, std::chrono::steady_clock::now())
            .value();
    request.probe_geometry_templates = true;
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kRejected);
    request.geometry.run = true;
    request.template_reuse_threshold = 0.95;
    request.template_query.perceptual_similarity_threshold = 0.95;
    request.template_query.template_ncc_threshold = 0.95;
    request.template_query.max_candidates = 1;
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kRejected);
    request.template_query.max_candidates = 16;
    request.enrollments = {{"foreign", {1, 1, 8, 8}}};
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kRejected);
    request.enrollments.clear();
    request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    int probes = 0;
    request.external_cancel = d::CancelToken([&] { return ++probes > 2; });
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kCancelled);
    request.external_cancel = {};
    const auto recovered = host.submit(request).get();
    check(recovered.outcome.kind == i::VisualAnalysisOutcome::Kind::kCompleted &&
          recovered.template_hits.size() == 2 && recovered.cache_stats.template_index_entries == 2);
    request.deadline = std::chrono::steady_clock::now();
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kTimedOut);
    host.stop();
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kRejected);
    auto insufficient = config.session;
    insufficient.templates.fingerprint_max_bytes = 1;
    i::VisualSessionHost failing(executor, "template-fingerprint-failure", insufficient);
    check(failing.start(failure));
    request.frame = i::to_mirador_frame(pixels, "template-fingerprint-failure", 1,
                                        std::chrono::steady_clock::now())
                        .value();
    request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    const auto failed = failing.submit(request).get();
    check(failed.outcome.kind == i::VisualAnalysisOutcome::Kind::kFailed &&
          failed.outcome.code == mirador::ErrorCode::kBudgetExceeded && !failed.snapshot &&
          failed.template_hits.empty());
    failing.stop();
}
void template_marks(kairo::Executor &executor) {
    mirage::testing::FakeDesktopEnvironment env;
    env.displays = {{"d1", {0, 0, 128, 80}, true}};
    const auto captured = env.screen()->capture_display("d1", {}, {});
    check(captured.ok);
    const auto frame =
        i::to_mirador_frame(captured.frame, "template-marks", 1, std::chrono::steady_clock::now());
    check(static_cast<bool>(frame));
    i::FakeDetectorConfig detector_config;
    detector_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    detector_config.regions = {{{20, 20, 40, 20}, 0, "icon", 0.9f}};
    i::FakeDetectorBackend detector(detector_config);
    i::VisualSessionConfig config;
    config.detector_backend = &detector;
    i::VisualSessionHost host(executor, "template-marks", config);
    std::string failure;
    check(host.start(failure));
    i::VisualAnalysisRequest request;
    request.frame = frame.value();
    request.enrollments = {{"fixture-icon", {20, 20, 40, 20}}};
    request.run_detector = true;
    request.probe_templates = true;
    request.fuse = true;
    request.display_transform = mirador::make_translation(
        -200, -40, mirador::CoordinateSpaceId::kOriented, mirador::CoordinateSpaceId::kDisplay);
    request.render_marks = true;
    request.mark_roi = mirador::RectI{15, 15, 50, 30};
    request.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    auto result = host.submit(request).get();
    check(result.outcome.kind == i::VisualAnalysisOutcome::Kind::kCompleted);
    check(result.template_hits.size() == 1 &&
          result.template_hits.front().template_id == "fixture-icon");
    check(result.snapshot && result.snapshot->regions.front().label == "fixture-icon");
    check(result.marked_frame.width == 50 && result.marked_frame.height == 30);
    check(result.marks.size() == 1 && result.marks.front().selectable);
    check(result.marks.front().anchor.x == -160 && result.marks.front().anchor.y == -10);
    request.mark_roi = mirador::RectI{127, 0, 2, 10};
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kFailed);
    request.external_cancel.request_cancel();
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kCancelled);
    request.fuse = false;
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kRejected);
    host.stop();
    check(host.submit(request).get().outcome.kind == i::VisualAnalysisOutcome::Kind::kRejected);
}
class Script final : public mira::IModelProvider {
  public:
    explicit Script(mira::ModelProfile profile, Fixture &fixture, int failure)
        : profile_(std::move(profile)), fixture_(fixture), failure_(failure) {}
    const mira::ModelProfile &profile() const override { return profile_; }
    int requests = 0, images = 0;
    bool saw_verified = false;
    mira::Result<mira::ModelResponse> infer(const mira::ModelRequest &request,
                                            const mira::OperationContext &,
                                            const mira::ProviderInferOptions &) override {
        ++requests;
        const Json *mark_result = nullptr;
        for (const auto &item : request.input)
            for (const auto &part : item.content) {
                if (std::get_if<mira::ImagePart>(&part))
                    ++images;
                if (const auto *tool = std::get_if<mira::ToolResultPart>(&part)) {
                    if (!tool->failed && tool->result.find("visual_scope") &&
                        tool->result.find("marks"))
                        mark_result = &tool->result;
                    if (const auto *v = tool->result.find("verified");
                        v && v->as_boolean().value_or(false))
                        saw_verified = true;
                }
            }
        mira::ModelResponse response;
        response.request_id = request.request_id;
        response.operation_id = request.operation_id;
        response.profile_id = profile_.id;
        response.status = mira::ModelCompletionStatus::Completed;
        const bool call_tool = requests == 1 || (requests == 2 && failure_ != 2);
        if (call_tool) {
            const std::string name = requests == 1 ? "desktop.vision.mark" : "desktop.vision.click";
            auto exposed =
                std::find_if(request.tools.begin(), request.tools.end(), [&](const auto &t) {
                    return t.wire_name == mirage::testing::fixture_tool_name(name);
                });
            check(exposed != request.tools.end());
            mira::ToolCallOutput call;
            call.provider_call_id.value = "visual-" + std::to_string(requests);
            call.tool_id = exposed->tool_id;
            call.provider_name = exposed->wire_name;
            if (requests == 1)
                call.arguments = Json{Json::Object{}};
            else {
                check(mark_result && images == 1);
                call.arguments = fixture_.target(*mark_result);
                if (failure_ == 1)
                    // Structural replacement of the marked subject between
                    // mark and click; the layered guard must reject it.
                    fixture_.env->fake.capture_frame_edit = [](auto &frame) {
                        paint_structural(frame, 20, 20, 60, 40);
                    };
            }
            call.arguments_digest = mira::digest_string(mira::to_json_string(call.arguments));
            response.output.emplace_back(std::move(call));
        } else {
            mira::MessageOutput message;
            message.content.emplace_back(mira::OutputTextPart{"done", {}});
            response.output.emplace_back(std::move(message));
        }
        return response;
    }

  private:
    mira::ModelProfile profile_;
    Fixture &fixture_;
    int failure_;
};
void model_loop(kairo::Executor &executor) {
    for (int failure = 0; failure < 3; ++failure) {
        Fixture f(executor);
        if (failure == 2)
            f.allow_capture = false;
        std::shared_ptr<Script> script;
        i::ModelProviderOverride factory([&](const auto &profile) {
            script = std::make_shared<Script>(profile, f, failure);
            return script;
        });
        i::ModelLayerConfig config;
        config.enabled = true;
        config.model_selector = "mark-fixture";
        config.dialect = "anthropic.messages.v1";
        config.supports_images = true;
        i::ModelLayer model(executor, config, &factory, f.tools);
        const auto completed = executor
                                   .submit_auto([&] {
                                       return model.complete_harness_turn(
                                           "", "Select the numbered visual target", f.ctx);
                                   })
                                   .get();
        if (completed.ok != (failure == 0)) {
            std::cerr << "model scenario " << failure << ": " << completed.error << "\n";
            for (const auto &part : completed.parts)
                std::cerr << part.name << " " << part.status << " " << part.output << "\n";
        }
        check(completed.ok == (failure == 0));
        check(completed.failed == (failure != 0));
        check(completed.supply_degradations == 0 && completed.supplied_input_dropped == 0);
        check(f.env->injector.calls == (failure == 0 ? 1 : 0));
        if (failure == 0)
            check(script->saw_verified && script->images == 1 && completed.parts.size() == 2);
        model.shutdown();
    }
}
// A read-only refresh before the first action retires the old image and
// selection. DEC-068 permits a new scoped action after a proven no-effect rejection.
void reobserve_before_input(kairo::Executor &executor) {
    for (const bool rejected_first : {false, true}) {
        Fixture f(executor);
        detail::DesktopActionVerification verification(f.tools, f.ctx, {});
        detail::DesktopImageTurn turn(f.source, f.tools, f.ctx, {});
        mira::BuiltinToolRegistry registry;
        const i::DesktopAtomToolset::Decorator decorate = [&](const std::string &name,
                                                              mira::BuiltinToolHandler handler) {
            return [&, name, handler](const auto &args, const auto &ctx) {
                return detail::DesktopActionVerification::mutation(name)
                           ? verification.execute(name, args, ctx, handler)
                           : handler(args, ctx);
            };
        };
        turn.register_tool(registry, decorate);
        const auto first = invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, f.ctx);
        check(!first.failed);
        mira::AgentLoopSpec spec;
        spec.task_id = f.ctx.task;
        spec.session_id = f.ctx.session;
        spec.task_epoch = f.ctx.task_epoch;
        const auto supplied = turn.supply(spec);
        check(supplied && supplied.value().size() == 1);
        const auto old_image = std::get<mira::ImagePart>(supplied.value().front().content.back());
        check(static_cast<bool>(f.source->fetch(old_image.source)));
        // Structural replacement of the marked subject; the pre-change whole
        // frame only shifted brightness, which DEC-083 legitimately passes.
        f.env->fake.capture_frame_edit = [](auto &frame) {
            paint_structural(frame, 20, 20, 60, 40);
        };
        if (rejected_first) {
            const auto denied =
                invoke(registry, "desktop.vision.click", f.target(first.result), f.ctx);
            check(denied.failed &&
                  denied.safe_error_summary.find("stale_observation") != std::string::npos);
            check(f.env->injector.calls == 0 && !verification.settle());
        }
        const auto fresh = invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, f.ctx);
        check(!fresh.failed && f.mark_calls == 2);
        check(!f.source->fetch(old_image.source));
        check(*first.result.find("visual_scope")->as_string() !=
              *fresh.result.find("visual_scope")->as_string());
        const auto &old_mark = first.result.find("marks")->as_array()->front();
        const auto &new_mark = fresh.result.find("marks")->as_array()->front();
        check(*old_mark.find("mark")->as_integer() == *new_mark.find("mark")->as_integer());
        bool old_resolved = false;
        const auto retired = f.visual.with_reference(
            *first.result.find("visual_scope")->as_string(), *old_mark.find("ref")->as_string(),
            [&](const auto &) { old_resolved = true; });
        check(!retired.code.empty() && !old_resolved);
        const auto current_image = turn.supply(spec);
        check(current_image && current_image.value().size() == 1);
        const auto current =
            std::get<mira::ImagePart>(current_image.value().front().content.back());
        check(current.source.id != old_image.source.id);
        check(static_cast<bool>(f.source->fetch(current.source)));
        const auto clicked =
            invoke(registry, "desktop.vision.click", f.target(fresh.result), f.ctx);
        check(!clicked.failed && f.env->injector.calls == 1);
        check(verification.settle());
        if (!rejected_first)
            check(clicked.result.find("verified")->as_boolean().value_or(false));
    }
}
void long_visual_condition(kairo::Executor &executor) {
    const std::string expected(1024, 'A');
    Fixture f(executor, true, expected);
    f.tools.ocr = [&](const auto &cancel, const auto &end) {
        i::DesktopOcrResult read;
        read.capture_begin = std::chrono::steady_clock::now();
        const auto fresh = f.pipeline->refresh(true, cancel, end);
        read.ok = fresh.captured && fresh.analyzed;
        read.cancelled = fresh.capture_cancelled || fresh.analysis_cancelled;
        read.snapshot = fresh.publish.snapshot;
        read.display_id = fresh.display_id;
        read.display_geometry = fresh.display_geometry;
        read.model_revision = "fake-long-text-contract";
        read.backend_calls = f.ocr->calls();
        read.capture_end = std::chrono::steady_clock::now();
        return read;
    };
    detail::DesktopActionVerification verification(f.tools, f.ctx, {});
    detail::DesktopImageTurn turn(std::make_shared<detail::DesktopImageSource>(), f.tools, f.ctx,
                                  {});
    mira::BuiltinToolRegistry registry;
    const i::DesktopAtomToolset::Decorator decorate = [&](const std::string &name,
                                                          mira::BuiltinToolHandler handler) {
        return [&, name, handler](const auto &args, const auto &ctx) {
            return detail::DesktopActionVerification::mutation(name)
                       ? verification.execute(name, args, ctx, handler)
                       : handler(args, ctx);
        };
    };
    turn.register_tool(registry, decorate);
    auto marked = invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, f.ctx);
    check(!marked.failed);
    auto args = *f.target(marked.result).as_object();
    args.back().second = Json{"visual_text_exists"};
    args.emplace_back("expected_text", expected + "A");
    check(invoke(registry, "desktop.vision.click", Json{args}, f.ctx).failed);
    check(f.env->injector.calls == 0);
    args.back().second = Json{expected};
    auto clicked = invoke(registry, "desktop.vision.click", Json{std::move(args)}, f.ctx);
    check(!clicked.failed && clicked.result.find("verified")->as_boolean().value_or(false));
    check(verification.settle());
    check(f.env->injector.calls == 1 && f.ocr->calls() == 3);
}
// DEC-062: a configured capture scope pins every supplied image to the
// focused scope window; crops may only narrow it and clicks must name it.
void scope_contracts(kairo::Executor &executor) {
    // Mark refuses when the focused front window is not the scope window,
    // before any pipeline capture runs.
    {
        Fixture f(executor, true, "button", "w1");
        f.env->fake.windows.front().focused = false;
        f.env->fake.windows.push_back({"w2", "Foreign", {-190, -30, 128, 80}, true});
        auto marked = f.mark();
        check(marked.failed && marked.safe_error_summary.find("not focused") != std::string::npos);
        check(f.mark_calls == 0 && f.env->fake.capture_calls == 0);
    }
    // Crops must sit fully inside the scope window; a contained crop narrows
    // the ROI instead of being refused.
    {
        Fixture f(executor, true, "button", "w1", true);
        for (const char *crop :
             {R"({"x":-200,"y":-40,"width":40,"height":20})",    // fully outside
              R"({"x":-100,"y":30,"width":100,"height":40})"}) { // hangs out of the window
            auto rejected = f.mark(mira::parse_json(crop).value());
            check(rejected.failed && rejected.safe_error_summary.find(
                                         "outside the capture scope window") != std::string::npos);
        }
        check(f.mark_calls == 0 && f.env->fake.capture_calls == 0);
        auto narrowed =
            f.mark(mira::parse_json(R"({"x":-185,"y":-25,"width":50,"height":30})").value());
        check(!narrowed.failed);
        check(*narrowed.result.find("origin_x")->as_integer() == -185);
        check(*narrowed.result.find("origin_y")->as_integer() == -25);
        check(*narrowed.result.find("width")->as_integer() == 50);
    }
    // Without a crop the supplied image is the scope window, not the display.
    {
        Fixture f(executor, true, "button", "w1", true);
        auto marked = f.mark();
        check(!marked.failed);
        check(*marked.result.find("origin_x")->as_integer() == -190);
        check(*marked.result.find("origin_y")->as_integer() == -30);
        check(*marked.result.find("width")->as_integer() == 128);
        check(*marked.result.find("height")->as_integer() == 80);
        check(*marked.result.find("window_id")->as_string() == "w1");
        const auto &marks = *marked.result.find("marks")->as_array();
        check(marks.size() == 1);
        for (const auto &entry : marks) {
            const auto x = *entry.find("x")->as_integer();
            const auto y = *entry.find("y")->as_integer();
            const auto w = *entry.find("width")->as_integer();
            const auto h = *entry.find("height")->as_integer();
            check(x >= -190 && y >= -30 && x + w <= -62 && y + h <= 50);
        }
    }
    // Clicks must name the scope window; a foreign window id is refused as a
    // stale observation and the correctly named target still injects.
    {
        Fixture f(executor, true, "button", "w1", true);
        f.env->fake.windows.push_back({"w2", "Other", {-60, -20, 40, 30}, false});
        auto marked = f.mark();
        check(!marked.failed);
        auto target = f.target(marked.result);
        auto foreign = *target.as_object();
        for (auto &[key, value] : foreign)
            if (key == "window_id")
                value = Json{"w2"};
        auto refused = f.click(Json{foreign});
        check(refused.failed &&
              refused.safe_error_summary.find("stale_observation") != std::string::npos);
        check(f.env->injector.calls == 0);
        auto clicked = f.click(target);
        check(!clicked.failed);
        check(f.env->fake.pointer.x == -160 && f.env->fake.pointer.y == -10);
        check(f.env->injector.calls == 1);
    }
}
// DEC-063: large display budgets stay unchanged; only explicit ROI pixels enter analysis.
void regional_capture(kairo::Executor &executor) {
    {
        Fixture f(executor, true, "button", {}, false, true);
        f.env->fake.displays[0].geometry = {-200, -40, 3200, 2000};
        const auto rejected = f.mark();
        check(rejected.failed && rejected.safe_error_summary.size() <= 512);
        const auto receipt = mira::parse_json(rejected.safe_error_summary);
        check(receipt && *receipt.value().find("code")->as_string() == "capture_too_large");
        check(*receipt.value().find("stage")->as_string() == "capture");
        check(receipt.value().find("side_effect_may_have_occurred")->as_boolean() == false);
        check(rejected.safe_error_summary.find("ROI") != std::string::npos);
        check(!f.mark(mira::parse_json(R"({"x":-200,"y":-40,"width":128,"height":80})").value())
                   .failed);
        check(f.env->injector.calls == 0);
    }
    {
        Fixture f(executor, true, "button", "w1", true, true);
        f.env->fake.capture_frame_edit = [](auto &frame) { --frame.width; };
        check(f.mark().failed && f.env->fake.capture_calls == 0 && f.env->injector.calls == 0);
    }
    {
        Fixture f(executor, true, "button", "w1", true, true);
        f.env->fake.failures.capture_error = true;
        check(f.mark().failed && f.env->fake.capture_calls == 0 && f.env->injector.calls == 0);
    }
    {
        Fixture f(executor, true, "button", "w1", true, true);
        f.env->fake.displays[0].geometry = {-200, -40, 3840, 2160};
        const auto marked = f.mark();
        check(!marked.failed);
        check(f.env->fake.capture_calls == 0); // No full-display allocation.
        const auto &m = marked.result.find("marks")->as_array()->front();
        check(*m.find("x")->as_integer() == -170 && *m.find("y")->as_integer() == -10);
        check(!f.click(f.target(marked.result)).failed && f.env->injector.calls == 1);
        check(f.env->fake.capture_calls == 0); // Revalidation uses the identical ROI.
    }
    {
        Fixture f(executor, true, "button", "w1", true, true);
        f.env->fake.displays[0].geometry = {-200, -40, 3840, 2160};
        const auto marked = f.mark();
        check(!marked.failed);
        // Structural replacement of the marked subject inside the ROI (the
        // brightness-only redraw is legitimately passed since DEC-083).
        f.env->fake.capture_frame_edit = [](auto &frame) {
            paint_structural(frame, 20, 20, 60, 40);
        };
        check(f.click(f.target(marked.result)).failed && f.env->injector.calls == 0);
    }
    {
        Fixture f(executor, true, "button", "w1", true, true);
        f.env->fake.displays[0].geometry = {-200, -40, 3840, 2160};
        const auto marked = f.mark();
        check(!marked.failed);
        ++f.env->fake.displays[0].geometry.width;
        check(f.click(f.target(marked.result)).failed && f.env->injector.calls == 0);
    }
    {
        Fixture f(executor, true, "button", "w1", true, true);
        f.env->fake.displays[0].geometry = {-200, -40, 3840, 2160};
        const auto marked = f.mark();
        check(!marked.failed);
        ++f.env->fake.windows[0].geometry.x;
        check(f.click(f.target(marked.result)).failed && f.env->injector.calls == 0);
    }
    {
        Fixture f(executor, true, "button", "w1", true, true);
        f.env->fake.displays[0].geometry = {-200, -40, 3840, 2160};
        const auto area = d::WindowGeometry{-180, -20, 64, 64};
        const auto marked = f.pipeline->refresh_marked(area, {}, {});
        check(marked.analyzed && marked.capture_roi && marked.frame.width == 64 &&
              marked.marked_frame.width == 64 && marked.frame.pixels.size() == 64u * 64u * 4u);
        check(marked.display_geometry.width == 3840 && marked.image_geometry.x == -180);
        const auto &r = marked.publish.snapshot.regions.front();
        check(r.bounds.x == -160 && r.bounds.y == 0);
        const auto ocr = f.pipeline->refresh_region(area, true, {}, {});
        check(ocr.analyzed && ocr.capture_roi && ocr.frame.width == 64 &&
              ocr.publish.snapshot.regions.front().bounds.x == -160);
        const auto outside = f.pipeline->refresh_region({-201, -40, 64, 64}, true, {}, {});
        check(!outside.captured && outside.capture_error.code == "invalid_argument");
        const auto over = f.pipeline->refresh_region({-200, -40, 3840, 2160}, true, {}, {});
        check(!over.captured && over.capture_error.code == "capture_too_large");
        d::CancelToken cancel;
        cancel.request_cancel();
        check(f.pipeline->refresh_region(area, true, cancel, {}).capture_cancelled);
        check(
            !f.pipeline->refresh_region(area, true, {}, std::chrono::steady_clock::now()).analyzed);
        f.pipeline->stop();
        check(!f.pipeline->refresh_region(area, true, {}, {}).analyzed);
    }
    {
        Fixture f(executor, true, "button", "w1", true); // Default keeps old full-display route.
        f.env->fake.displays[0].geometry = {-200, -40, 3840, 2160};
        check(f.mark().failed && f.env->fake.capture_calls == 1 && f.env->injector.calls == 0);
    }
}

void pointer_actions(kairo::Executor &executor) {
    for (const auto *operation : {"scroll", "drag"}) {
        const bool drag = std::string_view{operation} == "drag";
        const std::string name = std::string{"desktop.vision."} + operation;
        for (const auto *mode : {"good", "right", "middle", "unchanged", "denied", "changed",
                                 "edge_animation", "uniform_drift", "destination", "budget",
                                 "exception", "release", "io", "cancel", "takeover"}) {
            Fixture f(executor, true, "source", "w1", false, false, true);
            detail::DesktopActionVerification verification(f.tools, f.ctx, f.cancel);
            mira::BuiltinToolRegistry registry;
            f.turn->register_tool(registry, [&](const auto &tool,
                                                mira::BuiltinToolHandler handler) {
                return [&, tool, handler = std::move(handler)](const auto &args, const auto &ctx) {
                    return detail::DesktopActionVerification::mutation(tool)
                               ? verification.execute(tool, args, ctx, handler)
                               : handler(args, ctx);
                };
            });
            verification.register_tool(registry, {});
            const auto marked =
                invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, f.ctx);
            check(!marked.failed && marked.result.find("marks")->as_array()->size() == 2);
            auto fields = *f.target(marked.result).as_object();
            for (auto &[key, value] : fields)
                if (key == "verify_kind")
                    value = Json{"window_title_equals"};
            fields.emplace_back("expected_title", "After");
            if (drag)
                fields.emplace_back("destination_mark", std::int64_t{2});
            else {
                fields.emplace_back("horizontal_steps", std::int64_t{-2});
                fields.emplace_back("vertical_steps", std::int64_t{3});
            }
            const std::string scenario = mode;
            if (drag && (scenario == "right" || scenario == "middle"))
                fields.emplace_back("button", scenario);
            if (scenario == "good" || scenario == "right" || scenario == "middle")
                f.env->injector.after_pointer = [&] { f.env->fake.windows[0].title = "After"; };
            if (scenario == "denied")
                f.allow_input = false;
            if (scenario == "changed")
                // Structural replacement of the operated source mark; the
                // drag target spans both marks, so half of its core moving
                // still rejects.
                f.env->fake.capture_frame_edit = [](auto &frame) {
                    paint_structural(frame, 20, 20, 60, 40);
                };
            if (scenario == "edge_animation")
                // Animation outside the operated region's padded rectangle
                // (left frame band; the marks start at frame x=20 with an
                // 8px margin) never blocks under DEC-083.
                f.env->fake.capture_frame_edit = [](auto &frame) {
                    paint_structural(frame, 0, 0, 12, 80);
                };
            if (scenario == "uniform_drift")
                // Layout-preserving brightness shift; the subject stays put.
                f.env->fake.set_pixel_value(
                    static_cast<std::uint8_t>(f.env->fake.pixel_value() + 1));
            if (scenario == "destination") {
                if (drag) {
                    for (auto &[key, value] : fields)
                        if (key == "destination_mark")
                            value = Json{std::int64_t{1}};
                } else {
                    f.env->fake.windows[0].geometry.width = 1;
                }
            }
            if (scenario == "budget") {
                for (auto &[key, value] : fields) {
                    if (key == "destination_mark")
                        value = Json{std::int64_t{128}};
                    if (key == "horizontal_steps" || key == "vertical_steps")
                        value = Json{std::int64_t{16}};
                }
            }
            if (scenario == "exception")
                f.env->injector.mode = 1;
            if (scenario == "release")
                f.env->injector.mode = 2;
            if (scenario == "io")
                f.env->injector.mode = 3;
            if (scenario == "cancel")
                f.env->injector.mode = 5;
            if (scenario == "takeover")
                f.tools.actions->takeover();
            const Json args{std::move(fields)};
            const auto action = invoke(registry, name, args, f.ctx);
            const bool dispatched =
                (scenario == "good" || scenario == "right" || scenario == "middle") ||
                scenario == "unchanged" || scenario == "edge_animation" ||
                scenario == "uniform_drift";
            const bool called = dispatched || scenario == "exception" || scenario == "release" ||
                                scenario == "io" || scenario == "cancel";
            check(f.env->injector.calls == (called ? 1 : 0));
            check(action.failed != dispatched);
            if (dispatched) {
                check(action.result.find("action_id") &&
                      *action.result.find("input_released")->as_boolean());
                check(f.env->injector.last_scope.window_id == "w1");
                check(f.env->injector.from.x == -160 && f.env->injector.from.y == -10);
                check(drag ? f.env->injector.to.x == -110
                           : f.env->injector.horizontal == -2 && f.env->injector.vertical == 3);
                const auto verify = invoke(
                    registry, "desktop.action.verify",
                    Json{Json::Object{{"action_id", *action.result.find("action_id")}}}, f.ctx);
                check(!verify.failed &&
                      *verify.result.find("verified")->as_boolean() ==
                          (scenario == "good" || scenario == "right" || scenario == "middle"));
            }
            if (drag && (scenario == "right" || scenario == "middle"))
                check(f.env->injector.last_button == scenario);
            check(verification.settle() ==
                  (scenario == "good" || scenario == "right" || scenario == "middle"));
            if (scenario == "release" || scenario == "exception")
                check(!f.tools.actions->input_released() && !f.tools.actions->resume());
            // Old generation/epoch cannot dispatch twice, even after a confirmed release.
            check(invoke(registry, name, args, f.ctx).failed);
            check(f.env->injector.calls == (called ? 1 : 0));
            if (scenario == "good" || scenario == "right" || scenario == "middle") {
                const auto refreshed =
                    invoke(registry, "desktop.vision.mark", Json{Json::Object{}}, f.ctx);
                check(!refreshed.failed && *refreshed.result.find("visual_scope")->as_string() !=
                                               *marked.result.find("visual_scope")->as_string());
                check(invoke(registry, name, args, f.ctx).failed);
            }
        }
    }
    // Missing new capability or paired resolver never exposes a drag fallback.
    Fixture absent(executor);
    auto specs = absent.registry.exposed_tools();
    check(std::none_of(specs.begin(), specs.end(), [](const auto &spec) {
        return spec.wire_name == "desktop.vision.drag" || spec.wire_name == "desktop.vision.scroll";
    }));
    Fixture no_pair(executor, true, "source", "w1", false, false, true);
    no_pair.tools.with_visual_references = {};
    detail::DesktopImageTurn limited(no_pair.source, no_pair.tools, no_pair.ctx, no_pair.cancel);
    mira::BuiltinToolRegistry registry;
    limited.register_tool(registry, {}, true);
    specs = registry.exposed_tools();
    check(std::none_of(specs.begin(), specs.end(),
                       [](const auto &spec) { return spec.wire_name == "desktop.vision.drag"; }));
}
} // namespace
int main() {
    try {
        kairo::Executor executor;
        check(executor.initialize({}).ok);
        contracts(executor);
        geometric_marks(executor);
        thin_outline_marks(executor);
        configured_template_marks(executor);
        template_marks(executor);
        model_loop(executor);
        reobserve_before_input(executor);
        long_visual_condition(executor);
        scope_contracts(executor);
        regional_capture(executor);
        executor.submit_auto([&] { pointer_actions(executor); }).get();
        executor.shutdown(true);
        std::cout << "visual marks: " << checks << " checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
