// M7-10: the public host DPI declaration contract (DEC-086). The runtime
// hosts (mirage-service and the product tray) call declare_host_dpi_awareness()
// at startup; its mode decides whether enumeration, capture and injected
// input share the desktop's physical pixel space, and whether the DEC-086
// capture gate lets the GPU tiers through. On Windows a fresh console test
// process carries no manifest, so the first call must land per-monitor v2
// and a second call must report that prior declaration instead of claiming
// a new one; on Linux there is no process-level declaration and both calls
// report unavailable.

#include "../support/test.hpp"

#include <mirage/platform/host_environment.hpp>

#include <cstdio>

int main() {
    const auto first = mirage::platform::declare_host_dpi_awareness();
    std::fprintf(stderr, "[host] first declaration mode %d\n", static_cast<int>(first));
#ifdef _WIN32
    MIRAGE_CHECK(first == mirage::platform::DpiAwareness::per_monitor_v2);
#else
    MIRAGE_CHECK(first == mirage::platform::DpiAwareness::unavailable);
#endif

    // Idempotence: the second call reports the mode actually in effect.
    const auto second = mirage::platform::declare_host_dpi_awareness();
    std::fprintf(stderr, "[host] second declaration mode %d\n", static_cast<int>(second));
#ifdef _WIN32
    MIRAGE_CHECK(second == mirage::platform::DpiAwareness::prior_per_monitor);
#else
    MIRAGE_CHECK(second == mirage::platform::DpiAwareness::unavailable);
#endif

    return mirage::testing::finish("host_dpi_awareness_test");
}
