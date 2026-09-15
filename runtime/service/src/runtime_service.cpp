#include <mirage/runtime/runtime_service.hpp>

namespace mirage::runtime {

ServiceInfo runtime_service_info() {
    // Skeleton identity only. The M1 service owns the Mira Host lifetime and
    // survives GUI shutdown; concurrency is exclusively managed through the
    // executor pinned inside third_party/mira (root AGENTS.md).
    return {"mirage-runtime", true};
}

} // namespace mirage::runtime
