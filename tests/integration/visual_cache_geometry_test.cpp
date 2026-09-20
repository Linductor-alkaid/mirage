// M3-04 pipeline tests (plan item `M3-04`, DEC-016 decision 3): the visual
// session host's template-cache and geometry-proposal stages over a real
// Executor blocking worker and the fake backends. Covered here: the
// enroll -> detect -> probe -> fuse -> map -> publish -> click closed loop
// (the template_id is the agent-facing identity end to end), the
// detector-ROI-per-proposal stage, geometry proposals as fusion evidence,
// cache-stats visibility, every admission rejection of the pixel-space
// stages, the enrollment negative paths (outside-frame patch, fingerprint
// budget, pre-cancelled request), failure settlements that keep completed
// stage products, and the cross-frame re-location of an enrolled template.
//
// Frame designs (deterministic bytes, Bgra8 with B=G=R so the luma of every
// pixel is the painted value):
// - icon frame: flat ground (64) with a 16x16 horizontal-ramp icon; the ramp
//   sets every dHash bit of the mirador fingerprint, so flat crops can never
//   match it above the reuse threshold;
// - rectangle frame: a hollow 2 px bright rectangle outline; with the
//   detector's deviation tolerance widened to 2.0 the pinned
//   SegmentGrowingLineDetector reports exactly the four rectangle edges and
//   propose_regions closes them into one structure (closure 1.0).

#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_observation_mapper.hpp>
#include <mirage/integration/visual_session_host.hpp>

#include <mirage/desktop/cancellation.hpp>
#include <mirage/desktop/element_target_executor.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/desktop/visual_snapshot.hpp>

#include <executor/executor.hpp>

#include <mirador/frame.hpp>
#include <mirador/geometric_proposal.hpp>
#include <mirador/image_view.hpp>
#include <mirador/pixel_format.hpp>
#include <mirador/semantic_snapshot.hpp>
#include <mirador/status.hpp>
#include <mirador/transform.hpp>
#include <mirador/visual_index.hpp>

#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace desktop = mirage::desktop;

using mirage::integration::FakeDetectorBackend;
using mirage::integration::FakeDetectorConfig;
using mirage::integration::publish_visual_snapshot;
using mirage::integration::to_visual_snapshot;
using mirage::integration::VisualAnalysisOutcome;
using mirage::integration::VisualAnalysisRequest;
using mirage::integration::VisualAnalysisResult;
using mirage::integration::VisualGeometryStage;
using mirage::integration::VisualSessionConfig;
using mirage::integration::VisualSessionHost;
using mirage::integration::VisualTemplateEnrollment;

/// Upper bound for every session wait; a hang is a broken path, not a slow
/// machine.
constexpr std::chrono::seconds kWaitBound{10};

constexpr std::int32_t kFrameWidth = 64;
constexpr std::int32_t kFrameHeight = 48;
constexpr std::int32_t kIconSide = 16;
/// Pinned mirador per-entry cost at thumb 32: thumb bytes + fixed overhead.
constexpr std::int64_t kEntryBytes32 =
    std::int64_t{32} * 32 + mirador::VisualIndex::kEntryOverheadBytes;

bool settle_within(std::future<VisualAnalysisResult> &future, VisualAnalysisResult &out) {
    if (future.wait_for(kWaitBound) != std::future_status::ready) {
        return false;
    }
    out = future.get();
    return true;
}

struct OwnedFrame {
    std::shared_ptr<std::vector<std::uint8_t>> owner;
    mirador::Frame frame;
};

void put_gray(std::vector<std::uint8_t> &pixels, std::int64_t stride, std::int32_t x,
              std::int32_t y, std::uint8_t value) {
    const std::size_t offset = static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                               static_cast<std::size_t>(x) * 4u;
    pixels[offset + 0] = value; // B
    pixels[offset + 1] = value; // G
    pixels[offset + 2] = value; // R
    pixels[offset + 3] = 255;   // A
}

OwnedFrame
painted_frame(const std::string &source_id, std::uint64_t sequence,
              const std::function<void(std::vector<std::uint8_t> &, std::int64_t)> &paint) {
    const std::int64_t stride = static_cast<std::int64_t>(kFrameWidth) * 4;
    auto owner = std::make_shared<std::vector<std::uint8_t>>(
        static_cast<std::size_t>(stride) * static_cast<std::size_t>(kFrameHeight),
        std::uint8_t{64});
    if (paint) {
        paint(*owner, stride);
    }
    mirador::ImageView view;
    view.data = reinterpret_cast<const std::byte *>(owner->data());
    view.width = kFrameWidth;
    view.height = kFrameHeight;
    view.row_stride_bytes = stride;
    view.format = mirador::PixelFormat::kBgra8;
    view.rotation = mirador::Rotation::k0;
    MIRAGE_CHECK(mirador::validate(view).ok());

    mirador::Frame frame;
    frame.image = view;
    frame.sequence = sequence;
    frame.timestamp = std::chrono::steady_clock::time_point{} + std::chrono::seconds{7};
    frame.source_id = source_id;
    frame.owner = owner;
    return OwnedFrame{std::move(owner), frame};
}

/// Flat ground with one 16x16 horizontal-ramp icon at (icon_x, icon_y).
OwnedFrame icon_frame(const std::string &source_id, std::uint64_t sequence, std::int32_t icon_x,
                      std::int32_t icon_y) {
    return painted_frame(source_id, sequence,
                         [icon_x, icon_y](std::vector<std::uint8_t> &pixels, std::int64_t stride) {
                             for (std::int32_t y = 0; y < kIconSide; ++y) {
                                 for (std::int32_t x = 0; x < kIconSide; ++x) {
                                     put_gray(pixels, stride, icon_x + x, icon_y + y,
                                              static_cast<std::uint8_t>(40 + 10 * x));
                                 }
                             }
                         });
}

/// Flat ground with a hollow 2 px bright rectangle outline (8,8)-(40,32).
OwnedFrame rect_frame(const std::string &source_id, std::uint64_t sequence) {
    return painted_frame(source_id, sequence,
                         [](std::vector<std::uint8_t> &pixels, std::int64_t stride) {
                             for (std::int32_t y = 8; y < 32; ++y) {
                                 for (std::int32_t x = 8; x < 40; ++x) {
                                     if (x < 10 || x >= 38 || y < 10 || y >= 30) {
                                         put_gray(pixels, stride, x, y, 255);
                                     }
                                 }
                             }
                         });
}

/// Square frame with a non-k0 rotation (the adapter never produces this; it
/// must be refused whenever pixel-space stages are requested).
mirador::Frame rotated_frame(const std::string &source_id, std::uint64_t sequence) {
    constexpr std::int32_t kSide = 32;
    const std::size_t stride = static_cast<std::size_t>(kSide) * 4u;
    auto owner = std::make_shared<std::vector<std::uint8_t>>(
        stride * static_cast<std::size_t>(kSide), std::uint8_t{64});
    mirador::ImageView view;
    view.data = reinterpret_cast<const std::byte *>(owner->data());
    view.width = kSide;
    view.height = kSide;
    view.row_stride_bytes = static_cast<std::int64_t>(stride);
    view.format = mirador::PixelFormat::kBgra8;
    view.rotation = mirador::Rotation::k90;
    MIRAGE_CHECK(mirador::validate(view).ok());
    mirador::Frame frame;
    frame.image = view;
    frame.sequence = sequence;
    frame.timestamp = std::chrono::steady_clock::time_point{} + std::chrono::seconds{7};
    frame.source_id = source_id;
    frame.owner = std::move(owner);
    return frame;
}

mirador::Transform2D display_transform() {
    return mirador::make_translation(100.0, 0.0, mirador::CoordinateSpaceId::kOriented,
                                     mirador::CoordinateSpaceId::kDisplay);
}

FakeDetectorBackend bgra_detector() {
    FakeDetectorConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    return FakeDetectorBackend(config);
}

FakeDetectorBackend regions_detector(std::vector<mirador::DetectionRegion> regions) {
    FakeDetectorConfig config;
    config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    config.regions = std::move(regions);
    return FakeDetectorBackend(config);
}

mirador::DetectionRegion box(float x, float y, float width, float height) {
    mirador::DetectionRegion region;
    region.bounds = mirador::RectF{x, y, width, height};
    region.class_id = 0;
    region.label = "icon";
    region.confidence = 0.9F;
    return region;
}

/// The first kTemplate-bit region of a fused snapshot, or nullptr.
const mirador::VisualRegion *find_template_region(const mirador::SemanticSnapshot &snapshot) {
    for (const mirador::VisualRegion &region : snapshot.regions) {
        if (mirador::has_source(region.source_mask, mirador::RegionSource::kTemplate)) {
            return &region;
        }
    }
    return nullptr;
}

/// The first kTemplate-form entry of a mapped visual snapshot, or nullptr.
const desktop::VisualRegionEntry *find_template_entry(const desktop::VisualSnapshot &snapshot) {
    for (const desktop::VisualRegionEntry &entry : snapshot.regions) {
        if (entry.source == desktop::VisualRegionSource::kTemplate) {
            return &entry;
        }
    }
    return nullptr;
}

// ---- A. enroll -> probe -> fuse -> map -> publish -> click ----------------------

void template_identity_flows_through_the_whole_pipeline(executor::Executor &executor) {
    FakeDetectorBackend detector = regions_detector({box(8.0F, 8.0F, 16.0F, 16.0F)});

    VisualSessionConfig config;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "cache-loop-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    // Request A: enroll the icon patch cropped from the request frame.
    VisualAnalysisRequest enroll_request;
    enroll_request.frame = icon_frame("cache-loop-src", 1, 8, 8).frame;
    enroll_request.enrollments = {
        VisualTemplateEnrollment{"cache:settings", mirador::RectI{8, 8, kIconSide, kIconSide}}};
    std::future<VisualAnalysisResult> enroll_future = host.submit(std::move(enroll_request));
    VisualAnalysisResult enrolled;
    MIRAGE_CHECK(settle_within(enroll_future, enrolled));
    MIRAGE_CHECK(enrolled.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(enrolled.cache_stats.template_index_entries == 1);
    MIRAGE_CHECK(enrolled.cache_stats.template_index_bytes == kEntryBytes32);

    // Request B: detect the icon, probe the index at the detection region,
    // fuse, and stamp the enrolled identity onto the fused region.
    VisualAnalysisRequest probe_request;
    probe_request.frame = icon_frame("cache-loop-src", 2, 8, 8).frame;
    probe_request.run_detector = true;
    probe_request.probe_templates = true;
    probe_request.fuse = true;
    probe_request.display_transform = display_transform();
    std::future<VisualAnalysisResult> probe_future = host.submit(std::move(probe_request));
    VisualAnalysisResult probed;
    MIRAGE_CHECK(settle_within(probe_future, probed));
    MIRAGE_CHECK(probed.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(detector.calls() == 1);

    MIRAGE_CHECK(probed.template_hits.size() == 1);
    if (probed.template_hits.size() == 1) {
        MIRAGE_CHECK(probed.template_hits[0].template_id == "cache:settings");
        MIRAGE_CHECK(probed.template_hits[0].evidence ==
                     mirador::VisualEvidenceKind::kExactContent);
        MIRAGE_CHECK(probed.template_hits[0].similarity == 1.0);
    }

    // The probe hit merged with its detection into one region carrying both
    // provenance bits; the enrichment stamped the enrolled identity as label.
    MIRAGE_CHECK(probed.snapshot != nullptr);
    if (probed.snapshot == nullptr) {
        host.stop();
        return;
    }
    const mirador::SemanticSnapshot &fusion = *probed.snapshot;
    MIRAGE_CHECK(fusion.coordinate_space == mirador::CoordinateSpaceId::kDisplay);
    MIRAGE_CHECK(fusion.regions.size() == 1);
    if (fusion.regions.size() == 1) {
        const mirador::VisualRegion &region = fusion.regions[0];
        MIRAGE_CHECK(mirador::has_source(region.source_mask, mirador::RegionSource::kDetector));
        MIRAGE_CHECK(mirador::has_source(region.source_mask, mirador::RegionSource::kTemplate));
        MIRAGE_CHECK(region.label == "cache:settings");
        MIRAGE_CHECK((region.bounds == mirador::RectF{108.0F, 8.0F, 16.0F, 16.0F}));
    }

    // Mapping: the kTemplate form wins over the detector bit and carries the
    // identity as template_id.
    const desktop::VisualSnapshot mapped = to_visual_snapshot(fusion);
    MIRAGE_CHECK(mapped.regions.size() == 1);
    if (mapped.regions.size() == 1) {
        MIRAGE_CHECK(mapped.regions[0].source == desktop::VisualRegionSource::kTemplate);
        MIRAGE_CHECK(mapped.regions[0].template_id == "cache:settings");
        MIRAGE_CHECK(mapped.regions[0].bounds.x == 108);
        MIRAGE_CHECK(mapped.regions[0].bounds.y == 8);
        MIRAGE_CHECK(mapped.regions[0].bounds.width == 16);
        MIRAGE_CHECK(mapped.regions[0].bounds.height == 16);
    }

    // Publishing and acting: the template_id resolves through the visual hint
    // ring and the click lands on the fused region center.
    desktop::VisualReferenceRegistry registry;
    const desktop::VisualPublishOutcome published = publish_visual_snapshot(fusion, registry);
    MIRAGE_CHECK(published.ok);
    MIRAGE_CHECK(published.snapshot.regions.size() == 1);
    if (published.snapshot.regions.size() == 1) {
        MIRAGE_CHECK(published.snapshot.regions[0].ref == "@v1");
        MIRAGE_CHECK(published.snapshot.regions[0].template_id == "cache:settings");
    }

    mirage::testing::FakeDesktopEnvironment env;
    desktop::ElementTargetExecutor click_executor(env.accessibility(), &registry, env.input());
    desktop::ElementTarget target;
    target.visual.template_id = "cache:settings";
    const desktop::TargetResolution acted = click_executor.click(target, nullptr);
    MIRAGE_CHECK(acted.ok);
    MIRAGE_CHECK(acted.ring == desktop::ResolutionRing::kVisualHint);
    MIRAGE_CHECK(acted.point.x == 116);
    MIRAGE_CHECK(acted.point.y == 16);
    MIRAGE_CHECK(env.input_log.size() == 3);
    if (env.input_log.size() == 3) {
        MIRAGE_CHECK(env.input_log[0] == "move 116,16");
        MIRAGE_CHECK(env.input_log[1] == "press left");
        MIRAGE_CHECK(env.input_log[2] == "release left");
    }

    host.stop();
}

// ---- B. geometry proposals --------------------------------------------------------

void geometry_stage_proposes_the_painted_rectangle(executor::Executor &executor) {
    VisualSessionHost host(executor, "geometry-src", VisualSessionConfig{});
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualGeometryStage geometry;
    geometry.run = true;
    geometry.segment_detector.deviation_tolerance = 2.0; // single tuned knob

    VisualAnalysisRequest request;
    request.frame = rect_frame("geometry-src", 1).frame;
    request.geometry = geometry;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult first;
    MIRAGE_CHECK(settle_within(future, first));
    MIRAGE_CHECK(first.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);

    MIRAGE_CHECK(first.geometry_proposals.size() == 1);
    if (first.geometry_proposals.size() == 1) {
        const mirador::GeometricRegionProposal &proposal = first.geometry_proposals[0];
        MIRAGE_CHECK((proposal.tight_bounds == mirador::RectI{8, 8, 31, 23}));
        MIRAGE_CHECK(proposal.closure_score > 0.0F);
        MIRAGE_CHECK(proposal.closure_score <= 1.0F);
        MIRAGE_CHECK(proposal.closure_score == 1.0F); // the outline closes
        MIRAGE_CHECK(proposal.context_bounds.width >= proposal.tight_bounds.width);
        MIRAGE_CHECK(proposal.context_bounds.height >= proposal.tight_bounds.height);
        MIRAGE_CHECK(!proposal.supporting_segments.empty());
    }

    // Determinism: the same frame and parameters propose the same regions.
    VisualAnalysisRequest repeat;
    repeat.frame = rect_frame("geometry-src", 2).frame;
    repeat.geometry = geometry;
    std::future<VisualAnalysisResult> repeat_future = host.submit(std::move(repeat));
    VisualAnalysisResult second;
    MIRAGE_CHECK(settle_within(repeat_future, second));
    MIRAGE_CHECK(second.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(second.geometry_proposals.size() == first.geometry_proposals.size());
    if (second.geometry_proposals.size() == first.geometry_proposals.size() &&
        first.geometry_proposals.size() == 1) {
        MIRAGE_CHECK(second.geometry_proposals[0].tight_bounds ==
                     first.geometry_proposals[0].tight_bounds);
        MIRAGE_CHECK(second.geometry_proposals[0].context_bounds ==
                     first.geometry_proposals[0].context_bounds);
        MIRAGE_CHECK(second.geometry_proposals[0].closure_score ==
                     first.geometry_proposals[0].closure_score);
    }
    host.stop();
}

// ---- C. detector ROI per proposal ---------------------------------------------------

void detector_roi_from_geometry_runs_one_call_per_proposal(executor::Executor &executor) {
    FakeDetectorBackend detector = bgra_detector();
    VisualSessionConfig config;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "geometry-roi-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualGeometryStage geometry;
    geometry.run = true;
    geometry.segment_detector.deviation_tolerance = 2.0;
    geometry.detector_roi_from_geometry = true;

    VisualAnalysisRequest request;
    request.frame = rect_frame("geometry-roi-src", 1).frame;
    request.geometry = geometry;
    request.run_detector = true;
    request.fuse = true;
    request.display_transform = display_transform();

    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);

    // One proposal supplies one detection request; the fake's full-view box
    // comes back in the ROI's own pixels, recovered to the full-frame
    // oriented coordinates of the proposal's context bounds.
    MIRAGE_CHECK(result.geometry_proposals.size() == 1);
    MIRAGE_CHECK(detector.calls() == result.geometry_proposals.size());
    MIRAGE_CHECK(result.detections.size() == result.geometry_proposals.size());
    if (result.geometry_proposals.size() == 1 && result.detections.size() == 1) {
        const mirador::GeometricRegionProposal &proposal = result.geometry_proposals[0];
        MIRAGE_CHECK((result.detections[0].bounds ==
                      mirador::RectF{static_cast<float>(proposal.context_bounds.x),
                                     static_cast<float>(proposal.context_bounds.y),
                                     static_cast<float>(proposal.context_bounds.width),
                                     static_cast<float>(proposal.context_bounds.height)}));
    }

    // The fused detection region lands in kDisplay space.
    MIRAGE_CHECK(result.snapshot != nullptr);
    if (result.snapshot != nullptr && result.snapshot->regions.size() == 1) {
        const mirador::VisualRegion &region = result.snapshot->regions[0];
        MIRAGE_CHECK(mirador::has_source(region.source_mask, mirador::RegionSource::kDetector));
        MIRAGE_CHECK(!mirador::has_source(region.source_mask, mirador::RegionSource::kTemplate));
        MIRAGE_CHECK((region.bounds == mirador::RectF{100.0F, 0.0F, 47.0F, 39.0F}));
    }
    host.stop();
}

// ---- D. proposals as fusion evidence --------------------------------------------------

void fuse_proposals_adds_semantics_free_geometry_evidence(executor::Executor &executor) {
    VisualSessionHost host(executor, "geometry-fuse-src", VisualSessionConfig{});
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualGeometryStage geometry;
    geometry.run = true;
    geometry.segment_detector.deviation_tolerance = 2.0;
    geometry.fuse_proposals = true;

    VisualAnalysisRequest request;
    request.frame = rect_frame("geometry-fuse-src", 1).frame;
    request.geometry = geometry;
    request.fuse = true;
    request.display_transform = display_transform();

    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(result.snapshot != nullptr);
    MIRAGE_CHECK(result.geometry_proposals.size() == 1);
    if (result.snapshot == nullptr || result.geometry_proposals.size() != 1) {
        host.stop();
        return;
    }

    // Each proposal enters the fusion as an ExternalRegion with no role, no
    // text and the closure score as confidence: pure geometric fact.
    MIRAGE_CHECK(result.snapshot->regions.size() == 1);
    if (result.snapshot->regions.size() != 1) {
        host.stop();
        return;
    }
    const mirador::VisualRegion &region = result.snapshot->regions[0];
    const mirador::GeometricRegionProposal &proposal = result.geometry_proposals[0];
    MIRAGE_CHECK(region.source_mask ==
                 static_cast<std::uint32_t>(mirador::RegionSource::kExternal));
    MIRAGE_CHECK(region.label.empty());
    MIRAGE_CHECK(region.text.empty());
    MIRAGE_CHECK(
        (region.bounds == mirador::RectF{static_cast<float>(proposal.tight_bounds.x) + 100.0F,
                                         static_cast<float>(proposal.tight_bounds.y),
                                         static_cast<float>(proposal.tight_bounds.width),
                                         static_cast<float>(proposal.tight_bounds.height)}));
    MIRAGE_CHECK(region.confidence == static_cast<double>(proposal.closure_score));

    // The mapping routes the external-only provenance to the geometry form.
    const desktop::VisualSnapshot mapped = to_visual_snapshot(*result.snapshot);
    MIRAGE_CHECK(mapped.regions.size() == 1);
    if (mapped.regions.size() == 1) {
        MIRAGE_CHECK(mapped.regions[0].source == desktop::VisualRegionSource::kGeometry);
        MIRAGE_CHECK(mapped.regions[0].template_id.empty());
    }
    host.stop();
}

// ---- E. cache stats --------------------------------------------------------------------

void cache_stats_report_the_configured_budgets(executor::Executor &executor) {
    FakeDetectorBackend detector = regions_detector({box(8.0F, 8.0F, 16.0F, 16.0F)});
    VisualSessionConfig config;
    config.frame_cache_bytes = std::int64_t{1} * 1024 * 1024;
    config.result_cache_bytes = std::int64_t{1} * 1024 * 1024;
    config.templates.max_bytes = 4096;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "cache-stats-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest first;
    first.frame = icon_frame("cache-stats-src", 1, 8, 8).frame;
    first.enrollments = {
        VisualTemplateEnrollment{"cache:settings", mirador::RectI{8, 8, kIconSide, kIconSide}}};
    std::future<VisualAnalysisResult> first_future = host.submit(std::move(first));
    VisualAnalysisResult enrolled;
    MIRAGE_CHECK(settle_within(first_future, enrolled));
    MIRAGE_CHECK(enrolled.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(enrolled.cache_stats.template_index_entries == 1);
    MIRAGE_CHECK(enrolled.cache_stats.template_index_bytes == kEntryBytes32);
    MIRAGE_CHECK(enrolled.cache_stats.template_index_bytes <= config.templates.max_bytes);
    MIRAGE_CHECK(enrolled.cache_stats.frame_cache_bytes <= config.frame_cache_bytes);
    MIRAGE_CHECK(enrolled.cache_stats.result_cache_bytes <= config.result_cache_bytes);

    VisualAnalysisRequest second;
    second.frame = icon_frame("cache-stats-src", 2, 8, 8).frame;
    second.enrollments = {
        VisualTemplateEnrollment{"cache:other", mirador::RectI{40, 24, kIconSide, kIconSide}}};
    std::future<VisualAnalysisResult> second_future = host.submit(std::move(second));
    VisualAnalysisResult again;
    MIRAGE_CHECK(settle_within(second_future, again));
    MIRAGE_CHECK(again.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(again.cache_stats.template_index_entries == 2);
    MIRAGE_CHECK(again.cache_stats.template_index_bytes <= config.templates.max_bytes);
    host.stop();
}

// ---- F. admission rejections -------------------------------------------------------------

void admission_rejects_broken_pixel_stage_requests(executor::Executor &executor) {
    FakeDetectorBackend detector = regions_detector({box(8.0F, 8.0F, 16.0F, 16.0F)});
    VisualSessionConfig config;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "admission-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    const auto expect_rejected = [&](VisualAnalysisRequest request) {
        std::future<VisualAnalysisResult> future = host.submit(std::move(request));
        VisualAnalysisResult result;
        MIRAGE_CHECK(settle_within(future, result));
        MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kRejected);
        MIRAGE_CHECK(!result.outcome.message.empty());
        MIRAGE_CHECK(result.snapshot == nullptr);
        MIRAGE_CHECK(result.geometry_proposals.empty());
    };

    // Rotated capture with a pixel-space stage (enrollment).
    VisualAnalysisRequest rotated;
    rotated.frame = rotated_frame("admission-src", 1);
    rotated.enrollments = {
        VisualTemplateEnrollment{"cache:settings", mirador::RectI{8, 8, kIconSide, kIconSide}}};
    expect_rejected(std::move(rotated));

    // Probes without the detection capability.
    VisualAnalysisRequest probe_without_detector;
    probe_without_detector.frame = icon_frame("admission-src", 2, 8, 8).frame;
    probe_without_detector.probe_templates = true;
    expect_rejected(std::move(probe_without_detector));

    // Probes with detection output declared outside the oriented space.
    VisualAnalysisRequest probe_wrong_space;
    probe_wrong_space.frame = icon_frame("admission-src", 3, 8, 8).frame;
    probe_wrong_space.run_detector = true;
    probe_wrong_space.detector.output_space = mirador::CoordinateSpaceId::kDisplay;
    probe_wrong_space.probe_templates = true;
    expect_rejected(std::move(probe_wrong_space));

    // Reuse threshold outside [0, 1] (and NaN, which fails both bounds).
    VisualAnalysisRequest threshold_too_big;
    threshold_too_big.frame = icon_frame("admission-src", 4, 8, 8).frame;
    threshold_too_big.run_detector = true;
    threshold_too_big.probe_templates = true;
    threshold_too_big.template_reuse_threshold = 1.5;
    expect_rejected(std::move(threshold_too_big));

    VisualAnalysisRequest threshold_nan;
    threshold_nan.frame = icon_frame("admission-src", 5, 8, 8).frame;
    threshold_nan.run_detector = true;
    threshold_nan.probe_templates = true;
    threshold_nan.template_reuse_threshold = std::numeric_limits<double>::quiet_NaN();
    expect_rejected(std::move(threshold_nan));

    // An enrollment without an identity.
    VisualAnalysisRequest anonymous;
    anonymous.frame = icon_frame("admission-src", 6, 8, 8).frame;
    anonymous.enrollments = {
        VisualTemplateEnrollment{"", mirador::RectI{8, 8, kIconSide, kIconSide}}};
    expect_rejected(std::move(anonymous));

    // The geometry stage without a gray conversion budget.
    VisualAnalysisRequest zero_gray_budget;
    zero_gray_budget.frame = rect_frame("admission-src", 7).frame;
    zero_gray_budget.geometry.run = true;
    zero_gray_budget.geometry.gray_convert_max_bytes = 0;
    expect_rejected(std::move(zero_gray_budget));

    // The worker never executed: no backend call, no enrollment state.
    MIRAGE_CHECK(detector.calls() == 0);
    VisualAnalysisRequest proof;
    proof.frame = icon_frame("admission-src", 8, 8, 8).frame;
    std::future<VisualAnalysisResult> proof_future = host.submit(std::move(proof));
    VisualAnalysisResult proof_result;
    MIRAGE_CHECK(settle_within(proof_future, proof_result));
    MIRAGE_CHECK(proof_result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(proof_result.cache_stats.template_index_entries == 0);
    host.stop();
}

// ---- G. enrollment negative paths -----------------------------------------------------------

void enrollment_outside_the_frame_fails_without_state(executor::Executor &executor) {
    FakeDetectorBackend detector = regions_detector({box(8.0F, 8.0F, 16.0F, 16.0F)});
    VisualSessionConfig config;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "enroll-negative-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest bad;
    bad.frame = icon_frame("enroll-negative-src", 1, 8, 8).frame;
    bad.enrollments = {VisualTemplateEnrollment{"cache:settings",
                                                mirador::RectI{1000, 1000, kIconSide, kIconSide}}};
    std::future<VisualAnalysisResult> bad_future = host.submit(std::move(bad));
    VisualAnalysisResult failed;
    MIRAGE_CHECK(settle_within(bad_future, failed));
    MIRAGE_CHECK(failed.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(failed.outcome.code == mirador::ErrorCode::kInvalidArgument);
    MIRAGE_CHECK(detector.calls() == 0);

    // Nothing was enrolled: a later probe finds no template anywhere.
    VisualAnalysisRequest probe;
    probe.frame = icon_frame("enroll-negative-src", 2, 8, 8).frame;
    probe.run_detector = true;
    probe.probe_templates = true;
    probe.fuse = true;
    probe.display_transform = display_transform();
    std::future<VisualAnalysisResult> probe_future = host.submit(std::move(probe));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(probe_future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(result.template_hits.empty());
    MIRAGE_CHECK(result.cache_stats.template_index_entries == 0);
    MIRAGE_CHECK(result.snapshot != nullptr);
    if (result.snapshot != nullptr) {
        MIRAGE_CHECK(find_template_region(*result.snapshot) == nullptr);
    }
    host.stop();
}

void enrollment_fails_closed_on_a_tiny_fingerprint_budget(executor::Executor &executor) {
    FakeDetectorBackend detector = regions_detector({box(8.0F, 8.0F, 16.0F, 16.0F)});
    VisualSessionConfig config;
    // Above one raw 16x16 Bgra8 crop (1024 bytes, so the enrollment crop and
    // every probe crop still succeed) but below the gray intermediate plus
    // thumbnail need (256 + 1024), so exactly the fingerprint step fails.
    config.templates.fingerprint_max_bytes = 1100;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "enroll-budget-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualAnalysisRequest request;
    request.frame = icon_frame("enroll-budget-src", 1, 8, 8).frame;
    request.enrollments = {
        VisualTemplateEnrollment{"cache:settings", mirador::RectI{8, 8, kIconSide, kIconSide}}};
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult failed;
    MIRAGE_CHECK(settle_within(future, failed));
    MIRAGE_CHECK(failed.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(failed.outcome.code == mirador::ErrorCode::kBudgetExceeded);

    // Fail closed: the index stayed empty (nothing truncated to fit).
    VisualAnalysisRequest proof;
    proof.frame = icon_frame("enroll-budget-src", 2, 8, 8).frame;
    std::future<VisualAnalysisResult> proof_future = host.submit(std::move(proof));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(proof_future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(result.cache_stats.template_index_entries == 0);
    host.stop();
}

void precancelled_request_settles_before_any_side_effect(executor::Executor &executor) {
    FakeDetectorBackend detector = regions_detector({box(8.0F, 8.0F, 16.0F, 16.0F)});
    VisualSessionConfig config;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "enroll-cancel-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    desktop::CancelToken cancelled;
    cancelled.request_cancel();
    VisualAnalysisRequest request;
    request.frame = icon_frame("enroll-cancel-src", 1, 8, 8).frame;
    request.enrollments = {
        VisualTemplateEnrollment{"cache:settings", mirador::RectI{8, 8, kIconSide, kIconSide}}};
    request.external_cancel = cancelled;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kCancelled);
    MIRAGE_CHECK(detector.calls() == 0);

    VisualAnalysisRequest probe;
    probe.frame = icon_frame("enroll-cancel-src", 2, 8, 8).frame;
    probe.run_detector = true;
    probe.probe_templates = true;
    std::future<VisualAnalysisResult> probe_future = host.submit(std::move(probe));
    VisualAnalysisResult after;
    MIRAGE_CHECK(settle_within(probe_future, after));
    MIRAGE_CHECK(after.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(after.template_hits.empty());
    MIRAGE_CHECK(after.cache_stats.template_index_entries == 0);
    host.stop();
}

// ---- H. failure keeps completed stage products ------------------------------------------------

void failed_detection_keeps_the_geometry_products(executor::Executor &executor) {
    VisualSessionConfig config; // no detector backend: run_detector must fail
    VisualSessionHost host(executor, "stage-failure-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    VisualGeometryStage geometry;
    geometry.run = true;
    geometry.segment_detector.deviation_tolerance = 2.0;

    VisualAnalysisRequest request;
    request.frame = rect_frame("stage-failure-src", 1).frame;
    request.geometry = geometry;
    request.run_detector = true;
    std::future<VisualAnalysisResult> future = host.submit(std::move(request));
    VisualAnalysisResult result;
    MIRAGE_CHECK(settle_within(future, result));
    MIRAGE_CHECK(result.outcome.kind == VisualAnalysisOutcome::Kind::kFailed);
    MIRAGE_CHECK(result.outcome.code == mirador::ErrorCode::kBackendUnavailable);
    // The geometry stage ran before the failure; its proposals ride along.
    MIRAGE_CHECK(result.geometry_proposals.size() == 1);
    if (result.geometry_proposals.size() == 1) {
        MIRAGE_CHECK(result.geometry_proposals[0].closure_score == 1.0F);
    }
    host.stop();
}

// ---- I. cross-frame relocation ------------------------------------------------------------------

void enrolled_template_relocates_across_frames(executor::Executor &executor) {
    // One static fake detector announcing both candidate boxes; the icon is
    // found at whichever box currently holds it.
    FakeDetectorBackend detector =
        regions_detector({box(8.0F, 8.0F, 16.0F, 16.0F), box(32.0F, 16.0F, 16.0F, 16.0F)});
    VisualSessionConfig config;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "relocate-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    // Frame A: the icon sits at (8, 8); enroll it from that crop.
    VisualAnalysisRequest enroll;
    enroll.frame = icon_frame("relocate-src", 1, 8, 8).frame;
    enroll.enrollments = {
        VisualTemplateEnrollment{"cache:settings", mirador::RectI{8, 8, kIconSide, kIconSide}}};
    std::future<VisualAnalysisResult> enroll_future = host.submit(std::move(enroll));
    VisualAnalysisResult enrolled;
    MIRAGE_CHECK(settle_within(enroll_future, enrolled));
    MIRAGE_CHECK(enrolled.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);

    // Frame A probed: the icon is hit at its old box; the flat crop of the
    // other box matches nothing (the ramp fingerprint is orthogonal to it).
    VisualAnalysisRequest probe_a;
    probe_a.frame = icon_frame("relocate-src", 2, 8, 8).frame;
    probe_a.run_detector = true;
    probe_a.probe_templates = true;
    probe_a.fuse = true;
    probe_a.display_transform = display_transform();
    std::future<VisualAnalysisResult> probe_a_future = host.submit(std::move(probe_a));
    VisualAnalysisResult at_old;
    MIRAGE_CHECK(settle_within(probe_a_future, at_old));
    MIRAGE_CHECK(at_old.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(at_old.template_hits.size() == 1);
    if (at_old.template_hits.size() == 1) {
        MIRAGE_CHECK(at_old.template_hits[0].template_id == "cache:settings");
        MIRAGE_CHECK(at_old.template_hits[0].evidence ==
                     mirador::VisualEvidenceKind::kExactContent);
    }
    const mirador::VisualRegion *old_region =
        at_old.snapshot != nullptr ? find_template_region(*at_old.snapshot) : nullptr;
    MIRAGE_CHECK(old_region != nullptr);
    if (old_region != nullptr) {
        MIRAGE_CHECK(old_region->label == "cache:settings");
        MIRAGE_CHECK((old_region->bounds == mirador::RectF{108.0F, 8.0F, 16.0F, 16.0F}));
    }

    // Frame B: same source, sequence+1, the icon re-drawn at (32, 16). The
    // detection regions ride along; the index re-locates the enrolled
    // identity at the new crop with the same template_id.
    VisualAnalysisRequest probe_b;
    probe_b.frame = icon_frame("relocate-src", 3, 32, 16).frame;
    probe_b.run_detector = true;
    probe_b.probe_templates = true;
    probe_b.fuse = true;
    probe_b.display_transform = display_transform();
    std::future<VisualAnalysisResult> probe_b_future = host.submit(std::move(probe_b));
    VisualAnalysisResult at_new;
    MIRAGE_CHECK(settle_within(probe_b_future, at_new));
    MIRAGE_CHECK(at_new.outcome.kind == VisualAnalysisOutcome::Kind::kCompleted);
    MIRAGE_CHECK(at_new.template_hits.size() == 1);
    if (at_new.template_hits.size() == 1) {
        MIRAGE_CHECK(at_new.template_hits[0].template_id == "cache:settings");
    }
    const mirador::VisualRegion *new_region =
        at_new.snapshot != nullptr ? find_template_region(*at_new.snapshot) : nullptr;
    MIRAGE_CHECK(new_region != nullptr);
    if (new_region != nullptr) {
        MIRAGE_CHECK(new_region->label == "cache:settings");
        MIRAGE_CHECK((new_region->bounds == mirador::RectF{132.0F, 16.0F, 16.0F, 16.0F}));
    }

    // The mapped identity is stable while the bounds center moved.
    if (at_old.snapshot != nullptr && at_new.snapshot != nullptr) {
        // Materialize the mapped snapshots first: the mapping returns by
        // value, so pointers into a temporary would dangle.
        const desktop::VisualSnapshot mapped_old = to_visual_snapshot(*at_old.snapshot);
        const desktop::VisualSnapshot mapped_new = to_visual_snapshot(*at_new.snapshot);
        const desktop::VisualRegionEntry *entry_old = find_template_entry(mapped_old);
        const desktop::VisualRegionEntry *entry_new = find_template_entry(mapped_new);
        MIRAGE_CHECK(entry_old != nullptr);
        MIRAGE_CHECK(entry_new != nullptr);
        if (entry_old != nullptr && entry_new != nullptr) {
            MIRAGE_CHECK(entry_old->template_id == "cache:settings");
            MIRAGE_CHECK(entry_new->template_id == "cache:settings");
            const int old_center_x = entry_old->bounds.x + entry_old->bounds.width / 2;
            const int old_center_y = entry_old->bounds.y + entry_old->bounds.height / 2;
            const int new_center_x = entry_new->bounds.x + entry_new->bounds.width / 2;
            const int new_center_y = entry_new->bounds.y + entry_new->bounds.height / 2;
            MIRAGE_CHECK(old_center_x == 116);
            MIRAGE_CHECK(old_center_y == 16);
            MIRAGE_CHECK(new_center_x == 140);
            MIRAGE_CHECK(new_center_y == 24);
        }
    }

    MIRAGE_CHECK(detector.calls() == 2); // one detection call per frame content
    host.stop();
}

} // namespace

template <typename Scenario> void run_scenario(const char *name, Scenario scenario) {
    std::fprintf(stderr, "[visual_cache_geometry_test] scenario: %s\n", name);
    scenario();
}

int main() {
    // Every scenario runs a real VisualSessionHost on an Executor blocking
    // worker (same consumption form as visual_session_test); each owns a
    // unique source id and stops itself before the ordered shutdown below.
    executor::Executor executor;
    const bool executor_ready = executor.initialize_ex(executor::ExecutorConfig{}).ok;
    MIRAGE_CHECK(executor_ready);
    if (executor_ready) {
        run_scenario("template_identity_flows_through_the_whole_pipeline",
                     [&] { template_identity_flows_through_the_whole_pipeline(executor); });
        run_scenario("geometry_stage_proposes_the_painted_rectangle",
                     [&] { geometry_stage_proposes_the_painted_rectangle(executor); });
        run_scenario("detector_roi_from_geometry_runs_one_call_per_proposal",
                     [&] { detector_roi_from_geometry_runs_one_call_per_proposal(executor); });
        run_scenario("fuse_proposals_adds_semantics_free_geometry_evidence",
                     [&] { fuse_proposals_adds_semantics_free_geometry_evidence(executor); });
        run_scenario("cache_stats_report_the_configured_budgets",
                     [&] { cache_stats_report_the_configured_budgets(executor); });
        run_scenario("admission_rejects_broken_pixel_stage_requests",
                     [&] { admission_rejects_broken_pixel_stage_requests(executor); });
        run_scenario("enrollment_outside_the_frame_fails_without_state",
                     [&] { enrollment_outside_the_frame_fails_without_state(executor); });
        run_scenario("enrollment_fails_closed_on_a_tiny_fingerprint_budget",
                     [&] { enrollment_fails_closed_on_a_tiny_fingerprint_budget(executor); });
        run_scenario("precancelled_request_settles_before_any_side_effect",
                     [&] { precancelled_request_settles_before_any_side_effect(executor); });
        run_scenario("failed_detection_keeps_the_geometry_products",
                     [&] { failed_detection_keeps_the_geometry_products(executor); });
        run_scenario("enrolled_template_relocates_across_frames",
                     [&] { enrolled_template_relocates_across_frames(executor); });
        executor.shutdown(true);
    }
    return mirage::testing::finish("visual_cache_geometry_test");
}
