// Real X11/IBus and PP-OCR acceptance through the shared-session tool adapter.
// The X11 session shim is test-only; it is not Portal/compositor acceptance.
#include "../../integration/mira/src/desktop_image_supply.hpp"
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
namespace d = mirage::desktop;
namespace i = mirage::integration;
using J = mira::JsonValue;
namespace {
class Environment final : public d::DesktopEnvironment, public d::ScreenProvider {
  public:
    mirage::platform::linux_backend::LinuxDesktopEnvironment platform{
        {}, {true, std::getenv("DISPLAY")}};
    std::string display;
    explicit Environment() {
        const auto list = platform.screen()->list_displays({}, {});
        if (!list.ok || list.displays.empty())
            throw std::runtime_error("private display absent");
        display = list.displays.front().id;
    }
    d::EnvironmentInfo info() const override { return {"private-real-ime-fixture", "linux"}; }
    d::ScreenProvider *screen() override { return this; }
    bool desktop_observation_only() const override { return true; }
    bool desktop_interaction_supported() const override { return true; }
    struct Session final : d::DesktopCaptureSession {
        Environment &owner;
        std::uint64_t sequence = 0;
        explicit Session(Environment &v) : owner(v) {}
        bool input_supported() const override { return true; }
        bool reusable() const override { return true; }
        d::CaptureOutcome capture(const d::CaptureLimits &limits,
                                  const d::CancelToken &cancel) override {
            auto out = owner.platform.screen()->capture_display(owner.display, limits, cancel);
            if (out.ok)
                out.frame.source_sequence = ++sequence;
            return out;
        }
        d::SharedPointerQueryOutcome pointer_position(const d::CancelToken &cancel) override {
            const auto point = owner.platform.input()->pointer_position(cancel);
            return {point.ok, point.cancelled, {point.position.x, point.position.y}, 1100, 720,
                    sequence, point.error};
        }
        d::InputOutcome input(const d::SharedInputAction &action, const d::InputLimits &limits,
                              const d::CancelToken &cancel) override {
            using K = d::SharedInputAction::Kind;
            auto *input = owner.platform.input();
            if (action.kind == K::Move)
                return input->approach_target({int(action.x), int(action.y)}, 32, limits, cancel);
            if (action.kind == K::Click)
                return input->click_at(int(action.x), int(action.y), "left", limits, cancel);
            if (action.kind == K::Key)
                return input->key_chord({action.value}, limits, cancel);
            if (action.kind == K::Text)
                return input->type_text(action.value, limits, cancel);
            return {false, false, {"unsupported", "test shim"}};
        }
    };
    std::unique_ptr<d::DesktopCaptureSession> create_desktop_interaction_session() override {
        return std::make_unique<Session>(*this);
    }
    d::DisplayListOutcome list_displays(const d::DisplayListLimits &a,
                                        const d::CancelToken &b) override {
        return platform.screen()->list_displays(a, b);
    }
    d::CaptureOutcome capture_display(const std::string &a, const d::CaptureLimits &b,
                                      const d::CancelToken &c) override {
        return platform.screen()->capture_display(a, b, c);
    }
    d::CaptureOutcome capture_window(const std::string &a, const d::CaptureLimits &b,
                                     const d::CancelToken &c) override {
        return platform.screen()->capture_window(a, b, c);
    }
    d::CaptureOutcome capture_roi(const d::WindowGeometry &a, const d::CaptureLimits &b,
                                  const d::CancelToken &c) override {
        return platform.screen()->capture_roi(a, b, c);
    }
};
} // namespace
int main(int argc, char **argv) {
    if (argc != 4 || !std::getenv("MIRAGE_ACCEPTANCE_PRIVATE_SESSION"))
        return 2;
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = 2;
    config.max_threads = 4;
    if (!executor.initialize(config))
        return 3;
    int exit_code = 0;
    try {
        executor
            .submit_auto([&] {
                std::ifstream file(argv[2]);
                std::string data((std::istreambuf_iterator<char>(file)), {});
                const auto request = mira::parse_json(data);
                if (!request)
                    throw std::runtime_error("request missing");
                auto env = std::make_shared<Environment>();
                std::string error;
                auto ocr =
                    i::PpOcrPipeline::create(executor, *env->platform.screen(), argv[1], error);
                if (!ocr)
                    throw std::runtime_error("OCR: " + error);
                i::DesktopToolContext desktop;
                desktop.environment = env;
                desktop.actions = std::make_shared<d::ActionCoordinator>();
                desktop.gate = [](const auto &, const auto &, const auto &stop) { return !stop(); };
                std::size_t ocr_calls = 0;
                J::Array trace;
                desktop.ocr_image = [&](const auto &frame, const auto &cancel,
                                        const auto &deadline) {
                    ++ocr_calls;
                    auto out = ocr->read_image_ocr(frame, cancel, deadline);
                    J::Array regions;
                    for (const auto &r : out.regions)
                        regions.emplace_back(J::Object{{"text", r.text},
                                                       {"confidence", r.confidence},
                                                       {"x", r.x},
                                                       {"y", r.y},
                                                       {"width", r.width},
                                                       {"height", r.height}});
                    trace.emplace_back(J::Object{{"width", frame.width},
                                                 {"height", frame.height},
                                                 {"ok", out.ok},
                                                 {"regions", std::move(regions)}});
                    return out;
                };
                auto ctx = mira::make_control_context();
                ctx.task = mira::TaskId::generate();
                ctx.session = mira::SessionId::generate();
                ctx.task_epoch = 1;
                ctx.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
                auto source = std::make_shared<i::detail::DesktopImageSource>();
                i::detail::DesktopImageTurn turn(source, desktop, ctx, {}, true,
                                                 std::chrono::seconds(5), std::chrono::seconds(10));
                mira::BuiltinToolRegistry registry;
                turn.register_tool(registry, {}, true);
                turn.register_ime_tool(
                    registry,
                    [&](const auto &text, const auto &) -> std::optional<i::detail::ImePlan> {
                        i::detail::ImePlan plan;
                        for (const auto &unit : *request.value().find("units")->as_array())
                            plan.push_back({*unit.find("text")->as_string(),
                                            *unit.find("pinyin")->as_string()});
                        return i::detail::valid_ime_plan(text, plan)
                                   ? std::optional<i::detail::ImePlan>{plan}
                                   : std::nullopt;
                    },
                    {});
                const auto invoke = [&](const std::string &name, J args) {
                    const auto tools = registry.exposed_tools();
                    const auto found = std::find_if(tools.begin(), tools.end(), [&](const auto &t) {
                        return t.wire_name == name;
                    });
                    if (found == tools.end())
                        throw std::runtime_error("tool absent: " + name);
                    mira::ToolProposal p;
                    p.tool_id = found->tool_id;
                    p.wire_name = name;
                    p.tool_version = found->version;
                    p.provider_call_id.value = mira::OperationId::generate().to_string();
                    p.operation_id = mira::OperationId::generate();
                    p.has_side_effects = found->has_side_effects;
                    p.arguments = std::move(args);
                    p.arguments_digest = mira::canonical_json_digest(p.arguments);
                    return registry.execute(p, ctx);
                };
                auto capture = invoke("desktop.screen.capture",
                                      J{J::Object{{"source", "desktop"}, {"interaction", true}}});
                if (!capture || capture.value().failed)
                    throw std::runtime_error("initial capture failed");
                mira::AgentLoopSpec spec;
                spec.task_id = ctx.task;
                spec.session_id = ctx.session;
                spec.task_epoch = ctx.task_epoch;
                const auto offered = turn.supply(spec);
                if (!offered)
                    throw std::runtime_error("supply failed");
                mira::ModelRequest model_request;
                model_request.task_id = ctx.task;
                model_request.task_epoch = ctx.task_epoch;
                model_request.input = offered.value();
                if (!turn.validate_request(model_request, ctx))
                    throw std::runtime_error("supply validation failed");
                auto args = *request.value().find("arguments");
                args.set("image_scope", *capture.value().result.find("image_scope"));
                args.set("environment_epoch", *capture.value().result.find("environment_epoch"));
                const auto begin = std::chrono::steady_clock::now();
                const auto result = invoke("desktop.input.fill", args);
                J report{
                    J::Object{{"ok", bool(result) && !result.value().failed},
                              {"ocr_calls", std::int64_t(ocr_calls)},
                              {"duration_ms", std::chrono::duration_cast<std::chrono::milliseconds>(
                                                  std::chrono::steady_clock::now() - begin)
                                                  .count()},
                              {"ocr_trace", std::move(trace)}}};
                if (result)
                    report.set("result", result.value().failed
                                             ? J{result.value().safe_error_summary}
                                             : result.value().result);
                else
                    report.set("error", result.error().safe_message);
                std::ofstream(argv[3]) << mira::to_json_string(report) << '\n';
                ocr->stop();
            })
            .get();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        exit_code = 1;
    }
    executor.shutdown(true);
    return exit_code;
}
