#include <mirage/integration/visual_observation_mapper.hpp>

#include <cmath>

namespace mirage::integration {

namespace {

/// Rounds one continuous coordinate half away from zero (deterministic on
/// every platform libm that honors the C rounding contract).
std::int32_t round_component(float value) { return static_cast<std::int32_t>(std::lroundf(value)); }

mirage::desktop::VisualRegionEntry map_region(const mirador::VisualRegion &region) {
    mirage::desktop::VisualRegionEntry entry;
    entry.bounds = {round_component(region.bounds.x), round_component(region.bounds.y),
                    round_component(region.bounds.width), round_component(region.bounds.height)};
    entry.confidence = static_cast<double>(region.confidence);

    if (mirador::has_source(region.source_mask, mirador::RegionSource::kOcr)) {
        entry.source = mirage::desktop::VisualRegionSource::kOcr;
        entry.text = region.text.empty() ? region.label : region.text;
    } else if (mirador::has_source(region.source_mask, mirador::RegionSource::kTemplate) &&
               !region.label.empty()) {
        entry.source = mirage::desktop::VisualRegionSource::kTemplate;
        entry.template_id = region.label;
        entry.text = region.text;
    } else if (mirador::has_source(region.source_mask, mirador::RegionSource::kDetector)) {
        entry.source = mirage::desktop::VisualRegionSource::kDetector;
        entry.text = region.label.empty() ? region.text : region.label;
    } else {
        entry.source = mirage::desktop::VisualRegionSource::kGeometry;
        entry.text = region.label.empty() ? region.text : region.label;
    }
    return entry;
}

} // namespace

desktop::VisualSnapshot to_visual_snapshot(const mirador::SemanticSnapshot &fusion) {
    desktop::VisualSnapshot snapshot;
    snapshot.regions.reserve(fusion.regions.size());
    for (const mirador::VisualRegion &region : fusion.regions) {
        snapshot.regions.push_back(map_region(region));
    }
    return snapshot;
}

desktop::VisualPublishOutcome publish_visual_snapshot(const mirador::SemanticSnapshot &fusion,
                                                      desktop::VisualReferenceRegistry &registry,
                                                      const desktop::VisualSnapshotLimits &limits) {
    return registry.publish(to_visual_snapshot(fusion), limits);
}

} // namespace mirage::integration
