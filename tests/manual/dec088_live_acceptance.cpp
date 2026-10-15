// DEC-088 live acceptance driver (opt-in manual tool, never registered in
// ctest): drives the production ModelLayer - the pinned socket provider,
// real endpoint - over scripted desktop capture rounds and records the
// provider-reported usage of every model request. The API key is read once
// from the environment and resolved only inside the transport; reports carry
// token counts and completion flags, never credentials or message content.
//
//   dec088_live_acceptance compaction <report.json> [--rounds N] [--window W]
//       capability 2/3: repeated-capture task; window 0 is the raw-replay
//       baseline, a declared window engages the Layer 0 seam (0.45/0.70/0.90
//       desktop watermarks).
//   dec088_live_acceptance reuse <report.json> [--rounds N] [--no-identity]
//       capability 1: identical re-captures advance the cross-turn count;
//       --no-identity runs the unannotated control.
//   dec088_live_acceptance continuation <report.json> [--trials N]
//       [--max-output T]
//       capability 4: a tiny per-call output bound forces tolerated max_tokens
//       cutoffs; the anchored continuation must still land a coherent answer.
//
// Key environment variable: DEC088_LIVE_KEY.
#include "../support/fake_desktop_environment.hpp"
#include <kairo/executor.hpp>
#include <mira/environment.hpp>
#include <mira/json.hpp>
#include <mirage/desktop/action_coordinator.hpp>
#include <mirage/integration/model_layer.hpp>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr const char *kKeyEnv = "DEC088_LIVE_KEY";

struct UsageSample {
    std::uint64_t input = 0;
    std::uint64_t output = 0;
    std::uint64_t reasoning = 0;
};

struct Options {
    std::string scenario;
    std::string report_path;
    int rounds = 14;
    std::uint64_t window = 0;
    bool identity = true;
    int max_output = 2304;
    int trials = 1;
};

mira::JsonValue sample_json(const std::vector<UsageSample> &samples) {
    mira::JsonValue::Array curve;
    for (const auto &sample : samples) {
        curve.emplace_back(mira::JsonValue::Object{
            {"input", mira::JsonValue{static_cast<std::int64_t>(sample.input)}},
            {"output", mira::JsonValue{static_cast<std::int64_t>(sample.output)}},
            {"reasoning", mira::JsonValue{static_cast<std::int64_t>(sample.reasoning)}},
        });
    }
    return mira::JsonValue{std::move(curve)};
}

int run(const Options &options) {
    const char *key = std::getenv(kKeyEnv);
    if (key == nullptr || std::string(key).empty()) {
        std::cerr << "environment variable " << kKeyEnv << " must carry the API key\n";
        return 2;
    }

    kairo::Executor executor;
    kairo::ExecutorConfig executor_config;
    executor_config.min_threads = executor_config.max_threads = 3;
    if (!static_cast<bool>(executor.initialize(executor_config))) {
        std::cerr << "executor initialize failed\n";
        return 2;
    }

    auto env = std::make_shared<mirage::testing::FakeDesktopEnvironment>();
    env->displays = {{"d1", {-120, -40, 128, 64}, true}};
    env->windows = {{"w1", "Fixture", {-100, -30, 32, 16}, true}};
    mirage::integration::DesktopToolContext tools;
    tools.environment = env;
    tools.actions = std::make_shared<mirage::desktop::ActionCoordinator>();
    tools.gate = [](const auto &capability, const auto &, const auto &) {
        return capability == "screen.capture";
    };

    mirage::integration::ModelLayerConfig config;
    config.enabled = config.supports_images = true;
    config.dialect = "anthropic.messages.v1";
    config.endpoint_origin = "https://api.deepseek.com";
    config.api_prefix = "/anthropic";
    config.model_selector = "deepseek-flash";
    config.display_name = "DEC-088 live acceptance";
    config.credential_env = kKeyEnv;
    config.context_window_tokens = options.window;
    config.max_output_tokens = static_cast<std::uint64_t>(options.max_output);
    config.supply_input_reuse_identity = options.identity;

    std::vector<UsageSample> samples;
    std::mutex samples_mutex;
    config.usage_observer = [&](const mira::ModelUsage &usage) {
        UsageSample sample;
        if (usage.input_tokens)
            sample.input = *usage.input_tokens;
        if (usage.output_tokens)
            sample.output = *usage.output_tokens;
        if (usage.reasoning_tokens)
            sample.reasoning = *usage.reasoning_tokens;
        std::lock_guard lock(samples_mutex);
        samples.push_back(sample);
    };

    std::string task;
    if (options.scenario == "compaction") {
        task = "请连续截屏 " + std::to_string(options.rounds) +
               " 次：每次都调用 desktop.screen.capture 工具截取 d1 显示器，并在每次"
               "截屏后用一句话说明这是第几次截屏。全部完成后，用最后一条消息报告"
               "你总共完成的截屏次数并结束。不要跳过任何一次。";
    } else if (options.scenario == "reuse") {
        task = "请对 d1 显示器连续截屏 " + std::to_string(options.rounds) +
               " 次（每次都调用 desktop.screen.capture），每次截屏后用一句话报告"
               "屏幕内容是否变化。全部完成后报告结论并结束。";
    } else if (options.scenario == "continuation") {
        // The demand must exceed the per-call output bound so the reply is
        // genuinely cut and the anchored continuation carries the rest.
        task = "请用至少 600 字的中文详细说明：为什么桌面自动化在执行键鼠操作前"
               "需要先观察屏幕状态。要求分点论述，每点都要展开说明，内容充实，"
               "写满 600 字以上再结束。";
    } else {
        std::cerr << "unknown scenario: " << options.scenario << "\n";
        return 2;
    }

    mirage::integration::ModelLayer model(executor, config, /*provider_override=*/nullptr, tools);
    if (!model.running()) {
        std::cerr << "model layer failed to assemble (endpoint, dialect, credential)\n";
        return 2;
    }

    mira::JsonValue::Array trial_reports;
    int failed_trials = 0;
    for (int trial = 0; trial < std::max(options.trials, 1); ++trial) {
        mira::OperationContext context = mira::make_control_context();
        context.task = mira::TaskId::generate();
        context.session = mira::SessionId::generate();
        context.task_epoch = 7;
        const std::size_t before = samples.size();
        auto future =
            executor.submit_auto([&] { return model.complete_harness_turn("", task, context); });
        const auto completion = future.get();

        std::uint64_t input_total = 0, output_total = 0, reasoning_total = 0;
        std::vector<UsageSample> curve(samples.begin() + static_cast<long>(before), samples.end());
        for (const auto &sample : curve) {
            input_total += sample.input;
            output_total += sample.output;
            reasoning_total += sample.reasoning;
        }
        const bool trial_ok = completion.ok && !completion.failed && !completion.cancelled &&
                              !completion.reply_text.empty();
        if (!trial_ok)
            ++failed_trials;
        std::cout << "trial " << trial << ": ok=" << completion.ok << " requests=" << curve.size()
                  << " input_total=" << input_total << " final_input="
                  << (completion.input_tokens ? std::to_string(*completion.input_tokens) : "n/a")
                  << " transforms=" << completion.history_transforms
                  << " degradations=" << completion.history_transform_degradations
                  << " reuse_annotations=" << completion.supplied_input_reuse_annotations
                  << " steps=" << completion.model_steps << " tool_calls=" << completion.tool_calls
                  << " reply_chars=" << completion.reply_text.size() << '\n';
        trial_reports.emplace_back(mira::JsonValue::Object{
            {"trial", mira::JsonValue{static_cast<std::int64_t>(trial)}},
            {"ok", mira::JsonValue{completion.ok}},
            {"failed", mira::JsonValue{completion.failed}},
            {"cancelled", mira::JsonValue{completion.cancelled}},
            {"error", mira::JsonValue{completion.error}},
            {"requests", mira::JsonValue{static_cast<std::int64_t>(curve.size())}},
            {"input_total", mira::JsonValue{static_cast<std::int64_t>(input_total)}},
            {"output_total", mira::JsonValue{static_cast<std::int64_t>(output_total)}},
            {"reasoning_total", mira::JsonValue{static_cast<std::int64_t>(reasoning_total)}},
            {"final_input_tokens",
             completion.input_tokens
                 ? mira::JsonValue{static_cast<std::int64_t>(*completion.input_tokens)}
                 : mira::JsonValue{}},
            {"history_transforms",
             mira::JsonValue{static_cast<std::int64_t>(completion.history_transforms)}},
            {"history_transform_degradations",
             mira::JsonValue{static_cast<std::int64_t>(completion.history_transform_degradations)}},
            {"supplied_input_reuse_annotations", mira::JsonValue{static_cast<std::int64_t>(
                                                     completion.supplied_input_reuse_annotations)}},
            {"model_steps", mira::JsonValue{static_cast<std::int64_t>(completion.model_steps)}},
            {"tool_calls", mira::JsonValue{static_cast<std::int64_t>(completion.tool_calls)}},
            {"reply_chars",
             mira::JsonValue{static_cast<std::int64_t>(completion.reply_text.size())}},
            {"usage_curve", sample_json(curve)},
        });
    }

    model.shutdown();
    executor.shutdown(true);

    mira::JsonValue report = mira::JsonValue::Object{
        {"scenario", mira::JsonValue{options.scenario}},
        {"rounds", mira::JsonValue{static_cast<std::int64_t>(options.rounds)}},
        {"window", mira::JsonValue{static_cast<std::int64_t>(options.window)}},
        {"identity", mira::JsonValue{options.identity}},
        {"max_output", mira::JsonValue{static_cast<std::int64_t>(options.max_output)}},
        {"endpoint", mira::JsonValue{config.endpoint_origin}},
        {"model", mira::JsonValue{config.model_selector}},
        {"failed_trials", mira::JsonValue{static_cast<std::int64_t>(failed_trials)}},
        {"trials", mira::JsonValue{std::move(trial_reports)}},
    };
    std::ofstream out(options.report_path);
    out << mira::to_json_string(report) << '\n';
    std::cout << "report written: " << options.report_path << '\n';
    return failed_trials == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0]
                  << " <compaction|reuse|continuation> <report.json>"
                     " [--rounds N] [--window W] [--no-identity] [--trials N]"
                     " [--max-output T]\n";
        return 2;
    }
    Options options;
    options.scenario = argv[1];
    options.report_path = argv[2];
    for (int i = 3; i < argc; ++i) {
        const std::string flag = argv[i];
        const auto next = [&i, argc, argv] {
            return ++i < argc ? std::string(argv[i]) : std::string{};
        };
        if (flag == "--rounds") {
            options.rounds = std::stoi(next());
        } else if (flag == "--window") {
            options.window = std::stoull(next());
        } else if (flag == "--no-identity") {
            options.identity = false;
        } else if (flag == "--trials") {
            options.trials = std::stoi(next());
        } else if (flag == "--max-output") {
            options.max_output = std::stoi(next());
        } else {
            std::cerr << "unknown flag: " << flag << "\n";
            return 2;
        }
    }
    try {
        return run(options);
    } catch (const std::exception &failure) {
        std::cerr << "driver failed: " << failure.what() << '\n';
        return 1;
    }
}
