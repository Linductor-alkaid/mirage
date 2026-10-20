#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"
#include <cmath>
#include <kairo/executor.hpp>
#include <mira/workflow_runtime.hpp>
#include <mirage/integration/desktop_atom_toolset.hpp>
#include <mirage/integration/mira_environment_binding.hpp>
#include <stdexcept>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using Json = mira::JsonValue;
namespace {
struct Environment final : d::DesktopEnvironment, d::InputProvider {
    mirage::testing::FakeDesktopEnvironment fake;
    int clicks = 0;
    d::PointerPoint at;
    std::function<void()> on_click;
    bool complete = true, released = true, throws = false;
    d::EnvironmentInfo info() const override { return {"local-visual-fixture", "test"}; }
    d::WindowProvider *window() override { return fake.window(); }
    d::ScreenProvider *screen() override { return fake.screen(); }
    d::InputProvider *input() override { return this; }
    d::InputOutcome inject_key(const d::KeySym &key, bool pressed, const d::InputLimits &l,
                               const d::CancelToken &c) override {
        return fake.input()->inject_key(key, pressed, l, c);
    }
    d::InputOutcome type_text(const std::string &text, const d::InputLimits &l,
                              const d::CancelToken &c) override {
        return fake.input()->type_text(text, l, c);
    }
    d::InputOutcome pointer_move(std::int32_t x, std::int32_t y, const d::InputLimits &l,
                                 const d::CancelToken &c) override {
        return fake.input()->pointer_move(x, y, l, c);
    }
    d::InputOutcome pointer_button(const d::MouseButton &b, bool pressed, const d::InputLimits &l,
                                   const d::CancelToken &c) override {
        return fake.input()->pointer_button(b, pressed, l, c);
    }
    d::PointerQueryOutcome pointer_position(const d::CancelToken &c) override {
        return fake.input()->pointer_position(c);
    }
    bool timed_pointer_actions_supported() const override { return true; }
    d::InputOutcome smooth_move_to(std::int32_t x, std::int32_t y,
                                   std::chrono::milliseconds duration, std::uint32_t points,
                                   const d::InputLimits &limits, const d::CancelToken &cancel, std::optional<d::PointerMotionVariation> variation = std::nullopt) override {
        return fake.input()->smooth_move_to(x, y, duration, points, limits, cancel, variation);
    }
    bool complete_actions_supported() const override { return complete; }
    d::InputOutcome click_at(std::int32_t x, std::int32_t y, const d::MouseButton &,
                             const d::InputLimits &, const d::CancelToken &) override {
        ++clicks;
        at = {x, y};
        if (throws)
            throw std::runtime_error("injected input exception");
        if (on_click)
            on_click();
        d::InputOutcome out;
        out.ok = true;
        out.input_released = released;
        out.side_effect_may_have_occurred = true;
        return out;
    }
};
Json knowledge(const char *kind = "ocr") {
    return Json{Json::Object{{"knowledge_schema", 1},
                             {"window_title", "Canvas Editor"},
                             {"page_text", "Document"},
                             {"target_kind", kind},
                             {"target", std::string{kind} == "ocr" ? "Save" : "save-icon"},
                             {"expected_text", "Saved"}}};
}
Json replace(Json args, const char *key, Json value) {
    auto object = *args.as_object();
    for (auto &[name, member] : object)
        if (name == key)
            member = value;
    return Json{std::move(object)};
}
auto context() {
    auto ctx = mira::make_control_context();
    ctx.task = mira::TaskId::generate();
    ctx.session = mira::SessionId::generate();
    ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    return ctx;
}
auto dispatch(mira::BuiltinToolRegistry &registry, const Json &args,
              const mira::OperationContext &ctx) {
    for (const auto &spec : registry.exposed_tools()) {
        if (spec.wire_name != "desktop.knowledge.visual_click")
            continue;
        mira::ToolProposal proposal;
        proposal.tool_id = spec.tool_id;
        proposal.wire_name = spec.wire_name;
        proposal.tool_version = spec.version;
        proposal.has_side_effects = true;
        proposal.arguments = args;
        proposal.arguments_digest = mira::canonical_json_digest(args);
        proposal.operation_id = mira::OperationId::generate();
        proposal.provider_call_id.value = "local-visual-fixture";
        return registry.execute(proposal, ctx);
    }
    throw std::logic_error("local visual tool missing");
}
struct Fixture {
    std::shared_ptr<Environment> env = std::make_shared<Environment>();
    d::VisualReferenceRegistry registry;
    i::DesktopToolContext desktop;
    int marks = 0, reads = 0;
    bool saved = false, cancelled = false, allow_input = true, allow_capture = true;
    std::function<void(d::VisualSnapshot &)> on_mark, on_read;
    std::function<void(const std::string &)> on_gate;
    mira::OperationContext ctx = context();
    Fixture() {
        env->fake.displays = {{"d1", {-100, -50, 400, 200}, true}};
        env->fake.windows = {{"w1", "Canvas Editor", {-80, -40, 200, 120}, true}};
        env->on_click = [this] {
            saved = true;
            env->fake.set_pixel_value(env->fake.pixel_value() + 1);
        };
        ctx.cancellation_requested = [this] { return cancelled; };
        desktop.environment = env;
        desktop.actions = std::make_shared<d::ActionCoordinator>();
        desktop.gate = [this](const auto &cap, const auto &, const auto &probe) {
            if (on_gate)
                on_gate(cap);
            return !probe() && (cap == "input.inject" ? allow_input : allow_capture);
        };
        desktop.mark = [this](const auto &crop, const auto &cancel, const auto &) {
            ++marks;
            i::EnvironmentVisualRefresh result;
            const auto read = env->screen()->capture_roi(*crop, {}, cancel);
            result.captured = read.ok;
            result.capture_cancelled = read.cancelled;
            result.frame = result.marked_frame = read.frame;
            result.display_id = "d1";
            result.display_geometry = env->fake.displays[0].geometry;
            result.image_geometry = *crop;
            result.capture_roi = *crop;
            result.display_primary = true;
            result.visual_generation = result.frame_sequence = static_cast<std::uint64_t>(marks);
            auto snapshot = before();
            if (on_mark)
                on_mark(snapshot);
            result.publish = registry.publish(snapshot);
            result.analyzed = result.publish.ok;
            for (std::size_t n = 0; n < result.publish.snapshot.regions.size(); ++n) {
                const auto &r = result.publish.snapshot.regions[n];
                result.marks.push_back(
                    {static_cast<std::uint32_t>(n + 1),
                     r.ref,
                     {r.bounds.x + r.bounds.width / 2, r.bounds.y + r.bounds.height / 2},
                     true});
            }
            return result;
        };
        desktop.with_visual_reference = [this](const auto &scope, const auto &ref, const auto &fn) {
            return registry.with_reference(scope, ref, fn);
        };
        desktop.ocr = [this](const auto &, const auto &) {
            i::DesktopOcrResult result;
            result.capture_begin = std::chrono::steady_clock::now();
            result.ok = true;
            result.display_id = "d1";
            result.display_geometry = env->fake.displays[0].geometry;
            result.model_revision = "fixture-ocr";
            result.backend_calls = static_cast<std::uint64_t>(++reads);
            result.snapshot = before();
            if (saved)
                result.snapshot.regions.push_back(text("Saved", 30, 80));
            if (on_read)
                on_read(result.snapshot);
            for (std::size_t n = 0; n < result.snapshot.regions.size(); ++n)
                result.snapshot.regions[n].ref = "@v" + std::to_string(n + 1);
            result.snapshot.scope_ref = "@post" + std::to_string(reads);
            result.capture_end = std::chrono::steady_clock::now();
            return result;
        };
    }
    d::VisualRegionEntry text(const std::string &value, int x, int y) {
        const auto &window = env->fake.windows[0].geometry;
        return {{},  d::VisualRegionSource::kOcr, {window.x + x, window.y + y, 50, 15}, value, {},
                0.95};
    }
    d::VisualSnapshot before() { return {{}, {text("Document", 10, 10), text("Save", 30, 45)}}; }
};
void positive(kairo::Executor &executor, const char *kind) {
    Fixture f;
    if (std::string{kind} == "template")
        f.on_mark = [](auto &s) {
            s.regions[1].source = d::VisualRegionSource::kTemplate;
            s.regions[1].text.clear();
            s.regions[1].template_id = "save-icon";
        };
    auto tools = i::DesktopAtomToolset::build(f.desktop);
    std::string digest;
    for (int round = 0; round < 2; ++round) {
        f.saved = false;
        f.env->fake.windows[0].geometry.x += round * 25;
        if (round) {
            auto prior = f.on_mark;
            f.on_mark = [&, prior](auto &s) {
                if (prior)
                    prior(s);
                s.regions.insert(s.regions.begin(), f.text("Toolbar", 80, 10));
            };
        }
        const auto result =
            executor
                .submit_auto([&] { return dispatch(*tools->registry(), knowledge(kind), f.ctx); })
                .get();
        MIRAGE_CHECK(result && !result.value().failed);
        if (!result || result.value().failed)
            throw std::runtime_error("positive: " + (result ? result.value().safe_error_summary
                                                            : result.error().safe_message));
        const auto receipt = mira::parse_json(*result.value().result.as_string());
        MIRAGE_CHECK(receipt && receipt.value().find("verified")->as_boolean() == true);
        const auto current = *receipt.value().find("knowledge_digest")->as_string();
        MIRAGE_CHECK(!round || current == digest);
        digest = current;
        MIRAGE_CHECK(
            !receipt.value().find("verification")->find("evidence_id")->as_string()->empty());
        MIRAGE_CHECK(f.env->at.x == f.env->fake.windows[0].geometry.x + 55);
        MIRAGE_CHECK(f.env->clicks == round + 1);
        MIRAGE_CHECK(f.desktop.actions->input_released());
    }
    MIRAGE_CHECK(f.marks == 2 && f.reads == 2);
}
void delayed(kairo::Executor &executor) {
    Fixture f;
    f.on_read = [&](auto &snapshot) {
        if (f.reads == 1)
            snapshot.regions.pop_back();
    };
    auto tools = i::DesktopAtomToolset::build(f.desktop);
    const auto result =
        executor.submit_auto([&] { return dispatch(*tools->registry(), knowledge(), f.ctx); })
            .get();
    MIRAGE_CHECK(result && !result.value().failed);
    MIRAGE_CHECK(f.env->clicks == 1 && f.reads == 2);
}
void negative(kairo::Executor &executor, const std::string &mode) {
    Fixture f;
    auto args = knowledge();
    const bool effect = mode.starts_with("post_") || mode == "input_throw" || mode == "release" ||
                        mode == "cancel_after" || mode == "takeover_after";
    if (mode == "title")
        f.env->fake.windows[0].title = "Other";
    if (mode == "version")
        args = replace(args, "knowledge_schema", 2);
    if (mode == "control")
        args = replace(args, "target", "Save\n");
    if (mode == "bad_utf8")
        args = replace(args, "target", "\xff");
    if (mode == "same_condition")
        args = replace(args, "expected_text", "Document");
    if (mode == "input_denied")
        f.allow_input = false;
    if (mode == "capture_denied")
        f.allow_capture = false;
    if (mode == "cancel_before")
        f.cancelled = true;
    if (mode == "expired")
        f.ctx.deadline = std::chrono::steady_clock::now() - std::chrono::seconds{1};
    if (mode == "input_throw")
        f.env->throws = true;
    if (mode == "release")
        f.env->released = false;
    f.on_mark = [&](auto &s) {
        if (mode == "page")
            s.regions[0].text = "Other";
        if (mode == "duplicate_page")
            s.regions.push_back(s.regions[0]);
        if (mode == "low_page")
            s.regions[0].confidence = 0.79;
        if (mode == "missing")
            s.regions.resize(1);
        if (mode == "duplicate" || mode == "low_duplicate") {
            s.regions.push_back(s.regions[1]);
            if (mode == "low_duplicate")
                s.regions.back().confidence = 0.1;
        }
        if (mode == "low")
            s.regions[1].confidence = 0.79;
        if (mode == "nan")
            s.regions[1].confidence = NAN;
        if (mode == "outside")
            s.regions[1].bounds.x = 200;
        if (mode == "detector")
            s.regions[1].source = d::VisualRegionSource::kDetector;
        if (mode == "preexisting")
            s.regions.push_back(f.text("Saved", 30, 80));
        if (mode == "mark_throw")
            throw std::runtime_error("injected mark failure");
    };
    f.on_gate = [&](const auto &cap) {
        if (cap != "input.inject")
            return;
        if (mode == "stale_scope")
            f.registry.clear();
        if (mode == "focus")
            f.env->fake.windows[0].focused = false;
        if (mode == "move")
            ++f.env->fake.windows[0].geometry.x;
        if (mode == "retitle")
            f.env->fake.windows[0].title = "Changed";
        if (mode == "takeover")
            f.desktop.actions->takeover();
    };
    if (mode == "pixels") {
        // M7-16 / DEC-083: a single off-target pixel no longer proves
        // staleness. Keep the scenario's original fault-matrix intent
        // (changed pixels reject the click) by replacing the subject
        // structurally across the frame, which the layered guard fails
        // closed on wherever the cropped target sits.
        int captures = 0;
        f.env->fake.capture_frame_edit = [captures](auto &frame) mutable {
            if (++captures > 1)
                for (int y = 0; y < frame.height; ++y)
                    for (int x = 0; x < frame.width; ++x) {
                        const auto value =
                            static_cast<std::uint8_t>(((x / 4 + y / 4) % 2) ? 230 : 25);
                        const auto offset = static_cast<std::size_t>(y) * frame.stride +
                                            static_cast<std::size_t>(x) * 4;
                        frame.pixels[offset] = frame.pixels[offset + 1] = frame.pixels[offset + 2] =
                            value;
                        frame.pixels[offset + 3] = 255;
                    }
        };
    }
    f.env->on_click = [&] {
        f.saved = mode != "post_missing";
        if (mode == "cancel_after")
            f.cancelled = true;
        if (mode == "takeover_after")
            f.desktop.actions->takeover();
    };
    f.on_read = [&](auto &s) {
        if (mode == "post_page")
            s.regions[0].text = "Other";
        if (mode == "post_low_page")
            s.regions[0].confidence = 0.1;
        if (mode == "post_duplicate_page")
            s.regions.push_back(s.regions[0]);
        if (mode == "post_duplicate" || mode == "post_low_duplicate") {
            s.regions.push_back(s.regions.back());
            if (mode == "post_low_duplicate")
                s.regions.back().confidence = 0.1;
        }
        if (mode == "post_title")
            f.env->fake.windows[0].title = "Changed";
        if (mode == "post_throw")
            throw std::runtime_error("injected post failure");
    };
    auto tools = i::DesktopAtomToolset::build(f.desktop);
    const auto result =
        executor.submit_auto([&] { return dispatch(*tools->registry(), args, f.ctx); }).get();
    MIRAGE_CHECK(!result || result.value().failed);
    MIRAGE_CHECK(f.env->clicks == (effect ? 1 : 0));
    if (effect && result) {
        const auto &summary = result.value().safe_error_summary;
        MIRAGE_CHECK(summary.find("\"verified\":true") == std::string::npos);
    }
    MIRAGE_CHECK(f.desktop.actions->input_released() ==
                 (mode != "input_throw" && mode != "release"));
}
void workflow(kairo::Executor &executor) {
    Fixture f;
    auto tools = i::DesktopAtomToolset::build(f.desktop);
    auto binding = std::make_shared<i::MiraEnvironmentBinding>(f.env);
    mira::MiraRuntime runtime;
    MIRAGE_CHECK(runtime.initialize());
    auto session = runtime.open_session(binding);
    MIRAGE_CHECK(session && session.value().command.outcome(std::chrono::seconds{5}));
    mira::WorkflowRuntime workflow(executor, runtime, session.value().id, binding);
    workflow.set_tool_registry(tools->registry());
    mira::WorkflowDefinition definition;
    definition.workflow_id = mira::WorkflowId::generate();
    definition.name = "Visual local save";
    definition.allowed_policies = {mira::WorkflowPolicy::Strict, mira::WorkflowPolicy::Recoverable,
                                   mira::WorkflowPolicy::DryRun};
    mira::WorkflowStep step;
    step.id = mira::StepId::generate();
    step.name = "Save and verify";
    auto args = *knowledge().as_object();
    args.emplace_back("tool", "desktop.knowledge.visual_click");
    step.arguments = Json{std::move(args)};
    step.verification =
        mira::WorkflowPredicate{"step_result:" + step.id.to_string(),
                                mira::WorkflowPredicateOp::Contains, Json{"\"verified\":true"}};
    definition.steps = {step};
    const auto published =
        workflow.publish_validated(definition, "fixture", "local vision contract");
    MIRAGE_CHECK(published && f.env->clicks == 0 && f.marks == 0);
    if (!published)
        throw std::runtime_error(published.error().safe_message);
    auto run = [&](mira::WorkflowPolicy policy) {
        auto created = workflow.create_run(definition.workflow_id, published.value().ir_digest,
                                           Json{Json::Object{}}, policy);
        MIRAGE_CHECK(created);
        return workflow.execute_run(created.value().run_id, context());
    };
    const auto good = run(mira::WorkflowPolicy::Strict);
    MIRAGE_CHECK(good && good.value().state == mira::WorkflowRunState::Completed &&
                 f.env->clicks == 1);
    f.on_mark = [&](auto &s) { s.regions.push_back(f.text("Saved", 30, 80)); };
    const auto waiting = run(mira::WorkflowPolicy::Recoverable);
    MIRAGE_CHECK(waiting && waiting.value().state == mira::WorkflowRunState::WaitingAgent &&
                 f.env->clicks == 1);
    MIRAGE_CHECK(workflow.agent_continuation(waiting.value().run_id));
    MIRAGE_CHECK(workflow.shutdown().clean);
    const auto stop = runtime.request_shutdown();
    MIRAGE_CHECK(stop && stop.value().outcome(std::chrono::seconds{5}));
    MIRAGE_CHECK(runtime.finish_shutdown().clean);
}
} // namespace
int main() {
    kairo::Executor executor;
    kairo::ExecutorConfig cfg;
    cfg.min_threads = cfg.max_threads = 4;
    MIRAGE_CHECK(executor.initialize(cfg).ok);
    positive(executor, "ocr");
    positive(executor, "template");
    for (const std::string mode : {"title",
                                   "version",
                                   "control",
                                   "bad_utf8",
                                   "same_condition",
                                   "input_denied",
                                   "capture_denied",
                                   "cancel_before",
                                   "expired",
                                   "input_throw",
                                   "release",
                                   "page",
                                   "duplicate_page",
                                   "low_page",
                                   "missing",
                                   "duplicate",
                                   "low_duplicate",
                                   "low",
                                   "nan",
                                   "outside",
                                   "detector",
                                   "preexisting",
                                   "mark_throw",
                                   "stale_scope",
                                   "focus",
                                   "move",
                                   "retitle",
                                   "takeover",
                                   "pixels",
                                   "post_missing",
                                   "post_page",
                                   "post_low_page",
                                   "post_duplicate_page",
                                   "post_duplicate",
                                   "post_low_duplicate",
                                   "post_title",
                                   "post_throw",
                                   "cancel_after",
                                   "takeover_after"})
        negative(executor, mode);
    for (int n = 0; n < 6; ++n) {
        Fixture f;
        if (n == 0)
            f.desktop.mark = {};
        if (n == 1)
            f.desktop.ocr = {};
        if (n == 2)
            f.desktop.with_visual_reference = {};
        if (n == 3)
            f.desktop.actions.reset();
        if (n == 4)
            f.env->complete = false;
        if (n == 5)
            f.desktop.environment.reset();
        const auto tools = i::DesktopAtomToolset::build(f.desktop)->exposed_atoms();
        MIRAGE_CHECK(std::none_of(tools.begin(), tools.end(), [](const auto &s) {
            return s.wire_name == "desktop.knowledge.visual_click";
        }));
    }
    delayed(executor);
    workflow(executor);
    executor.shutdown(true);
    return mirage::testing::finish("local_visual_workflow_test");
}
