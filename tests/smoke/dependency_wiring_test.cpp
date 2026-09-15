#include "../support/test.hpp"

#include <mira/version.hpp>

#include <mirage/integration/mirador_service.hpp>
#include <mirage/platform/platform_info.hpp>
#include <mirage/runtime/mira_host.hpp>
#include <mirage/runtime/runtime_service.hpp>

namespace {

// Smoke test for the pinned-dependency wiring (DEC-001): the binary must be
// genuinely linked against the pinned mira and mirador cores, not just
// compiled against their headers.
void mira_wiring() {
    const mirage::runtime::MiraCoreVersion core = mirage::runtime::mira_core_version();
    MIRAGE_CHECK(core.major == mira::kVersion.major);
    MIRAGE_CHECK(core.minor == mira::kVersion.minor);
    MIRAGE_CHECK(core.patch == mira::kVersion.patch);
    MIRAGE_CHECK(mirage::runtime::mira_core_version_string() == "0.1.0");
    MIRAGE_CHECK(mirage::runtime::mira_core_compatible_with(0, 1));
    MIRAGE_CHECK(!mirage::runtime::mira_core_compatible_with(1, 0));
}

void mirador_wiring() {
    mirage::integration::VisualBackendIdentity good;
    good.name = "mirage-wiring-fake";
    good.implementation_version = "0.1.0";
    good.accepted_formats = {"rgb8", "bgra8"};
    MIRAGE_CHECK(mirage::integration::validate_backend_identity(good));

    mirage::integration::VisualBackendIdentity anonymous;
    anonymous.implementation_version = "0.1.0";
    anonymous.accepted_formats = {"rgb8"};
    MIRAGE_CHECK(!mirage::integration::validate_backend_identity(anonymous));

    mirage::integration::VisualBackendIdentity bad_format;
    bad_format.name = "mirage-wiring-fake";
    bad_format.implementation_version = "0.1.0";
    bad_format.accepted_formats = {"yuv444"};
    MIRAGE_CHECK(!mirage::integration::validate_backend_identity(bad_format));
}

void runtime_and_platform_skeleton() {
    mirage::runtime::SkeletonMiraHost host;
    MIRAGE_CHECK(host.status() == mirage::runtime::HostStatus::Stopped);

    const mirage::runtime::ServiceInfo service = mirage::runtime::runtime_service_info();
    MIRAGE_CHECK(service.name == "mirage-runtime");
    MIRAGE_CHECK(service.background_capable);

    MIRAGE_CHECK(!mirage::platform::platform_info().os.empty());
}

} // namespace

int main() {
    mira_wiring();
    mirador_wiring();
    runtime_and_platform_skeleton();
    return mirage::testing::finish("dependency_wiring");
}
