#include "../support/test.hpp"

#define None 0L // X11 headers define this macro before downstream includes.
#include <mirage/integration/visual_observation_mapper.hpp>
#undef None

#include <array>

using namespace mirage;

int main() {
    mirador::VisualRegion region;
    region.stable_id = 42;
    region.bounds = {10.4F, -5.5F, 80.1F, 24.0F};
    region.text = "保存";
    region.label = "detector-class";
    region.confidence = 0.9F;
    region.source_mask = mirador::RegionSource::kOcr | mirador::RegionSource::kDetector;
    auto object = integration::to_visual_scene_object(region, 3, 7);
    MIRAGE_CHECK(object.id == 42 && object.incarnation == 3 && object.content_revision == 7);
    MIRAGE_CHECK(object.source == desktop::VisualRegionSource::kOcr);
    MIRAGE_CHECK(object.text == "保存" && object.label == "detector-class");
    MIRAGE_CHECK(object.bounds.x == 10 && object.bounds.y == -6);
    MIRAGE_CHECK(!object.identity_confirmed);
    desktop::VisualScene scene("source-session");
    std::array objects{object};
    MIRAGE_CHECK(scene.apply({"source-session", 1, 1, 0, true, objects, {}}).ok);
    desktop::VisualSemanticBinding annotation{
        "source-session", "request-1", "保存按钮", 1, 42, 3, 7, 1};
    MIRAGE_CHECK(scene.annotate(annotation).error.code == "semantic_identity_unconfirmed");
    // Identity confirmation comes from the owning guard/tracker. Mapping raw
    // fusion output must not silently promote model semantics or privileges.
    objects[0] = integration::to_visual_scene_object(region, 3, 7, true);
    MIRAGE_CHECK(scene.apply({"source-session", 1, 2, scene.revision(), false, objects, {}}).ok);
    MIRAGE_CHECK(scene.annotate(annotation).ok);
    region.bounds.x += 20;
    objects[0] = integration::to_visual_scene_object(region, 3, 7, true);
    const auto moved = scene.apply({"source-session", 1, 3, scene.revision(), false, objects, {}});
    MIRAGE_CHECK(moved.ok && moved.updated.size() == 1);
    MIRAGE_CHECK(moved.updated[0].semantic_state == desktop::VisualSemanticState::kActive);
    MIRAGE_CHECK(moved.updated[0].binding->frame_sequence == 1);
    MIRAGE_CHECK(moved.updated[0].object.label == "detector-class");
    MIRAGE_CHECK(moved.updated[0].object.text == "保存");
    return testing::finish("visual_scene_mapping");
}
