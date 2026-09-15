#include "../support/test.hpp"

#include <mirage/desktop/desktop_environment.hpp>
#include <mirage/desktop/desktop_observation.hpp>
#include <mirage/desktop/element_reference.hpp>

#include <string>

namespace {

void observation_defaults() {
    mirage::desktop::DesktopObservation observation;
    MIRAGE_CHECK(observation.active_application.empty());
    MIRAGE_CHECK(observation.active_window.empty());
    MIRAGE_CHECK(observation.window_geometry.width == 0);
    MIRAGE_CHECK(observation.window_geometry.height == 0);
    MIRAGE_CHECK(observation.semantic_snapshot.empty());
    MIRAGE_CHECK(observation.visual_snapshot_ref.empty());
    MIRAGE_CHECK(!observation.window_focused);
}

void schema_tag_is_stable_string() {
    MIRAGE_CHECK(std::string(mirage::desktop::kObservationSchemaVersion) == "0.1");
}

} // namespace

int main() {
    observation_defaults();
    schema_tag_is_stable_string();
    return mirage::testing::finish("desktop_observation");
}
