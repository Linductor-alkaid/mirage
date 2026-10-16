// M7-20: export actual ScreenProvider pixels and production Mirador marks.
// Exit success means execution succeeded; quality requires visual review.
#include "../../integration/mirador/src/ppocr_pipeline.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <mira/json.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_session_host.hpp>
#include <mirage/platform/linux/linux_desktop_environment.hpp>

namespace d = mirage::desktop;
namespace i = mirage::integration;
using Json = mira::JsonValue;
namespace {
void save(const std::filesystem::path &path, const d::ImageFrame &frame) {
    if (frame.width <= 0 || frame.height <= 0 ||
        frame.stride < static_cast<std::size_t>(frame.width) * 4 ||
        frame.pixels.size() < frame.stride * static_cast<std::size_t>(frame.height))
        throw std::runtime_error("invalid exported image");
    std::ofstream out(path, std::ios::binary);
    out << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            const auto *p = frame.pixels.data() + static_cast<std::size_t>(y) * frame.stride +
                            static_cast<std::size_t>(x) * 4;
            const char rgb[] = {static_cast<char>(p[2]), static_cast<char>(p[1]),
                                static_cast<char>(p[0])};
            out.write(rgb, 3);
        }
    if (!out)
        throw std::runtime_error("image write failed");
}
Json rect(const d::WindowGeometry &r) {
    return Json{Json::Array{Json{std::int64_t{r.x}}, Json{std::int64_t{r.y}},
                            Json{std::int64_t{r.width}}, Json{std::int64_t{r.height}}}};
}
void run(kairo::Executor &executor, int argc, char **argv) {
    const std::filesystem::path output(argv[2]);
    std::filesystem::create_directories(output);
    const bool desktop = std::string(argv[3]) == "--desktop";
    mirage::platform::linux_backend::LinuxDesktopEnvironment environment(
        {}, {true, desktop ? "" : argv[3]});
    auto *screen = environment.screen();
    if (!screen)
        throw std::runtime_error("ScreenProvider unavailable");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(desktop ? 45 : 5);
    const d::CancelToken cancel(
        [deadline] { return std::chrono::steady_clock::now() >= deadline; });
    Json::Object report{{"schema", std::int64_t{1}}, {"quality_review", "pending"}};
    if (desktop) {
        // Read-only task-owned Portal session; no remembered grant or input request.
        auto session = screen->create_desktop_capture_session();
        if (!session)
            throw std::runtime_error("desktop capture session unavailable");
        auto captured = session->capture({32u << 20}, cancel);
        if (!captured.ok)
            throw std::runtime_error(captured.error.code + ": " + captured.error.message);
        save(output / "raw.ppm", captured.frame);
        report.emplace_back("source", "Wayland Portal ScreenProvider; capture only");
        report.emplace_back("width", std::int64_t{captured.frame.width});
        report.emplace_back("height", std::int64_t{captured.frame.height});
    } else {
        std::optional<d::WindowGeometry> roi;
        if (argc == 8)
            roi = d::WindowGeometry{std::stoi(argv[4]), std::stoi(argv[5]), std::stoi(argv[6]),
                                    std::stoi(argv[7])};
        std::string error;
        auto pipeline = i::PpOcrPipeline::create(executor, *screen, argv[1], error);
        if (!pipeline)
            throw std::runtime_error(error);
        auto result = pipeline->mark(roi, cancel, deadline);
        if (!result.captured || !result.analyzed)
            throw std::runtime_error(
                result.capture_error.code + ": " + result.capture_error.message + " / " +
                result.analysis_error.code + ": " + result.analysis_error.message);
        save(output / "raw.ppm", result.frame);
        save(output / "marked.ppm", result.marked_frame);
        Json::Array regions, marks;
        for (const auto &r : result.publish.snapshot.regions)
            regions.emplace_back(Json::Object{{"ref", r.ref},
                                              {"source", std::int64_t{static_cast<int>(r.source)}},
                                              {"bounds", rect(r.bounds)},
                                              {"text", r.text},
                                              {"confidence", r.confidence}});
        for (const auto &m : result.marks)
            marks.emplace_back(Json::Object{{"number", std::int64_t{m.number}},
                                            {"ref", m.ref},
                                            {"anchor_x", std::int64_t{m.anchor.x}},
                                            {"anchor_y", std::int64_t{m.anchor.y}},
                                            {"selectable", m.selectable}});
        report.emplace_back("source", "X11 ScreenProvider -> production PpOcrPipeline::mark");
        report.emplace_back("image_geometry", rect(result.image_geometry));
        report.emplace_back("generation", static_cast<std::int64_t>(result.visual_generation));
        report.emplace_back("frame_sequence", static_cast<std::int64_t>(result.frame_sequence));
        report.emplace_back("detector_backend_calls",
                            static_cast<std::int64_t>(pipeline->detector_calls()));
        report.emplace_back("backend_calls", static_cast<std::int64_t>(pipeline->backend_calls()));
        report.emplace_back("model_revision", pipeline->model_revision());
        report.emplace_back("regions", Json{std::move(regions)});
        report.emplace_back("marks", Json{std::move(marks)});
        pipeline->stop();
        if (std::getenv("MIRAGE_REVIEW_GEOMETRY_DIAG")) {
            // Diagnose proposal recall before OCR/fusion/product area filtering.
            auto frame = i::to_mirador_frame(result.frame, "review.geometry", 1,
                                             std::chrono::steady_clock::now());
            if (!frame)
                throw std::runtime_error(frame.status().message());
            i::VisualSessionHost host(executor, "review.geometry");
            if (!host.start(error))
                throw std::runtime_error(error);
            i::VisualAnalysisRequest request;
            request.frame = frame.take_value();
            request.geometry.run = true;
            request.deadline = deadline;
            request.external_cancel = cancel;
            auto geometry = host.submit(std::move(request)).get();
            host.stop();
            if (geometry.outcome.kind != i::VisualAnalysisOutcome::Kind::kCompleted)
                throw std::runtime_error(geometry.outcome.message);
            Json::Array proposals;
            for (const auto &p : geometry.geometry_proposals)
                proposals.emplace_back(
                    Json::Object{{"bounds", rect({p.tight_bounds.x, p.tight_bounds.y,
                                                  p.tight_bounds.width, p.tight_bounds.height})},
                                 {"closure_score", p.closure_score}});
            report.emplace_back("geometry_before_fusion", Json{std::move(proposals)});
        }
    }
    std::ofstream out(output / "report.json");
    out << mira::to_json_string(Json{std::move(report)}) << '\n';
    if (!out)
        throw std::runtime_error("report write failed");
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 4 && argc != 8) {
        std::cerr << "usage: mirador_visual_review MANIFEST OUTPUT DISPLAY|--desktop [X Y W H]\n";
        return 2;
    }
    kairo::Executor executor;
    if (!executor.initialize({}).ok)
        return 2;
    int code = 0;
    try {
        // A finite task, observed future; workers stop before the owner shuts down.
        executor.submit_auto([&] { run(executor, argc, argv); }).get();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        code = 1;
    }
    executor.shutdown(true);
    return code;
}
