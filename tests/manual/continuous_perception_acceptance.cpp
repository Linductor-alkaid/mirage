// Private Xvfb/GTK/real PP-OCR sampling. Does not authorize or exercise Portal.
#include "../../integration/mira/src/perception_semantics.hpp"
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <mira/json.hpp>
#include <mirage/integration/continuous_desktop_session.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>
#include <sys/resource.h>
#include <thread>
namespace d = mirage::desktop;
namespace i = mirage::integration;
using J = mira::JsonValue;
using Clock = std::chrono::steady_clock;
namespace {
class Session final : public d::DesktopCaptureSession {
    d::ScreenProvider &screen_;
    std::string display_;
    std::uint64_t sequence_ = 0;

  public:
    Session(d::ScreenProvider &s, std::string id) : screen_(s), display_(std::move(id)) {}
    bool reusable() const override { return true; }
    d::CaptureOutcome capture(const d::CaptureLimits &limits,
                              const d::CancelToken &cancel) override {
        auto out = screen_.capture_display(display_, limits, cancel);
        if (out.ok)
            out.frame.source_sequence = ++sequence_;
        return out;
    }
};
} // namespace
int main(int argc, char **argv) {
    if (argc != 4 && argc != 5)
        return 2;
    const bool semantic_mode = argc == 5 && std::string_view(argv[4]) == "semantic";
    kairo::Executor executor;
    kairo::ExecutorConfig config;
    config.min_threads = 2;
    config.max_threads = 4;
    config.max_in_flight_tasks = 64;
    config.queue_capacity = 64;
    if (!executor.initialize(config))
        return 3;
    int result = 0;
    try {
        mirage::platform::linux_backend::LinuxDesktopEnvironment env{
            {}, {true, std::getenv("DISPLAY")}};
        const auto displays = env.screen()->list_displays({}, {});
        if (!displays.ok || displays.displays.empty())
            throw std::runtime_error("private display unavailable");
        std::string error;
        auto pipeline = i::PpOcrPipeline::create(executor, *env.screen(), argv[1], error);
        if (!pipeline)
            throw std::runtime_error("OCR: " + error);
        auto raw = std::make_unique<Session>(*env.screen(), displays.displays.front().id);
        if (!raw->capture({}, {}).ok)
            throw std::runtime_error("initial capture failed");
        i::PerceptionSessionOptions options;
        options.source_id = "private-transient-fixture";
        std::string last_analysis_error;
        i::ResidentDesktopPerception resident([&](auto source, auto lifetime) {
            return i::connect_continuous_desktop(
                executor, std::move(source), std::move(lifetime),
                [&](const auto &image, const auto &key, const auto &focus, const auto &cancel,
                    const auto &deadline) {
                    auto out = pipeline->observe_image(image, key, focus, cancel, deadline);
                    if (!out.ok && !out.error.message.empty()) {
                        last_analysis_error = out.error.code + ":" + out.error.message;
                        std::cerr << last_analysis_error << "\n";
                    }
                    return out;
                });
        });
        if (!resident.adopt(std::move(raw), options))
            throw std::runtime_error("resident perception admission failed");
        auto connection = resident.attach(false, {});
        if (!connection.session || !connection.feed)
            throw std::runtime_error("resident consumer admission failed");
        std::cout << "READY\n" << std::flush;
        const auto end = Clock::now() + std::chrono::milliseconds(std::stoi(argv[2]));
        J::Array events;
        J::Array semantic_samples;
        i::detail::PerceptionSemantics semantics;
        std::string previous_semantics;
        std::uint64_t annotated = 0, annotation_failed = 0;
        std::uint64_t seen = 0;
        std::size_t peak_journal = 0, peak_previews = 0;
        while (Clock::now() < end) {
            const auto view = connection.feed->view();
            if (semantic_mode) {
                semantics.sync(view);
                const auto projection = semantics.project();
                J::Array objects;
                for (const auto &object : *projection.find("objects")->as_array()) {
                    J record = object;
                    record.set("object_ref", "");
                    objects.push_back(std::move(record));
                }
                // Projection order rotates for fairness; it is not a scene change.
                std::sort(objects.begin(), objects.end(), [](const auto &a, const auto &b) {
                    return a.find("entity")->as_integer() < b.find("entity")->as_integer();
                });
                const auto serialized = mira::to_json_string(J{objects});
                if (serialized != previous_semantics && semantic_samples.size() < 256) {
                    semantic_samples.emplace_back(J::Object{
                        {"observed_s",
                         std::chrono::duration<double>(Clock::now().time_since_epoch()).count()},
                        {"objects", J{std::move(objects)}},
                        {"error", projection.find("error") ? *projection.find("error") : J{}}});
                    previous_semantics = serialized;
                }
                semantics.consumed("private-scripted-annotation");
                for (const auto &object : *projection.find("objects")->as_array()) {
                    const auto &ref = *object.find("object_ref")->as_string();
                    if (!ref.empty() && *object.find("semantic_state")->as_string() != "active") {
                        const auto annotation =
                            semantics.annotate(ref, "task:" + *object.find("text")->as_string());
                        if (annotation.ok)
                            ++annotated;
                        else
                            ++annotation_failed;
                    }
                }
            }
            peak_journal = std::max(peak_journal, view.journal_bytes);
            peak_previews = std::max(peak_previews, view.preview_bytes);
            for (const auto &e : view.events) {
                if (e.sequence <= seen)
                    continue;
                seen = e.sequence;
                events.emplace_back(J::Object{
                    {"sequence", static_cast<std::int64_t>(e.sequence)},
                    {"kind", e.kind},
                    {"text", e.text},
                    {"confirmed", e.confirmed},
                    {"entity", static_cast<std::int64_t>(e.entity)},
                    {"x", e.x},
                    {"y", e.y},
                    {"width", e.width},
                    {"height", e.height},
                    {"observed_s",
                     std::chrono::duration<double>(e.observed_at.time_since_epoch()).count()},
                    {"delivered_s",
                     std::chrono::duration<double>(Clock::now().time_since_epoch()).count()}});
            }
            if (seen)
                (void)connection.feed->acknowledge(seen);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        const auto view = connection.feed->view();
        struct rusage usage {};
        getrusage(RUSAGE_SELF, &usage);
        connection.session.reset();
        connection.feed.reset();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        auto next_task = resident.attach(false, {});
        const auto continuation_deadline = Clock::now() + std::chrono::seconds(4);
        while (next_task.feed && next_task.feed->view().frames <= view.frames &&
               Clock::now() < continuation_deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        const auto resident_frames = next_task.feed ? next_task.feed->view().frames : 0;
        const bool continued = resident_frames > view.frames;
        if (!continued)
            result = 1;
        next_task.session.reset();
        next_task.feed.reset();
        resident.stop();
        J report{
            J::Object{{"last_analysis_error", last_analysis_error},
                      {"resident_continued_after_task", continued},
                      {"resident_frames_after_task", static_cast<std::int64_t>(resident_frames)},
                      {"semantic_samples", J{std::move(semantic_samples)}},
                      {"scripted_annotations", static_cast<std::int64_t>(annotated)},
                      {"annotation_failed", static_cast<std::int64_t>(annotation_failed)},
                      {"events", J{std::move(events)}},
                      {"state", view.state},
                      {"error", view.error},
                      {"frames", static_cast<std::int64_t>(view.frames)},
                      {"analyzed", static_cast<std::int64_t>(view.analyzed)},
                      {"skipped", static_cast<std::int64_t>(view.skipped)},
                      {"analysis_failed", static_cast<std::int64_t>(view.analysis_failed)},
                      {"dropped", static_cast<std::int64_t>(view.dropped)},
                      {"peak_journal_bytes", static_cast<std::int64_t>(peak_journal)},
                      {"peak_preview_bytes", static_cast<std::int64_t>(peak_previews)},
                      {"process_peak_rss_kib", static_cast<std::int64_t>(usage.ru_maxrss)},
                      {"vlm_calls", std::int64_t{0}},
                      {"png_encodes", std::int64_t{0}}}};
        std::ofstream output(argv[3]);
        output << mira::to_json_string(report);
        if (!output)
            throw std::runtime_error("report write failed");
        connection.session.reset();
        pipeline->stop();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        result = 1;
    }
    executor.shutdown(true);
    return result;
}
