// M9-28/33: real-weight, controlled image replay. No desktop input or LLM calls.
// Usage: layered_perception_acceptance manifest output.json base.ppm changed.ppm blank.ppm
#include "../../integration/mirador/src/layered_visual_sampler.hpp"
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include "../support/fake_desktop_environment.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <mira/json.hpp>

namespace i = mirage::integration;
namespace d = mirage::desktop;
using Json = mira::JsonValue;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace {
Json integer(std::uint64_t n) { return Json{static_cast<std::int64_t>(n)}; }
Json bounds(const d::WindowGeometry &r) {
    return Json{Json::Array{Json{std::int64_t(r.x)}, Json{std::int64_t(r.y)},
                            Json{std::int64_t(r.width)}, Json{std::int64_t(r.height)}}};
}
Json work_json(const i::PerceptionRecognitionWork &w) {
    return Json{Json::Object{
        {"ocr", w.ocr}, {"detector", w.detector}, {"roi", w.roi ? bounds(*w.roi) : Json{}}}};
}
d::ImageFrame load(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    std::string magic;
    d::ImageFrame image;
    int limit = 0;
    in >> magic >> image.width >> image.height >> limit;
    in.get();
    if (!in || magic != "P6" || limit != 255 || image.width <= 0 || image.height <= 0 ||
        image.width > 1600 || image.height > 1600)
        throw std::runtime_error("invalid bounded P6 image");
    image.stride = std::size_t(image.width) * 4;
    image.pixels.resize(image.stride * image.height);
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            unsigned char rgb[3];
            if (!in.read(reinterpret_cast<char *>(rgb), 3))
                throw std::runtime_error("truncated P6");
            auto *p = image.pixels.data() + y * image.stride + x * 4;
            p[0] = rgb[2];
            p[1] = rgb[1];
            p[2] = rgb[0];
            p[3] = 255;
        }
    return image;
}
int run(kairo::Executor &executor, char **argv) {
    mirage::testing::FakeDesktopEnvironment environment;
    std::string error;
    auto pipeline = executor
                        .submit_auto([&] {
                            return i::PpOcrPipeline::create(executor, *environment.screen(),
                                                            argv[1], error, 5000, false, true);
                        })
                        .get();
    if (!pipeline)
        throw std::runtime_error(error);
    auto base = executor.submit_auto([&] { return load(argv[3]); }).get();
    auto changed = executor.submit_auto([&] { return load(argv[4]); }).get();
    auto blank = executor.submit_auto([&] { return load(argv[5]); }).get();
    i::detail::LayeredVisualSampler sampler(false);
    Json::Array rows, gates;
    bool pass = true;
    bool automatic_text_observed = false;
    std::uint64_t sequence = 0;
    const auto gate = [&](const char *name, bool ok) {
        gates.emplace_back(Json::Object{{"name", name}, {"pass", ok}});
        pass = pass && ok;
    };
    const auto observe = [&](d::ImageFrame &image, const std::string &name,
                             const i::PerceptionRecognitionWork &work,
                             const std::string &key = "real-weight-replay") {
        image.source_sequence = ++sequence;
        const auto at = Clock::now();
        const auto ocr = pipeline->backend_calls(), detector = pipeline->detector_calls();
        auto sample = pipeline->observe_image(image, key, {}, {}, at + 3s, at, 1, work);
        const auto end = Clock::now();
        Json::Array objects, events;
        for (const auto &entry : sample.objects) {
            const auto &o = entry.object;
            objects.emplace_back(Json::Object{{"bounds", bounds(o.bounds)},
                                              {"text", o.text},
                                              {"label", o.label},
                                              {"source", Json{std::int64_t(o.source)}},
                                              {"confirmed", o.identity_confirmed},
                                              {"pending", o.pending}});
        }
        for (const auto &e : sample.events)
            events.emplace_back(Json::Object{{"kind", e.kind},
                                             {"text", e.text},
                                             {"confirmed", e.confirmed},
                                             {"x", e.x},
                                             {"y", e.y},
                                             {"width", e.width},
                                             {"height", e.height}});
        if (name == "text-change")
            automatic_text_observed =
                sample.ok &&
                std::any_of(sample.events.begin(), sample.events.end(), [](const auto &e) {
                    return e.kind == "regional_text_observed" && !e.confirmed &&
                           e.text.find("SAVED") != std::string::npos;
                });
        bool committed = false, notify = false;
        auto frame = i::detail::sampled_frame(image, "real-replay", sequence, at);
        if (sample.ok && !sample.skipped) {
            if (work.roi) {
                auto accepted = sampler.recognize_region(frame, sample, {});
                committed = accepted.ok();
                notify = accepted && accepted.value();
            } else if (key == "real-weight-replay")
                committed = sampler.recognize(frame, sample, {}).ok();
        }
        rows.emplace_back(
            Json::Object{{"stage", name},
                         {"ok", sample.ok},
                         {"error", sample.error.code},
                         {"requested", work_json(work)},
                         {"actual", sample.recognition ? work_json(*sample.recognition) : Json{}},
                         {"ocr_calls", integer(pipeline->backend_calls() - ocr)},
                         {"detector_calls", integer(pipeline->detector_calls() - detector)},
                         {"wall_ms", std::chrono::duration<double, std::milli>(end - at).count()},
                         {"objects_complete", sample.objects_complete},
                         {"committed", committed},
                         {"notify", notify},
                         {"objects", Json{std::move(objects)}},
                         {"events", Json{std::move(events)}}});
        std::cout << name << " ok=" << sample.ok << " OCR=" << pipeline->backend_calls() - ocr
                  << " detector=" << pipeline->detector_calls() - detector << '\n'
                  << std::flush;
        return sample;
    };
    bool stable = false;
    i::PerceptionSample baseline;
    for (int attempt = 0; attempt < 4 && !stable; ++attempt) {
        baseline = observe(base, "bootstrap-" + std::to_string(attempt), {});
        stable = baseline.ok && baseline.objects_complete && !baseline.objects.empty() &&
                 std::all_of(baseline.objects.begin(), baseline.objects.end(), [](const auto &o) {
                     return o.content_observed && o.object.identity_confirmed && !o.object.pending;
                 });
    }
    gate("real_full_ocr_and_yolo", stable && pipeline->detector_calls() > 0);
    gate("base_text",
         std::any_of(baseline.objects.begin(), baseline.objects.end(), [](const auto &o) {
             return o.object.text.find("READY") != std::string::npos;
         }));
    const auto tick = [&](d::ImageFrame &image, const std::string &name) {
        auto frame = i::detail::sampled_frame(image, "real-replay", ++sequence, Clock::now());
        auto start = Clock::now();
        auto probe = sampler.sample(frame, 5s, {});
        if (!probe)
            throw std::runtime_error(probe.status().message());
        rows.emplace_back(Json::Object{
            {"stage", name + "-cv"},
            {"recognize", probe.value().recognize},
            {"reason", probe.value().reason},
            {"cumulative_class", Json{std::int64_t(probe.value().cumulative.classification)}},
            {"work", work_json(probe.value().work)},
            {"cv_width", Json{std::int64_t(probe.value().cv_width)}},
            {"cv_height", Json{std::int64_t(probe.value().cv_height)}},
            {"cv_bytes", Json{probe.value().cv_bytes}},
            {"cv_ms", std::chrono::duration<double, std::milli>(Clock::now() - start).count()}});
        if (probe.value().recognize)
            observe(image, name, probe.value().work);
        return probe.take_value();
    };
    auto previous_ocr = pipeline->backend_calls(), previous_detector = pipeline->detector_calls();
    for (int n = 0; n < 20; ++n)
        tick(base, "static");
    gate("static_20_zero_backend_calls", pipeline->backend_calls() == previous_ocr &&
                                             pipeline->detector_calls() == previous_detector);
    const auto selected = tick(changed, "text-change");
    gate("bounded_gray_cv_input", selected.cv_width <= i::detail::kCvMaxSide &&
                                      selected.cv_height <= i::detail::kCvMaxSide &&
                                      selected.cv_bytes == selected.cv_width * selected.cv_height &&
                                      selected.cv_width < changed.width);
    gate("automatic_text_only",
         selected.recognize && selected.work.roi && selected.work.ocr && !selected.work.detector);
    gate("automatic_changed_text_observed", automatic_text_observed);
    previous_ocr = pipeline->backend_calls();
    previous_detector = pipeline->detector_calls();
    for (int n = 0; n < 20; ++n)
        tick(changed, "static-after-change");
    gate("regional_static_20_zero_backend_calls",
         pipeline->backend_calls() == previous_ocr &&
             pipeline->detector_calls() == previous_detector);
    const double sx = double(changed.width) / 640, sy = double(changed.height) / 360;
    i::PerceptionRecognitionWork region{
        true, false, d::WindowGeometry{int(32 * sx), int(120 * sy), int(300 * sx), int(70 * sy)}};
    auto text = observe(changed, "explicit-text-roi", region);
    gate("real_text_roi",
         text.ok && text.recognition && text.recognition->ocr && !text.recognition->detector &&
             std::any_of(text.events.begin(), text.events.end(), [](const auto &e) {
                 return e.text.find("SAVED") != std::string::npos && !e.confirmed;
             }));
    region.ocr = false;
    region.detector = true;
    auto detection = observe(changed, "explicit-detector-roi", region);
    gate("real_detector_only_roi", detection.ok && detection.recognition &&
                                       !detection.recognition->ocr &&
                                       detection.recognition->detector);
    auto timer = executor.submit_delayed(5100, [] {});
    timer.future.get();
    auto aged = tick(changed, "age-audit");
    gate("age_forces_full_scan", aged.recognize && !aged.work.roi);
    auto negative = observe(blank, "blank", {}, "negative-control");
    gate("blank_negative", negative.ok && negative.objects_complete && negative.objects.empty());
    gate("no_capture_or_action_refs",
         environment.capture_calls == 0 && pipeline->registry().size() == 0);
    pipeline->stop();
    std::ofstream out(argv[2]);
    out << mira::to_json_string(Json{Json::Object{
        {"schema", Json{std::int64_t{1}}},
        {"source", "controlled P6 replay; real PP-OCRv5 and OmniParser; no desktop capture"},
        {"passed", pass},
        {"gates", Json{std::move(gates)}},
        {"steps", Json{std::move(rows)}}}});
    if (!out)
        throw std::runtime_error("report write failed");
    return pass ? 0 : 1;
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 6)
        return 2;
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = 2;
    config.max_threads = 4;
    if (!executor.initialize(config).ok)
        return 2;
    int result = 1;
    try {
        result = run(executor, argv);
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
    }
    executor.shutdown(true);
    return result;
}
