// M3-02 mirador integration adapter tests (DEC-016 decisions 3/4/5): the
// execution-context and frame translations, the bounded backend registry and
// the latest-wins visual session host. The session scenarios run against a
// real Executor blocking worker: happy path with fusion into kDisplay space,
// cache hits proven by flat backend call counters, every documented negative
// path (null backend, unreachable format, budget, missing display transform,
// deadline, external cancellation), admission rejection, latest-wins
// supersede of an in-flight analysis, stop settlement and the first-frame
// change semantics.

#include "../support/test.hpp"

#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/mirador_service.hpp>
#include <mirage/integration/visual_backend_registry.hpp>
#include <mirage/integration/visual_execution_context.hpp>
#include <mirage/integration/visual_frame.hpp>
#include <mirage/integration/visual_session_host.hpp>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/screen_provider.hpp>

#include <executor/executor.hpp>

#include <mirador/change_detection.hpp>
#include <mirador/execution_context.hpp>
#include <mirador/frame.hpp>
#include <mirador/image_view.hpp>
#include <mirador/ocr_backend.hpp>
#include <mirador/pixel_format.hpp>
#include <mirador/status.hpp>
#include <mirador/transform.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

namespace desktop = mirage::desktop;

using mirage::integration::convert_frame;
using mirage::integration::FakeDetectorBackend;
using mirage::integration::FakeDetectorConfig;
using mirage::integration::FakeOcrBackend;
using mirage::integration::FakeOcrConfig;
using mirage::integration::to_execution_context;
using mirage::integration::to_mirador_frame;
using mirage::integration::VisualAnalysisOutcome;
using mirage::integration::VisualAnalysisRequest;
using mirage::integration::VisualAnalysisResult;
using mirage::integration::VisualBackendIdentity;
using mirage::integration::VisualBackendRegistry;
using mirage::integration::VisualSessionConfig;
using mirage::integration::VisualSessionHost;

/// Upper bound for every wait in this suite; a hang means a broken
/// cancellation or settlement path, never a test expectation.
constexpr std::chrono::seconds kWaitBound{10};

bool contains(const std::string &text, const char *needle) {
    return text.find(needle) != std::string::npos;
}

/// Polls `predicate` every millisecond up to kWaitBound (bounded spin for
/// cross-thread observations, mirroring the mirador context poll discipline).
template <typename Predicate> bool wait_until(Predicate predicate) {
    const auto give_up = std::chrono::steady_clock::now() + kWaitBound;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= give_up) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return true;
}

bool settle_within(std::future<VisualAnalysisResult> &future, VisualAnalysisResult &out) {
    if (future.wait_for(kWaitBound) != std::future_status::ready) {
        return false;
    }
    out = future.get();
    return true;
}

desktop::ImageFrame make_capture(std::int32_t width, std::int32_t height, std::size_t stride,
                                 std::size_t pixel_bytes, std::uint8_t first_byte,
                                 std::uint8_t last_byte) {
    desktop::ImageFrame capture;
    capture.format = desktop::ImageFormat::Bgra8;
    capture.width = width;
    capture.height = height;
    capture.stride = stride;
    capture.pixels.assign(pixel_bytes, std::uint8_t{0});
    if (!capture.pixels.empty()) {
        capture.pixels.front() = first_byte;
        capture.pixels.back() = last_byte;
    }
    return capture;
}

/// Builds a valid owning Bgra8 frame directly. The session and conversion
/// scenarios use this so their verdicts stay independent of the capture
/// translation under test in scenario B.
mirador::Frame owned_bgra8_frame(std::int32_t width, std::int32_t height,
                                 const std::string &source_id, std::uint64_t sequence,
                                 bool left_half_red) {
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(
        stride * static_cast<std::size_t>(height), std::uint8_t{0x40});
    if (left_half_red) {
        for (std::int32_t y = 0; y < height; ++y) {
            for (std::int32_t x = 0; x < width; ++x) {
                const std::size_t offset =
                    static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4u;
                const bool left = x < width / 2;
                (*pixels)[offset + 0] = std::uint8_t{0};                            // B
                (*pixels)[offset + 1] = left ? std::uint8_t{0} : std::uint8_t{255}; // G
                (*pixels)[offset + 2] = left ? std::uint8_t{255} : std::uint8_t{0}; // R
                (*pixels)[offset + 3] = std::uint8_t{255};                          // A
            }
        }
    }
    mirador::ImageView view;
    view.data = reinterpret_cast<const std::byte *>(pixels->data());
    view.width = width;
    view.height = height;
    view.row_stride_bytes = static_cast<std::int64_t>(stride);
    view.format = mirador::PixelFormat::kBgra8;
    view.rotation = mirador::Rotation::k0;
    MIRAGE_CHECK(mirador::validate(view).ok());

    mirador::Frame frame;
    frame.image = view;
    frame.sequence = sequence;
    frame.timestamp = std::chrono::steady_clock::time_point{} + std::chrono::seconds{7};
    frame.source_id = source_id;
    frame.owner = std::move(pixels);
    return frame;
}

/// A valid frame for a session scenario: uniform pixels (identity semantics
/// for the fake backends and the change detector).
mirador::Frame session_frame(const std::string &source_id, std::uint64_t sequence) {
    return owned_bgra8_frame(32, 16, source_id, sequence, false);
}

/// OCR backend that publishes its entry, then spins until the polled context
/// observes cancellation (bounded), so the test controls the in-flight window
/// of the latest-wins scenario deterministically. Thread-safe via atomics;
/// the session keeps it to one call at a time anyway.
class SpinningOcrBackend final : public mirador::OcrBackend {
  public:
    explicit SpinningOcrBackend(std::string name)
        : name_(std::move(name)), entered_{false}, calls_{0} {}

    [[nodiscard]] mirador::BackendInfo info() const override {
        mirador::BackendInfo backend_info;
        backend_info.name = name_;
        backend_info.implementation_version = "1.0.0";
        backend_info.accepted_formats = {mirador::PixelFormat::kBgra8};
        return backend_info;
    }

    mirador::Result<std::vector<mirador::TextRegion>>
    recognize(const mirador::ImageView &prepared_image, const mirador::OcrRequest &,
              const mirador::ExecutionContext &context) override {
        calls_.fetch_add(1, std::memory_order_acq_rel);
        entered_.store(true, std::memory_order_release);
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds{5};
        while (!mirador::is_cancelled(context) && std::chrono::steady_clock::now() < give_up) {
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }
        if (mirador::is_cancelled(context)) {
            return mirador::Status{mirador::ErrorCode::kCancelled,
                                   "spinning ocr observed cancellation"};
        }
        mirador::TextRegion region;
        region.bounds = mirador::RectF{0.0F, 0.0F, static_cast<float>(prepared_image.width),
                                       static_cast<float>(prepared_image.height)};
        region.utf8_text = "spinning";
        region.confidence = 1.0F;
        return std::vector<mirador::TextRegion>{region};
    }

    [[nodiscard]] bool entered() const noexcept { return entered_.load(std::memory_order_acquire); }

    [[nodiscard]] bool wait_entered() const {
        return wait_until([this] { return entered(); });
    }

    [[nodiscard]] std::size_t calls() const noexcept {
        return calls_.load(std::memory_order_acquire);
    }

  private:
    std::string name_;
    std::atomic<bool> entered_;
    std::atomic<std::size_t> calls_;
};

FakeOcrBackend bgra_ocr() {
    FakeOcrConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    return FakeOcrBackend(config);
}

FakeDetectorBackend bgra_detector() {
    FakeDetectorConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    return FakeDetectorBackend(config);
}

// ---- A. execution context ----------------------------------------------------------

void execution_context_maps_cancellation_and_deadline() {
    mirage::desktop::CancelToken token;
    const mirador::ExecutionContext context = to_execution_context(token);
    MIRAGE_CHECK(!mirador::is_cancelled(context));
    MIRAGE_CHECK(!mirador::deadline_reached(context));
    MIRAGE_CHECK(!context.deadline.has_value());

    const auto past = std::chrono::steady_clock::now() - std::chrono::seconds{1};
    const mirador::ExecutionContext dated = to_execution_context(token, past);
    MIRAGE_CHECK(dated.deadline.has_value());
    MIRAGE_CHECK(dated.deadline == past);
    MIRAGE_CHECK(mirador::deadline_reached(dated));

    token.request_cancel();
    // The context captured the token by value; copies share one state.
    MIRAGE_CHECK(mirador::is_cancelled(context));
    MIRAGE_CHECK(mirador::is_cancelled(dated));
}

// ---- B. to_mirador_frame -----------------------------------------------------------

void to_mirador_frame_wraps_a_valid_capture() {
    const desktop::ImageFrame capture =
        make_capture(8, 4, 32, 128, std::uint8_t{0xAA}, std::uint8_t{0x55});
    const auto wrapped = to_mirador_frame(
        capture, "wrap-src", 7, std::chrono::steady_clock::time_point{} + std::chrono::seconds{3});
    MIRAGE_CHECK(wrapped.ok());
    if (!wrapped.ok()) {
        return;
    }
    const mirador::ImageView &view = wrapped.value().image;
    MIRAGE_CHECK(mirador::validate(view).ok());
    MIRAGE_CHECK(view.width == 8);
    MIRAGE_CHECK(view.height == 4);
    MIRAGE_CHECK(view.row_stride_bytes == 32);
    MIRAGE_CHECK(view.format == mirador::PixelFormat::kBgra8);
    MIRAGE_CHECK(view.rotation == mirador::Rotation::k0);
    MIRAGE_CHECK(wrapped.value().sequence == 7);
    MIRAGE_CHECK(wrapped.value().source_id == "wrap-src");
    MIRAGE_CHECK(wrapped.value().timestamp ==
                 std::chrono::steady_clock::time_point{} + std::chrono::seconds{3});
}

void to_mirador_frame_accepts_a_padded_stride() {
    const desktop::ImageFrame capture =
        make_capture(8, 4, 40, 160, std::uint8_t{0x11}, std::uint8_t{0x22});
    const auto wrapped =
        to_mirador_frame(capture, "padded-src", 1, std::chrono::steady_clock::now());
    MIRAGE_CHECK(wrapped.ok());
    if (wrapped.ok()) {
        MIRAGE_CHECK(wrapped.value().image.row_stride_bytes == 40);
        MIRAGE_CHECK(mirador::validate(wrapped.value().image).ok());
    }
}

void to_mirador_frame_pixels_outlive_the_capture() {
    mirador::Frame survived;
    {
        const desktop::ImageFrame ephemeral =
            make_capture(8, 4, 32, 128, std::uint8_t{0xAA}, std::uint8_t{0x55});
        const auto wrapped =
            to_mirador_frame(ephemeral, "owner-src", 1, std::chrono::steady_clock::now());
        MIRAGE_CHECK(wrapped.ok());
        if (wrapped.ok()) {
            survived = wrapped.value(); // copies share the pixel owner
        }
    } // the caller's ImageFrame dies here
    const mirador::ImageView &view = survived.image;
    MIRAGE_CHECK(view.data != nullptr);
    if (view.data != nullptr) {
        MIRAGE_CHECK(view.data[0] == std::byte{0xAA});
        const std::int64_t last_index =
            view.row_stride_bytes * static_cast<std::int64_t>(view.height) - 1;
        MIRAGE_CHECK(view.data[static_cast<std::size_t>(last_index)] == std::byte{0x55});
    }
}

void to_mirador_frame_rejects_broken_captures() {
    const desktop::ImageFrame valid = make_capture(8, 4, 32, 128, std::uint8_t{1}, std::uint8_t{2});
    const auto empty_source = to_mirador_frame(valid, "", 1, std::chrono::steady_clock::now());
    MIRAGE_CHECK(!empty_source.ok());
    MIRAGE_CHECK(empty_source.status().code() == mirador::ErrorCode::kInvalidArgument);

    desktop::ImageFrame zero_width = valid;
    zero_width.width = 0;
    const auto zeroed = to_mirador_frame(zero_width, "src", 1, std::chrono::steady_clock::now());
    MIRAGE_CHECK(!zeroed.ok());
    MIRAGE_CHECK(zeroed.status().code() == mirador::ErrorCode::kInvalidArgument);

    // Only the canonical Bgra8 capture format exists in the desktop contract;
    // synthesize a foreign format value to reach the format gate.
    desktop::ImageFrame foreign = valid;
    foreign.format = static_cast<desktop::ImageFormat>(1);
    const auto unsupported = to_mirador_frame(foreign, "src", 1, std::chrono::steady_clock::now());
    MIRAGE_CHECK(!unsupported.ok());
    MIRAGE_CHECK(unsupported.status().code() == mirador::ErrorCode::kUnsupportedFormat);

    // Beyond the mirador dimension bound (65535).
    desktop::ImageFrame oversized =
        make_capture(65536, 1, 65536u * 4u, 65536u * 4u, std::uint8_t{1}, std::uint8_t{2});
    const auto oversized_result =
        to_mirador_frame(oversized, "src", 1, std::chrono::steady_clock::now());
    MIRAGE_CHECK(!oversized_result.ok());
    MIRAGE_CHECK(oversized_result.status().code() == mirador::ErrorCode::kInvalidArgument);

    // Stride below width * 4.
    desktop::ImageFrame narrow_stride =
        make_capture(8, 4, 28, 112, std::uint8_t{1}, std::uint8_t{2});
    const auto narrow_result =
        to_mirador_frame(narrow_stride, "src", 1, std::chrono::steady_clock::now());
    MIRAGE_CHECK(!narrow_result.ok());
    MIRAGE_CHECK(narrow_result.status().code() == mirador::ErrorCode::kInvalidArgument);

    // Pixel buffer smaller than stride * height.
    desktop::ImageFrame short_buffer =
        make_capture(8, 4, 32, 127, std::uint8_t{1}, std::uint8_t{2});
    const auto short_result =
        to_mirador_frame(short_buffer, "src", 1, std::chrono::steady_clock::now());
    MIRAGE_CHECK(!short_result.ok());
    MIRAGE_CHECK(short_result.status().code() == mirador::ErrorCode::kInvalidArgument);
}

// ---- C. convert_frame --------------------------------------------------------------

void convert_frame_maps_bgra8_to_rgb8() {
    // Left half pure red, right half pure green, in Bgra8 byte order.
    const mirador::Frame base = owned_bgra8_frame(4, 2, "conv-src", 11, true);
    MIRAGE_CHECK(base.image.data != nullptr);
    const auto converted = convert_frame(base, mirador::PixelFormat::kRgb8, std::int64_t{1} << 20);
    MIRAGE_CHECK(converted.ok());
    if (!converted.ok()) {
        return;
    }
    const mirador::ImageView &view = converted.value().image;
    MIRAGE_CHECK(mirador::validate(view).ok());
    MIRAGE_CHECK(view.format == mirador::PixelFormat::kRgb8);
    MIRAGE_CHECK(view.width == 4);
    MIRAGE_CHECK(view.height == 2);
    MIRAGE_CHECK(view.rotation == mirador::Rotation::k0);
    MIRAGE_CHECK(converted.value().sequence == 11);
    MIRAGE_CHECK(converted.value().source_id == "conv-src");
    // First pixel was pure red (Bgra8 B,G,R,A = 0,0,255,255) -> Rgb8 255,0,0.
    MIRAGE_CHECK(view.data[0] == std::byte{255});
    MIRAGE_CHECK(view.data[1] == std::byte{0});
    MIRAGE_CHECK(view.data[2] == std::byte{0});
    // Last pixel (3,1) was pure green (Bgra8 0,255,0,255) -> Rgb8 0,255,0.
    MIRAGE_CHECK(view.data[9] == std::byte{0});
    MIRAGE_CHECK(view.data[10] == std::byte{255});
    MIRAGE_CHECK(view.data[11] == std::byte{0});
}

void convert_frame_enforces_budget_and_format_matrix() {
    const mirador::Frame base = owned_bgra8_frame(4, 2, "conv-src", 1, true);
    const auto over_budget = convert_frame(base, mirador::PixelFormat::kRgb8, 1);
    MIRAGE_CHECK(!over_budget.ok());
    MIRAGE_CHECK(over_budget.status().code() == mirador::ErrorCode::kBudgetExceeded);

    // Bgra8 -> kNv12 has no conversion kernel.
    const auto to_nv12 = convert_frame(base, mirador::PixelFormat::kNv12, std::int64_t{1} << 20);
    MIRAGE_CHECK(!to_nv12.ok());
    MIRAGE_CHECK(to_nv12.status().code() == mirador::ErrorCode::kUnsupportedFormat);
}

// ---- D. visual backend registry ----------------------------------------------------

void registry_registers_and_finds_backends() {
    FakeOcrBackend ocr;
    FakeDetectorBackend detector;
    const VisualBackendIdentity ocr_identity{"mirage-fake-ocr", "1.0.0", "", "", {"rgba8"}};
    const VisualBackendIdentity detector_identity{
        "mirage-fake-detector", "1.0.0", "", "", {"rgba8"}};

    VisualBackendRegistry registry;
    const auto ocr_registration = registry.register_ocr(ocr_identity, ocr);
    MIRAGE_CHECK(ocr_registration.ok);
    const auto detector_registration = registry.register_detector(detector_identity, detector);
    MIRAGE_CHECK(detector_registration.ok);

    MIRAGE_CHECK(registry.find_ocr("mirage-fake-ocr") == &ocr);
    MIRAGE_CHECK(registry.find_detector("mirage-fake-detector") == &detector);
    MIRAGE_CHECK(registry.find_ocr("mirage-fake-detector") == nullptr); // kind-scoped
    MIRAGE_CHECK(registry.find_ocr("missing") == nullptr);
    MIRAGE_CHECK(registry.find_detector("missing") == nullptr);
    MIRAGE_CHECK(registry.size() == 2);
    const std::vector<std::string> registered_names = registry.names();
    MIRAGE_CHECK(registered_names.size() == 2);
    MIRAGE_CHECK(registered_names[0] == "mirage-fake-ocr");
    MIRAGE_CHECK(registered_names[1] == "mirage-fake-detector");
}

void registry_rejects_an_identity_that_fails_the_contract() {
    FakeOcrBackend ocr;
    const VisualBackendIdentity formatless{"mirage-fake-ocr", "1.0.0", "", "", {}};
    VisualBackendRegistry registry;
    const auto registration = registry.register_ocr(formatless, ocr);
    MIRAGE_CHECK(!registration.ok);
    MIRAGE_CHECK(contains(registration.error, "capability contract"));
    MIRAGE_CHECK(registry.find_ocr("mirage-fake-ocr") == nullptr);
}

void registry_rejects_an_identity_that_contradicts_info() {
    FakeOcrConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kRgba8, mirador::PixelFormat::kBgra8};
    FakeOcrBackend ocr(config);

    // A changed model revision contradicts the backend's own info() query.
    VisualBackendRegistry revision_registry;
    const VisualBackendIdentity wrong_revision{
        "mirage-fake-ocr", "1.0.0", "", "r2", {"rgba8", "bgra8"}};
    const auto revision_registration = revision_registry.register_ocr(wrong_revision, ocr);
    MIRAGE_CHECK(!revision_registration.ok);
    MIRAGE_CHECK(contains(revision_registration.error, "info() query"));

    // The accepted-format preference order is compared element by element.
    VisualBackendRegistry order_registry;
    const VisualBackendIdentity matching_order{
        "mirage-fake-ocr", "1.0.0", "", "", {"rgba8", "bgra8"}};
    const auto matching_registration = order_registry.register_ocr(matching_order, ocr);
    MIRAGE_CHECK(matching_registration.ok);
    MIRAGE_CHECK(order_registry.find_ocr("mirage-fake-ocr") == &ocr);

    VisualBackendRegistry swapped_registry;
    const VisualBackendIdentity swapped_order{
        "mirage-fake-ocr", "1.0.0", "", "", {"bgra8", "rgba8"}};
    const auto swapped_registration = swapped_registry.register_ocr(swapped_order, ocr);
    MIRAGE_CHECK(!swapped_registration.ok);
    MIRAGE_CHECK(contains(swapped_registration.error, "info() query"));
}

void registry_rejects_duplicate_names() {
    FakeOcrBackend first_ocr;
    FakeOcrBackend second_ocr;
    FakeDetectorConfig detector_config; // detector sharing the OCR name
    detector_config.info.name = "mirage-fake-ocr";
    FakeDetectorBackend name_collision_detector(detector_config);

    const VisualBackendIdentity identity{"mirage-fake-ocr", "1.0.0", "", "", {"rgba8"}};
    VisualBackendRegistry registry;
    MIRAGE_CHECK(registry.register_ocr(identity, first_ocr).ok);

    const auto same_kind = registry.register_ocr(identity, second_ocr);
    MIRAGE_CHECK(!same_kind.ok);
    MIRAGE_CHECK(contains(same_kind.error, "already registered under this name"));

    const VisualBackendIdentity detector_identity{"mirage-fake-ocr", "1.0.0", "", "", {"rgba8"}};
    const auto cross_kind = registry.register_detector(detector_identity, name_collision_detector);
    MIRAGE_CHECK(!cross_kind.ok);
    MIRAGE_CHECK(contains(cross_kind.error, "already registered under this name"));
}

void registry_is_capacity_bounded() {
    std::vector<std::unique_ptr<FakeOcrBackend>> crowd;
    VisualBackendRegistry registry;
    for (std::size_t index = 0; index < VisualBackendRegistry::kMaxBackends; ++index) {
        FakeOcrConfig config;
        config.info.name = "crowd-ocr-" + std::to_string(index);
        crowd.push_back(std::make_unique<FakeOcrBackend>(config));
        const VisualBackendIdentity identity{config.info.name, "1.0.0", "", "", {"rgba8"}};
        const auto registration = registry.register_ocr(identity, *crowd.back());
        MIRAGE_CHECK(registration.ok);
    }
    MIRAGE_CHECK(registry.size() == VisualBackendRegistry::kMaxBackends);

    FakeOcrConfig overflow_config;
    overflow_config.info.name = "crowd-ocr-overflow";
    FakeOcrBackend overflow(overflow_config);
    const VisualBackendIdentity overflow_identity{
        overflow_config.info.name, "1.0.0", "", "", {"rgba8"}};
    const auto rejected = registry.register_ocr(overflow_identity, overflow);
    MIRAGE_CHECK(!rejected.ok);
    MIRAGE_CHECK(contains(rejected.error, "registry is full"));
    MIRAGE_CHECK(registry.find_ocr(overflow_config.info.name) == nullptr);
}

// ---- E. session happy path ---------------------------------------------------------

void session_completes_a_full_analysis_with_fusion(executor::Executor &executor) {
    FakeOcrBackend ocr = bgra_ocr();
    FakeDetectorBackend detector = bgra_detector();
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "happy-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("happy-src", 42);
    request.analyze_change = true;
    request.run_ocr = true;
    request.run_detector = true;
    request.fuse = true;
    request.display_transform = mirador::make_translation(
        100.0, 0.0, mirador::CoordinateSpaceId::kOriented, mirador::CoordinateSpaceId::kDisplay);

    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);

    MIRAGE_CHECK(result.text.size() == 1);
    MIRAGE_CHECK(result.detections.size() == 1);
    if (result.text.size() == 1) {
        MIRAGE_CHECK(result.text[0].utf8_text == "mirage-fake");
    }
    if (result.detections.size() == 1) {
        MIRAGE_CHECK(result.detections[0].label == "button");
    }

    MIRAGE_CHECK(result.snapshot != nullptr);
    if (result.snapshot != nullptr) {
        const mirador::SemanticSnapshot &snapshot = *result.snapshot;
        MIRAGE_CHECK(snapshot.coordinate_space == mirador::CoordinateSpaceId::kDisplay);
        MIRAGE_CHECK(snapshot.frame_sequence == 42);
        MIRAGE_CHECK(snapshot.regions.size() == 1);
        if (snapshot.regions.size() == 1) {
            const mirador::VisualRegion &region = snapshot.regions[0];
            // The full-view cluster translated by the display transform.
            MIRAGE_CHECK((region.bounds == mirador::RectF{100.0F, 0.0F, 32.0F, 16.0F}));
            MIRAGE_CHECK((region.anchor == mirador::PointF{116.0F, 8.0F}));
            MIRAGE_CHECK(region.stable_id != 0);
            MIRAGE_CHECK(mirador::has_source(region.source_mask, mirador::RegionSource::kOcr));
            MIRAGE_CHECK(mirador::has_source(region.source_mask, mirador::RegionSource::kDetector));
            MIRAGE_CHECK(region.text == "mirage-fake");
            MIRAGE_CHECK(region.label == "button");
        }
    }
    MIRAGE_CHECK(ocr.calls() == 1);
    MIRAGE_CHECK(detector.calls() == 1);
    host.stop();
}

// ---- F. capability cache hit -------------------------------------------------------

void session_serves_a_repeated_request_from_the_cache(executor::Executor &executor) {
    FakeOcrBackend ocr = bgra_ocr();
    FakeDetectorBackend detector = bgra_detector();
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "cache-hit-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    // The same frame and the same requests: the second analysis must be
    // served from the mirador capability cache (DEC-016 decision 5), proven
    // by the backend call counters staying flat.
    const mirador::Frame frame = session_frame("cache-hit-src", 5);

    VisualAnalysisRequest first;
    first.frame = frame;
    first.run_ocr = true;
    first.run_detector = true;
    std::future<VisualAnalysisResult> first_future = host.submit(std::move(first));
    VisualAnalysisResult first_result;
    MIRAGE_CHECK(settle_within(first_future, first_result));
    MIRAGE_CHECK(first_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(ocr.calls() == 1);
    MIRAGE_CHECK(detector.calls() == 1);

    VisualAnalysisRequest second;
    second.frame = frame;
    second.run_ocr = true;
    second.run_detector = true;
    std::future<VisualAnalysisResult> second_future = host.submit(std::move(second));
    VisualAnalysisResult second_result;
    MIRAGE_CHECK(settle_within(second_future, second_result));
    MIRAGE_CHECK(second_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(second_result.text == first_result.text);
    MIRAGE_CHECK(ocr.calls() == 1);
    MIRAGE_CHECK(detector.calls() == 1);
    host.stop();
}

// ---- G. negative paths -------------------------------------------------------------

void session_reports_null_backend_as_failure(executor::Executor &executor) {
    VisualSessionHost host(executor, "g-null-ocr-src", VisualSessionConfig{});
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("g-null-ocr-src", 1);
    request.run_ocr = true;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBackendUnavailable);
    host.stop();
}

void session_reports_an_unreachable_backend_format(executor::Executor &executor) {
    FakeOcrConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kNv12}; // Bgra8 has no kernel into NV12
    FakeOcrBackend nv12_ocr(config);
    VisualSessionConfig host_config;
    host_config.ocr_backend = &nv12_ocr;
    VisualSessionHost host(executor, "g-nv12-src", host_config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("g-nv12-src", 1);
    request.run_ocr = true;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kUnsupportedFormat);
    MIRAGE_CHECK(nv12_ocr.calls() == 0); // the pipeline failed before the backend
    host.stop();
}

void session_reports_a_backend_budget_overflow(executor::Executor &executor) {
    FakeOcrConfig config; // more regions than the backend's output budget
    config.max_regions = 4;
    for (int index = 0; index < 5; ++index) {
        mirador::TextRegion region;
        region.bounds = mirador::RectF{0.0F, static_cast<float>(index), 8.0F, 1.0F};
        region.utf8_text = "row-" + std::to_string(index);
        region.confidence = 0.5F;
        config.regions.push_back(region);
    }
    FakeOcrBackend budget_ocr(config);
    VisualSessionConfig host_config;
    host_config.ocr_backend = &budget_ocr;
    VisualSessionHost host(executor, "g-budget-src", host_config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("g-budget-src", 1);
    request.run_ocr = true;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBudgetExceeded);
    MIRAGE_CHECK(budget_ocr.calls() == 1); // executed, then failed instead of truncating
    host.stop();
}

void session_requires_the_display_transform_for_fusion(executor::Executor &executor) {
    VisualSessionHost host(executor, "g-fusion-src", VisualSessionConfig{});
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("g-fusion-src", 1);
    request.fuse = true; // no display_transform
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kInvalidArgument);
    MIRAGE_CHECK(contains(result.outcome.message, "DEC-016"));
    host.stop();
}

void session_reports_an_expired_deadline_without_side_effects(executor::Executor &executor) {
    FakeOcrBackend ocr = bgra_ocr();
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    VisualSessionHost host(executor, "g-deadline-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("g-deadline-src", 1);
    request.run_ocr = true;
    request.deadline = std::chrono::steady_clock::now() - std::chrono::seconds{1};
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kTimedOut);
    MIRAGE_CHECK(ocr.calls() == 0); // the deadline is judged before any backend runs
    host.stop();
}

void session_reports_pre_cancelled_requests_without_side_effects(executor::Executor &executor) {
    FakeOcrBackend ocr = bgra_ocr();
    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    VisualSessionHost host(executor, "g-cancel-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    mirage::desktop::CancelToken cancelled;
    cancelled.request_cancel();
    VisualAnalysisRequest request;
    request.frame = session_frame("g-cancel-src", 1);
    request.run_ocr = true;
    request.external_cancel = cancelled;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled);
    MIRAGE_CHECK(ocr.calls() == 0);
    host.stop();
}

/// Non-positive cache budgets are rejected by the mirador session creation
/// (never clamped): start fails closed with a reason, and a legal host on
/// the same executor is unaffected.
void session_rejects_invalid_cache_budgets(executor::Executor &executor) {
    VisualSessionConfig zero_frame;
    zero_frame.frame_cache_bytes = 0;
    VisualSessionHost zero_host(executor, "g-zero-frame-budget-src", zero_frame);
    std::string zero_error;
    MIRAGE_CHECK(!zero_host.start(zero_error));
    MIRAGE_CHECK(!zero_error.empty());
    MIRAGE_CHECK(!zero_host.running());

    VisualSessionConfig negative_result;
    negative_result.result_cache_bytes = -1;
    VisualSessionHost negative_host(executor, "g-negative-result-budget-src", negative_result);
    std::string negative_error;
    MIRAGE_CHECK(!negative_host.start(negative_error));
    MIRAGE_CHECK(!negative_error.empty());
    MIRAGE_CHECK(!negative_host.running());

    VisualSessionHost legal(executor, "legal-budget-src", VisualSessionConfig{});
    std::string legal_error;
    MIRAGE_CHECK(legal.start(legal_error));
    MIRAGE_CHECK(legal.running());
    legal.stop();
}

// ---- H. admission rejection --------------------------------------------------------

void session_rejects_requests_it_cannot_accept(executor::Executor &executor) {
    VisualSessionHost never_started(executor, "h-cold-src", VisualSessionConfig{});
    VisualAnalysisRequest cold;
    cold.frame = session_frame("h-cold-src", 1);
    std::future<VisualAnalysisResult> cold_future = never_started.submit(std::move(cold));
    VisualAnalysisResult cold_result;
    MIRAGE_CHECK(settle_within(cold_future, cold_result));
    MIRAGE_CHECK(cold_result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected);

    VisualSessionHost host(executor, "h-live-src", VisualSessionConfig{});
    std::string error;
    MIRAGE_CHECK(host.start(error));
    std::string restart_error;
    MIRAGE_CHECK(!host.start(restart_error)); // a running host refuses a second start

    VisualAnalysisRequest mismatched;
    mismatched.frame = session_frame("another-source", 1);
    std::future<VisualAnalysisResult> mismatch_future = host.submit(std::move(mismatched));
    VisualAnalysisResult mismatch_result;
    MIRAGE_CHECK(settle_within(mismatch_future, mismatch_result));
    MIRAGE_CHECK(mismatch_result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected);

    host.stop();
    MIRAGE_CHECK(!host.running());

    VisualAnalysisRequest after_stop;
    after_stop.frame = session_frame("h-live-src", 2);
    std::future<VisualAnalysisResult> stopped_future = host.submit(std::move(after_stop));
    VisualAnalysisResult stopped_result;
    MIRAGE_CHECK(settle_within(stopped_future, stopped_result));
    MIRAGE_CHECK(stopped_result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected);
}

// ---- I. latest wins ----------------------------------------------------------------

void session_supersedes_an_in_flight_analysis(executor::Executor &executor) {
    SpinningOcrBackend slow("slow-ocr");
    VisualSessionConfig config;
    config.ocr_backend = &slow;
    VisualSessionHost host(executor, "latest-wins-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest first;
    first.frame = session_frame("latest-wins-src", 1);
    first.run_ocr = true;
    std::future<VisualAnalysisResult> first_future = host.submit(std::move(first));
    MIRAGE_CHECK(slow.wait_entered()); // the first analysis is in flight

    VisualAnalysisRequest second;
    second.frame = session_frame("latest-wins-src", 2);
    second.run_ocr = true;
    std::future<VisualAnalysisResult> second_future = host.submit(std::move(second));

    VisualAnalysisResult first_result;
    MIRAGE_CHECK(settle_within(first_future, first_result));
    MIRAGE_CHECK(first_result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled);

    VisualAnalysisResult second_result;
    MIRAGE_CHECK(settle_within(second_future, second_result));
    MIRAGE_CHECK(second_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(second_result.text.size() == 1);
    MIRAGE_CHECK(slow.calls() == 2); // both analyses really executed
    host.stop();
}

// ---- J. stop settlement ------------------------------------------------------------

void session_settles_an_in_flight_analysis_on_stop(executor::Executor &executor) {
    SpinningOcrBackend slow("slow-ocr-stop");
    VisualSessionConfig config;
    config.ocr_backend = &slow;
    VisualSessionHost host(executor, "stop-settle-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("stop-settle-src", 1);
    request.run_ocr = true;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    MIRAGE_CHECK(slow.wait_entered());

    host.stop();
    MIRAGE_CHECK(!host.running());

    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled);

    host.stop(); // idempotent
    std::string restart_error;
    MIRAGE_CHECK(!host.start(restart_error)); // the host is one-shot, like the executor worker name

    { // the destructor settles and joins too
        VisualSessionHost transient(executor, "transient-src", VisualSessionConfig{});
        std::string transient_error;
        MIRAGE_CHECK(transient.start(transient_error));
    }
}

/// Stop with a request in the publication window: B is published while A is
/// in flight, then the host stops before B is awaited. Under latest-wins B
/// is either queued (settled by the stop path) or already in flight
/// (cancelled through its token); both outcomes are kCancelled, and each
/// future settles exactly once (settle_within consumes each future once).
void session_settles_a_queued_analysis_on_stop(executor::Executor &executor) {
    SpinningOcrBackend slow("slow-ocr-stop-queued");
    VisualSessionConfig config;
    config.ocr_backend = &slow;
    VisualSessionHost host(executor, "stop-queued-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest first;
    first.frame = session_frame("stop-queued-src", 1);
    first.run_ocr = true;
    std::future<VisualAnalysisResult> first_future = host.submit(std::move(first));
    MIRAGE_CHECK(slow.wait_entered()); // A is in flight

    VisualAnalysisRequest second;
    second.frame = session_frame("stop-queued-src", 2);
    second.run_ocr = true;
    std::future<VisualAnalysisResult> second_future = host.submit(std::move(second));
    host.stop(); // B never awaited before the stop: queued or in flight

    MIRAGE_CHECK(!host.running());

    VisualAnalysisResult first_result;
    MIRAGE_CHECK(settle_within(first_future, first_result));
    MIRAGE_CHECK(first_result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled);

    VisualAnalysisResult second_result;
    MIRAGE_CHECK(settle_within(second_future, second_result));
    MIRAGE_CHECK(second_result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled);

    host.stop(); // idempotent
}

// ---- K. first-frame semantics ------------------------------------------------------

void session_reports_the_first_frame_as_a_global_change(executor::Executor &executor) {
    VisualSessionHost host(executor, "first-frame-src", VisualSessionConfig{});
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = session_frame("first-frame-src", 1);
    request.analyze_change = true;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(result.change.classification == mirador::ChangeClassification::kGlobal);
    MIRAGE_CHECK(result.change.reason == mirador::ChangeReason::kFirstFrame);
    host.stop();
}

template <typename Scenario> void run_scenario(const char *name, Scenario scenario) {
    std::fprintf(stderr, "[visual_session_test] scenario: %s\n", name);
    scenario();
}

} // namespace

int main() {
    run_scenario("execution_context_maps_cancellation_and_deadline",
                 execution_context_maps_cancellation_and_deadline);
    run_scenario("to_mirador_frame_wraps_a_valid_capture", to_mirador_frame_wraps_a_valid_capture);
    run_scenario("to_mirador_frame_accepts_a_padded_stride",
                 to_mirador_frame_accepts_a_padded_stride);
    run_scenario("to_mirador_frame_pixels_outlive_the_capture",
                 to_mirador_frame_pixels_outlive_the_capture);
    run_scenario("to_mirador_frame_rejects_broken_captures",
                 to_mirador_frame_rejects_broken_captures);
    run_scenario("convert_frame_maps_bgra8_to_rgb8", convert_frame_maps_bgra8_to_rgb8);
    run_scenario("convert_frame_enforces_budget_and_format_matrix",
                 convert_frame_enforces_budget_and_format_matrix);
    run_scenario("registry_registers_and_finds_backends", registry_registers_and_finds_backends);
    run_scenario("registry_rejects_an_identity_that_fails_the_contract",
                 registry_rejects_an_identity_that_fails_the_contract);
    run_scenario("registry_rejects_an_identity_that_contradicts_info",
                 registry_rejects_an_identity_that_contradicts_info);
    run_scenario("registry_rejects_duplicate_names", registry_rejects_duplicate_names);
    run_scenario("registry_is_capacity_bounded", registry_is_capacity_bounded);

    // Session scenarios share one default-configured Executor instance with
    // its own isolated ExecutorManager; every host owns a unique source id
    // (and therefore a unique lifetime-registered worker name) and stops
    // itself before the ordered executor shutdown below.
    executor::Executor executor;
    const bool executor_ready = executor.initialize_ex(executor::ExecutorConfig{}).ok;
    MIRAGE_CHECK(executor_ready);
    if (executor_ready) {
        run_scenario("session_completes_a_full_analysis_with_fusion",
                     [&] { session_completes_a_full_analysis_with_fusion(executor); });
        run_scenario("session_serves_a_repeated_request_from_the_cache",
                     [&] { session_serves_a_repeated_request_from_the_cache(executor); });
        run_scenario("session_reports_null_backend_as_failure",
                     [&] { session_reports_null_backend_as_failure(executor); });
        run_scenario("session_reports_an_unreachable_backend_format",
                     [&] { session_reports_an_unreachable_backend_format(executor); });
        run_scenario("session_reports_a_backend_budget_overflow",
                     [&] { session_reports_a_backend_budget_overflow(executor); });
        run_scenario("session_requires_the_display_transform_for_fusion",
                     [&] { session_requires_the_display_transform_for_fusion(executor); });
        run_scenario("session_reports_an_expired_deadline_without_side_effects",
                     [&] { session_reports_an_expired_deadline_without_side_effects(executor); });
        run_scenario("session_reports_pre_cancelled_requests_without_side_effects", [&] {
            session_reports_pre_cancelled_requests_without_side_effects(executor);
        });
        run_scenario("session_rejects_invalid_cache_budgets",
                     [&] { session_rejects_invalid_cache_budgets(executor); });
        run_scenario("session_rejects_requests_it_cannot_accept",
                     [&] { session_rejects_requests_it_cannot_accept(executor); });
        run_scenario("session_supersedes_an_in_flight_analysis",
                     [&] { session_supersedes_an_in_flight_analysis(executor); });
        run_scenario("session_settles_an_in_flight_analysis_on_stop",
                     [&] { session_settles_an_in_flight_analysis_on_stop(executor); });
        run_scenario("session_settles_a_queued_analysis_on_stop",
                     [&] { session_settles_a_queued_analysis_on_stop(executor); });
        run_scenario("session_reports_the_first_frame_as_a_global_change",
                     [&] { session_reports_the_first_frame_as_a_global_change(executor); });
        executor.shutdown(true);
    }
    return mirage::testing::finish("visual_session_test");
}
