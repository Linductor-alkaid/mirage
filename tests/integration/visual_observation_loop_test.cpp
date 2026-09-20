// M3-03 visual observation loop tests (DEC-016 decisions 2 and 3): the
// fusion -> VisualSnapshot mapping (provenance precedence, text/template_id
// forms, half-away-from-zero bounds rounding, determinism), the
// publish_visual_snapshot boundary (ref numbering, scope handles, budget
// refusal), and the closed loop without any display server: a real visual
// session over the fake backends fuses a frame into kDisplay space, the
// fusion is published into the VisualReferenceRegistry, an
// ObservationAssembler observe captures the generation, and an
// ElementTargetExecutor click on "@v1" injects the pointer sequence at the
// fused region's center.

#include "../support/fake_desktop_environment.hpp"
#include "../support/test.hpp"

#include <mirage/integration/fake_visual_backend.hpp>
#include <mirage/integration/visual_observation_mapper.hpp>
#include <mirage/integration/visual_session_host.hpp>

#include <mirage/desktop/element_target_executor.hpp>
#include <mirage/desktop/observation_assembler.hpp>
#include <mirage/desktop/visual_reference_registry.hpp>
#include <mirage/desktop/visual_snapshot.hpp>

#include <executor/executor.hpp>

#include <mirador/image_view.hpp>
#include <mirador/ocr_backend.hpp>
#include <mirador/pixel_format.hpp>
#include <mirador/semantic_snapshot.hpp>
#include <mirador/transform.hpp>

#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace desktop = mirage::desktop;

using mirage::integration::FakeDetectorBackend;
using mirage::integration::FakeDetectorConfig;
using mirage::integration::FakeOcrBackend;
using mirage::integration::FakeOcrConfig;
using mirage::integration::publish_visual_snapshot;
using mirage::integration::to_visual_snapshot;
using mirage::integration::VisualAnalysisOutcome;
using mirage::integration::VisualAnalysisRequest;
using mirage::integration::VisualAnalysisResult;
using mirage::integration::VisualSessionConfig;
using mirage::integration::VisualSessionHost;

/// Upper bound for every session wait; a hang is a broken path, not a slow
/// machine.
constexpr std::chrono::seconds kWaitBound{10};

bool settle_within(std::future<VisualAnalysisResult> &future, VisualAnalysisResult &out) {
    if (future.wait_for(kWaitBound) != std::future_status::ready) {
        return false;
    }
    out = future.get();
    return true;
}

/// A uniform owning Bgra8 frame (identity semantics for the fake backends
/// and the change detector).
mirador::Frame session_frame(const std::string &source_id, std::uint64_t sequence) {
    const std::int32_t width = 32;
    const std::int32_t height = 16;
    const std::size_t stride = static_cast<std::size_t>(width) * 4u;
    auto pixels = std::make_shared<std::vector<std::uint8_t>>(
        stride * static_cast<std::size_t>(height), std::uint8_t{0x40});
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

mirador::SemanticSnapshot eight_region_fusion() {
    using mirador::RegionSource;
    mirador::SemanticSnapshot fusion;
    fusion.coordinate_space = mirador::CoordinateSpaceId::kDisplay;

    mirador::VisualRegion ocr;
    ocr.source_mask = static_cast<std::uint32_t>(RegionSource::kOcr);
    ocr.text = "Save file";
    ocr.label = "ignored-label";
    ocr.bounds = mirador::RectF{1.2F, 2.5F, -3.7F, 10.5F};
    ocr.confidence = 0.91F;
    fusion.regions.push_back(ocr);

    mirador::VisualRegion template_region;
    template_region.source_mask = static_cast<std::uint32_t>(RegionSource::kTemplate);
    template_region.text = "caption";
    template_region.label = "btn-save";
    template_region.bounds = mirador::RectF{0.5F, 0.0F, 2.5F, 4.5F};
    template_region.confidence = 0.8F;
    fusion.regions.push_back(template_region);

    mirador::VisualRegion detector;
    detector.source_mask = static_cast<std::uint32_t>(RegionSource::kDetector);
    detector.label = "checkbox";
    detector.bounds = mirador::RectF{10.4F, -0.5F, 7.5F, 3.49F};
    detector.confidence = 0.7F;
    fusion.regions.push_back(detector);

    mirador::VisualRegion detector_text_fallback;
    detector_text_fallback.source_mask = static_cast<std::uint32_t>(RegionSource::kDetector);
    detector_text_fallback.text = "row-header"; // label empty: text carries the line form
    detector_text_fallback.bounds = mirador::RectF{0.0F, 0.0F, 1.0F, 1.0F};
    fusion.regions.push_back(detector_text_fallback);

    mirador::VisualRegion external_text;
    external_text.source_mask = static_cast<std::uint32_t>(RegionSource::kExternal);
    external_text.text = "ext-note";
    external_text.bounds = mirador::RectF{2.0F, 2.0F, 3.0F, 3.0F};
    fusion.regions.push_back(external_text);

    mirador::VisualRegion external_label;
    external_label.source_mask = static_cast<std::uint32_t>(RegionSource::kExternal);
    external_label.label = "panel";
    external_label.bounds = mirador::RectF{4.0F, 4.0F, 5.0F, 5.0F};
    fusion.regions.push_back(external_label);

    mirador::VisualRegion composite;
    composite.source_mask =
        static_cast<std::uint32_t>(RegionSource::kOcr) | RegionSource::kDetector;
    composite.text = "combo";
    composite.label = "combo-label";
    composite.bounds = mirador::RectF{6.0F, 6.0F, 6.0F, 6.0F};
    composite.confidence = 0.55F;
    fusion.regions.push_back(composite);

    // A template bit without a label is not a template entry: it degrades to
    // the geometry form instead of inventing an empty template_id.
    mirador::VisualRegion labelless_template;
    labelless_template.source_mask = static_cast<std::uint32_t>(RegionSource::kTemplate);
    labelless_template.text = "orphan";
    labelless_template.bounds = mirador::RectF{8.0F, 8.0F, 2.0F, 2.0F};
    fusion.regions.push_back(labelless_template);

    return fusion;
}

bool same_mapped_region(const desktop::VisualRegionEntry &lhs,
                        const desktop::VisualRegionEntry &rhs) {
    return lhs.source == rhs.source && lhs.bounds.x == rhs.bounds.x &&
           lhs.bounds.y == rhs.bounds.y && lhs.bounds.width == rhs.bounds.width &&
           lhs.bounds.height == rhs.bounds.height && lhs.text == rhs.text &&
           lhs.template_id == rhs.template_id && lhs.confidence == rhs.confidence;
}

// ---- to_visual_snapshot -------------------------------------------------------

void to_visual_snapshot_maps_provenance_precedence_and_fields() {
    const mirador::SemanticSnapshot fusion = eight_region_fusion();
    const desktop::VisualSnapshot mapped = to_visual_snapshot(fusion);

    MIRAGE_CHECK(mapped.scope_ref.empty()); // the registry assigns refs and scope
    MIRAGE_CHECK(mapped.regions.size() == fusion.regions.size());
    if (mapped.regions.size() != fusion.regions.size()) {
        return;
    }

    const desktop::VisualRegionEntry &ocr = mapped.regions[0];
    MIRAGE_CHECK(ocr.source == desktop::VisualRegionSource::kOcr);
    MIRAGE_CHECK(ocr.text == "Save file"); // fusion text wins over the label
    MIRAGE_CHECK(ocr.template_id.empty());
    MIRAGE_CHECK(ocr.bounds.x == 1);
    MIRAGE_CHECK(ocr.bounds.y == 3); // 2.5 rounds half away from zero
    MIRAGE_CHECK(ocr.bounds.width == -4);
    MIRAGE_CHECK(ocr.bounds.height == 11); // 10.5 rounds half away from zero
    MIRAGE_CHECK(ocr.confidence == static_cast<double>(0.91F));
    MIRAGE_CHECK(ocr.ref.empty());

    const desktop::VisualRegionEntry &template_entry = mapped.regions[1];
    MIRAGE_CHECK(template_entry.source == desktop::VisualRegionSource::kTemplate);
    MIRAGE_CHECK(template_entry.template_id == "btn-save"); // the label is the template id
    MIRAGE_CHECK(template_entry.text == "caption");         // the fusion text rides along
    MIRAGE_CHECK(template_entry.bounds.x == 1);             // 0.5 -> 1
    MIRAGE_CHECK(template_entry.bounds.y == 0);
    MIRAGE_CHECK(template_entry.bounds.width == 3);  // 2.5 -> 3
    MIRAGE_CHECK(template_entry.bounds.height == 5); // 4.5 -> 5

    const desktop::VisualRegionEntry &detector = mapped.regions[2];
    MIRAGE_CHECK(detector.source == desktop::VisualRegionSource::kDetector);
    MIRAGE_CHECK(detector.text == "checkbox"); // the label is the agent-facing line
    MIRAGE_CHECK(detector.template_id.empty());
    MIRAGE_CHECK(detector.bounds.x == 10);
    MIRAGE_CHECK(detector.bounds.y == -1);     // -0.5 -> -1 (away from zero)
    MIRAGE_CHECK(detector.bounds.width == 8);  // 7.5 -> 8
    MIRAGE_CHECK(detector.bounds.height == 3); // 3.49 -> 3

    const desktop::VisualRegionEntry &detector_fallback = mapped.regions[3];
    MIRAGE_CHECK(detector_fallback.source == desktop::VisualRegionSource::kDetector);
    MIRAGE_CHECK(detector_fallback.text == "row-header");

    const desktop::VisualRegionEntry &external_text = mapped.regions[4];
    MIRAGE_CHECK(external_text.source == desktop::VisualRegionSource::kGeometry);
    MIRAGE_CHECK(external_text.text == "ext-note");

    const desktop::VisualRegionEntry &external_label = mapped.regions[5];
    MIRAGE_CHECK(external_label.source == desktop::VisualRegionSource::kGeometry);
    MIRAGE_CHECK(external_label.text == "panel");

    const desktop::VisualRegionEntry &composite = mapped.regions[6];
    MIRAGE_CHECK(composite.source == desktop::VisualRegionSource::kOcr); // kOcr bit wins
    MIRAGE_CHECK(composite.text == "combo");

    const desktop::VisualRegionEntry &labelless_template = mapped.regions[7];
    MIRAGE_CHECK(labelless_template.source == desktop::VisualRegionSource::kGeometry);
    MIRAGE_CHECK(labelless_template.text == "orphan");
    MIRAGE_CHECK(labelless_template.template_id.empty());
}

void identical_fusions_map_identically() {
    const mirador::SemanticSnapshot fusion = eight_region_fusion();
    const desktop::VisualSnapshot first = to_visual_snapshot(fusion);
    const desktop::VisualSnapshot second = to_visual_snapshot(fusion);

    MIRAGE_CHECK(first.regions.size() == second.regions.size());
    for (std::size_t index = 0; index < first.regions.size() && index < second.regions.size();
         ++index) {
        MIRAGE_CHECK(same_mapped_region(first.regions[index], second.regions[index]));
    }
}

// ---- publish_visual_snapshot --------------------------------------------------

void publish_numbers_refs_and_assigns_scope_handles() {
    desktop::VisualReferenceRegistry registry;
    mirador::SemanticSnapshot fusion = eight_region_fusion();
    // Keep only the first two regions for a compact assertion surface.
    fusion.regions.resize(2);

    const desktop::VisualPublishOutcome outcome = publish_visual_snapshot(fusion, registry);
    MIRAGE_CHECK(outcome.ok);
    MIRAGE_CHECK(outcome.error.code.empty());
    MIRAGE_CHECK(outcome.snapshot.scope_ref == "@vs1");
    MIRAGE_CHECK(outcome.snapshot.regions.size() == 2);
    if (outcome.snapshot.regions.size() == 2) {
        MIRAGE_CHECK(outcome.snapshot.regions[0].ref == "@v1");
        MIRAGE_CHECK(outcome.snapshot.regions[0].text == "Save file");
        MIRAGE_CHECK(outcome.snapshot.regions[1].ref == "@v2");
        MIRAGE_CHECK(outcome.snapshot.regions[1].template_id == "btn-save");
    }
    MIRAGE_CHECK(registry.size() == 2);
    MIRAGE_CHECK(registry.current().scope_ref == "@vs1");
    MIRAGE_CHECK(registry.current().regions.size() == outcome.snapshot.regions.size());
    if (registry.current().regions.size() == outcome.snapshot.regions.size()) {
        MIRAGE_CHECK(
            same_mapped_region(registry.current().regions[0], outcome.snapshot.regions[0]));
        MIRAGE_CHECK(
            same_mapped_region(registry.current().regions[1], outcome.snapshot.regions[1]));
    }

    // The next publication continues the scope sequence and renumbers.
    const desktop::VisualPublishOutcome second = publish_visual_snapshot(fusion, registry);
    MIRAGE_CHECK(second.ok);
    MIRAGE_CHECK(second.snapshot.scope_ref == "@vs2");
    MIRAGE_CHECK(registry.current().scope_ref == "@vs2");
}

void over_budget_publish_refuses_without_touching_the_active_set() {
    desktop::VisualReferenceRegistry registry;
    mirador::SemanticSnapshot small = eight_region_fusion();
    small.regions.resize(1);
    const desktop::VisualPublishOutcome seeded = publish_visual_snapshot(small, registry);
    MIRAGE_CHECK(seeded.ok);
    MIRAGE_CHECK(seeded.snapshot.scope_ref == "@vs1");

    mirador::SemanticSnapshot oversized = eight_region_fusion(); // 8 regions
    const desktop::VisualSnapshotLimits budget{/*max_regions=*/2};
    const desktop::VisualPublishOutcome refused =
        publish_visual_snapshot(oversized, registry, budget);
    MIRAGE_CHECK(!refused.ok);
    MIRAGE_CHECK(refused.error.code == "snapshot_too_large");
    MIRAGE_CHECK(refused.snapshot.regions.empty());
    // Nothing was truncated or replaced: generation 1 stays fully active.
    MIRAGE_CHECK(registry.size() == 1);
    MIRAGE_CHECK(registry.current().scope_ref == "@vs1");
    const auto active = registry.resolve("@v1");
    MIRAGE_CHECK(active.has_value());
    if (active) {
        MIRAGE_CHECK(active->text == "Save file");
    }
}

// ---- end-to-end loop (no display server) --------------------------------------

void fusion_flows_through_publish_observe_and_click(executor::Executor &executor) {
    FakeOcrConfig ocr_config;
    ocr_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    FakeOcrBackend ocr(ocr_config);
    FakeDetectorConfig detector_config;
    detector_config.info.accepted_formats = {mirador::PixelFormat::kBgra8};
    FakeDetectorBackend detector(detector_config);

    VisualSessionConfig config;
    config.ocr_backend = &ocr;
    config.detector_backend = &detector;
    VisualSessionHost host(executor, "loop-src", config);
    std::string error;
    MIRAGE_CHECK(host.start(error));

    // Fusion into kDisplay space: the (100, 0) translation stands in for a
    // display offset, so the fused bounds are already global desktop
    // coordinates (DEC-016 decision 3).
    VisualAnalysisRequest request;
    request.frame = session_frame("loop-src", 42);
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
    MIRAGE_CHECK(result.snapshot != nullptr);
    if (result.snapshot == nullptr) {
        host.stop();
        return;
    }

    // The fusion merged the full-view OCR and detector evidence into one
    // region carrying both bits, translated into kDisplay space.
    const mirador::SemanticSnapshot &fusion = *result.snapshot;
    MIRAGE_CHECK(fusion.coordinate_space == mirador::CoordinateSpaceId::kDisplay);
    MIRAGE_CHECK(fusion.regions.size() == 1);
    if (fusion.regions.size() == 1) {
        MIRAGE_CHECK(
            mirador::has_source(fusion.regions[0].source_mask, mirador::RegionSource::kOcr));
        MIRAGE_CHECK(
            mirador::has_source(fusion.regions[0].source_mask, mirador::RegionSource::kDetector));
        MIRAGE_CHECK(fusion.regions[0].text == "mirage-fake");
        MIRAGE_CHECK((fusion.regions[0].bounds == mirador::RectF{100.0F, 0.0F, 32.0F, 16.0F}));
    }

    // Publish into the registry: the composite region maps onto the kOcr
    // source form with its text kept.
    desktop::VisualReferenceRegistry registry;
    const desktop::VisualPublishOutcome published = publish_visual_snapshot(fusion, registry);
    MIRAGE_CHECK(published.ok);
    MIRAGE_CHECK(published.snapshot.scope_ref == "@vs1");
    MIRAGE_CHECK(published.snapshot.regions.size() == 1);
    if (published.snapshot.regions.size() == 1) {
        MIRAGE_CHECK(published.snapshot.regions[0].ref == "@v1");
        MIRAGE_CHECK(published.snapshot.regions[0].source == desktop::VisualRegionSource::kOcr);
        MIRAGE_CHECK(published.snapshot.regions[0].text == "mirage-fake");
        MIRAGE_CHECK(published.snapshot.regions[0].bounds.x == 100);
        MIRAGE_CHECK(published.snapshot.regions[0].bounds.y == 0);
        MIRAGE_CHECK(published.snapshot.regions[0].bounds.width == 32);
        MIRAGE_CHECK(published.snapshot.regions[0].bounds.height == 16);
    }

    // Observe: the assembler captures the published generation.
    mirage::testing::FakeDesktopEnvironment env;
    desktop::ObservationAssembler assembler(env, &registry);
    desktop::ObservationComponents components;
    components.visual_snapshot = true;
    const desktop::ObservationAssemblyOutcome observed = assembler.assemble(components);
    MIRAGE_CHECK(observed.ok);
    MIRAGE_CHECK(observed.visual_snapshot.captured);
    MIRAGE_CHECK(observed.observation.visual_snapshot_ref == "@vs1");
    MIRAGE_CHECK(observed.observation.visual_snapshot.regions.size() == 1);

    // Act: the "@v1" reference resolves against the published generation and
    // the click lands at the region center ((100+32/2), (0+16/2)).
    desktop::ElementTargetExecutor click_executor(env.accessibility(), &registry, env.input());
    desktop::ElementTarget target;
    target.reference.id = "@v1";
    const desktop::TargetResolution acted =
        click_executor.click(target, &observed.observation.semantic_snapshot);
    MIRAGE_CHECK(acted.ok);
    MIRAGE_CHECK(acted.ring == desktop::ResolutionRing::kVisualReference);
    MIRAGE_CHECK(acted.point.x == 116);
    MIRAGE_CHECK(acted.point.y == 8);
    MIRAGE_CHECK(env.input_log.size() == 3);
    if (env.input_log.size() == 3) {
        MIRAGE_CHECK(env.input_log[0] == "move 116,8");
        MIRAGE_CHECK(env.input_log[1] == "press left");
        MIRAGE_CHECK(env.input_log[2] == "release left");
    }

    // The same region is reachable through its OCR text (the kOcr form of
    // design doc section 8) at the same point.
    desktop::ElementTarget by_text;
    by_text.visual.ocr_text = "mirage-fake";
    const desktop::TargetResolution text_click = click_executor.click(by_text, nullptr);
    MIRAGE_CHECK(text_click.ok);
    MIRAGE_CHECK(text_click.ring == desktop::ResolutionRing::kVisualHint);
    MIRAGE_CHECK(text_click.point.x == 116);
    MIRAGE_CHECK(text_click.point.y == 8);
    MIRAGE_CHECK(env.input_log.size() == 6);
    if (env.input_log.size() == 6) {
        MIRAGE_CHECK(env.input_log[3] == "move 116,8");
    }

    // A freshly published generation is what the next observe sees.
    const desktop::VisualPublishOutcome republished = publish_visual_snapshot(fusion, registry);
    MIRAGE_CHECK(republished.ok);
    MIRAGE_CHECK(republished.snapshot.scope_ref == "@vs2");
    const desktop::ObservationAssemblyOutcome second_observe = assembler.assemble(components);
    MIRAGE_CHECK(second_observe.ok);
    MIRAGE_CHECK(second_observe.observation.visual_snapshot_ref == "@vs2");

    host.stop();
}

} // namespace

template <typename Scenario> void run_scenario(const char *name, Scenario scenario) {
    std::fprintf(stderr, "[visual_observation_loop_test] scenario: %s\n", name);
    scenario();
}

int main() {
    run_scenario("to_visual_snapshot_maps_provenance_precedence_and_fields",
                 to_visual_snapshot_maps_provenance_precedence_and_fields);
    run_scenario("identical_fusions_map_identically", identical_fusions_map_identically);
    run_scenario("publish_numbers_refs_and_assigns_scope_handles",
                 publish_numbers_refs_and_assigns_scope_handles);
    run_scenario("over_budget_publish_refuses_without_touching_the_active_set",
                 over_budget_publish_refuses_without_touching_the_active_set);

    // The loop scenario runs a real VisualSessionHost on an Executor blocking
    // worker (same consumption form as visual_session_test).
    executor::Executor executor;
    const bool executor_ready = executor.initialize_ex(executor::ExecutorConfig{}).ok;
    MIRAGE_CHECK(executor_ready);
    if (executor_ready) {
        run_scenario("fusion_flows_through_publish_observe_and_click",
                     [&] { fusion_flows_through_publish_observe_and_click(executor); });
        executor.shutdown(true);
    }
    return mirage::testing::finish("visual_observation_loop_test");
}
