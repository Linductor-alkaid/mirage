#include <mirage/desktop/desktop_environment.hpp>

namespace mirage::desktop {

// Keeps the static library non-empty while the provider surface is still
// skeleton-only; the observation schema tag is part of the DesktopObservation
// payload contract shared with Mira.
static_assert(kObservationSchemaVersion != nullptr);

} // namespace mirage::desktop
