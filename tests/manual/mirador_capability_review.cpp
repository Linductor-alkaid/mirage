// M7-21/22: real captured pixels, public Mirador APIs, observed Executor task.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <kairo/executor.hpp>
#include <mira/json.hpp>
#include <mirador/change_detection.hpp>
#include <mirador/color_convert.hpp>
#include <mirador/crop.hpp>
#include <mirador/geometric_proposal.hpp>
#include <mirador/object_tracker.hpp>
#include <mirador/patch_fingerprint.hpp>
#include <mirador/perception_session.hpp>
#include <mirador/segment_growing_line_detector.hpp>
#include <mirador/set_of_mark.hpp>
#include <mirador/shift_estimation.hpp>
#include <mirador/visual_index.hpp>
#include <ppocr_backend.hpp>

using J = mira::JsonValue;
namespace m = mirador;
namespace fs = std::filesystem;
namespace {
template <class T> T take(m::Result<T> result) {
    if (!result)
        throw std::runtime_error(result.status().message());
    return result.take_value();
}
void require(m::Result<void> result) {
    if (!result)
        throw std::runtime_error(result.status().message());
}
m::Frame load(const fs::path &file, std::uint64_t sequence = 1) {
    std::ifstream in(file, std::ios::binary);
    std::string magic;
    int w = 0, h = 0, max = 0;
    in >> magic >> w >> h >> max;
    in.get();
    if (magic != "P6" || w <= 0 || h <= 0 || w > 1800 || h > 1100 || max != 255)
        throw std::runtime_error("invalid bounded PPM: " + file.string());
    auto pixels = std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(w) * h * 3);
    if (!in.read(reinterpret_cast<char *>(pixels->data()),
                 static_cast<std::streamsize>(pixels->size())))
        throw std::runtime_error("short PPM");
    m::Frame frame;
    frame.image.data = pixels->data();
    frame.image.width = w;
    frame.image.height = h;
    frame.image.row_stride_bytes = w * 3;
    frame.image.format = m::PixelFormat::kRgb8;
    frame.sequence = sequence;
    frame.source_id = "review";
    frame.owner = pixels;
    frame.timestamp = std::chrono::steady_clock::now();
    return frame;
}
J box(m::RectI b) {
    return J{J::Array{J{std::int64_t{b.x}}, J{std::int64_t{b.y}}, J{std::int64_t{b.width}},
                      J{std::int64_t{b.height}}}};
}
void save(const fs::path &p, const m::MarkedImage &image) {
    std::ofstream out(p, std::ios::binary);
    out << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    out.write(reinterpret_cast<const char *>(image.pixels.data()),
              static_cast<std::streamsize>(image.pixels.size()));
    if (!out)
        throw std::runtime_error("write failed");
}
J geometry(const fs::path &root, const fs::path &out) {
    J::Array rows;
    for (const auto *name : {"geometry", "form-light", "host-crop"}) {
        const auto frame = load(root / "samples" / name / "raw.ppm");
        auto gray = take(m::convert_color(frame.image, m::PixelFormat::kGray8, 4u << 20));
        for (const double length : {8., 16., 24.})
            for (const double distance : {1.5, 4.})
                for (const double radius : {4., 8., 12.})
                    for (const double deviation : {1.5, 2.5, 4.}) {
                        if (deviation != 1.5 && (distance != 1.5 || radius != 4))
                            continue;
                        const auto start = std::chrono::steady_clock::now();
                        m::SegmentGrowingParams sp;
                        sp.min_length = length;
                        sp.deviation_tolerance = deviation;
                        m::SegmentGrowingLineDetector detector(sp);
                        auto detected = detector.detect(gray.view(), {}, {});
                        if (!detected) {
                            rows.emplace_back(J::Object{{"scene", name},
                                                        {"min_length", length},
                                                        {"merge_distance", distance},
                                                        {"endpoint_radius", radius},
                                                        {"deviation", deviation},
                                                        {"ok", false},
                                                        {"error", detected.status().message()}});
                            continue;
                        }
                        auto segments = detected.take_value();
                        if (std::string(name) == "geometry" && length == 8 && distance == 1.5 &&
                            radius == 4 && deviation == 1.5) {
                            J::Array diagnostic;
                            for (const auto &s : segments.segments)
                                diagnostic.emplace_back(J::Array{J{static_cast<double>(s.begin.x)},
                                                                 J{static_cast<double>(s.begin.y)},
                                                                 J{static_cast<double>(s.end.x)},
                                                                 J{static_cast<double>(s.end.y)}});
                            std::ofstream file(out / "default-lines.json");
                            file << mira::to_json_string(J{std::move(diagnostic)});
                        }
                        auto filtered = take(m::filter_segments(segments.segments, {}));
                        m::CollinearMergeParams mp;
                        mp.distance_tolerance = distance;
                        auto merged = take(m::merge_collinear(filtered, mp));
                        m::GeometricProposalParams pp;
                        pp.endpoint_radius_px = radius;
                        auto proposed = m::propose_regions(merged, pp, {});
                        J::Array boxes;
                        m::SemanticSnapshot snapshot;
                        if (proposed)
                            for (const auto &p : proposed.value()) {
                                boxes.push_back(box(p.tight_bounds));
                                m::VisualRegion r;
                                r.stable_id = snapshot.regions.size() + 1;
                                r.bounds = {static_cast<float>(p.tight_bounds.x),
                                            static_cast<float>(p.tight_bounds.y),
                                            static_cast<float>(p.tight_bounds.width),
                                            static_cast<float>(p.tight_bounds.height)};
                                r.anchor = {r.bounds.x + r.bounds.width / 2,
                                            r.bounds.y + r.bounds.height / 2};
                                snapshot.regions.push_back(r);
                            }
                        const auto ms = std::chrono::duration<double, std::milli>(
                                            std::chrono::steady_clock::now() - start)
                                            .count();
                        const auto variant =
                            std::string(name) + "-l" + std::to_string(static_cast<int>(length)) +
                            "-d" + std::to_string(static_cast<int>(distance * 10)) + "-r" +
                            std::to_string(static_cast<int>(radius)) + "-v" +
                            std::to_string(static_cast<int>(deviation * 10));
                        if (proposed)
                            save(out / (variant + ".ppm"),
                                 take(m::render_set_of_mark(frame.image, snapshot)).image);
                        rows.emplace_back(J::Object{
                            {"scene", name},
                            {"variant", variant},
                            {"min_length", length},
                            {"merge_distance", distance},
                            {"endpoint_radius", radius},
                            {"deviation", deviation},
                            {"ok", proposed.ok()},
                            {"segments", static_cast<std::int64_t>(segments.segments.size())},
                            {"merged", static_cast<std::int64_t>(merged.size())},
                            {"proposals", J{std::move(boxes)}},
                            {"milliseconds", ms}});
                    }
    }
    return J{std::move(rows)};
}
J capabilities(const fs::path &root, const fs::path &out) {
    const auto original = load(root / "samples/form-light/raw.ppm");
    const auto translated = load(out / "translated.ppm", 2);
    const auto small = load(out / "small-edit.ppm", 2);
    const auto blank = load(root / "samples/blank/raw.ppm", 3);
    const auto imposter = load(out / "imposter.ppm", 3);
    const auto dark = load(root / "samples/terminal-dark/raw.ppm", 4);
    J::Object report;
    J::Array changes;
    for (const auto &[name, frame] :
         std::vector<std::pair<std::string, m::Frame>>{{"identical", original},
                                                       {"small-edit", small},
                                                       {"translation", translated},
                                                       {"scene-replace", dark}}) {
        for (const bool strict : {false, true}) {
            m::ChangeDetectionParams params;
            if (strict) {
                params.fingerprint_similarity_threshold = 1.;
                params.thumbnail_size = 128;
                params.block_size = 4;
                params.block_diff_threshold = 2;
            }
            const auto c = take(m::detect_change(original.image, frame.image, params));
            J::Array rois;
            for (auto b : c.changed_regions)
                rois.push_back(box(b));
            changes.emplace_back(
                J::Object{{"sample", name},
                          {"strict", strict},
                          {"classification", std::int64_t{static_cast<int>(c.classification)}},
                          {"reason", std::int64_t{static_cast<int>(c.reason)}},
                          {"similarity", c.frame_similarity},
                          {"area_ratio", c.changed_area_ratio},
                          {"rois", J{std::move(rois)}}});
        }
    }
    report.emplace_back("change", J{std::move(changes)});
    auto local_before = take(m::crop(original.image, {625, 295, 75, 75}, 1u << 20));
    auto local_after = take(m::crop(small.image, {625, 295, 75, 75}, 1u << 20));
    auto local_change = take(m::detect_change(local_before.view(), local_after.view(), {}));
    report.emplace_back("small_edit_roi_classification",
                        std::int64_t{static_cast<int>(local_change.classification)});
    auto shift = take(m::estimate_global_shift(original.image, translated.image, {}));
    report.emplace_back("shift", J{J::Object{{"dx", static_cast<double>(shift.dx)},
                                             {"dy", static_cast<double>(shift.dy)},
                                             {"confidence", static_cast<double>(shift.confidence)},
                                             {"expected_dx", 12.},
                                             {"expected_dy", -8.}}});
    const m::RectI target{55, 530, 98, 27};
    auto patch = take(m::crop(original.image, target, 1u << 20));
    auto fp = take(m::make_visual_patch_fingerprint(patch.view(), {}, 1u << 20));
    auto index = take(m::VisualIndex::create(64u << 10, 32));
    require(index.insert(1001, fp));
    J::Array queries;
    for (const auto &[name, frame, b] : std::vector<std::tuple<std::string, m::Frame, m::RectI>>{
             {"same", original, target},
             {"translated", translated, {67, 522, 98, 27}},
             {"removed", blank, target}}) {
        auto p = take(m::crop(frame.image, b, 1u << 20));
        auto f = take(m::make_visual_patch_fingerprint(p.view(), {}, 1u << 20));
        auto hits = take(index.query(f, {}));
        J::Array candidates;
        for (auto h : hits)
            candidates.emplace_back(J::Object{{"id", static_cast<std::int64_t>(h.entry_id)},
                                              {"tier", std::int64_t{static_cast<int>(h.evidence)}},
                                              {"similarity", h.similarity}});
        queries.emplace_back(J::Object{{"sample", name}, {"hits", J{std::move(candidates)}}});
    }
    report.emplace_back("index", J{std::move(queries)});
    auto session = take(m::PerceptionSession::create({"review"}));
    m::EvidenceSet evidence;
    m::TextRegion text;
    text.bounds = {55, 530, 98, 27};
    text.utf8_text = "保存 Save";
    text.confidence = .99F;
    (void)take(evidence.add_text(text));
    (void)take(evidence.add_template(1001, text.bounds, 1.));
    auto first = take(session.analyze_change(original));
    (void)first;
    auto s1 = take(session.fuse(original, evidence));
    auto second = take(session.analyze_change(original));
    (void)second;
    auto s2 = take(session.fuse(original, evidence));
    auto last = take(session.analyze_change(dark));
    (void)last;
    m::EvidenceSet fresh;
    m::TextRegion new_text;
    new_text.bounds = {30, 37, 433, 38};
    new_text.utf8_text = "Runtime diagnostics 运行诊断";
    new_text.confidence = .99F;
    (void)take(fresh.add_text(new_text));
    auto s3 = take(session.fuse(dark, fresh));
    report.emplace_back(
        "fusion",
        J{J::Object{{"regions", static_cast<std::int64_t>(s1.regions.size())},
                    {"source_mask", std::int64_t{s1.regions.at(0).source_mask}},
                    {"same_id", s1.regions.at(0).stable_id == s2.regions.at(0).stable_id},
                    {"old_generation_rejected", !m::is_generation_current(s3, s1.generation)}}});
    // Explicitly adopted image patch: no detector or semantic identity claim.
    for (const double psr_threshold : {5., 4.}) {
        m::ObjectTrackerOptions options;
        options.peak_sidelobe_ratio_min = psr_threshold;
        auto tracker = take(m::ObjectTracker::create(options));
        auto region = s1.regions.at(0);
        region.stable_id = 1001;
        auto adoption = take(tracker.adopt_track(region, original.image, 1));
        (void)adoption;
        J::Array tracks;
        for (const auto &[name, frame] :
             std::vector<std::pair<std::string, m::Frame>>{{"same", original},
                                                           {"translated", translated},
                                                           {"removed", blank},
                                                           {"imposter", imposter}}) {
            auto start = std::chrono::steady_clock::now();
            auto verified = tracker.verify_track(1001, frame.image, std::nullopt);
            J::Object entry{{"sample", name},
                            {"psr_threshold", psr_threshold},
                            {"ok", verified.ok()},
                            {"milliseconds", std::chrono::duration<double, std::milli>(
                                                 std::chrono::steady_clock::now() - start)
                                                 .count()}};
            if (verified) {
                const auto &a = verified.value().appearance;
                entry.emplace_back("dx", std::int64_t{a.best_offset_dx});
                entry.emplace_back("dy", std::int64_t{a.best_offset_dy});
                entry.emplace_back("ncc", a.peak_ncc);
                entry.emplace_back("psr", a.peak_sidelobe_ratio);
                entry.emplace_back("outcome", std::int64_t{static_cast<int>(a.outcome)});
                auto snap = s1;
                snap.regions[0].bounds.x += a.best_offset_dx;
                snap.regions[0].bounds.y += a.best_offset_dy;
                snap.regions[0].anchor.x += a.best_offset_dx;
                snap.regions[0].anchor.y += a.best_offset_dy;
                if (a.outcome == m::AppearanceChannelOutcome::kNone)
                    snap.regions.clear();
                save(out / ("track-" + name + "-psr" +
                            std::to_string(static_cast<int>(psr_threshold)) + ".ppm"),
                     take(m::render_set_of_mark(frame.image, snap)).image);
            } else
                entry.emplace_back("error", verified.status().message());
            tracks.emplace_back(std::move(entry));
        }
        report.emplace_back("experimental_tracker_psr" +
                                std::to_string(static_cast<int>(psr_threshold)),
                            J{std::move(tracks)});
        m::ExecutionContext cancelled;
        cancelled.is_cancelled = [] { return true; };
        auto refused = tracker.verify_track(1001, original.image, std::nullopt, cancelled);
        report.emplace_back("tracker_cancel_psr" + std::to_string(static_cast<int>(psr_threshold)),
                            !refused && refused.status().code() == m::ErrorCode::kCancelled);
    }
    auto tiny = take(m::VisualIndex::create(1, 32));
    auto exhausted = tiny.insert(1, fp);
    report.emplace_back("index_budget",
                        !exhausted && exhausted.status().code() == m::ErrorCode::kBudgetExceeded);
    return J{std::move(report)};
}
J cache(const fs::path &root, const fs::path &models) {
    m::integrations::PpOcrDetOptions det;
    det.param_path = (models / "det_param").string();
    det.bin_path = (models / "det_bin").string();
    det.identity = {"review-ppocr-v5", "locked-v1"};
    m::integrations::PpOcrRecOptions rec;
    rec.param_path = (models / "rec_param").string();
    rec.bin_path = (models / "rec_bin").string();
    rec.charset_path = (models / "charset").string();
    rec.rec_width = 640;
    rec.identity = det.identity;
    auto backend = take(m::integrations::PpOcrBackend::create(det, rec));
    struct Counter final : m::OcrBackend {
        m::OcrBackend &delegate;
        int calls = 0;
        std::string revision;
        explicit Counter(m::OcrBackend &b) : delegate(b) {}
        m::BackendInfo info() const override {
            auto i = delegate.info();
            i.model_revision += revision;
            return i;
        }
        m::Result<std::vector<m::TextRegion>> recognize(const m::ImageView &v,
                                                        const m::OcrRequest &r,
                                                        const m::ExecutionContext &c) override {
            ++calls;
            return delegate.recognize(v, r, c);
        }
    } counter(backend);
    auto frame = load(root / "samples/form-light/raw.ppm");
    auto session = take(m::PerceptionSession::create({"review"}));
    J::Array runs;
    m::OcrRequest request;
    std::vector<m::TextRegion> previous;
    for (int stage = 0; stage < 5; ++stage) {
        ++frame.sequence;
        request.cache_policy = stage == 2 ? m::CachePolicy::kRefresh : m::CachePolicy::kReadWrite;
        if (stage == 3)
            counter.revision = "-changed";
        if (stage == 4)
            request.roi = m::RectF{0, 0, 500, 500};
        auto start = std::chrono::steady_clock::now();
        auto text = take(session.run_ocr(frame, &counter, request));
        runs.emplace_back(J::Object{{"stage", std::int64_t{stage}},
                                    {"backend_calls", std::int64_t{counter.calls}},
                                    {"regions", static_cast<std::int64_t>(text.size())},
                                    {"same_result", stage == 1 && text == previous},
                                    {"milliseconds", std::chrono::duration<double, std::milli>(
                                                         std::chrono::steady_clock::now() - start)
                                                         .count()}});
        previous = std::move(text);
    }
    m::ExecutionContext stopped;
    stopped.is_cancelled = [] { return true; };
    auto cancelled = session.run_ocr(frame, &counter, request, stopped);
    m::ExecutionContext late;
    late.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    auto timed = session.run_ocr(frame, &counter, request, late);
    return J{J::Object{
        {"runs", J{std::move(runs)}},
        {"cancelled", !cancelled && cancelled.status().code() == m::ErrorCode::kCancelled},
        {"timed_out", !timed && timed.status().code() == m::ErrorCode::kTimeout},
        {"final_calls", std::int64_t{counter.calls}}}};
}
} // namespace
int main(int argc, char **argv) {
    if (argc != 4 && argc != 5)
        return 2;
    kairo::Executor executor;
    if (!executor.initialize({}).ok)
        return 2;
    int code = 0;
    try {
        executor
            .submit_auto([&] {
                const fs::path out(argv[2]);
                fs::create_directories(out);
                const std::string mode(argv[3]);
                auto result = mode == "geometry"             ? geometry(argv[1], out)
                              : mode == "cache" && argc == 5 ? cache(argv[1], argv[4])
                                                             : capabilities(argv[1], out);
                std::ofstream file(out / "report.json");
                file << mira::to_json_string(result) << '\n';
                if (!file)
                    throw std::runtime_error("report write failed");
            })
            .get();
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        code = 1;
    }
    executor.shutdown(true);
    return code;
}
