// Controlled replay resource/latency gate. No host capture, input or LLM calls.
#include "../../integration/mirador/src/layered_visual_sampler.hpp"
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include "../support/fake_desktop_environment.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <mira/json.hpp>
#include <mirage/integration/continuous_desktop_session.hpp>
#include <sys/resource.h>

namespace i = mirage::integration;
namespace d = mirage::desktop;
using Json = mira::JsonValue;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
std::int64_t ticks(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}
d::ImageFrame load(const char *path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    int maximum = 0;
    d::ImageFrame image;
    in >> magic >> image.width >> image.height >> maximum;
    in.get();
    if (!in || magic != "P6" || maximum != 255 || image.width < 1 || image.height < 1 ||
        image.width > 1600 || image.height > 1600)
        throw std::runtime_error("invalid bounded replay image");
    image.stride = std::size_t(image.width) * 4;
    image.pixels.resize(image.stride * image.height);
    for (std::size_t n = 0; n < image.pixels.size(); n += 4) {
        unsigned char rgb[3];
        if (!in.read(reinterpret_cast<char *>(rgb), 3))
            throw std::runtime_error("truncated replay");
        image.pixels[n] = rgb[2];
        image.pixels[n + 1] = rgb[1];
        image.pixels[n + 2] = rgb[0];
        image.pixels[n + 3] = 255;
    }
    return image;
}
struct Replay {
    d::ImageFrame base, changed, blank;
    std::atomic<std::int64_t> begin{0};
    std::int64_t duration = 0;
    std::vector<std::int64_t> captured; // Single capture owner; read only after join.
    std::uint64_t sequence = 0;
};
class Source final : public d::DesktopCaptureSession {
    std::shared_ptr<Replay> replay_;

  public:
    explicit Source(std::shared_ptr<Replay> replay) : replay_(std::move(replay)) {}
    bool reusable() const override { return true; }
    d::CaptureOutcome capture(const d::CaptureLimits &, const d::CancelToken &cancel) override {
        if (cancel.cancelled())
            return {false, true, {}, {"cancelled", "replay stopped"}};
        const auto now = ticks(Clock::now()), begin = replay_->begin.load();
        const auto age = begin ? now - begin : -1;
        const auto &pixels = age < replay_->duration / 3       ? replay_->base
                             : age < replay_->duration * 2 / 3 ? replay_->changed
                                                               : replay_->blank;
        auto image = pixels;
        image.source_sequence = ++replay_->sequence;
        if (age >= 0 && age < replay_->duration && replay_->captured.size() < 10000)
            replay_->captured.push_back(now);
        return {true, false, std::move(image), {}};
    }
};
double cpu(const rusage &r) {
    return r.ru_utime.tv_sec + r.ru_stime.tv_sec + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e6;
}
double percentile(std::vector<double> samples, double quantile) {
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    return samples[std::min(samples.size() - 1, std::size_t(quantile * samples.size()))];
}
int run(kairo::Executor &executor, char **argv) {
    const std::string mode = argv[3];
    const int hz = std::stoi(argv[4]), seconds = std::stoi(argv[5]);
    if ((mode != "layered" && mode != "legacy") || (hz != 5 && hz != 10) || seconds < 15 ||
        seconds > 300)
        throw std::runtime_error("invalid benchmark profile");
    auto replay = std::make_shared<Replay>();
    replay->base = load(argv[6]);
    replay->changed = load(argv[7]);
    replay->blank = load(argv[8]);
    if (replay->base.width != replay->changed.width ||
        replay->base.height != replay->changed.height ||
        replay->base.width != replay->blank.width || replay->base.height != replay->blank.height)
        throw std::runtime_error("replay dimensions differ");
    replay->duration = std::int64_t(seconds) * 1000000000;
    replay->captured.reserve(10000);
    mirage::testing::FakeDesktopEnvironment environment;
    std::string error;
    // Both arms load the same OCR/YOLO capabilities. Only scheduling differs;
    // this is not the older OCR-only product's CPU baseline.
    auto pipeline = executor
                        .submit_auto([&] {
                            return i::PpOcrPipeline::create(executor, *environment.screen(),
                                                            argv[1], error, 5000, false, true);
                        })
                        .get();
    if (!pipeline)
        throw std::runtime_error(error);
    i::PerceptionSessionOptions options;
    options.source_id = "production-replay";
    options.sample_period = std::chrono::milliseconds(1000 / hz);
    auto source =
        mode == "layered"
            ? i::connect_layered_desktop(
                  executor, std::make_unique<Source>(replay), options,
                  [&](const auto &image, const auto &key, const auto &focus, const auto &cancel,
                      auto deadline, auto at, auto generation, const auto &work) {
                      return pipeline->observe_image(image, key, focus, cancel, deadline, at,
                                                     generation, work);
                  },
                  5s, true)
            : i::connect_continuous_desktop(
                  executor, std::make_unique<Source>(replay), options,
                  [&](const auto &image, const auto &key, const auto &focus, const auto &cancel,
                      const auto &deadline) {
                      return pipeline->observe_image(image, key, focus, cancel, deadline);
                  });
    if (!source.session || !source.feed)
        throw std::runtime_error("source admission failed");
    auto warmup = executor.submit_delayed(5000, [] {});
    warmup.future.get();
    rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    const auto begin = Clock::now();
    replay->begin = ticks(begin);
    auto timer = executor.submit_delayed(seconds * 1000, [] {});
    double saved_latency = -1;
    std::size_t max_pixels = 0;
    while (timer.future.wait_for(50ms) != std::future_status::ready) {
        const auto view = source.feed->view();
        if (view.dispatch)
            max_pixels = std::max(max_pixels, view.dispatch->retained_pixel_bytes);
        if (saved_latency < 0)
            for (const auto &event : view.events)
                if (event.text.find("SAVED") != std::string::npos) {
                    saved_latency =
                        std::chrono::duration<double>(Clock::now() - begin).count() - seconds / 3.0;
                    break;
                }
    }
    timer.future.get();
    const auto view = source.feed->view();
    source.session.reset(); // Joins the producer before reading its capture log.
    pipeline->stop();
    getrusage(RUSAGE_SELF, &after);
    const auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    std::vector<double> intervals;
    for (std::size_t n = 1; n < replay->captured.size(); ++n)
        intervals.push_back(double(replay->captured[n] - replay->captured[n - 1]) / 1e6);
    const double rate = double(replay->captured.size()) / seconds;
    const double cpu_percent = (cpu(after) - cpu(before)) / elapsed * 100;
    Json::Array gates;
    bool passed = true;
    const auto gate = [&](const char *name, bool ok) {
        gates.emplace_back(Json::Object{{"name", name}, {"pass", ok}});
        passed = passed && ok;
    };
    gate("saved_observed", saved_latency >= 0 && saved_latency <= 3.0 + 2.0 / hz);
    gate("no_analysis_failure", view.analysis_failed == 0);
    gate("no_host_capture_or_refs",
         environment.capture_calls == 0 && pipeline->registry().size() == 0);
    gate("pixel_budget", max_pixels <= 20u * 1024 * 1024);
    // DEC-128 capacity revision: the 1600px fixed-model legacy arm already
    // exceeds 768MiB. Keep that failed goal visible; qualify HD deployment at
    // 1GiB without reducing model/recognition resolution to pass a memory gate.
    const std::int64_t rss_budget_kib =
        std::max(replay->base.width, replay->base.height) <= 640 ? 768 * 1024 : 1024 * 1024;
    if (mode == "layered") {
        gate("sample_rate", rate >= hz * .9);
        gate("sample_interval_p95", percentile(intervals, .95) <= 1250.0 / hz);
        gate("cpu_budget", cpu_percent <= 150);
        gate("rss_profile_budget", after.ru_maxrss <= rss_budget_kib);
    }
    Json report{Json::Object{
        {"mode", mode},
        {"hz", Json{std::int64_t(hz)}},
        {"seconds", Json{std::int64_t(seconds)}},
        {"passed", passed},
        {"gates", Json{std::move(gates)}},
        {"sample_rate", rate},
        {"interval_p50_ms", percentile(intervals, .5)},
        {"interval_p95_ms", percentile(intervals, .95)},
        {"interval_max_ms", percentile(intervals, 1)},
        {"cpu_percent", cpu_percent},
        {"peak_rss_kib", Json{std::int64_t(after.ru_maxrss)}},
        {"rss_profile_budget_kib", Json{rss_budget_kib}},
        {"original_768mib_pass", after.ru_maxrss <= 768 * 1024},
        {"saved_latency_seconds", saved_latency},
        {"elapsed_with_drain", elapsed},
        {"frames", Json{std::int64_t(view.frames)}},
        {"analyzed", Json{std::int64_t(view.analyzed)}},
        {"analysis_failed", Json{std::int64_t(view.analysis_failed)}},
        {"ocr_calls", Json{std::int64_t(pipeline->backend_calls())}},
        {"detector_calls", Json{std::int64_t(pipeline->detector_calls())}},
        {"max_retained_pixels", Json{std::int64_t(max_pixels)}},
        {"scope", "owned pixel replay; capture transport/LLM/video/audio not evaluated"}}};
    std::ofstream out(argv[2]);
    out << mira::to_json_string(report);
    if (!out)
        throw std::runtime_error("cannot write resource report");
    std::cout << mira::to_json_string(report) << '\n';
    return passed ? 0 : 1;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 9)
        return 2;
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = 2;
    config.max_threads = 4;
    config.queue_capacity = 64;
    config.max_in_flight_tasks = 64;
    if (!executor.initialize(config).ok)
        return 2;
    int status = 2;
    try {
        status = run(executor, argv);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    executor.shutdown(true);
    return status;
}
