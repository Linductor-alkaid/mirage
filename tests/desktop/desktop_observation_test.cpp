#include "../support/test.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/element_reference.hpp>
#include <mirage/desktop/semantic_snapshot.hpp>

#include <string>

namespace {

void observation_defaults() {
    mirage::desktop::DesktopObservation observation;
    MIRAGE_CHECK(observation.active_application.empty());
    MIRAGE_CHECK(observation.active_window.empty());
    MIRAGE_CHECK(observation.window_geometry.width == 0);
    MIRAGE_CHECK(observation.window_geometry.height == 0);
    MIRAGE_CHECK(!observation.window_focused);
    MIRAGE_CHECK(observation.focused_element.empty());
    MIRAGE_CHECK(observation.semantic_snapshot.nodes.empty());
    MIRAGE_CHECK(observation.semantic_snapshot.application.empty());
    MIRAGE_CHECK(observation.visual_snapshot_ref.empty());
    MIRAGE_CHECK(observation.pointer_state.x == 0);
    MIRAGE_CHECK(observation.pointer_state.y == 0);
    MIRAGE_CHECK(observation.environment_state.empty());
}

void schema_tag_frozen_at_v1() {
    MIRAGE_CHECK(std::string(mirage::desktop::kObservationSchemaVersion) == "1.0");
}

} // namespace

int main() {
    observation_defaults();
    schema_tag_frozen_at_v1();
    return mirage::testing::finish("desktop_observation");
}
