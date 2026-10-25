#include "ppocr_pipeline.hpp"
#include <mira/event_store.hpp>
#include <mira/json.hpp>
#include <mirage/integration/visual_backend_registry.hpp>
#include <mirage/integration/visual_observation_pipeline.hpp>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <set>
#include <stdexcept>
#include <thread>
#ifdef MIRAGE_WITH_PPOCR
#include <ppocr_backend.hpp>
#include <ppocr_budgeted.hpp>
#ifdef MIRAGE_WITH_YOLO
#include <yolo_backend.hpp>
#endif
#endif

namespace mirage::integration {
namespace {
#ifdef MIRAGE_WITH_PPOCR
using Json = mira::JsonValue;
std::string text(const Json &j, const char *key) {
    const auto *v = j.find(key);
    if (!v || !v->as_string() || v->as_string()->empty() || v->as_string()->size() > 4096)
        throw std::runtime_error("OCR manifest string invalid");
    return *v->as_string();
}
void keys(const Json &j, std::initializer_list<std::string_view> allowed) {
    if (!j.as_object())
        throw std::runtime_error("OCR manifest object required");
    for (const auto &[key, _] : *j.as_object())
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            throw std::runtime_error("OCR manifest unknown field");
}
std::int64_t number(const Json &j, const char *key, std::int64_t fallback, std::int64_t low,
                    std::int64_t high) {
    const auto *v = j.find(key);
    const auto n = v ? v->as_integer() : std::optional<std::int64_t>{fallback};
    if (!n || *n < low || *n > high)
        throw std::runtime_error("OCR manifest number invalid");
    return *n;
}
std::string read(const std::filesystem::path &p, std::uintmax_t bound) {
    if (!std::filesystem::is_regular_file(p))
        throw std::runtime_error("OCR resource unavailable");
    const auto size = std::filesystem::file_size(p);
    if (size > bound || size == 0)
        throw std::runtime_error("OCR resource budget exceeded");
    std::ifstream f(p, std::ios::binary);
    std::string bytes(static_cast<std::size_t>(size), '\0');
    if (!f.read(bytes.data(), static_cast<std::streamsize>(size)) ||
        f.peek() != std::char_traits<char>::eof())
        throw std::runtime_error("OCR resource read failed");
    return bytes;
}
std::vector<VisualTemplateAsset>
load_templates(const Json &j, const std::filesystem::path &manifest, bool geometry) {
    const auto *declared = j.find("templates");
    if (!declared)
        return {};
    const auto *list = declared->as_array();
    if (!geometry || !list || list->empty() || list->size() > 16)
        throw std::runtime_error("template array/geometry configuration invalid");
    std::set<std::string> ids;
    std::vector<VisualTemplateAsset> assets;
    std::size_t total = 0;
    for (const auto &entry : *list) {
        keys(entry, {"id", "format", "path", "sha256", "bytes", "width", "height"});
        VisualTemplateAsset asset;
        asset.template_id = text(entry, "id");
        if (asset.template_id.size() > 64 || !ids.insert(asset.template_id).second ||
            !std::all_of(asset.template_id.begin(), asset.template_id.end(),
                         [](unsigned char c) {
                             return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                    (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
                         }) ||
            text(entry, "format") != "bgra8")
            throw std::runtime_error("template identity/format invalid");
        auto &p = asset.pixels;
        p.width = static_cast<std::int32_t>(number(entry, "width", 0, 8, 256));
        p.height = static_cast<std::int32_t>(number(entry, "height", 0, 8, 256));
        p.stride = static_cast<std::size_t>(p.width) * 4;
        const auto count = number(entry, "bytes", 0, 1, 256u << 10);
        if (static_cast<std::size_t>(count) != p.stride * static_cast<std::size_t>(p.height))
            throw std::runtime_error("template pixel size invalid");
        total += static_cast<std::size_t>(count);
        if (total > (4u << 20))
            throw std::runtime_error("template pixel budget exceeded");
        const auto bytes = read(manifest.parent_path() / text(entry, "path"), 256u << 10);
        if (bytes.size() != static_cast<std::size_t>(count) ||
            mira::digest_string(bytes).to_string() != text(entry, "sha256"))
            throw std::runtime_error("template digest/size mismatch");
        p.pixels.assign(bytes.begin(), bytes.end());
        assets.push_back(std::move(asset));
    }
    return assets;
}
#ifdef MIRAGE_WITH_YOLO
class CountedDetector final : public mirador::DetectorBackend {
  public:
    explicit CountedDetector(mirador::integrations::YoloDetectorBackend backend)
        : backend_(std::move(backend)) {}
    mirador::BackendInfo info() const override { return backend_.info(); }
    mirador::Result<std::vector<mirador::DetectionRegion>>
    detect(const mirador::ImageView &image, const mirador::DetectionRequest &request,
           const mirador::ExecutionContext &context) override {
        ++calls;
        return backend_.detect(image, request, context);
    }
    std::atomic<std::uint64_t> calls{0};

  private:
    mirador::integrations::YoloDetectorBackend backend_;
};
#endif
class CountedOcr final : public mirador::OcrBackend {
  public:
    explicit CountedOcr(mirador::integrations::PpOcrBackend b) : backend_(std::move(b)) {}
    mirador::BackendInfo info() const override { return backend_.info(); }
    mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &image, const mirador::OcrRequest &request,
              const mirador::ExecutionContext &context) override {
        ++calls;
        return backend_.recognize(image, request, context);
    }
    std::atomic<std::uint64_t> calls{0};

  private:
    mirador::integrations::PpOcrBackend backend_;
};
/// M8-07 (upstream DEC-025): budgeted companion of `CountedOcr`; separate
/// SPI, so it wraps the budgeted backend rather than deriving from it.
class CountedBudgetedOcr final : public mirador::BudgetedOcrBackend {
  public:
    explicit CountedBudgetedOcr(mirador::integrations::PpOcrBudgetedBackend b)
        : backend_(std::move(b)) {}
    mirador::BackendInfo info() const override { return backend_.info(); }
    mirador::Result<mirador::OcrBatchResult>
    recognize_budgeted(const mirador::ImageView &image, const mirador::OcrBatchRequest &request,
                       const mirador::ExecutionContext &context) override {
        ++calls;
        return backend_.recognize_budgeted(image, request, context);
    }
    std::atomic<std::uint64_t> calls{0};

  private:
    mirador::integrations::PpOcrBudgetedBackend backend_;
};
#endif
} // namespace
struct PpOcrPipeline::Impl {
    desktop::VisualReferenceRegistry registry;
    std::string revision;
    desktop::ScreenProvider *screen = nullptr;
    std::string display_id;
    std::filesystem::path resources;
#ifdef MIRAGE_WITH_PPOCR
    std::unique_ptr<CountedOcr> backend;
    std::unique_ptr<CountedBudgetedOcr> budgeted;
#ifdef MIRAGE_WITH_YOLO
    std::unique_ptr<CountedDetector> detector;
#endif
#endif
    std::unique_ptr<VisualObservationPipeline> pipeline;
    ~Impl() {
        if (pipeline)
            pipeline->stop();
        pipeline.reset();
#ifdef MIRAGE_WITH_PPOCR
        backend.reset();
        budgeted.reset();
#ifdef MIRAGE_WITH_YOLO
        detector.reset();
#endif
#endif
        std::error_code ignored;
        if (!resources.empty())
            std::filesystem::remove_all(resources, ignored);
    }
};
PpOcrPipeline::PpOcrPipeline(std::unique_ptr<Impl> p) : impl_(std::move(p)) {}
PpOcrPipeline::~PpOcrPipeline() = default;
bool PpOcrPipeline::supported() {
#ifdef MIRAGE_WITH_PPOCR
    return true;
#else
    return false;
#endif
}
std::unique_ptr<PpOcrPipeline> PpOcrPipeline::create(kairo::Executor &executor,
                                                     desktop::ScreenProvider &screen,
                                                     const std::filesystem::path &manifest,
                                                     std::string &error) {
    error.clear();
#ifndef MIRAGE_WITH_PPOCR
    (void)executor;
    (void)screen;
    (void)manifest;
    error = "OCR unavailable: build with MIRAGE_WITH_PPOCR=ON";
    return {};
#else
    try {
        const auto body = read(manifest, 32u << 10);
        auto decoded = mira::parse_json(body);
        if (!decoded)
            throw std::runtime_error("OCR manifest JSON invalid");
        const auto &j = decoded.value();
        keys(j, {"schema", "model_id", "artifacts", "det_side", "rec_width", "rec_height",
                 "display_id", "geometry_marks", "templates", "roi_capture", "detector"});
        if (number(j, "schema", 0, 1, 1) != 1)
            throw std::runtime_error("OCR manifest schema invalid");
        const auto *geometry = j.find("geometry_marks");
        if (geometry && !geometry->as_boolean())
            throw std::runtime_error("OCR geometry_marks boolean required");
        const bool geometry_marks = geometry && *geometry->as_boolean();
        const auto *regional = j.find("roi_capture");
        if (regional && !regional->as_boolean())
            throw std::runtime_error("OCR roi_capture boolean required");
        const bool roi_capture = regional && *regional->as_boolean();
        auto template_assets = load_templates(j, manifest, geometry_marks);
        const auto id = text(j, "model_id");
        if (id.size() > 128)
            throw std::runtime_error("OCR model identity too large");
        const auto *artifacts = j.find("artifacts");
        if (!artifacts)
            throw std::runtime_error("OCR resources missing");
        keys(*artifacts, {"det_param", "det_bin", "rec_param", "rec_bin", "charset"});
        auto p = std::make_unique<Impl>();
        p->revision = mira::canonical_json_digest(j).to_string();
        p->resources = std::filesystem::temp_directory_path() /
                       ("mirage-ocr-" + mira::OperationId::generate().to_string());
        if (!std::filesystem::create_directory(p->resources))
            throw std::runtime_error("OCR private resource staging failed");
        std::filesystem::permissions(p->resources, std::filesystem::perms::owner_all);
        for (const auto *name : {"det_param", "det_bin", "rec_param", "rec_bin", "charset"}) {
            const auto *a = artifacts->find(name);
            if (!a)
                throw std::runtime_error("OCR resource declaration missing");
            keys(*a, {"path", "sha256", "bytes"});
            const auto limit = std::string_view{name}.ends_with("bin") ? (64u << 20) : (2u << 20);
            const auto size = number(*a, "bytes", 0, 1, limit);
            auto bytes = read(manifest.parent_path() / text(*a, "path"), limit);
            if (bytes.size() != static_cast<std::size_t>(size) ||
                mira::digest_string(bytes).to_string() != text(*a, "sha256"))
                throw std::runtime_error("OCR resource digest/size mismatch");
            if (std::string_view{name} == "charset" && !desktop::is_valid_utf8(bytes))
                throw std::runtime_error("OCR charset UTF-8 invalid");
            std::ofstream f(p->resources / name, std::ios::binary);
            if (!f.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
                throw std::runtime_error("OCR verified resource staging failed");
        }
        mirador::integrations::PpOcrDetOptions det;
        mirador::integrations::PpOcrRecOptions rec;
        det.param_path = (p->resources / "det_param").string();
        det.bin_path = (p->resources / "det_bin").string();
        rec.param_path = (p->resources / "rec_param").string();
        rec.bin_path = (p->resources / "rec_bin").string();
        rec.charset_path = (p->resources / "charset").string();
        det.identity = rec.identity = {id, p->revision};
        det.det_side = static_cast<std::int32_t>(number(j, "det_side", 960, 32, 960));
        rec.rec_width = static_cast<std::int32_t>(number(j, "rec_width", 320, 32, 640));
        rec.rec_height = static_cast<std::int32_t>(number(j, "rec_height", 48, 16, 64));
        // DEC-076: native candidate analysis is bounded separately from
        // the 128-region wire prefix. The five-second deadline still applies.
        det.postprocess.max_boxes = screen.desktop_observation_only() ? 512 : 128;
        // The pinned backends default to single-threaded ncnn. Measured on the
        // bundled PP-OCRv5-mobile model (1600x1000 dense frame, 359 det boxes):
        // det drops ~2x at four threads, while per-line rec degrades 1.6-1.8x
        // under default libgomp scheduling (fork/join overhead dominates the
        // tiny line tensors), so only det fans out.
        det.num_threads =
            std::clamp<int>(static_cast<int>(std::thread::hardware_concurrency() / 2), 1, 4);
        rec.num_threads = 1;
        auto backend = mirador::integrations::PpOcrBackend::create(det, rec);
        if (!backend)
            throw std::runtime_error("OCR backend could not load verified resources");
        p->backend = std::make_unique<CountedOcr>(std::move(backend).take_value());
        // M8-07 (upstream DEC-025): the interactive image-OCR path requires
        // budgeted execution, so a budgeted backend that cannot load fails
        // pipeline creation instead of silently downgrading to the
        // all-or-nothing path. The unit bound is the M8-06 frozen reference
        // (longest measured rec line x1.6 margin); the det stage config,
        // including its DEC-076 candidate cap, is shared verbatim.
        mirador::integrations::PpOcrBudgetedOptions budgeted_options;
        budgeted_options.det = det;
        budgeted_options.rec = rec;
        budgeted_options.rec_unit_upper_bound = std::chrono::milliseconds{100};
        auto budgeted = mirador::integrations::PpOcrBudgetedBackend::create(budgeted_options);
        if (!budgeted)
            throw std::runtime_error("OCR budgeted backend could not load verified resources");
        p->budgeted = std::make_unique<CountedBudgetedOcr>(std::move(budgeted).take_value());
        const auto identity = p->backend->info();
        VisualBackendRegistry registrations;
        const VisualBackendIdentity registration{identity.name,
                                                 identity.implementation_version,
                                                 identity.model_id,
                                                 identity.model_revision,
                                                 {"rgb8"}};
        if (!registrations.register_ocr(registration, *p->backend).ok)
            throw std::runtime_error("OCR backend identity rejected");
        VisualObservationPipelineConfig config;
        // Executor retains worker registrations after stop; each replacement
        // pipeline owns a fresh source/session instead of reusing a dead worker.
        config.source_id = "mirage.desktop.ocr." + mira::OperationId::generate().to_string();
        if (j.find("display_id"))
            config.display_id = text(j, "display_id");
        p->screen = &screen;
        p->display_id = config.display_id;
        config.capture_limits.max_bytes = 32u << 20; // DEC-068: bounded 4K acquisition.
        config.run_ocr = true;
        config.session.enable_image_session = true;
        config.session.enable_background_session = true;
        config.geometry_marks = geometry_marks;
        config.roi_capture = roi_capture;
        config.session.template_assets = std::move(template_assets);
        // Public Refresh avoids treating perceptual dHash equality as exact OCR identity.
        config.refresh_ocr = true;
        config.session.ocr_backend = p->backend.get();
        config.session.budgeted_ocr_backend = p->budgeted.get();
        if (const auto *detector = j.find("detector")) {
#ifndef MIRAGE_WITH_YOLO
            (void)detector;
            throw std::runtime_error("YOLO unavailable: build with MIRAGE_WITH_YOLO=ON");
#else
            keys(*detector, {"model_id", "artifacts", "class_names", "input_side", "preprocessing",
                             "confidence_milli"});
            mirador::integrations::YoloDetectorOptions options;
            options.identity = {text(*detector, "model_id"), p->revision};
            if (options.identity.model_id.size() > 128)
                throw std::runtime_error("YOLO model identity too large");
            const auto *labels = detector->find("class_names");
            if (!labels || !labels->as_array() || labels->as_array()->empty() ||
                labels->as_array()->size() > 256)
                throw std::runtime_error("YOLO class_names invalid");
            for (const auto &label : *labels->as_array()) {
                if (!label.as_string() || label.as_string()->empty() ||
                    label.as_string()->size() > 64 || !desktop::is_valid_utf8(*label.as_string()))
                    throw std::runtime_error("YOLO class label invalid");
                options.class_names.push_back(*label.as_string());
            }
            options.num_classes = static_cast<std::int32_t>(options.class_names.size());
            options.input_side =
                static_cast<std::int32_t>(number(*detector, "input_side", 640, 64, 1024));
            const auto profile = text(*detector, "preprocessing");
            if (profile == "bilinear114") {
                options.interpolation = mirador::integrations::YoloInterpolation::kBilinear;
                options.pad_value = 114;
            } else if (profile != "area0") {
                throw std::runtime_error("YOLO preprocessing profile invalid");
            }
            const auto *files = detector->find("artifacts");
            if (!files)
                throw std::runtime_error("YOLO artifacts missing");
            keys(*files, {"param", "bin"});
            for (const auto *name : {"param", "bin"}) {
                const auto *artifact = files->find(name);
                if (!artifact)
                    throw std::runtime_error("YOLO artifact missing");
                keys(*artifact, {"path", "sha256", "bytes"});
                const auto limit = std::string_view{name} == "bin" ? (128u << 20) : (2u << 20);
                const auto size = number(*artifact, "bytes", 0, 1, limit);
                const auto bytes = read(manifest.parent_path() / text(*artifact, "path"), limit);
                if (bytes.size() != static_cast<std::size_t>(size) ||
                    mira::digest_string(bytes).to_string() != text(*artifact, "sha256"))
                    throw std::runtime_error("YOLO artifact digest/size mismatch");
                const auto target = p->resources / (std::string("yolo_") + name);
                std::ofstream file(target, std::ios::binary);
                if (!file.write(bytes.data(), static_cast<std::streamsize>(bytes.size())))
                    throw std::runtime_error("YOLO verified staging failed");
            }
            options.param_path = (p->resources / "yolo_param").string();
            options.bin_path = (p->resources / "yolo_bin").string();
            auto loaded = mirador::integrations::YoloDetectorBackend::create(options);
            if (!loaded)
                throw std::runtime_error("YOLO could not load verified resources");
            p->detector = std::make_unique<CountedDetector>(loaded.take_value());
            const auto info = p->detector->info();
            if (!registrations
                     .register_detector({info.name,
                                         info.implementation_version,
                                         info.model_id,
                                         info.model_revision,
                                         {"rgb8"}},
                                        *p->detector)
                     .ok)
                throw std::runtime_error("YOLO backend identity rejected");
            config.detector_marks = true;
            config.detector_min_confidence =
                static_cast<float>(number(*detector, "confidence_milli", 250, 1, 1000)) / 1000.F;
            config.session.detector_backend = p->detector.get();
#endif
        }

        p->pipeline =
            std::make_unique<VisualObservationPipeline>(executor, screen, p->registry, config);
        if (!p->pipeline->start(error))
            return {};
        return std::unique_ptr<PpOcrPipeline>(new PpOcrPipeline(std::move(p)));
    } catch (const std::exception &e) {
        error = e.what();
        return {};
    }
#endif
}
bool PpOcrPipeline::running() const { return impl_->pipeline && impl_->pipeline->running(); }
desktop::VisualReferenceRegistry &PpOcrPipeline::registry() { return impl_->registry; }
EnvironmentVisualRefresh
PpOcrPipeline::refresh(bool analyze, const desktop::CancelToken &cancel,
                       const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    return refresh_impl(analyze, false, std::nullopt, cancel, deadline);
}
EnvironmentVisualRefresh PpOcrPipeline::refresh_presented(
    bool analyze, const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline,
    const desktop::CapturePresentation &presentation) {
    return refresh_impl(analyze, false, std::nullopt, cancel, deadline, presentation);
}
EnvironmentVisualRefresh
PpOcrPipeline::mark(const std::optional<desktop::WindowGeometry> &crop,
                    const desktop::CancelToken &cancel,
                    const std::optional<std::chrono::steady_clock::time_point> &deadline,
                    const desktop::CapturePresentation &presentation) {
    return refresh_impl(true, true, crop, cancel, deadline, presentation);
}
EnvironmentVisualRefresh
PpOcrPipeline::refresh_region(const desktop::WindowGeometry &area,
                              const desktop::CancelToken &cancel,
                              const std::optional<std::chrono::steady_clock::time_point> &deadline,
                              const desktop::CapturePresentation &presentation) {
    return refresh_impl(true, false, area, cancel, deadline, presentation);
}
EnvironmentVisualRefresh
PpOcrPipeline::refresh_impl(bool analyze, bool marks,
                            const std::optional<desktop::WindowGeometry> &crop,
                            const desktop::CancelToken &cancel,
                            const std::optional<std::chrono::steady_clock::time_point> &deadline,
                            const desktop::CapturePresentation &presentation) {
    const desktop::CancelToken bounded([cancel, deadline] {
        return cancel.cancelled() || (deadline && std::chrono::steady_clock::now() >= *deadline);
    });
    EnvironmentVisualRefresh failed;
    const auto reject = [&](const char *message) {
        impl_->registry.clear();
        failed.capture_cancelled = bounded.cancelled();
        failed.capture_error = {"unavailable", message};
        return failed;
    };
    if (!running() || bounded.cancelled())
        return reject("OCR stopped, cancelled or expired");
    const auto before = impl_->screen->list_displays({}, bounded);
    if (!before.ok || bounded.cancelled())
        return reject("OCR display observation failed");
    const desktop::DisplayInfo *target = nullptr;
    for (const auto &display : before.displays) {
        if ((!impl_->display_id.empty() && display.id == impl_->display_id) ||
            (impl_->display_id.empty() && display.primary)) {
            target = &display;
            break;
        }
    }
    if (!target && impl_->display_id.empty() && !before.displays.empty())
        target = &before.displays.front();
    if (!target)
        return reject("OCR display unavailable");
    auto result =
        marks  ? impl_->pipeline->refresh_marked(crop, bounded, deadline, presentation)
        : crop ? impl_->pipeline->refresh_region(*crop, analyze, bounded, deadline, presentation)
               : impl_->pipeline->refresh_presented(analyze, bounded, deadline, presentation);
    if (!result.captured || (analyze && !result.analyzed) || bounded.cancelled()) {
        impl_->registry.clear();
        return result;
    }
    const auto after = impl_->screen->list_displays({}, bounded);
    if (!after.ok || bounded.cancelled())
        return reject("OCR display changed");
    const auto same =
        std::find_if(after.displays.begin(), after.displays.end(), [&](const auto &d) {
            return d.id == target->id && d.geometry.x == target->geometry.x &&
                   d.geometry.y == target->geometry.y &&
                   d.geometry.width == target->geometry.width &&
                   d.geometry.height == target->geometry.height && d.primary == target->primary;
        });
    if (same == after.displays.end() || result.display_id != target->id ||
        result.frame.width != result.capture_roi.value_or(target->geometry).width ||
        result.frame.height != result.capture_roi.value_or(target->geometry).height)
        return reject("OCR display changed");
    return result;
}
ImageOcrOutcome
PpOcrPipeline::read_image_ocr(const desktop::ImageFrame &image, const desktop::CancelToken &cancel,
                              const std::optional<std::chrono::steady_clock::time_point> &deadline,
                              const std::string &source_key) {
    if (!running())
        return {
            false,
            cancel.cancelled(),
            {"unavailable", "image OCR pipeline is not running"},
            {},
            {},
            0,
            0,
            std::nullopt,
        };
    auto result = impl_->pipeline->read_image_ocr(image, cancel, deadline, source_key);
    result.model_revision = model_revision();
    result.backend_calls = backend_calls();
    return result;
}
PerceptionSample
PpOcrPipeline::observe_image(const desktop::ImageFrame &image, const std::string &key,
                             const std::optional<desktop::WindowGeometry> &focus,
                             const desktop::CancelToken &cancel,
                             const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    if (!impl_->pipeline || !impl_->pipeline->running()) {
        PerceptionSample out;
        out.error = {"unavailable", "continuous pipeline is not running"};
        return out;
    }
    return impl_->pipeline->observe_image(image, key, focus, cancel, deadline);
}

ImageContinuousState PpOcrPipeline::query_image_state(
    const std::string &source_key, const desktop::CancelToken &cancel,
    const std::optional<std::chrono::steady_clock::time_point> &deadline) {
    if (!running()) {
        ImageContinuousState out;
        out.error = {"unavailable", "image pipeline is not running"};
        return out;
    }
    return impl_->pipeline->query_image_state(source_key, cancel, deadline);
}
void PpOcrPipeline::stop() {
    if (impl_->pipeline)
        impl_->pipeline->stop();
    impl_->registry.clear();
}
const std::string &PpOcrPipeline::model_revision() const { return impl_->revision; }
std::uint64_t PpOcrPipeline::backend_calls() const {
#ifdef MIRAGE_WITH_PPOCR
    // M8-07: a budgeted image-OCR run is an OCR backend call like any other;
    // the DEC-076 diagnostic counts both execution paths.
    return impl_->backend->calls.load() + impl_->budgeted->calls.load();
#else
    return 0;
#endif
}
std::uint64_t PpOcrPipeline::detector_calls() const {
#ifdef MIRAGE_WITH_YOLO
    return impl_->detector ? impl_->detector->calls.load() : 0;
#else
    return 0;
#endif
}
} // namespace mirage::integration
