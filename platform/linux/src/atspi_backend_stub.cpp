// Link-time stub for machines without the AT-SPI2 dev packages: the backend
// exists (so linux_desktop_environment.cpp compiles unchanged) but reports
// no capability, and no atspi/glib header is touched. See
// platform/CMakeLists.txt.

#include "atspi_backend.hpp"

namespace mirage::platform::linux_backend {

struct AtspiBackend::Impl {};

AtspiBackend::~AtspiBackend() = default;

std::unique_ptr<AtspiBackend> AtspiBackend::open(mirage::desktop::WindowProvider *) {
    return nullptr; // fail closed: no AT-SPI2 support in this build (DEC-015)
}

mirage::desktop::SnapshotOutcome
AtspiBackend::semantic_snapshot(const std::string &,
                                const mirage::desktop::SemanticSnapshotLimits &,
                                const mirage::desktop::CancelToken &) {
    return {};
}

mirage::desktop::ElementActionOutcome
AtspiBackend::activate_element(const mirage::desktop::ElementTarget &,
                               const mirage::desktop::CancelToken &) {
    return {};
}

mirage::desktop::ElementActionOutcome AtspiBackend::set_text(const mirage::desktop::ElementTarget &,
                                                             const std::string &,
                                                             const mirage::desktop::InputLimits &,
                                                             const mirage::desktop::CancelToken &) {
    return {};
}

} // namespace mirage::platform::linux_backend
